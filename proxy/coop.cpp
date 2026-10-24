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
#include "coop.h"

void Log(const char* msg);
extern HMODULE g_selfModule();

static CRITICAL_SECTION g_lock; static bool g_lockInit;
static witness::SyncCore* g_core;
static SOCKET g_sock = INVALID_SOCKET; static sockaddr_in g_peerAddr; static bool g_peerSet;
void CoopSendRaw(const uint8_t* d, size_t n) { if (g_sock != INVALID_SOCKET && g_peerSet) sendto(g_sock, (const char*)d, (int)n, 0, (sockaddr*)&g_peerAddr, sizeof g_peerAddr); }
void VideoOnPacket(const uint8_t* data, size_t n);

witness::SyncCore* CoopCore() { return g_core; }
static volatile LONG g_fakeOn; static float g_fake[4]; static volatile LONG g_fakeSeq;     // test hook (witness_cmd.txt: fakepose): a pose the peer never sent
void CoopSetFakeRemote(bool on, const float* pos, float yaw) {
    if (on) { g_fake[0] = pos[0]; g_fake[1] = pos[1]; g_fake[2] = pos[2]; g_fake[3] = yaw; InterlockedIncrement(&g_fakeSeq); }
    InterlockedExchange(&g_fakeOn, on ? 1 : 0);
}
bool CoopGetRemote(RemotePose* out) {
    if (g_fakeOn) { out->valid = true; memcpy(out->pos, g_fake, 12); out->ang[0] = g_fake[3]; out->ang[1] = 0; out->ageMs = 0; out->seq = (unsigned)g_fakeSeq; out->id = 99; return true; }
    witness::SyncCore* c = g_core; if (!c) return false;
    witness::RemotePose r; bool ok = c->remotePose(&r, GetTickCount64());
    out->valid = r.valid; memcpy(out->pos, r.pos, sizeof out->pos); memcpy(out->ang, r.ang, sizeof out->ang);
    out->ageMs = (DWORD)r.ageMs; out->seq = r.seq; out->id = r.id;
    return ok;
}

static DWORD WINAPI NetThread(LPVOID) {
    wchar_t wp[MAX_PATH]; GetModuleFileNameW(g_selfModule(), wp, MAX_PATH);
    wchar_t* sl = wcsrchr(wp, L'\\'); if (sl) *(sl + 1) = 0; wcscat_s(wp, L"witness_coop.cfg");
    FILE* f = _wfopen(wp, L"r");
    if (!f) { Log("coop: no witness_coop.cfg, networking off"); return 0; }
    witness::Flags flags; int id = 0, listenPort = 0, peerPort = 0; char host[64] = "127.0.0.1", line[200]; int dropPct = 0; uint32_t dropState = 1;
    while (fgets(line, sizeof line, f)) {
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
    }
    fclose(f);
    WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in me = {}; me.sin_family = AF_INET; me.sin_addr.s_addr = htonl(INADDR_LOOPBACK); me.sin_port = htons((u_short)listenPort);
    if (strcmp(host, "127.0.0.1") != 0) me.sin_addr.s_addr = INADDR_ANY;
    if (bind(s, (sockaddr*)&me, sizeof me) != 0) { char m[100]; snprintf(m, sizeof m, "coop: bind %d failed (%d)", listenPort, WSAGetLastError()); Log(m); return 0; }
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
    sockaddr_in peer = {}; peer.sin_family = AF_INET; peer.sin_addr.s_addr = inet_addr(host); peer.sin_port = htons((u_short)peerPort);
    g_sock = s; g_peerAddr = peer; g_peerSet = true;
    char m[160]; snprintf(m, sizeof m, "coop: id=%d listening %d, sending to %s:%d", id, listenPort, host, peerPort); Log(m);

    witness::Hooks h;
    h.send = [&](const witness::Bytes& b) {
        if (dropPct > 0) { dropState = dropState * 1664525u + 1013904223u; if ((int)((dropState >> 16) % 100) < dropPct) return; }   // deterministic seeded loss
        sendto(s, (const char*)b.data(), (int)b.size(), 0, (sockaddr*)&peer, sizeof peer); };
    h.log = [](const char* t) { Log(t); };
    h.lock = [] { EnterCriticalSection(&g_lock); }; h.unlock = [] { LeaveCriticalSection(&g_lock); };
    uint32_t session = (uint32_t)time(nullptr);     // must grow with every start: the core ignores packets from an older session
    witness::SyncCore* core = new witness::SyncCore(id, session, GameWorldInstance(), h, flags);
    g_core = core;
    for (;;) {
        uint64_t now = GetTickCount64();
        core->tick(now);
        uint8_t buf[1500]; int n;
        while ((n = recv(s, (char*)buf, sizeof buf, 0)) > 0) { if (n > 128) VideoOnPacket(buf, (size_t)n); else core->onPacket(buf, (size_t)n, now); }
        Sleep(2);
    }
}

void CoopStart() {
    if (!g_lockInit) { InitializeCriticalSection(&g_lock); g_lockInit = true; }
    HANDLE t = CreateThread(nullptr, 0, NetThread, nullptr, 0, nullptr); if (t) CloseHandle(t);
}
