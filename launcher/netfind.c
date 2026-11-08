/* See netfind.h. One worker thread for the Join list (started once, idle while the list is hidden), one per invite probe, one per open
 * lobby for the hosting checks. Everything they find is published under one lock; the UI thread only reads. MIT licence. */
#include "../common/coop_witness.h"      /* first: the Witness's constants for the shared headers */
#include "../common/coop_discover_win.h"
#include "../common/launcher_host_win.h"
#include "netfind.h"
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

void (*nf_log)(const char *line);
static void logf_(const char *fmt, ...)
{
    char b[400];
    va_list a;
    if (!nf_log) return;
    va_start(a, fmt); vsnprintf(b, sizeof(b), fmt, a); va_end(a);
    nf_log(b);
}
static CRITICAL_SECTION lk;
static LONG lk_made;
static void lock_make(void) { if (InterlockedCompareExchange(&lk_made, 1, 0) == 0) InitializeCriticalSection(&lk); }
static void ip_text(uint32_t ip, char *out, size_t cap) { snprintf(out, cap, "%u.%u.%u.%u", ip >> 24, ip >> 16 & 255, ip >> 8 & 255, ip & 255); }
static void wsa(void) { static int done; WSADATA d; if (!done) { WSAStartup(MAKEWORD(2, 2), &d); done = 1; } }
static uint32_t random_u32(void)
{
    typedef BOOLEAN (WINAPI *gen_fn)(PVOID, ULONG);
    HMODULE adv = lhw_system_dll("advapi32.dll");
    gen_fn gen = adv ? (gen_fn)(void (*)(void))GetProcAddress(adv, "SystemFunction036") : NULL;
    uint32_t v = 0;
    if (!gen || !gen(&v, sizeof(v))) v = GetTickCount() * 2654435761u ^ (GetCurrentProcessId() << 16);
    if (adv) FreeLibrary(adv);
    return v;
}

/* ================= Join list ================= */
static struct {
    struct cd_list list;
    struct lj_source src[LJ_SRCS];
    struct cd_recents recents;
    wchar_t recents_path[MAX_PATH];
    struct nf_settings set;
    char version[CD_VERSION_MAX + 1];
    volatile LONG shown;
    HANDLE thread;
    int scans, ts_state;            /* ts_state: 0 unknown, 1 running, 2 not installed, 3 installed but not connected */
    DWORD ts_at;
} J;
static struct cd_ts ts_cache;
static char *ts_json;

