// See lobby_steam.h. The Steam calls follow the MDK2 co-op mod's bridge/steam.c (MIT, same author), on the x64 ABI (common/steam_abi.h).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include "lobby_steam.h"
#include "../common/steam_abi.h"
#include "../common/steam_ids.h"

namespace lsteam {
using namespace steamabi;
namespace {
SteamLobby* g_owner;
struct Cb { CallbackBase base; int bytes; };
void CbRun(CallbackBase* c, void* data) { if (g_owner) g_owner->OnCallback(c->id, data); }
int CbSize(CallbackBase* c) { return ((Cb*)c)->bytes; }
const CallbackVtable kVtable = {CbRun, CbRun, CbSize};
Cb g_cbs[4];
std::string FromSteam(const char* s) { return s ? std::string(s) : std::string(); }
}

bool SteamLobby::Init(const std::wstring& gameDir, std::string* err) {
    if (ready_) return true;
    wchar_t had[32] = {}; if (!GetEnvironmentVariableW(L"SteamAppId", had, 32)) { SetEnvironmentVariableW(L"SteamAppId", L"210970"); SetEnvironmentVariableW(L"SteamGameId", L"210970"); }      // as Steam does when it starts the game
    HMODULE api = LoadLibraryW((gameDir + L"\\steam_api64.dll").c_str());
    if (!api) { *err = "The game folder has no steam_api64.dll. Steam lobbies need the Steam version of The Witness."; return false; }
    auto running = (bool (*)())GetProcAddress(api, "SteamAPI_IsSteamRunning"); auto init = (bool (*)())GetProcAddress(api, "SteamAPI_Init");
    auto getClient = (void* (*)())GetProcAddress(api, "SteamClient"); auto getUser = (int (*)())GetProcAddress(api, "SteamAPI_GetHSteamUser"); auto getPipe = (int (*)())GetProcAddress(api, "SteamAPI_GetHSteamPipe");
    auto reg = (void (*)(CallbackBase*, int))GetProcAddress(api, "SteamAPI_RegisterCallback"); runCallbacks_ = (void (*)())GetProcAddress(api, "SteamAPI_RunCallbacks");
    if (!running || !init || !getClient || !getUser || !getPipe || !reg || !runCallbacks_) { *err = "This steam_api64.dll lacks calls the launcher needs."; return false; }
    if (!running()) { *err = "Steam is not running. Start Steam, log in, and try again."; return false; }
    if (!init()) { *err = "Steam did not let the launcher in as The Witness. Is The Witness in this Steam account's library?"; return false; }
    int user = getUser(), pipe = getPipe(); void* client = getClient();
    if (!user || !pipe || !client) { *err = "Steam started without a user."; return false; }
    user_ = Call<void*>(client, SC_USER, user, pipe, kUser); friends_ = Call<void*>(client, SC_FRIENDS, user, pipe, kFriends);
    mm_ = Call<void*>(client, SC_MATCHMAKING, user, pipe, kMatchmaking); utils_ = Call<void*>(client, SC_UTILS, pipe, kUtils);
    msgs_ = Call<void*>(client, SC_GENERIC, user, pipe, kMessages);
    if (!user_ || !friends_ || !mm_ || !utils_ || !msgs_) { *err = "Steam's lobby or networking interfaces are missing. Update Steam."; return false; }
    uint32_t app = Call<uint32_t>(utils_, UT_APP_ID);
    if (app != kAppId) { *err = "Steam runs this as app " + std::to_string(app) + ", not The Witness (210970). Remove any steam_appid.txt from the game folder."; return false; }
    if (!Call<bool>(user_, SU_LOGGED_ON)) { *err = "Steam is offline. Go online in Steam and try again."; return false; }
    self_ = CallId(user_, SU_ID); if (!self_) { *err = "Steam did not say who is logged in."; return false; }
    g_owner = this; static const int ids[4] = {CB_JOIN, CB_PRESENCE_JOIN, CB_CHAT_UPDATE, CB_SESSION_REQUEST}; static const int sizes[4] = {sizeof(JoinRequested), sizeof(PresenceJoin), sizeof(ChatUpdate), sizeof(Identity)};
    for (int i = 0; i < 4; ++i) { g_cbs[i].base.vtable = &kVtable; g_cbs[i].base.flags = 0; g_cbs[i].base.id = ids[i]; g_cbs[i].bytes = sizes[i]; reg(&g_cbs[i].base, ids[i]); }
    ready_ = true; SetStatus("Steam is ready."); return true;
}

std::string SteamLobby::PersonaName() { return ready_ ? FromSteam(Call<const char*>(friends_, SF_PERSONA_NAME)) : std::string(); }
unsigned SteamLobby::MemberLimit() { return lobby_ ? (unsigned)Call<int>(mm_, MM_MEMBER_LIMIT, lobby_) : 0u; }
std::string SteamLobby::ReadData(const char* key) { return lobby_ ? FromSteam(Call<const char*>(mm_, MM_DATA, lobby_, key)) : std::string(); }

bool SteamLobby::Host(const std::string& code) {
    if (!ready_) return false; if (call_) { SetStatus("Waiting for Steam's answer to the last request."); return false; }
    Leave(); code_ = code; hosting_ = true; wanted_ = 0; failed_ = false;
    call_ = Call<uint64_t>(mm_, MM_CREATE, (int)kLobbyFriendsOnly, (int)wlobby::kMaxMembers); callKind_ = CB_CREATED; callSince_ = GetTickCount64();
    if (!call_) { SetStatus("Steam could not start the lobby. Check your connection and try again.", true); return false; }
    SetStatus("Creating a friends-only Steam lobby ..."); return true;
}
bool SteamLobby::Join(uint64_t lobby) {
    if (!ready_) return false; if (call_) { SetStatus("Waiting for Steam's answer to the last request."); return false; }
    if (lobby == lobby_) return true;
    Leave(); hosting_ = false; wanted_ = lobby; failed_ = false; code_.clear();
    call_ = Call<uint64_t>(mm_, MM_JOIN, lobby); callKind_ = CB_ENTER; callSince_ = GetTickCount64();
    if (!call_) { SetStatus("Steam could not start joining. Check your connection and try again.", true); return false; }
    SetStatus("Joining the Steam lobby ..."); return true;
}
void SteamLobby::Leave() {
    if (!ready_) return;
    for (uint64_t m : members_) if (m != self_) { Identity i = IdentityOf(m); Call<bool>(msgs_, NM_CLOSE, (const Identity*)&i); }
    if (lobby_) { if (owner_ == self_) Call<bool>(mm_, MM_JOINABLE, lobby_, false); Call<void>(mm_, MM_LEAVE, lobby_); }
    bool was = lobby_ != 0; lobby_ = owner_ = 0; members_.clear(); paused_ = false; Call<bool>(friends_, SF_PRESENCE, "connect", (const char*)nullptr);
    if (was) SetStatus("You left the Steam lobby.");
}
bool SteamLobby::InviteDialog() {
    if (!lobby_) { SetStatus("Host a Steam lobby first."); return false; }
    if (!Call<bool>(utils_, UT_OVERLAY)) { SetStatus("The Steam overlay is not available to the launcher: invite a friend from the list, or give them the lobby number."); return false; }
    Call<void>(friends_, SF_INVITE_DIALOG, lobby_); return true;
}
bool SteamLobby::InviteFriend(uint64_t f) {
    if (!lobby_) { SetStatus("Host a Steam lobby first."); return false; }
    bool ok = Call<bool>(mm_, MM_INVITE, lobby_, f); SetStatus(ok ? "Invite sent through Steam chat. Your friend accepts it in Steam." : "Steam did not send the invite.", !ok); return ok;
}
std::vector<std::pair<uint64_t, std::string>> SteamLobby::OnlineFriends() {
    std::vector<std::pair<uint64_t, std::string>> out; if (!ready_) return out;
    int n = Call<int>(friends_, SF_FRIEND_COUNT, (int)kFriendFlagImmediate);
    for (int i = 0; i < n && i < 500; ++i) { uint64_t f = 0; Call<uint64_t*>(friends_, SF_FRIEND_BY_INDEX, &f, i, (int)kFriendFlagImmediate); if (!f || Call<int>(friends_, SF_FRIEND_STATE, f) == 0) continue; out.push_back({f, FromSteam(Call<const char*>(friends_, SF_FRIEND_NAME, f))}); }
    return out;
}

void SteamLobby::Entered(uint64_t lobby, bool hosting) {
    lobby_ = lobby; { uint64_t o = 0; Call<uint64_t*>(mm_, MM_OWNER, &o, lobby); owner_ = o; }
    if (hosting) {
        if (owner_ != self_ || !Call<bool>(mm_, MM_SET_DATA, lobby, kModKey, kModValue) || !Call<bool>(mm_, MM_SET_DATA, lobby, kProtocolKey, kProtocolValue) || !Call<bool>(mm_, MM_SET_DATA, lobby, kTokenKey, code_.c_str())) {
            Leave(); SetStatus("Steam did not take the lobby's settings. Try hosting again.", true); return; }
    } else {
        std::string mod = ReadData(kModKey); if (mod != kModValue) { Leave(); SetStatus("That Steam lobby is not a Witness co-op lobby.", true); return; }
        std::string proto = ReadData(kProtocolKey); if (proto != kProtocolValue) { Leave(); SetStatus("The host has a different version of the co-op mod (" + (proto.empty() ? std::string("unknown") : proto) + ", you have " + kProtocolValue + "). Everyone needs the same version.", true); return; }
        code_ = ReadData(kTokenKey); if (!owner_ || owner_ == self_ || code_.empty()) { Leave(); SetStatus("That Steam lobby has no host any more.", true); return; }
    }
    RefreshMembers(); if (!lobby_) return;
    std::string connect = "+connect_lobby " + steamids::Text(lobby); Call<bool>(friends_, SF_PRESENCE, "connect", connect.c_str());
    SetStatus(hosting ? "Steam lobby ready. Invite friends." : "In the Steam lobby.");
}
void SteamLobby::RefreshMembers() {
    if (!lobby_) return;
    uint64_t o = 0; Call<uint64_t*>(mm_, MM_OWNER, &o, lobby_);
    if (o != owner_) { Leave(); SetStatus("The host left the Steam lobby. Join their new lobby when they host again.", true); return; }      // no host migration: the host is the authority
    int n = Call<int>(mm_, MM_MEMBERS, lobby_); std::vector<uint64_t> now;
    for (int i = 0; i < n && i < 64; ++i) { uint64_t m = 0; Call<uint64_t*>(mm_, MM_MEMBER, &m, lobby_, i); if (m) now.push_back(m); }
    for (uint64_t old : members_) { bool still = false; for (uint64_t m : now) still = still || m == old; if (!still && old != self_) { Identity i = IdentityOf(old); Call<bool>(msgs_, NM_CLOSE, (const Identity*)&i); } }
    std::vector<uint64_t> fresh; for (uint64_t m : now) { bool known = false; for (uint64_t o : members_) known = known || o == m; if (!known && m != self_) fresh.push_back(m); }
    members_ = now;
    static const uint8_t kHello[4] = {'W', 'T', 'S', '0'};      // a first message to every new member: a session we started is accepted without waiting for Steam's session request (the lobby ignores it)
    for (uint64_t m : fresh) Send(m, kHello, sizeof kHello);
}

void SteamLobby::OnCallback(int id, void* data) {
    if (id == CB_JOIN) { joinRequest_ = ((JoinRequested*)data)->lobby; }
    else if (id == CB_PRESENCE_JOIN) { PresenceJoin* p = (PresenceJoin*)data; char c[257]; memcpy(c, p->connect, 256); c[256] = 0; uint64_t l = 0; if (steamids::ParseConnect(c, &l)) joinRequest_ = l; }
    else if (id == CB_CHAT_UPDATE) { ChatUpdate* u = (ChatUpdate*)data; if (u->lobby == lobby_) { if ((u->state & (2 | 4 | 8 | 16)) && (u->changed == self_ || u->changed == owner_)) { Leave(); SetStatus(u->changed == self_ ? "You are no longer in the Steam lobby." : "The host left the Steam lobby.", true); return; } RefreshMembers(); } }
    else if (id == CB_SESSION_REQUEST) {      // somebody wants to talk: only lobby members, and not while this player's game has the Steam connection
        Identity* who = (Identity*)data; uint64_t peer = IdOf(*who); RefreshMembers();
        if (!paused_ && Gate().Allowed(peer)) Call<bool>(msgs_, NM_ACCEPT, (const Identity*)who); else Call<bool>(msgs_, NM_CLOSE, (const Identity*)who);
    }
}

void SteamLobby::Poll(uint64_t now) {
    if (!ready_) return;
    runCallbacks_();
    if (call_) {
        bool failed = false;
        if (!Call<bool>(utils_, UT_COMPLETED, call_, &failed)) { if (now - callSince_ > 30000) SetStatus("Steam is taking long to answer. Check Steam's connection.", false); }
        else {
            uint64_t call = call_; int kind = callKind_; call_ = 0;
            union { Created created; Enter enter; } r; memset(&r, 0, sizeof r); int size = kind == CB_CREATED ? (int)sizeof r.created : (int)sizeof r.enter;
            if (failed || !Call<bool>(utils_, UT_RESULT, call, (void*)&r, size, kind, &failed) || failed) SetStatus("Steam's lobby request failed. Check that Steam is online and try again.", true);
            else if (kind == CB_CREATED && r.created.result == 1 && r.created.lobby) Entered(r.created.lobby, true);
            else if (kind == CB_CREATED) SetStatus("Steam could not create a lobby (result " + std::to_string(r.created.result) + "). Try again in a moment.", true);
            else if (r.enter.response == kEnterSuccess && r.enter.lobby == wanted_) Entered(r.enter.lobby, false);
            else if (r.enter.response == kEnterFull) SetStatus("That Steam lobby is full (16 players).", true);
            else if (r.enter.response == kEnterDoesntExist) SetStatus("That Steam lobby does not exist any more. Ask the host for a new invite.", true);
            else if (r.enter.response == kEnterNotAllowed || r.enter.response == kEnterBanned || r.enter.response == kEnterLimited) SetStatus("Steam did not let you into that lobby (it is for the host's friends).", true);
            else SetStatus("Could not join that Steam lobby. Ask the host for a new invite.", true);
        }
    }
    if (lobby_ && now >= nextMembers_) { RefreshMembers(); nextMembers_ = now + 1000; }
}

bool SteamLobby::Send(uint64_t to, const uint8_t* d, size_t n) {
    if (!ready_ || paused_ || !Gate().Allowed(to) || n == 0 || n > 1500) return false;
    Identity i = IdentityOf(to);
    return Call<int>(msgs_, NM_SEND, (const Identity*)&i, (const void*)d, (uint32_t)n, kSendUnreliable | kSendNoNagle | kSendAutoRestartBrokenSession, kLobbyChannel) == 1;
}
int SteamLobby::Receive(uint8_t* buf, size_t cap, uint64_t* from) {
    if (!ready_ || paused_) return 0;
    for (;;) {
        Message* m = nullptr; if (Call<int>(msgs_, NM_RECEIVE, kLobbyChannel, &m, 1) != 1 || !m) return 0;
        uint64_t peer = IdOf(m->identity); int n = m->size; bool ok = peer && m->data && n > 0 && (size_t)n <= cap;
        if (ok) { memcpy(buf, m->data, (size_t)n); *from = peer; }
        m->release(m);
        if (ok) return n;
    }
}
void SteamLobby::Pause(bool on) {
    if (on == paused_) return; paused_ = on;
    if (on) for (uint64_t m : members_) if (m != self_) { Identity i = IdentityOf(m); Call<bool>(msgs_, NM_CLOSE, (const Identity*)&i); }
}
}  // namespace lsteam
