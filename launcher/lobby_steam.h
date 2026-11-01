#pragma once
// The lobby over Steam (Windows): a friends-only Steam lobby, invites through the Steam overlay or a Steam chat invite, rich presence "connect" so "Join Game"
// in the friends list works, and the lobby's messages relayed by Steam (ISteamNetworkingMessages, channel 1), so nobody forwards a port.
// The launcher is its own process: it loads the game folder's steam_api64.dll and starts the Steam API as The Witness (app 210970) by setting the SteamAppId
// environment variable, as Steam does when it starts a game. Nothing is written into the game folder (no steam_appid.txt). The game the launcher starts inherits
// that environment, so the game's own SteamAPI_Init succeeds and the mod in the game uses the game's connection (proxy/steam_net.h).
// While this launcher's game runs, the launcher stops its own Steam messages (Pause) so that only one process of this player talks to the others over Steam.
// Every call runs on one thread (the lobby session's), apart from Init.
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "lobby.h"

namespace lsteam {
constexpr const char* kModKey = "witness_mod"; constexpr const char* kModValue = "witness-coop";
constexpr const char* kProtocolKey = "witness_protocol"; constexpr const char* kProtocolValue = "lobby2-net1";      // the lobby protocol (lobby.cpp kVersion) and the games' peer syntax
constexpr const char* kTokenKey = "witness_code";
constexpr int kLobbyChannel = 1;

class SteamLobby {
public:
    bool Init(const std::wstring& gameDir, std::string* err);      // once per process; never shut down (the process's exit ends it)
    bool Ready() const { return ready_; }
    void Poll(uint64_t nowMs);                                      // callbacks, lobby requests, the member list
    bool Host(const std::string& code);                             // a friends-only lobby for 16; async: InLobby() when done, Status() says how it went
    bool Join(uint64_t lobby);                                      // async
    void Leave();
    bool InviteDialog();                                            // the Steam overlay's invite dialog; false (and the status says so) when the overlay is off
    bool InviteFriend(uint64_t friendId);                           // a Steam chat invite: works without the overlay
    std::vector<std::pair<uint64_t, std::string>> OnlineFriends();
    std::string PersonaName();
    // the transport (only lobby members)
    bool Send(uint64_t to, const uint8_t* d, size_t n);
    int Receive(uint8_t* buf, size_t cap, uint64_t* from);
    void Pause(bool on);                                            // while this launcher's game runs: nothing is sent, nothing is read, sessions are closed
    bool Paused() const { return paused_; }
    // what the lobby is
    bool InLobby() const { return lobby_ != 0; }
    bool IsOwner() const { return lobby_ && owner_ == self_; }
    uint64_t LobbyId() const { return lobby_; } uint64_t Owner() const { return owner_; } uint64_t Self() const { return self_; }
    std::string Code() const { return code_; }
    wlobby::Gate Gate() const { wlobby::Gate g; g.self = self_; for (uint64_t m : members_) g.members.insert(m); return g; }
    unsigned MemberCount() const { return (unsigned)members_.size(); }
    unsigned MemberLimit();
    std::string Status() const { return status_; }
    bool Failed() const { return failed_; }                         // the last request failed or the lobby was lost (Status says why)
    uint64_t TakeJoinRequest() { uint64_t r = joinRequest_; joinRequest_ = 0; return r; }      // a lobby the player chose in Steam (invite accepted, Join Game)
    std::string ReadData(const char* key);                          // tests: a lobby key read back through the Steam API
    // callbacks (from Steam, on Poll's thread)
    void OnCallback(int id, void* data);
private:
    void Entered(uint64_t lobby, bool hosting);
    void RefreshMembers();
    void SetStatus(const std::string& s, bool failed = false) { status_ = s; failed_ = failed; }
    bool ready_ = false, paused_ = false, failed_ = false; void *user_ = nullptr, *friends_ = nullptr, *mm_ = nullptr, *utils_ = nullptr, *msgs_ = nullptr; void (*runCallbacks_)() = nullptr;
    uint64_t self_ = 0, lobby_ = 0, owner_ = 0, call_ = 0, wanted_ = 0, joinRequest_ = 0, callSince_ = 0, nextMembers_ = 0; int callKind_ = 0; bool hosting_ = false;
    std::string code_, status_ = "Steam is not started yet."; std::vector<uint64_t> members_;
};
}  // namespace lsteam