struct scan_ctx { const struct cd_candidates *c; int found[CD_ROUTES]; uint32_t now; };
static void on_reply(void *ctx, int candidate, uint32_t ip, unsigned port, const struct cd_reply *reply, uint32_t rtt)
{
    struct scan_ctx *s = (struct scan_ctx *)ctx;
    int route = s->c->c[candidate].route;
#ifdef NF_MUT_IGNORE_REPLIES
    return;     /* mutant: the Join list ignores every CPS2 answer */
#endif
    EnterCriticalSection(&lk);
    if (wc_forget_invite_only(&J.list, reply, ip, port)) cd_expire(&J.list, GetTickCount());      /* the lobby there turned invite only: its open row goes now */
    if (cd_seen(&J.list, reply, route, ip, port, -1, (int)rtt, s->now + rtt, J.version) >= 0 && route >= 0 && route < CD_ROUTES) s->found[route]++;
    LeaveCriticalSection(&lk);
}
static void scan_once(void)
{
    static struct cdw_adapter wa[32];
    static struct cd_adapter ad[32];
    static struct cd_candidates cand;
    static struct cd_recents rec;
    struct nf_settings set;
    struct lj_scan_input in;
    struct scan_ctx ctx;
    int na = 0, ts_on;
    DWORD now = GetTickCount();
    EnterCriticalSection(&lk);
    set = J.set; rec = J.recents;
    J.src[LJ_SRC_LAN].state = set.find_lan ? LJ_S_LOOKING : LJ_S_OFF;
    J.src[LJ_SRC_TAILSCALE].state = set.find_tailscale ? LJ_S_LOOKING : LJ_S_OFF;
    J.src[LJ_SRC_RECENT].state = LJ_S_LOOKING;
    J.src[LJ_SRC_STEAM].state = LJ_S_INVITES;     /* friends' Steam lobbies come as Steam invites (Join game in Steam), not as rows */
    LeaveCriticalSection(&lk);
    if (set.find_lan) { na = cdw_adapters(wa, 32); if (na < 0) na = 0; cdw_adapter_list(wa, na, ad); }
    ts_on = 0;
    if (set.find_tailscale) {
        if (!J.ts_state || now - J.ts_at > 30000) {     /* tailscale.exe at most every 30 s */
            int got;
            if (!ts_json) ts_json = (char *)malloc(CD_JSON_MAX);
            got = ts_json ? cdw_tailscale_status(ts_json, CD_JSON_MAX, 2000) : -1;
            if (got == 0) J.ts_state = 2;
            else if (got > 0 && cd_ts_parse(ts_json, (size_t)got, &ts_cache) && ts_cache.running) J.ts_state = 1;
            else { J.ts_state = 3; memset(&ts_cache, 0, sizeof(ts_cache)); }
            J.ts_at = now;
        }
        ts_on = J.ts_state == 1;
    }
    memset(&in, 0, sizeof(in));
    in.adapters = ad; in.nadapters = na; in.ts = &ts_cache; in.tailscale_on = ts_on; in.recents = &rec;
    in.nports = lj_ports(set.port, in.ports);
    lj_candidates(&cand, &in);
    memset(&ctx, 0, sizeof(ctx)); ctx.c = &cand; ctx.now = GetTickCount();
    if (cand.n) cdw_scan(&cand, random_u32(), LJ_PROBE_ID, CD_WINDOW_MS, 0, on_reply, &ctx, NULL);
    EnterCriticalSection(&lk);
    cd_expire(&J.list, GetTickCount());
    if (set.find_lan) { J.src[LJ_SRC_LAN].state = na ? LJ_S_DONE : LJ_S_FAILED; J.src[LJ_SRC_LAN].found = ctx.found[CD_LAN]; }
    if (set.find_tailscale) {
        J.src[LJ_SRC_TAILSCALE].state = J.ts_state == 1 ? LJ_S_DONE : J.ts_state == 2 ? LJ_S_MISSING : LJ_S_FAILED;
        J.src[LJ_SRC_TAILSCALE].found = ctx.found[CD_TAILSCALE];
    }
    J.src[LJ_SRC_RECENT].state = LJ_S_DONE; J.src[LJ_SRC_RECENT].found = ctx.found[CD_RECENT];
    J.scans++;
    LeaveCriticalSection(&lk);
    logf_("join list: scan %d: %d queries (%d adapters, tailscale %s, %d recents), %d answers", J.scans, cand.n, na,
          !set.find_tailscale ? "off" : ts_on ? "on" : "not connected", rec.n, ctx.found[CD_LAN] + ctx.found[CD_TAILSCALE] + ctx.found[CD_RECENT]);
}
static DWORD WINAPI join_worker(void *unused)
{
    struct cd_refresh rf;
    (void)unused;
    memset(&rf, 0, sizeof(rf));
    wsa();
    for (;;) {
        int shown = InterlockedCompareExchange(&J.shown, 0, 0) != 0;
        if (cd_refresh_due(&rf, shown, GetTickCount())) scan_once();
        else Sleep(100);
    }
    return 0;
}
void nf_join_show(int shown, const struct nf_settings *s, const char *our_version)
{
    lock_make();
    EnterCriticalSection(&lk);
    if (s) J.set = *s;
    if (our_version) snprintf(J.version, sizeof(J.version), "%s", our_version);
    if (!shown && J.shown) { memset(&J.list, 0, sizeof(J.list)); memset(J.src, 0, sizeof(J.src)); }   /* hidden: nothing kept, nothing sent */
    LeaveCriticalSection(&lk);
    InterlockedExchange(&J.shown, shown ? 1 : 0);
    if (shown && !J.thread) J.thread = CreateThread(NULL, 0, join_worker, NULL, 0, NULL);
}
int nf_join_rows(struct nf_row *out, int max)
{
    int idx[CD_ROWS], n, i;
    lock_make();
    EnterCriticalSection(&lk);
    n = lj_order(&J.list, idx);
    if (n > max) n = max;
    for (i = 0; i < n; i++) {
        const struct cd_row *r = &J.list.r[idx[i]];
        struct nf_row *o = &out[i];
        memset(o, 0, sizeof(*o));
        snprintf(o->name, sizeof(o->name), "%s", lj_row_name(r));
        snprintf(o->version, sizeof(o->version), "%s", r->version);
        lj_row_line(r, o->line, sizeof(o->line));
        o->state = r->state; o->players = r->players; o->limit = r->limit; o->mismatch = r->mismatch; o->route = r->route; o->rtt = r->rtt;
        o->full = lj_row_full(r); o->ip = r->ip; o->port = r->port;
    }
    LeaveCriticalSection(&lk);
    return n;
}
void nf_join_sources(char *out, size_t cap)
{
    size_t used = 0;
    int i;
    lock_make();
    out[0] = 0;
    EnterCriticalSection(&lk);
    for (i = 0; i < LJ_SRCS; i++) {
        char t[64];
        int n;
        if (i == LJ_SRC_TAILSCALE && J.src[i].state == LJ_S_FAILED) snprintf(t, sizeof(t), "Tailscale not connected");
        else if (i == LJ_SRC_LAN && J.src[i].state == LJ_S_FAILED) snprintf(t, sizeof(t), "Network: no adapter");
        else lj_source_text(i, &J.src[i], t, sizeof(t));
        n = snprintf(out + used, cap - used, "%s%s%s", i ? "  -  " : "", t, lj_source_ticked(&J.src[i]) ? " \xE2\x9C\x93" : "");
        if (n < 0 || (size_t)n >= cap - used) break;
        used += (size_t)n;
    }
    LeaveCriticalSection(&lk);
}
int nf_join_source_states(int *states, int *found)
{
    int i;
    lock_make();
    EnterCriticalSection(&lk);
    for (i = 0; i < LJ_SRCS; i++) { states[i] = J.src[i].state; found[i] = J.src[i].found; }
    LeaveCriticalSection(&lk);
    return LJ_SRCS;
}
int nf_join_scans(void) { return J.scans; }

