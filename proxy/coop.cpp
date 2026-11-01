// The transport and configuration for the sync core (core/sync_core.cpp holds all the logic and is tested without the game).
// witness_coop.cfg (next to the DLL):  id=1  listen=47001  peer=47002  [peer_host=127.0.0.1]   (two players over UDP, the old form)
//   or a group: id=<n> listen=<port> and one line per other player: peer=<id>,<IPv4>,<port> (UDP) or peer=<id>,steam:<steamid64> (Steam, relayed:
//   no port forwarding; the launcher writes these for a Steam lobby). core/peers.h holds the roster and the rule for whom a datagram is taken from.
//   test-only switches: drop_pct=30 drop_seed=7 (seeded random loss of outgoing datagrams)  drop_first_event=1  no_resend=1  no_echo_guard=1
#define WIN32_LEAN_AND_MEAN
#include <ctime>
#include <winsock2.h>
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <map>
#include <atomic>
#include "coop.h"
#include "tv.h"
#include "race_game.h"
#include "social.h"
#include "core/peers.h"
#include "core/version_notice.h"
#if __has_include("version.h")
#include "version.h"      // MOD_VERSION: git describe, written by the Makefile
#endif
#ifndef MOD_VERSION
#define MOD_VERSION "dev"
#endif
#include "steam_net.h"
#include "../common/steam_abi.h"
#include "diag.h"

void Log(const char* msg);
extern HMODULE g_selfModule();

static CRITICAL_SECTION g_lock; static bool g_lockInit;
static std::atomic<witness::SyncCore*> g_core{nullptr};
static wpeers::PeerTable g_table; static wpeers::Endpoint g_single;      // the roster (group), or the one peer of the old two-player form; fixed before g_peerSet
static SOCKET g_sock = INVALID_SOCKET; static std::atomic<bool> g_peerSet{false}, g_steam{false}, g_wantSteam{false};      // g_wantSteam: the cfg asks for Steam (up or not)
static void RawSend(const wpeers::Endpoint& e, const uint8_t* d, size_t n) {      // one datagram on this game's transport (any thread)
    if (e.isSteam()) { wsteam::Send(e.steam, d, n); return; }
    sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_addr.s_addr = e.ip; a.sin_port = htons(e.port); sendto(g_sock, (const char*)d, (int)n, 0, (const sockaddr*)&a, sizeof a);
}
void CoopSendRaw(const uint8_t* d, size_t n) {
    if (!g_peerSet.load(std::memory_order_acquire)) return;
    if (g_table.empty()) RawSend(g_single, d, n);
    else for (const auto& kv : g_table.peers()) RawSend(kv.second, d, n);
}
void CoopSendRawTo(unsigned id, const uint8_t* d, size_t n) {      // one datagram to one player (any thread); with a single peer, to that peer
    if (!g_peerSet.load(std::memory_order_acquire)) return;
    if (g_table.empty()) { RawSend(g_single, d, n); return; }
    if (const wpeers::Endpoint* e = g_table.Find(id)) RawSend(*e, d, n);
}
#include <cstdarg>
#include "devmenu.h"
static void Msg(const char* fmt, ...) {      // a message shown in the game for a few seconds (the toast: devmenu.cpp), and in the log
    char b[200]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap); DevMenuNotice(b);
}
void CoopNetStat() {      // witness_cmd.txt `netstat` / `steamstat`: which transport this game uses and how its sessions are
    char m[160]; snprintf(m, sizeof m, "netstat: transport %s up %d peers %zu", g_wantSteam ? "steam" : "udp", (g_peerSet.load() || g_steam.load()) ? 1 : 0, g_table.empty() ? (size_t)(g_wantSteam ? 0 : 1) : g_table.peers().size()); Log(m);
    if (!g_steam) return;
    std::vector<uint64_t> sids; std::vector<unsigned> ids; for (const auto& kv : g_table.peers()) { ids.push_back(kv.first); sids.push_back(kv.second.steam); }
    wsteam::Stat(sids.data(), ids.data(), ids.size());
}
void VideoOnPacket(const uint8_t* data, size_t n);
void VoiceOnPacket(const uint8_t* data, size_t n);      // voice_audio.cpp
void VoiceStart();
void VideoOnRequest(const uint8_t* data, size_t n);

