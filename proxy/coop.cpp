// UDP transport and configuration for the sync core (core/sync_core.cpp holds all the logic and is tested without the game).
// witness_coop.cfg (next to the DLL):  id=1  listen=47001  peer=47002  [peer_host=127.0.0.1]
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

void Log(const char* msg);
extern HMODULE g_selfModule();

static CRITICAL_SECTION g_lock; static bool g_lockInit;
static std::atomic<witness::SyncCore*> g_core{nullptr};
static std::vector<sockaddr_in> g_rawPeers;
static SOCKET g_sock = INVALID_SOCKET; static sockaddr_in g_peerAddr; static std::atomic<bool> g_peerSet{false};
void CoopSendRaw(const uint8_t* d, size_t n) {
    if (!g_peerSet.load(std::memory_order_acquire)) return;
    if (g_rawPeers.empty()) sendto(g_sock, (const char*)d, (int)n, 0, (sockaddr*)&g_peerAddr, sizeof g_peerAddr);
    else for (const auto& addr : g_rawPeers) sendto(g_sock, (const char*)d, (int)n, 0, (const sockaddr*)&addr, sizeof addr);
}
void VideoOnPacket(const uint8_t* data, size_t n);

witness::SyncCore* CoopCore() { return g_core.load(std::memory_order_acquire); }
static volatile LONG g_fakeOn; static float g_fake[4]; static volatile LONG g_fakeSeq;     // test hook (witness_cmd.txt: fakepose): a pose the peer never sent
void CoopSetFakeRemote(bool on, const float* pos, float yaw) {
    if (on) { g_fake[0] = pos[0]; g_fake[1] = pos[1]; g_fake[2] = pos[2]; g_fake[3] = yaw; InterlockedIncrement(&g_fakeSeq); }
    InterlockedExchange(&g_fakeOn, on ? 1 : 0);
}
std::vector<witness::RemoteLine> CoopGetRemoteLines() { if (auto* c = CoopCore()) return c->remoteLines(GetTickCount64()); return {}; }
std::vector<RemotePose> CoopGetRemotes() {
    if (g_fakeOn) { RemotePose p; CoopGetRemote(&p); return {p}; }
    std::vector<RemotePose> out;
    if (auto* c = CoopCore()) for (const auto& r : c->remotePoses(GetTickCount64())) {
        RemotePose p; p.valid = r.valid; memcpy(p.pos, r.pos, sizeof p.pos); memcpy(p.ang, r.ang, sizeof p.ang);
        p.ageMs = (DWORD)r.ageMs; p.seq = r.seq; p.id = r.id; out.push_back(p);
    }
    return out;
}
bool CoopGetRemote(RemotePose* out, unsigned id) {
    if (g_fakeOn) { out->valid = true; memcpy(out->pos, g_fake, 12); out->ang[0] = g_fake[3]; out->ang[1] = 0; out->ageMs = 0; out->seq = (unsigned)g_fakeSeq; out->id = 99; return true; }
    witness::SyncCore* c = CoopCore(); if (!c) return false;
    witness::RemotePose r; bool ok = false;
    for (auto pose : c->remotePoses(GetTickCount64())) if (!id || pose.id == id) { r = pose; ok = true; break; }
    out->valid = r.valid; memcpy(out->pos, r.pos, sizeof out->pos); memcpy(out->ang, r.ang, sizeof out->ang);
    out->ageMs = (DWORD)r.ageMs; out->seq = r.seq; out->id = r.id;
    return ok;
}

void CoopDumpStatus(unsigned submittedPlayers) {
    auto* c = CoopCore(); if (!c) return;
    auto remotes = c->remotePoses(GetTickCount64());
    wchar_t path[MAX_PATH]; GetModuleFileNameW(g_selfModule(), path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\'); if (slash) *(slash + 1) = 0;
    wcscat_s(path, L"witness_net.json");
    wchar_t tmp[MAX_PATH]; wcscpy_s(tmp, path); wcscat_s(tmp, L".tmp");
    FILE* f = _wfopen(tmp, L"w"); if (!f) return;
    fprintf(f, "{\"ready\":%s,\"unacked\":%zu,\"queued\":%zu,\"remotes\":[", c->ready() ? "true" : "false", c->unacked(), c->queuedRemote());
    bool comma = false;
    for (const auto& r : remotes) {
        fprintf(f, "%s{\"id\":%u,\"seq\":%u,\"age_ms\":%llu}", comma ? "," : "", r.id, r.seq, (unsigned long long)r.ageMs); comma = true;
    }
    fprintf(f, "],\"submitted_models\":%u}\n", submittedPlayers); fclose(f); MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING);
}