/* ================= recents ================= */
static void recents_save(void)
{
    static char text[CD_RECENT_BYTES + 64];
    int n = cd_recents_encode(&J.recents, text, sizeof(text));
    wchar_t tmp[MAX_PATH + 8];
    FILE *f;
    if (n <= 0 || !J.recents_path[0]) return;
    _snwprintf(tmp, MAX_PATH + 8, L"%ls.tmp", J.recents_path);
    tmp[MAX_PATH + 7] = 0;
    if (!(f = _wfopen(tmp, L"wb"))) return;
    fwrite(text, 1, (size_t)n, f);
    fclose(f);
    MoveFileExW(tmp, J.recents_path, MOVEFILE_REPLACE_EXISTING);
}
void nf_recents_load(const wchar_t *path)
{
    static char text[CD_RECENT_BYTES];
    FILE *f;
    size_t n = 0;
    lock_make();
    EnterCriticalSection(&lk);
    wcsncpy(J.recents_path, path, MAX_PATH - 1);
    memset(&J.recents, 0, sizeof(J.recents));
    if ((f = _wfopen(path, L"rb")) != NULL) {
        n = fread(text, 1, sizeof(text) - 1, f);
        fclose(f);
        if (!cd_recents_decode(&J.recents, text, n)) memset(&J.recents, 0, sizeof(J.recents));   /* a damaged store is started again */
    }
    LeaveCriticalSection(&lk);
}
void nf_recent_add(const char *name, uint32_t ip, unsigned port, int recent_route)
{
    char host[20];
    lock_make();
    ip_text(ip, host, sizeof(host));
    EnterCriticalSection(&lk);
    if (cd_recent_add(&J.recents, name ? name : "", host, port, recent_route, (uint64_t)time(NULL)) >= 0) recents_save();
    LeaveCriticalSection(&lk);
}
int nf_recents_count(void) { return J.recents.n; }

