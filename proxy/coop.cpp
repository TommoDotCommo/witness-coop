// Two-player pose exchange over UDP (step 1 of co-op: each player sees the other as a puppet).
// witness_coop.cfg (next to the DLL):  id=1  listen=47001  peer=47002  [peer_host=127.0.0.1]
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "coop.h"

void Log(const char* msg);
extern HMODULE g_selfModule();

static const uintptr_t kCamPos = 0x6304B0 + 0x10, kCamAng = 0x6303EC;
static CRITICAL_SECTION g_lock; static bool g_lockInit;
#pragma pack(push, 1)
struct Packet { uint32_t magic, id, seq; float pos[3], ang[2]; uint32_t session; };   // pose, 30 Hz
struct EvPacket { uint32_t magic, id, epoch, seq, kind, entity; };                       // reliable event
struct AckPacket { uint32_t magic, id, epoch, ack; };                                    // cumulative ack of the peer's stream
#pragma pack(pop)
static const uint32_t kMagic = 0x50435457;     // "WTCP" pose
static const uint32_t kMagicEv = 0x45435457;   // "WTCE" event
static const uint32_t kMagicAck = 0x41435457;  // "WTCA" ack

struct SentEv { uint32_t seq, kind, entity; bool acked; DWORD lastSend; };
static SentEv g_sent[4096]; static int g_sentN;                  // my stream, oldest first
static uint32_t g_epoch = 1, g_nextSeq = 1, g_session;           // my stream epoch, my process session
static uint32_t g_peerSession, g_peerEpoch, g_peerApplied;       // what I know of the peer's stream
static bool g_resync; static bool g_flag[3];
bool CoopTestFlag(int w) { return g_flag[w]; }
struct RecvEv { uint8_t kind; uint32_t entity; }; static RecvEv g_recvQ[1024]; static int g_recvN;
static volatile LONG g_stats[6];

void CoopSendEvent(uint8_t kind, uint32_t entity) {
    if (!g_lockInit) return;
    EnterCriticalSection(&g_lock);
    if (g_sentN < 4096) { g_sent[g_sentN++] = {g_nextSeq++, kind, entity, false, 0}; InterlockedIncrement(&g_stats[0]); }
    LeaveCriticalSection(&g_lock);
}
bool CoopPopRemoteEvent(uint8_t* kind, uint32_t* entity) {
    if (!g_lockInit) return false;
    bool got = false;
    EnterCriticalSection(&g_lock);
    if (g_recvN > 0) { *kind = g_recvQ[0].kind; *entity = g_recvQ[0].entity; memmove(g_recvQ, g_recvQ + 1, sizeof(RecvEv) * (--g_recvN)); got = true; }
    LeaveCriticalSection(&g_lock);
    return got;
}
bool CoopTakeResyncRequest() {
    if (!g_lockInit) return false;
    EnterCriticalSection(&g_lock); bool r = g_resync; g_resync = false; LeaveCriticalSection(&g_lock);
    return r;
}
unsigned CoopStats(int w) { return (unsigned)g_stats[w]; }

static RemotePose g_remote; static DWORD g_remoteTick;

bool CoopGetRemote(RemotePose* out) {
    if (!g_lockInit) return false;
    EnterCriticalSection(&g_lock);
    *out = g_remote; out->ageMs = g_remote.valid ? GetTickCount() - g_remoteTick : 0;
    LeaveCriticalSection(&g_lock);
    return out->valid;
}