static DWORD WINAPI NetThread(LPVOID) {
    wchar_t wp[MAX_PATH]; GetModuleFileNameW(g_selfModule(), wp, MAX_PATH);
    wchar_t* sl = wcsrchr(wp, L'\\'); if (sl) *(sl + 1) = 0; wcscat_s(wp, L"witness_coop.cfg");
    FILE* f = _wfopen(wp, L"r");
    if (!f) { Log("coop: no witness_coop.cfg, networking off"); return 0; }
    witness::Flags flags; int id = 0, listenPort = 0, peerPort = 0; char host[64] = "127.0.0.1", line[200]; int dropPct = 0; uint32_t dropState = 1;
    std::map<unsigned, sockaddr_in> endpoints; bool badConfig = false;
    while (fgets(line, sizeof line, f)) {
        unsigned peerId; int port; char peerHost[64];
        if (sscanf(line, "peer=%u,%63[^,],%d", &peerId, peerHost, &port) == 3) {
            sockaddr_in addr = {}; addr.sin_family = AF_INET; addr.sin_port = htons((u_short)port); addr.sin_addr.s_addr = inet_addr(peerHost);
            if (!peerId || peerId > 0x7fffffffU || port < 1 || port > 65535 || addr.sin_addr.s_addr == INADDR_NONE || endpoints.size() >= witness::kMaxPlayers - 1 || !endpoints.emplace(peerId, addr).second) badConfig = true;
            continue;
        }
        if (!strncmp(line, "peer=", 5) && strchr(line, ',')) { badConfig = true; continue; }
        int v; sscanf(line, "id=%d", &id); sscanf(line, "listen=%d", &listenPort); sscanf(line, "peer=%d", &peerPort); sscanf(line, "peer_host=%63s", host);
        if (sscanf(line, "drop_first_event=%d", &v) == 1) flags.dropFirstEvent = v;
        if (sscanf(line, "drop_pct=%d", &v) == 1) dropPct = v;
        if (sscanf(line, "drop_seed=%d", &v) == 1) dropState = (uint32_t)v * 2654435761u + 1;
        if (sscanf(line, "no_resend=%d", &v) == 1) flags.noResend = v;
        if (sscanf(line, "no_echo_guard=%d", &v) == 1) flags.noEchoGuard = v;
        if (sscanf(line, "no_resync_on_restart=%d", &v) == 1) flags.noResyncOnRestart = v;
        if (sscanf(line, "no_panel_power=%d", &v) == 1) flags.noPanelPower = v;
        if (sscanf(line, "no_reload_sync=%d", &v) == 1) flags.noReloadSync = v;
        if (sscanf(line, "no_cable_sync=%d", &v) == 1) flags.noCableSync = v;
        if (sscanf(line, "no_cable_off=%d", &v) == 1) flags.noCableOff = v;
        if (sscanf(line, "no_off_snapshot=%d", &v) == 1) flags.noOffSnapshot = v;
        if (sscanf(line, "no_cable_arbiter=%d", &v) == 1) flags.noCableArbiter = v;
        if (sscanf(line, "no_snap_reassert=%d", &v) == 1) flags.noSnapReassert = v;
        if (sscanf(line, "no_line_send=%d", &v) == 1) flags.noLineSend = v;
        if (sscanf(line, "no_line_resend=%d", &v) == 1) flags.noLineResend = v;
    }
    fclose(f);
    if (id < 1 || listenPort < 1 || listenPort > 65535 || badConfig || endpoints.count((unsigned)id) || (endpoints.empty() && (peerPort < 1 || peerPort > 65535))) {
        Log("coop: invalid id, port or peer roster; networking off"); return 0;
    }
    for (auto a = endpoints.begin(); a != endpoints.end(); ++a) for (auto b = endpoints.begin(); b != a; ++b)
        if (a->second.sin_port == b->second.sin_port && a->second.sin_addr.s_addr == b->second.sin_addr.s_addr) {
            Log("coop: duplicate peer endpoint; networking off"); return 0;
        }
    WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in me = {}; me.sin_family = AF_INET; me.sin_addr.s_addr = htonl(INADDR_LOOPBACK); me.sin_port = htons((u_short)listenPort);
    if (strcmp(host, "127.0.0.1") != 0) me.sin_addr.s_addr = INADDR_ANY;
    for (const auto& p : endpoints) if (p.second.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) me.sin_addr.s_addr = INADDR_ANY;
    if (bind(s, (sockaddr*)&me, sizeof me) != 0) { char m[100]; snprintf(m, sizeof m, "coop: bind %d failed (%d)", listenPort, WSAGetLastError()); Log(m); return 0; }
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
    sockaddr_in peer = {}; peer.sin_family = AF_INET; peer.sin_addr.s_addr = inet_addr(host); peer.sin_port = htons((u_short)peerPort);
    if (!endpoints.empty()) peer = endpoints.begin()->second;
    char m[160]; snprintf(m, sizeof m, "coop: id=%d listening %d, sending to %s:%d", id, listenPort, host, peerPort); Log(m);

    witness::Hooks h;
    auto transmit = [&](const sockaddr_in& addr, const witness::Bytes& b) {
        if (dropPct > 0) { dropState = dropState * 1664525u + 1013904223u; if ((int)((dropState >> 16) % 100) < dropPct) return; }   // deterministic seeded loss
        sendto(s, (const char*)b.data(), (int)b.size(), 0, (const sockaddr*)&addr, sizeof addr); };
    if (endpoints.empty()) h.send = [&](const witness::Bytes& b) { transmit(peer, b); };
    else h.sendTo = [&](unsigned target, const witness::Bytes& b) { auto it = endpoints.find(target); if (it != endpoints.end()) transmit(it->second, b); };
    h.log = [](const char* t) { Log(t); };
    h.lock = [] { EnterCriticalSection(&g_lock); }; h.unlock = [] { LeaveCriticalSection(&g_lock); };
    uint32_t session = (uint32_t)time(nullptr);     // must grow with every start: the core ignores packets from an older session
    witness::SyncCore* core = new witness::SyncCore(id, session, GameWorldInstance(), h, flags);
    if (!endpoints.empty()) {
        std::vector<unsigned> ids; for (const auto& p : endpoints) ids.push_back(p.first);
        if (!core->setPeers(ids)) { Log("coop: invalid roster"); delete core; closesocket(s); return 0; }
        snprintf(m, sizeof m, "coop: group with %zu remote peers", endpoints.size()); Log(m);
    }
    for (const auto& p : endpoints) g_rawPeers.push_back(p.second);
    g_sock = s; g_peerAddr = peer; g_peerSet.store(true, std::memory_order_release);
    g_core.store(core, std::memory_order_release);
    for (;;) {
        uint64_t now = GetTickCount64();
        core->tick(now);
        uint8_t buf[1500]; int n;
        sockaddr_in source = {}; int sourceLen = sizeof source;
        while ((n = recvfrom(s, (char*)buf, sizeof buf, 0, (sockaddr*)&source, &sourceLen)) > 0) {
            const sockaddr_in* expected = &peer;
            if (!endpoints.empty()) {
                if (n < 8) continue;
                unsigned from = unsigned(buf[4]) | unsigned(buf[5]) << 8 | unsigned(buf[6]) << 16 | unsigned(buf[7]) << 24;
                // Video remains one selected peer's stream; its legacy header has no sender id.
                if (n > 128) expected = &endpoints.begin()->second;
                else { auto it = endpoints.find(from); if (it == endpoints.end()) continue; expected = &it->second; }
            }
            if (source.sin_port != expected->sin_port || source.sin_addr.s_addr != expected->sin_addr.s_addr) continue;
            if (n >= 4 && !memcmp(buf, "WTL4", 4)) core->onPacket(buf, (size_t)n, now);      // a drawn line (can be longer than a pose or an event)
            else if (n > 128) VideoOnPacket(buf, (size_t)n); else core->onPacket(buf, (size_t)n, now);
            sourceLen = sizeof source;
        }
        Sleep(2);
    }
}

void CoopStart() {
    if (!g_lockInit) { InitializeCriticalSection(&g_lock); g_lockInit = true; }
    HANDLE t = CreateThread(nullptr, 0, NetThread, nullptr, 0, nullptr); if (t) CloseHandle(t);
}
