// See steam_net.h. Runs on coop.cpp's network thread; ISteamNetworkingMessages is thread-safe.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>
#include "steam_net.h"
#include "../common/steam_abi.h"

void Log(const char* msg);
namespace wsteam {
namespace {
using namespace steamabi;
void* g_messages; void* g_utils; uint64_t g_self; std::atomic<unsigned> g_sent{0}, g_received{0}, g_sendFailed{0}, g_dropped{0};
struct In { uint64_t from; std::vector<uint8_t> d; }; std::deque<In> g_in;
}

bool Open(unsigned timeoutMs, uint64_t* self, char* why, size_t cap) {
    auto fail = [&](const char* t) { snprintf(why, cap, "%s", t); return false; };
    HMODULE api = GetModuleHandleW(L"steam_api64.dll");
    if (!api) return fail("The game was started without Steam (steam_api64.dll is not loaded).");
    auto getClient = (void* (*)())GetProcAddress(api, "SteamClient");
    auto getUser = (int (*)())GetProcAddress(api, "SteamAPI_GetHSteamUser");
    auto getPipe = (int (*)())GetProcAddress(api, "SteamAPI_GetHSteamPipe");
    if (!getClient || !getUser || !getPipe) return fail("This steam_api64.dll lacks the calls the mod needs.");
    ULONGLONG until = GetTickCount64() + timeoutMs; int user = 0, pipe = 0;
    while (!(user = getUser()) || !(pipe = getPipe())) {      // the game calls SteamAPI_Init itself, early in its start
        if (GetTickCount64() > until) return fail("The game has no Steam connection. Start the game from the launcher's Steam lobby with Steam running and online.");
        Sleep(250);
    }
    void* client = getClient(); if (!client) return fail("The game has no Steam client interface.");
    void* suser = Call<void*>(client, SC_USER, user, pipe, kUser);
    g_utils = Call<void*>(client, SC_UTILS, pipe, kUtils);
    g_messages = Call<void*>(client, SC_GENERIC, user, pipe, kMessages);
    if (!suser || !g_utils || !g_messages) return fail("Steam's networking is not available. Update Steam and try again.");
    uint32_t app = Call<uint32_t>(g_utils, UT_APP_ID);
    if (app != kAppId) { char b[160]; snprintf(b, sizeof b, "The game runs as Steam app %u, not The Witness (%u). Remove any steam_appid.txt override.", app, kAppId); return fail(b); }
    if (!Call<bool>(suser, SU_LOGGED_ON)) return fail("Steam is offline. Go online in Steam and start again from the lobby.");
    g_self = CallId(suser, SU_ID); if (!g_self) return fail("Steam did not say who is logged in.");
    *self = g_self; char m[160]; snprintf(m, sizeof m, "steam: the game's Steam connection is up (app %u, interfaces %s)", app, kMessages); Log(m);
    return true;
}

bool Send(uint64_t to, const uint8_t* d, size_t n) {
    if (!g_messages || !to || n == 0 || n > 1500) { ++g_sendFailed; return false; }      // (as over UDP: every receiver reads into 1500 bytes; a TV chunk is 1200 pixels plus its header)
    Identity id = IdentityOf(to);
    int r = Call<int>(g_messages, NM_SEND, (const Identity*)&id, (const void*)d, (uint32_t)n, kSendUnreliable | kSendNoNagle | kSendAutoRestartBrokenSession, kGameChannel);
    if (r == 1) { ++g_sent; return true; }
    ++g_sendFailed; return false;
}

int Receive(uint8_t* buf, size_t cap, uint64_t* from) {
    if (!g_messages) return 0;
    if (g_in.empty()) {
        Message* got[32]; int k = Call<int>(g_messages, NM_RECEIVE, kGameChannel, got, 32);
        for (int i = 0; i < k; ++i) {
            Message* m = got[i]; uint64_t peer = IdOf(m->identity);
            if (peer && m->data && m->size > 0) g_in.push_back({peer, std::vector<uint8_t>((const uint8_t*)m->data, (const uint8_t*)m->data + m->size)}); else ++g_dropped;
            m->release(m);      // always released, taken or not
        }
    }
    while (!g_in.empty()) {
        In x = std::move(g_in.front()); g_in.pop_front();
        if (x.d.size() > cap) { ++g_dropped; continue; }
        memcpy(buf, x.d.data(), x.d.size()); *from = x.from; ++g_received; return (int)x.d.size();
    }
    return 0;
}

bool FloatingKeyboard(int x, int y, int w, int h) {
    void* utils = g_utils;
    if (!utils) {      // (the game may run over UDP: the Steam API is the game's own, asked without waiting)
        HMODULE api = GetModuleHandleW(L"steam_api64.dll"); if (!api) return false;
        auto getClient = (void* (*)())GetProcAddress(api, "SteamClient"); auto getPipe = (int (*)())GetProcAddress(api, "SteamAPI_GetHSteamPipe");
        if (!getClient || !getPipe) return false;
        int pipe = getPipe(); void* client = pipe ? getClient() : nullptr; if (!client) return false;
        utils = Call<void*>(client, SC_UTILS, pipe, kUtils); if (!utils) return false;
    }
    bool shown = (Call<bool>(utils, UT_BIG_PICTURE) || Call<bool>(utils, UT_ON_DECK)) && Call<bool>(utils, UT_FLOATING_KEYBOARD, 0 /* single line */, x, y, w, h);
    char m[96]; snprintf(m, sizeof m, "steam: floating keyboard %s", shown ? "shown" : "not available (the mod's own keyboard instead)"); Log(m);
    return shown;
}

int State(uint64_t peer) {
    if (!g_messages) return kConnNone;
    Identity id = IdentityOf(peer); return Call<int>(g_messages, NM_INFO, (const Identity*)&id, (void*)nullptr, (void*)nullptr);
}
const char* StateName(int s) {
    switch (s) { case kConnNone: return "none"; case kConnConnecting: return "connecting"; case kConnFindingRoute: return "finding_route"; case kConnConnected: return "connected";
                 case kConnClosedByPeer: return "closed_by_peer"; case kConnProblemLocally: return "problem_locally"; default: return "other"; }
}
void Stat(const uint64_t* peers, const unsigned* ids, size_t n) {
    char m[200]; snprintf(m, sizeof m, "steamstat: transport steam up %d app %u self_set %d sent %u received %u send_failed %u dropped %u peers %zu", g_messages ? 1 : 0, g_utils ? Call<uint32_t>(g_utils, UT_APP_ID) : 0u, g_self ? 1 : 0,
                          g_sent.load(), g_received.load(), g_sendFailed.load(), g_dropped.load(), n); Log(m);
    for (size_t i = 0; i < n; ++i) { snprintf(m, sizeof m, "steamstat: peer %u state %s", ids[i], StateName(State(peers[i]))); Log(m); }
}
}  // namespace wsteam