/* ================= C4: an invite's routes at once ================= */
static struct { struct lj_invite_join j; struct ci_invite v; volatile LONG running, stop; int result, route; uint32_t ip; unsigned port; HANDLE thread; } P;
static void probe_reply(void *ctx, int candidate, uint32_t ip, unsigned port, const struct cd_reply *reply, uint32_t rtt)
{
    (void)ctx; (void)ip; (void)port; (void)reply;
    lj_invite_reply(&P.j, candidate, rtt);
    if (lj_invite_choice(&P.j, rtt) >= 0) InterlockedExchange(&P.stop, 1);   /* the best route tried answered: no need to wait */
}
static DWORD WINAPI probe_worker(void *unused)
{
    int choice = -1;
    (void)unused;
    wsa();
    if (lj_invite_start(&P.j, &P.v) > 0) {
        cdw_scan(&P.j.c, random_u32(), LJ_PROBE_ID, 1500, 0, probe_reply, NULL, &P.stop);
        if (P.j.he.first >= 0) choice = lj_invite_choice(&P.j, P.j.he.first_at + CD_PREFER_MS);
    }
    EnterCriticalSection(&lk);
    if (choice >= 0) { P.ip = P.j.c.c[choice].ip; P.port = P.j.c.c[choice].port; P.route = P.j.c.c[choice].route; P.result = 1; }
    else P.result = 0;
    LeaveCriticalSection(&lk);
    logf_("invite: %d route(s) tried, %s", P.j.c.n, choice >= 0 ? cd_route_name(P.route) : "no answer");
    InterlockedExchange(&P.running, 0);
    return 0;
}
void nf_invite_probe_start(const struct ci_invite *v)
{
    lock_make();
    if (P.running) return;
    if (P.thread) { WaitForSingleObject(P.thread, 3000); CloseHandle(P.thread); P.thread = NULL; }
    memset(&P.j, 0, sizeof(P.j));
    P.v = *v; P.stop = 0; P.result = -1; P.running = 1;
    P.thread = CreateThread(NULL, 0, probe_worker, NULL, 0, NULL);
    if (!P.thread) { P.running = 0; P.result = 0; }
}
int nf_invite_probe_result(uint32_t *ip, unsigned *port, int *route)
{
    int r;
    lock_make();
    if (P.running) return -1;
    EnterCriticalSection(&lk);
    r = P.result; *ip = P.ip; *port = P.port; *route = P.route;
    LeaveCriticalSection(&lk);
    return r;
}