static DWORD WINAPI NetThread(LPVOID) {
    wchar_t wp[MAX_PATH]; GetModuleFileNameW(g_selfModule(), wp, MAX_PATH);
    wchar_t* sl = wcsrchr(wp, L'\\'); if (sl) *(sl + 1) = 0; wcscat_s(wp, L"witness_coop.cfg");
    FILE* f = _wfopen(wp, L"r");
    if (!f) { Log("coop: no witness_coop.cfg, networking off"); return 0; }
    g_session = GetTickCount() ^ (GetCurrentProcessId() * 2654435761u); g_epoch = g_session;
    int id = 0, listenPort = 0, peerPort = 0; char host[64] = "127.0.0.1", line[200];
    while (fgets(line, sizeof line, f)) { sscanf(line, "id=%d", &id); sscanf(line, "listen=%d", &listenPort); sscanf(line, "peer=%d", &peerPort); sscanf(line, "peer_host=%63s", host); int v; if (sscanf(line, "drop_first_event=%d", &v) == 1) g_flag[0] = v; if (sscanf(line, "no_resend=%d", &v) == 1) g_flag[1] = v; if (sscanf(line, "no_echo_guard=%d", &v) == 1) g_flag[2] = v; }
    fclose(f);
    WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in me = {}; me.sin_family = AF_INET; me.sin_addr.s_addr = htonl(INADDR_LOOPBACK); me.sin_port = htons((u_short)listenPort);
    if (strcmp(host, "127.0.0.1") != 0) me.sin_addr.s_addr = INADDR_ANY;
    if (bind(s, (sockaddr*)&me, sizeof me) != 0) { char m[100]; snprintf(m, sizeof m, "coop: bind %d failed (%d)", listenPort, WSAGetLastError()); Log(m); return 0; }
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
    sockaddr_in peer = {}; peer.sin_family = AF_INET; peer.sin_addr.s_addr = inet_addr(host); peer.sin_port = htons((u_short)peerPort);
    char m[160]; snprintf(m, sizeof m, "coop: id=%d listening %d, sending to %s:%d", id, listenPort, host, peerPort); Log(m);
    uintptr_t base = (uintptr_t)GetModuleHandleA(nullptr); uint32_t seq = 0; DWORD nextSend = 0; unsigned got = 0;
    for (;;) {
        DWORD now = GetTickCount();
        if ((int)(now - nextSend) >= 0) {
            nextSend = now + 33;
            const float* cam = (const float*)(base + kCamPos); const float* ang = (const float*)(base + kCamAng);
            if (!IsBadReadPtr(cam, 12) && !IsBadReadPtr(ang, 8) && (cam[0] != 0 || cam[1] != 0)) {
                Packet p = {kMagic, (uint32_t)id, ++seq, {cam[0], cam[1], cam[2]}, {ang[0], ang[1]}, g_session};
                sendto(s, (const char*)&p, sizeof p, 0, (sockaddr*)&peer, sizeof peer);
            }
        }
        {   // resend unacknowledged events, oldest first, at most 32 per round
            static DWORD nextResend; DWORD t = GetTickCount();
            if ((int)(t - nextResend) >= 0) {
                nextResend = t + 150; int sentNow = 0;
                EnterCriticalSection(&g_lock);
                for (int i = 0; i < g_sentN && sentNow < 32; i++) {
                    if (g_sent[i].acked) continue;
                    if (g_flag[1] && g_sent[i].lastSend) continue;                      // test: never resend
                    EvPacket e = {kMagicEv, (uint32_t)id, g_epoch, g_sent[i].seq, g_sent[i].kind, g_sent[i].entity};
                    bool dropIt = g_flag[0] && !g_sent[i].lastSend;                     // test: first send of each event is lost
                    if (!dropIt) sendto(s, (const char*)&e, sizeof e, 0, (sockaddr*)&peer, sizeof peer);
                    sentNow++;
                    if (g_sent[i].lastSend) InterlockedIncrement(&g_stats[1]);
                    g_sent[i].lastSend = t;
                }
                LeaveCriticalSection(&g_lock);
            }
        }
        char buf[128]; int n;
        while ((n = recv(s, buf, sizeof buf, 0)) > 0) {
            uint32_t magic = n >= 4 ? *(uint32_t*)buf : 0;
            if (magic == kMagicEv && n == (int)sizeof(EvPacket)) {
                EvPacket* e = (EvPacket*)buf; if ((int)e->id == id) continue;
                bool dup = false, ordered = false;
                EnterCriticalSection(&g_lock);
                if (e->epoch != g_peerEpoch) { g_peerEpoch = e->epoch; g_peerApplied = 0; }
                if (e->seq == g_peerApplied + 1 && g_recvN < 1024) { g_recvQ[g_recvN++] = {(uint8_t)e->kind, e->entity}; g_peerApplied = e->seq; ordered = true; }
                else if (e->seq <= g_peerApplied) dup = true;
                AckPacket a = {kMagicAck, (uint32_t)id, g_peerEpoch, g_peerApplied};
                LeaveCriticalSection(&g_lock);
                InterlockedIncrement(&g_stats[ordered ? 2 : (dup ? 3 : 4)]);
                sendto(s, (const char*)&a, sizeof a, 0, (sockaddr*)&peer, sizeof peer);
                continue;
            }
            if (magic == kMagicAck && n == (int)sizeof(AckPacket)) {
                AckPacket* a = (AckPacket*)buf; if ((int)a->id == id || a->epoch != g_epoch) continue;
                EnterCriticalSection(&g_lock);
                for (int i = 0; i < g_sentN; i++) if (g_sent[i].seq <= a->ack && !g_sent[i].acked) { g_sent[i].acked = true; InterlockedIncrement(&g_stats[5]); }
                LeaveCriticalSection(&g_lock);
                continue;
            }
            if (n != (int)sizeof(Packet)) continue;
            Packet& in = *(Packet*)buf;
            if (in.magic != kMagic || (int)in.id == id) continue;
            if (in.session != g_peerSession) {   // peer (re)started: begin a new stream epoch and resend the full state
                EnterCriticalSection(&g_lock);
                g_peerSession = in.session; g_epoch = GetTickCount() ^ (g_peerSession * 2654435761u); g_nextSeq = 1; g_sentN = 0; g_resync = true;
                LeaveCriticalSection(&g_lock);
                char l[120]; snprintf(l, sizeof l, "coop: peer session %08x seen, new event epoch %08x, full state resync", in.session, g_epoch); Log(l);
            }
            EnterCriticalSection(&g_lock);
            g_remote.valid = true; g_remote.id = in.id; g_remote.seq = in.seq;
            memcpy(g_remote.pos, in.pos, 12); memcpy(g_remote.ang, in.ang, 8); g_remoteTick = GetTickCount();
            LeaveCriticalSection(&g_lock);
            if (++got == 1 || got % 300 == 0) { char l[200]; snprintf(l, sizeof l, "coop: recv #%u from id %u seq %u pos (%.2f,%.2f,%.2f) ang (%.3f,%.3f)", got, in.id, in.seq, in.pos[0], in.pos[1], in.pos[2], in.ang[0], in.ang[1]); Log(l); }
        }
        Sleep(2);
    }
}

void CoopStart() {
    if (!g_lockInit) { InitializeCriticalSection(&g_lock); g_lockInit = true; }
    HANDLE t = CreateThread(nullptr, 0, NetThread, nullptr, 0, nullptr); if (t) CloseHandle(t);
}