static std::atomic<unsigned> g_myId{0};
// Every "state:" line of the core also goes to witness_events.log with the time in milliseconds of the machine's performance counter, the same
// clock for every game on this PC: a solve's "local panel .. solved -> event" in one game and its "remote panel .. applied" in another give the
// co-op propagation time exactly (tools/playthrough/run.py reads them).
static void EventLog(const char* t) {
    static FILE* f; static double scale;
    if (!f) {
        wchar_t path[MAX_PATH]; GetModuleFileNameW(g_selfModule(), path, MAX_PATH); wchar_t* sl = wcsrchr(path, L'\\'); if (sl) sl[1] = 0;
        wcscat_s(path, L"witness_events.log"); f = _wfopen(path, L"a"); LARGE_INTEGER fr; QueryPerformanceFrequency(&fr); scale = 1000.0 / (double)fr.QuadPart;
        if (!f) return;
    }
    LARGE_INTEGER c; QueryPerformanceCounter(&c); fprintf(f, "%.3f %s\n", (double)c.QuadPart * scale, t); fflush(f);
}
unsigned CoopMyId() { return g_myId.load(); }
static char g_myName[40] = "";
const char* CoopMyName() { return g_myName[0] ? g_myName : "me"; }
witness::SyncCore* CoopCore() { return g_core.load(std::memory_order_acquire); }
static volatile LONG g_fakeOn; static float g_fake[4]; static volatile LONG g_fakeSeq;     // test hook (witness_cmd.txt: fakepose): a pose the peer never sent
void CoopSetFakeRemote(bool on, const float* pos, float yaw) {
    if (on) { g_fake[0] = pos[0]; g_fake[1] = pos[1]; g_fake[2] = pos[2]; g_fake[3] = yaw; InterlockedIncrement(&g_fakeSeq); }
    InterlockedExchange(&g_fakeOn, on ? 1 : 0);
}
std::vector<witness::RemoteLine> CoopGetRemoteLines() { if (auto* c = CoopCore()) return c->remoteLines(GetTickCount64()); return {}; }
std::vector<RemotePose> CoopGetRemotes() {
    if (g_fakeOn) { RemotePose p{}; if (CoopGetRemote(&p)) return {p}; return {}; }
    std::vector<RemotePose> out;
    if (auto* c = CoopCore()) for (const auto& r : c->remotePoses(GetTickCount64())) {
        RemotePose p{}; p.pingMs = r.pingMs; strncpy(p.name, r.name.c_str(), sizeof p.name - 1); p.valid = r.valid; memcpy(p.pos, r.pos, sizeof p.pos); memcpy(p.ang, r.ang, sizeof p.ang);
        p.ageMs = (DWORD)r.ageMs; p.seq = r.seq; p.id = r.id;
        p.appearance = r.appearance; p.emote = r.emote; p.emoteSeq = r.emoteSeq; p.typing = r.typing; p.speaking = r.speaking; p.proximitySpeaking = r.proximitySpeaking; p.radioEquipped = r.radioEquipped; p.radioTx = r.radioTx; p.voiceChannel = r.voiceChannel; out.push_back(p);
    }
    return out;
}
bool CoopGetRemote(RemotePose* out, unsigned id) {
    if (g_fakeOn) { FillFakeRemote(*out, g_fake, g_fake[3], (unsigned)g_fakeSeq); return true; }
    witness::SyncCore* c = CoopCore(); if (!c) return false;
    witness::RemotePose r; bool ok = false;
    for (auto pose : c->remotePoses(GetTickCount64())) if (!id || pose.id == id) { r = pose; ok = true; break; }
    out->valid = r.valid; memcpy(out->pos, r.pos, sizeof out->pos); memcpy(out->ang, r.ang, sizeof out->ang);
    out->emote = r.emote; out->emoteSeq = r.emoteSeq; out->speaking = r.speaking; out->proximitySpeaking = r.proximitySpeaking; out->typing = r.typing; out->radioEquipped = r.radioEquipped; out->radioTx = r.radioTx; out->voiceChannel = r.voiceChannel; out->appearance = r.appearance; out->ageMs = (DWORD)r.ageMs; out->seq = r.seq; out->id = r.id;
    return ok;
}