/* ================= hosting: Friends can join by ================= */
#define NF_ADDRS 8
enum { RT_OFF, RT_TRYING, RT_MAPPED, RT_FAILED };
enum { TS_CHECKING, TS_RUNNING, TS_ABSENT, TS_STOPPED };
static volatile LONG host_gen;
static struct {
    uint32_t advertise;
    int open, steam, invite_only, nlan, ts_state, router_state, lan_probe, fw_done, game_mapped;
    unsigned port, game_port;
    uint32_t ts_ip;
    struct nf_settings set;
    struct lh_addr lan[NF_ADDRS];
    char ts_name[128], launcher_exe[MAX_PATH], game_exe[MAX_PATH];
    struct lhw_router router;
    struct lhw_fw fw, fw_game;
    HANDLE thread;
    volatile LONG stop;
} H;
static int host_stopping(LONG gen) { return gen != host_gen || H.stop; }
static int lan_addresses(struct lh_addr *out, int max)
{
    struct cdw_adapter a[16];
    uint32_t main_ip = lhw_local_ip_to(0x08080808u);
    int n = cdw_adapters(a, 16), i, k = 0;
    if (main_ip && (lh_private_ip(main_ip) || (main_ip & 0xffff0000u) == 0xa9fe0000u) && k < max) { out[k].ip = main_ip; out[k++].prefix = 24; }
    for (i = 0; i < n && k < max; i++) {
        uint32_t ip = a[i].a.ip;
        if (ip == main_ip || !(lh_private_ip(ip) || (ip & 0xffff0000u) == 0xa9fe0000u)) continue;
        out[k].ip = ip; out[k++].prefix = a[i].a.prefix;
    }
    return k;
}
/* This PC's lobby, asked on its own LAN address: the lobby socket (lobby_win.cpp) answers it. */
static int probe_self(uint32_t ip, unsigned port, LONG gen)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in to, from;
    uint8_t q[CD_QUERY_LEN], b[CD_REPLY_MAX + 8];
    uint32_t nonce = random_u32();
    struct cd_reply rep;
    int k, ok = 0;
    if (s == INVALID_SOCKET) return 0;
    lhw_sin(&to, ip, port);
    cd_query(q, nonce, LJ_PROBE_ID, 0);
    for (k = 0; k < 3 && !ok && !host_stopping(gen); k++) {
        DWORD start = GetTickCount();
        sendto(s, (const char *)q, CD_QUERY_LEN, 0, (struct sockaddr *)&to, sizeof(to));
        while (!ok && lhw_left(start, 700) && lhw_wait(s, 0, lhw_left(start, 700))) {
            int fl = sizeof(from), n = recvfrom(s, (char *)b, sizeof(b), 0, (struct sockaddr *)&from, &fl);
            if (n > 0 && cd_parse_reply(b, (size_t)n, nonce, LJ_PROBE_ID, &rep)) ok = 1;
        }
    }
    closesocket(s);
    return ok;
}
static DWORD WINAPI host_worker(void *param)
{
    LONG gen = (LONG)(INT_PTR)param;
    struct lh_addr lan[NF_ADDRS];
    struct lhw_router_cfg rcfg;
    struct lhw_router router, grouter;
    struct lhw_fw fw, fwg;
    char *json;
    int n, mapped = 0, gmapped = 0;
    unsigned port = H.port, gport = H.game_port;
    struct nf_settings set = H.set;
    memset(lan, 0, sizeof(lan));
    memset(&router, 0, sizeof(router)); memset(&grouter, 0, sizeof(grouter));
    wsa();
    if (H.advertise) { lan[0].ip = H.advertise; lan[0].prefix = 32; n = 1; }      /* the address the player set: the only one shown and probed */
    else n = lan_addresses(lan, NF_ADDRS);
    EnterCriticalSection(&lk);
    if (gen == host_gen) { memcpy(H.lan, lan, sizeof(lan)); H.nlan = n; }
    LeaveCriticalSection(&lk);
    if (!H.steam) {
        int ok = n > 0 && probe_self(lan[0].ip, port, gen);
        EnterCriticalSection(&lk);
        if (gen == host_gen) H.lan_probe = ok ? 1 : 2;
        LeaveCriticalSection(&lk);
        logf_("host: same network: %d address(es), lobby %s", n, ok ? "answers on this PC's LAN address" : "did not answer");
        if (!host_stopping(gen)) {
            lhw_firewall(H.launcher_exe, port, &fw);
            if (H.game_exe[0]) lhw_firewall(H.game_exe, gport, &fwg); else fwg = fw;
            EnterCriticalSection(&lk);
            if (gen == host_gen) { H.fw = fw; H.fw_game = fwg; H.fw_done = 1; }
            LeaveCriticalSection(&lk);
            logf_("host: firewall: launcher private=%d public=%d, game private=%d public=%d (%d rules) %s", fw.private_verdict, fw.public_verdict,
                  fwg.private_verdict, fwg.public_verdict, fw.rules_seen, fw.error);
        }
    }
    if (!host_stopping(gen) && (json = (char *)malloc(CD_JSON_MAX)) != NULL) {
        static struct cd_ts ts;
        int got = cdw_tailscale_status(json, CD_JSON_MAX, 2000), state;
        if (got == 0) state = TS_ABSENT;
        else if (got > 0 && cd_ts_parse(json, (size_t)got, &ts) && ts.running && ts.self.ip) state = TS_RUNNING;
        else state = TS_STOPPED;
        free(json);
        EnterCriticalSection(&lk);
        if (gen == host_gen) {
            H.ts_state = state;
            if (state == TS_RUNNING) { H.ts_ip = ts.self.ip; snprintf(H.ts_name, sizeof(H.ts_name), "%s", ts.self.dns[0] ? ts.self.dns : ts.self.host); }
        }
        LeaveCriticalSection(&lk);
        logf_("host: tailscale: %s", state == TS_RUNNING ? "running" : state == TS_ABSENT ? "not installed" : "not connected");
    }
    memset(&rcfg, 0, sizeof(rcfg));
    if (!H.steam && !host_stopping(gen) && set.upnp) {
        mapped = lhw_router_open(&router, &rcfg, port);
        if (mapped && gport && gport != port) gmapped = lhw_router_open(&grouter, &rcfg, gport);     /* the game's own port too: the games reach each other on it */
        EnterCriticalSection(&lk);
        if (gen == host_gen) { H.router = router; H.router_state = mapped ? RT_MAPPED : RT_FAILED; H.game_mapped = gmapped; }
        LeaveCriticalSection(&lk);
        logf_("host: router: %s (lobby UDP %u%s; wan class %d)", router.said, port, gmapped ? ", game port too" : "", router.wan);
    }
    while ((mapped || gmapped) && !host_stopping(gen)) {     /* renewed while the lobby (and the game it started) runs */
        Sleep(250);
        if (mapped) lhw_router_renew(&router, &rcfg, GetTickCount());
        if (gmapped) lhw_router_renew(&grouter, &rcfg, GetTickCount());
    }
#ifndef NF_MUT_KEEP_MAPPING
    if (mapped) { lhw_router_close(&router, &rcfg); logf_("host: router mapping of UDP %u removed", port); }
    if (gmapped) { lhw_router_close(&grouter, &rcfg); logf_("host: router mapping of UDP %u removed", gport); }
#endif
    EnterCriticalSection(&lk);
    if (gen == host_gen && H.router_state == RT_MAPPED) H.router_state = RT_OFF;
    LeaveCriticalSection(&lk);
    return 0;
}
void nf_host_open(unsigned lobby_port, unsigned game_port, int steam, int invite_only, const struct nf_settings *s, const char *launcher_exe, const char *game_exe,
                  uint32_t advertise_ip)
{
    lock_make();
    nf_host_close();
    EnterCriticalSection(&lk);
    memset(&H, 0, sizeof(H));
    H.open = 1; H.steam = steam; H.invite_only = invite_only; H.port = lobby_port; H.game_port = game_port; H.set = *s;
    snprintf(H.launcher_exe, sizeof(H.launcher_exe), "%s", launcher_exe ? launcher_exe : "");
    snprintf(H.game_exe, sizeof(H.game_exe), "%s", game_exe ? game_exe : "");
    H.ts_state = TS_CHECKING; H.advertise = advertise_ip;
    H.router_state = !steam && s->upnp ? RT_TRYING : RT_OFF;
    LeaveCriticalSection(&lk);
    H.thread = CreateThread(NULL, 0, host_worker, (void *)(INT_PTR)InterlockedIncrement(&host_gen), 0, NULL);
    logf_("host: lobby %s%s, checking the ways in", steam ? "on Steam" : invite_only ? "invite only" : "open to my networks", "");
}
void nf_host_close(void)
{
    lock_make();
    if (!H.thread) { H.open = 0; return; }
    InterlockedExchange(&H.stop, 1);
    if (WaitForSingleObject(H.thread, 8000) != WAIT_OBJECT_0) {
        InterlockedIncrement(&host_gen);    /* it finishes (and removes any mapping) on its own, publishing nothing */
        logf_("host: the checks did not finish in time");
    }
    CloseHandle(H.thread);
    H.thread = NULL;
    H.open = 0;
}
static const char *fix_firewall = "Run Setup and tick \"Allow hosting through Windows Firewall\", or allow The Witness when Windows asks (Private and Public).";
int nf_host_rows(struct nf_host_row *r)
{
    char lan[16], ext[16], ts[16];
    lock_make();
    memset(r, 0, sizeof(*r) * NF_ROWS);
    EnterCriticalSection(&lk);
    snprintf(r[NF_STEAM].title, sizeof(r[0].title), "Steam");
    snprintf(r[NF_LAN].title, sizeof(r[0].title), "Same network");
    snprintf(r[NF_TAILSCALE].title, sizeof(r[0].title), "Tailscale");
    snprintf(r[NF_INTERNET].title, sizeof(r[0].title), "Internet");
    snprintf(r[NF_FIREWALL].title, sizeof(r[0].title), "Windows Firewall");
    ip_text(H.nlan ? H.lan[0].ip : 0, lan, sizeof(lan));
    ip_text(H.router.ext_ip, ext, sizeof(ext));
    ip_text(H.ts_ip, ts, sizeof(ts));
    if (H.steam) {
        r[NF_STEAM].state = NF_OK;
        snprintf(r[NF_STEAM].result, sizeof(r[0].result), "A friends-only Steam lobby: friends use Join game in Steam, an invite, or the code.");
        r[NF_LAN].state = r[NF_INTERNET].state = r[NF_FIREWALL].state = NF_OFF;
        snprintf(r[NF_LAN].result, sizeof(r[0].result), "Not needed: Steam carries this lobby.");
        snprintf(r[NF_INTERNET].result, sizeof(r[0].result), "Not needed: Steam relays, no port to open.");
        snprintf(r[NF_FIREWALL].result, sizeof(r[0].result), "Not needed: nothing comes in from outside.");
    } else {
        r[NF_STEAM].state = NF_OFF;
        snprintf(r[NF_STEAM].result, sizeof(r[0].result), "Not used by this lobby.");
        snprintf(r[NF_STEAM].fix, sizeof(r[0].fix), "To host over Steam, leave and choose Through Steam on Play: friends then use Join game in Steam.");
        if (!H.nlan) {
            r[NF_LAN].state = H.lan_probe ? NF_WARN : NF_CHECKING;
            snprintf(r[NF_LAN].result, sizeof(r[0].result), H.lan_probe ? "No local network found." : "Checking...");
            if (H.lan_probe) snprintf(r[NF_LAN].fix, sizeof(r[0].fix), "Connect this PC to a network (cable or Wi-Fi).");
        } else if (!H.lan_probe) {
            r[NF_LAN].state = NF_CHECKING;
            snprintf(r[NF_LAN].result, sizeof(r[0].result), "Checking %s port %u...", lan, H.port);
        } else if (H.lan_probe == 1) {
            r[NF_LAN].state = NF_OK;
            snprintf(r[NF_LAN].result, sizeof(r[0].result), "This PC: %s, port %u.%s", lan, H.port,
                     H.invite_only ? " Invite only: friends need the invite." : " Friends see it in Join.");
        } else {
            r[NF_LAN].state = NF_WARN;
            snprintf(r[NF_LAN].result, sizeof(r[0].result), "The lobby did not answer on %s port %u.", lan, H.port);
            snprintf(r[NF_LAN].fix, sizeof(r[0].fix), "Another program may be using UDP %u: pick another lobby port in Settings > Network.", H.port);
        }
        if (H.router_state == RT_OFF && !H.set.upnp) {
            r[NF_INTERNET].state = NF_OFF;
            snprintf(r[NF_INTERNET].result, sizeof(r[0].result), "Router port: off (Settings > Network).");
            snprintf(r[NF_INTERNET].fix, sizeof(r[0].fix), "Forward UDP %u and %u to %s in the router yourself, or use Steam or Tailscale.", H.port, H.game_port, lan);
        } else if (H.router_state == RT_TRYING) {
            r[NF_INTERNET].state = NF_CHECKING;
            snprintf(r[NF_INTERNET].result, sizeof(r[0].result), "Asking the router to open UDP %u...", H.port);
        } else if (H.router_state == RT_MAPPED && H.router.wan == LH_WAN_PUBLIC) {
            r[NF_INTERNET].state = H.set.internet_in_invite ? NF_OK : NF_WARN;
            snprintf(r[NF_INTERNET].result, sizeof(r[0].result), "%s Internet address %s%s", H.router.said, ext,
                     H.set.internet_in_invite ? ", in the invite." : ", left out of invites (Settings > Network).");
        } else if (H.router_state == RT_MAPPED && (H.router.wan == LH_WAN_CGNAT || H.router.wan == LH_WAN_PRIVATE)) {
            r[NF_INTERNET].state = NF_WARN;
            snprintf(r[NF_INTERNET].result, sizeof(r[0].result), "%s But the router's internet address is %s.", H.router.said, ext);
            snprintf(r[NF_INTERNET].fix, sizeof(r[0].fix), H.router.wan == LH_WAN_CGNAT ?
                     "Your internet provider shares your address with other customers, so friends cannot reach you from the internet. Use Steam or Tailscale." :
                     "Another router sits in front of this one, so friends cannot reach you from the internet. Use Steam or Tailscale.");
        } else if (H.router_state == RT_MAPPED) {
            r[NF_INTERNET].state = NF_WARN;
            snprintf(r[NF_INTERNET].result, sizeof(r[0].result), "%s It did not say the internet address.", H.router.said);
            snprintf(r[NF_INTERNET].fix, sizeof(r[0].fix), "Give friends your internet address yourself, or use Steam or Tailscale.");
        } else {
            r[NF_INTERNET].state = NF_WARN;
            snprintf(r[NF_INTERNET].result, sizeof(r[0].result), "%s", H.router.said[0] ? H.router.said : "The router did not open the port.");
            snprintf(r[NF_INTERNET].fix, sizeof(r[0].fix), "Turn on UPnP or NAT-PMP in the router, or forward UDP %u to %s. Or use Steam or Tailscale.", H.port, lan);
        }
        if (!H.fw_done) {
            r[NF_FIREWALL].state = NF_CHECKING;
            snprintf(r[NF_FIREWALL].result, sizeof(r[0].result), "Checking...");
        } else if (!H.fw.ok) {
            r[NF_FIREWALL].state = NF_WARN;
            snprintf(r[NF_FIREWALL].result, sizeof(r[0].result), "%s", H.fw.error);
            snprintf(r[NF_FIREWALL].fix, sizeof(r[0].fix), "%s", fix_firewall);
        } else {
            int pv = H.fw.private_verdict, uv = H.fw.public_verdict;      /* the worse of the launcher (lobby) and the game */
            if (H.fw_game.private_verdict != LH_FW_ALLOWED && H.fw_game.private_verdict != LH_FW_OFF) pv = H.fw_game.private_verdict;
            if (H.fw_game.public_verdict != LH_FW_ALLOWED && H.fw_game.public_verdict != LH_FW_OFF) uv = H.fw_game.public_verdict;
            r[NF_FIREWALL].state = lh_fw_line(pv, uv, r[NF_FIREWALL].result, sizeof(r[0].result)) ? NF_OK : NF_WARN;
            if (r[NF_FIREWALL].state != NF_OK) snprintf(r[NF_FIREWALL].fix, sizeof(r[0].fix), "%s", fix_firewall);
        }
    }
    if (H.ts_state == TS_CHECKING) {
        r[NF_TAILSCALE].state = NF_CHECKING;
        snprintf(r[NF_TAILSCALE].result, sizeof(r[0].result), "Checking...");
    } else if (H.ts_state == TS_RUNNING) {
        r[NF_TAILSCALE].state = H.steam ? NF_OFF : NF_OK;
        snprintf(r[NF_TAILSCALE].result, sizeof(r[0].result), H.steam ? "This PC: %s (not needed: Steam carries this lobby)." : "This PC: %s.", H.ts_name[0] ? H.ts_name : ts);
    } else if (H.ts_state == TS_ABSENT) {
        r[NF_TAILSCALE].state = NF_OFF;
        snprintf(r[NF_TAILSCALE].result, sizeof(r[0].result), "Not installed. It is optional.");
        snprintf(r[NF_TAILSCALE].fix, sizeof(r[0].fix), "With Tailscale (free), friends on your tailnet join without router setup.");
    } else {
        r[NF_TAILSCALE].state = NF_WARN;
        snprintf(r[NF_TAILSCALE].result, sizeof(r[0].result), "Installed, not connected.");
        snprintf(r[NF_TAILSCALE].fix, sizeof(r[0].fix), "Start Tailscale and log in, then open the lobby again.");
    }
    LeaveCriticalSection(&lk);
    return NF_ROWS;
}
int nf_host_routes(struct lh_addr *lan, int max, int *nlan, uint32_t *tailscale_ip, uint32_t *internet_ip, int *internet_ok)
{
    int i;
    lock_make();
    EnterCriticalSection(&lk);
    *nlan = H.nlan < max ? H.nlan : max;
    for (i = 0; i < *nlan; i++) lan[i] = H.lan[i];
    *tailscale_ip = H.ts_state == TS_RUNNING ? H.ts_ip : 0;
    *internet_ip = H.router.ext_ip;
    *internet_ok = H.set.internet_in_invite && H.router_state == RT_MAPPED && H.router.wan == LH_WAN_PUBLIC;
    LeaveCriticalSection(&lk);
    return H.open;
}
void nf_host_dump(char *out, size_t cap)
{
    struct nf_host_row r[NF_ROWS];
    size_t used;
    int i, n;
    nf_host_rows(r);
    n = snprintf(out, cap, "hostcheck open %d steam %d invite_only %d upnp %d router_state %d router_method %d router_wan %d lan_probe %d tailscale %d firewall %d %d\n",
                 H.open, H.steam, H.invite_only, H.set.upnp, H.router_state, H.router.method, H.router.wan, H.lan_probe, H.ts_state,
                 H.fw_done ? H.fw.private_verdict : -1, H.fw_done ? H.fw.public_verdict : -1);
    used = n > 0 && (size_t)n < cap ? (size_t)n : 0;
    for (i = 0; i < NF_ROWS; i++) {
        n = snprintf(out + used, cap - used, "hostrow %d state %d %s: %s\n", i, r[i].state, r[i].title, r[i].result);
        if (n < 0 || (size_t)n >= cap - used) break;
        used += (size_t)n;
    }
}