void CoopDumpStatus(unsigned submittedPlayers, const SocialSnapshot* uiState) {
    auto* c = CoopCore(); if (!c) return;
    auto remotes = c->remotePoses(GetTickCount64());
    wchar_t path[MAX_PATH]; GetModuleFileNameW(g_selfModule(), path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\'); if (slash) *(slash + 1) = 0;
    wcscat_s(path, L"witness_net.json");
    wchar_t tmp[MAX_PATH]; wcscpy_s(tmp, path); wcscat_s(tmp, L".tmp");
    FILE* f = _wfopen(tmp, L"w"); if (!f) return;
    auto ui=uiState ? *uiState : SocialSnapshot{};
    fprintf(f,"{\"emote\":%d,\"emote_seq\":%u,\"wheel\":%s,\"emote_page\":%d,\"guide\":%s,\"paused\":%s,",ui.emote,ui.sequence,ui.wheel?"true":"false",ui.page,ui.guide?"true":"false",ui.paused?"true":"false");
    fprintf(f, "\"ready\":%s,\"unacked\":%zu,\"queued\":%zu,\"remotes\":[", c->ready() ? "true" : "false", c->unacked(), c->queuedRemote());
    bool comma = false;
    for (const auto& r : remotes) {
        fprintf(f, "%s{\"id\":%u,\"seq\":%u,\"age_ms\":%llu", comma ? "," : "", r.id, r.seq, (unsigned long long)r.ageMs);
        fprintf(f,",\"pos\":[%.5f,%.5f,%.5f]",r.pos[0],r.pos[1],r.pos[2]);
        fprintf(f, ",\"appearance\":["); for (size_t i = 0; i < witness::kAppearanceFeatures; ++i) fprintf(f, "%s%u", i ? "," : "", unsigned(r.appearance.choice[i])); fprintf(f, "],\"emote\":%u,\"proximity_speaking\":%s}", r.emote, r.proximitySpeaking ? "true" : "false"); comma = true;
    }
    fprintf(f, "],\"submitted_models\":%u}\n", submittedPlayers); fclose(f); MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING);
}

static DWORD WINAPI NetThread(LPVOID) {
    wchar_t wp[MAX_PATH]; GetModuleFileNameW(g_selfModule(), wp, MAX_PATH);
    wchar_t* sl = wcsrchr(wp, L'\\'); if (sl) *(sl + 1) = 0; wcscat_s(wp, L"witness_coop.cfg");
    FILE* f = _wfopen(wp, L"r");
    if (!f) { Log("coop: no witness_coop.cfg, networking off"); return 0; }
    witness::Flags flags; int id = 0, listenPort = 0, peerPort = 0; char host[64] = "127.0.0.1", line[200], name[32] = ""; int dropPct = 0; uint32_t dropState = 1;
    wpeers::PeerTable table; std::map<unsigned, std::string> peerVersion; wpeers::VersionNotice versionNotice; std::string transport;      // transport=steam (the launcher writes it for a Steam lobby) | udp | (none: from the peer lines)
    while (fgets(line, sizeof line, f)) {
        if (table.ParseLine(line, 0, [](const char* h) { return (uint32_t)inet_addr(h); })) continue;      // peer=<id>,<ip>,<port> | peer=<id>,steam:<steamid64>
        { char nm[64]; if (sscanf(line, "name=%23[^\r\n]", nm) == 1) strncpy(name, nm, sizeof name - 1); }
        { unsigned pid; char pv[40]; if (sscanf(line, "peer_version=%u,%39[^\r\n ]", &pid, pv) == 2) peerVersion[pid] = pv; }      // the launcher's roster: which player runs which mod version
        int v; sscanf(line, "id=%d", &id); sscanf(line, "listen=%d", &listenPort); sscanf(line, "peer=%d", &peerPort); sscanf(line, "peer_host=%63s", host);
        if (sscanf(line, "drop_first_event=%d", &v) == 1) flags.dropFirstEvent = v;
        if (sscanf(line, "drop_pct=%d", &v) == 1) dropPct = v;
        if (sscanf(line, "drop_seed=%d", &v) == 1) dropState = (uint32_t)v * 2654435761u + 1;
        if (sscanf(line, "no_resend=%d", &v) == 1) flags.noResend = v;
        if (sscanf(line, "no_echo_guard=%d", &v) == 1) flags.noEchoGuard = v;
        if (sscanf(line, "no_resync_on_restart=%d", &v) == 1) flags.noResyncOnRestart = v;
        if (sscanf(line, "no_panel_power=%d", &v) == 1) flags.noPanelPower = v;
        if (sscanf(line, "no_reload_sync=%d", &v) == 1) flags.noReloadSync = v;
        if (sscanf(line, "reload_gap_ignored=%d", &v) == 1) flags.reloadGapIgnored = v;      // twin: a load's gap is not a load (the rescan then folds a loaded save into a silent baseline)
        if (sscanf(line, "no_cable_sync=%d", &v) == 1) flags.noCableSync = v;
        if (sscanf(line, "no_cable_off=%d", &v) == 1) flags.noCableOff = v;
        if (sscanf(line, "no_unsolve_sync=%d", &v) == 1) flags.noUnsolveSync = v;
        if (sscanf(line, "no_door_close_sync=%d", &v) == 1) flags.noDoorCloseSync = v;
        if (sscanf(line, "open_dedupe_by_position=%d", &v) == 1) flags.openDedupeByPosition = v;
        if (sscanf(line, "no_off_snapshot=%d", &v) == 1) flags.noOffSnapshot = v;
        if (sscanf(line, "no_cable_arbiter=%d", &v) == 1) flags.noCableArbiter = v;
        if (sscanf(line, "no_snap_reassert=%d", &v) == 1) flags.noSnapReassert = v;
        if (sscanf(line, "no_line_send=%d", &v) == 1) flags.noLineSend = v;
        if (sscanf(line, "no_ping_reply=%d", &v) == 1) flags.noPingReply = v;
        if (sscanf(line, "no_name_send=%d", &v) == 1) flags.noNameSend = v;
        if (sscanf(line, "no_line_resend=%d", &v) == 1) flags.noLineResend = v;
        if (sscanf(line, "no_social_resend=%d", &v) == 1) flags.noSocialResend = v;
        if (sscanf(line, "no_social_dedupe=%d", &v) == 1) flags.noSocialDedupe = v;
        if (sscanf(line, "no_social_order=%d", &v) == 1) flags.noSocialOrder = v;
        if (sscanf(line, "no_presence_send=%d", &v) == 1) flags.noPresenceSend = v;
        { char gm[16]; if (sscanf(line, "game_mode=%15s", gm) == 1) flags.raceMode = !strcmp(gm, "race"); }
        { char tr[16]; if (sscanf(line, "transport=%15s", tr) == 1) transport = tr; }
        if (sscanf(line, "race_emits=%d", &v) == 1) flags.raceEmits = v;
        if (sscanf(line, "race_applies=%d", &v) == 1) flags.raceApplies = v;
    }
    fclose(f);
    const bool steam = table.steam() || transport == "steam";
    if (steam && !table.empty() && !table.steam()) { Log("coop: transport=steam with UDP peers; networking off"); Msg("Co-op is off: the cfg mixes Steam and direct players. Start again from the launcher."); return 0; }
    if (id < 1 || (!steam && (listenPort < 1 || listenPort > 65535)) || !table.ok() || (!steam && table.empty() && (peerPort < 1 || peerPort > 65535))) {
        char m[160]; snprintf(m, sizeof m, "coop: invalid id, port or peer roster (%s); networking off", table.ok() ? "id or port" : table.error().c_str()); Log(m); return 0;
    }
    table.CheckSelf((unsigned)id, 0); if (!table.ok()) { Log("coop: this game's id is in its own roster; networking off"); return 0; }
    SOCKET s = INVALID_SOCKET; char m[200];
    if (steam) {      // Steam: the game's own Steam connection. If it is not there the game plays alone and says why: never a silent fallback to UDP.
        g_wantSteam = true; uint64_t self = 0; char why[200] = "";
        if (!wsteam::Open(60000, &self, why, sizeof why)) { snprintf(m, sizeof m, "coop: steam transport down: %s; networking off", why); Log(m); for (int k = 0; k < 6; ++k) { Msg("Co-op over Steam is off: %s", why); Sleep(6000); } return 0; }
        table.CheckSelf((unsigned)id, self); if (!table.ok()) { Log("coop: this game's own SteamID is in its roster; networking off"); Msg("Co-op over Steam is off: the lobby's roster lists this player twice."); return 0; }
        snprintf(m, sizeof m, "coop: id=%d over Steam (relayed, channel %d), %zu peers", id, wsteam::kGameChannel, table.peers().size()); Log(m);
        if (table.empty()) { g_steam = true; Log("coop: alone in a Steam lobby: nobody to talk to"); return 0; }
    } else {
        WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
        s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in me = {}; me.sin_family = AF_INET; me.sin_addr.s_addr = htonl(INADDR_LOOPBACK); me.sin_port = htons((u_short)listenPort);
        if (strcmp(host, "127.0.0.1") != 0) me.sin_addr.s_addr = INADDR_ANY;
        for (const auto& p : table.peers()) if (p.second.ip != htonl(INADDR_LOOPBACK)) me.sin_addr.s_addr = INADDR_ANY;
        if (bind(s, (sockaddr*)&me, sizeof me) != 0) { snprintf(m, sizeof m, "coop: bind %d failed (%d)", listenPort, WSAGetLastError()); Log(m); return 0; }
        u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
        snprintf(m, sizeof m, "coop: id=%d listening %d, sending to %s:%d", id, listenPort, host, peerPort); Log(m);
    }
    wpeers::Endpoint single = wpeers::Udp((uint32_t)inet_addr(host), (uint16_t)peerPort);

    witness::Hooks h;
    auto transmit = [&](const wpeers::Endpoint& to, const witness::Bytes& b) {
        if (dropPct > 0) { dropState = dropState * 1664525u + 1013904223u; if ((int)((dropState >> 16) % 100) < dropPct) return; }   // deterministic seeded loss
        RawSend(to, b.data(), b.size()); };
    if (table.empty()) h.send = [&](const witness::Bytes& b) { transmit(single, b); };
    else h.sendTo = [&](unsigned target, const witness::Bytes& b) { if (const wpeers::Endpoint* e = table.Find(target)) transmit(*e, b); };
    h.log = [](const char* t) { Log(t); if (!strncmp(t, "state: ", 7)) EventLog(t); DiagCoreLog(t); };
    h.lock = [] { EnterCriticalSection(&g_lock); }; h.unlock = [] { LeaveCriticalSection(&g_lock); };
    uint32_t session = (uint32_t)time(nullptr);     // must grow with every start: the core ignores packets from an older session
    witness::SyncCore* core = new witness::SyncCore(id, session, GameWorldInstance(), h, flags);
    { char nm[40]; if (name[0]) snprintf(nm, sizeof nm, "%s", name); else snprintf(nm, sizeof nm, "Player %d", id); core->setName(nm); snprintf(g_myName, sizeof g_myName, "%s", nm); }
    if (!table.empty()) {
        std::vector<unsigned> ids; for (const auto& p : table.peers()) ids.push_back(p.first);
        if (!core->setPeers(ids)) { Log("coop: invalid roster"); delete core; if (s != INVALID_SOCKET) closesocket(s); return 0; }
        snprintf(m, sizeof m, "coop: group with %zu remote peers", table.peers().size()); Log(m);
    }
    g_table = table; g_single = single; g_steam = steam;
    g_myId.store((unsigned)id); g_sock = s; g_peerSet.store(true, std::memory_order_release);
    g_core.store(core, std::memory_order_release);
    VoiceStart();
    if (flags.raceMode) {      // a race: this thread also runs the race state machine, which reports to everybody
        std::vector<unsigned> others; if (table.empty()) others.push_back(id == 1 ? 2u : 1u); else for (const auto& p : table.peers()) others.push_back(p.first);
        RaceStart((unsigned)id, others, [](const std::vector<uint8_t>& b) { CoopSendRaw(b.data(), b.size()); });
    }
    std::map<unsigned, int> lastState; uint64_t nextWatch = GetTickCount64() + 15000;      // Steam: sessions are watched, and a lost one is said in the game
    for (;;) {
        uint64_t now = GetTickCount64();
        core->tick(now);
        if (flags.raceMode) RaceTick(now, core->ready(), core->solvedPanels());
        if (steam && now >= nextWatch) {
            nextWatch = now + 5000;
            for (const auto& kv : table.peers()) {
                int st = wsteam::State(kv.second.steam); int& was = lastState[kv.first];
                bool bad = wpeers::SessionLost(was, st);
                if (bad) { std::string who = "player " + std::to_string(kv.first); for (const auto& r : core->remotePoses(now)) if (r.id == kv.first && !r.name.empty()) who = r.name;
                           Msg("Co-op over Steam: no connection to %s (Steam says %s). Steam keeps trying; check that both of you are online.", who.c_str(), wsteam::StateName(st)); }
                if (st != was) { snprintf(m, sizeof m, "coop: steam session with %u: %s -> %s", kv.first, wsteam::StateName(was), wsteam::StateName(st)); Log(m); }
                was = st;
            }
        }
        uint8_t buf[1500]; int n;
        sockaddr_in source = {}; int sourceLen = sizeof source; uint64_t steamFrom = 0;
        while ((n = steam ? wsteam::Receive(buf, sizeof buf, &steamFrom) : recvfrom(s, (char*)buf, sizeof buf, 0, (sockaddr*)&source, &sourceLen)) > 0) {
            wpeers::Endpoint from = steam ? wpeers::Steam(steamFrom) : wpeers::Udp(source.sin_addr.s_addr, ntohs(source.sin_port));
            sourceLen = sizeof source;
            if (!table.Accept(from, buf, (size_t)n, single)) continue;      // every datagram, pictures included, names its sender: only from that sender's own endpoint
            DiagRxPose(buf, (size_t)n);
            { unsigned sender = n >= 8 ? unsigned(buf[4]) | unsigned(buf[5]) << 8 | unsigned(buf[6]) << 16 | unsigned(buf[7]) << 24 : 0; auto pv = peerVersion.find(sender);
              std::string note = versionNotice.Observe(buf, (size_t)n, MOD_VERSION, pv == peerVersion.end() ? "" : pv->second);      // another version's packet from a player in the roster: said once, not dropped in silence
              if (!note.empty()) Msg("%s", note.c_str()); }      // (the toast logs it too)
            if (n >= 4 && (!memcmp(buf, "WTT1", 4) || !memcmp(buf, "WTU1", 4) || !memcmp(buf, "WTD2", 4))) TvOnPacket(buf, (size_t)n);
            else if (n >= 4 && !memcmp(buf, "WTV4", 4)) VideoOnPacket(buf, (size_t)n);            // a chunk of a POV picture
            else if (n >= 4 && !memcmp(buf, "WTO5", 4)) VoiceOnPacket(buf, (size_t)n);         // 20 ms of a player's voice
            else if (n >= 4 && !memcmp(buf, "WTR1", 4)) RaceOnPacket(buf, (size_t)n, now);         // a race report: phase, progress, finish time
            else if (n >= 4 && !memcmp(buf, "WTR4", 4)) VideoOnRequest(buf, (size_t)n);       // a request for this game's POV pictures
            else core->onPacket(buf, (size_t)n, now);
        }
        Sleep(2);
    }
}

void CoopStart() {
    if (!g_lockInit) { InitializeCriticalSection(&g_lock); g_lockInit = true; }
    HANDLE t = CreateThread(nullptr, 0, NetThread, nullptr, 0, nullptr); if (t) CloseHandle(t);
}
