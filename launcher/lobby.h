#pragma once
// The lobby, game-free (T0 test: lobby_test.cpp). Before a game starts the players meet in the launcher: one hosts, the others join with an invite, everybody sees who is there, chats by
// text and voice, and the host picks what to play (co-op or race, a new game with parts of the map done) and starts it. When the game ends everybody is back in the lobby with the result.
// The design is a star: the host is the authority and the voice relay, so only the host needs a reachable port. The room state is repeated (the newest wins, nothing needs to be reliable);
// chat is a small acknowledged stream both ways. Starting the game is a state too: a countdown that every launcher counts down by itself, then each one writes the cfg of its game from
// the same roster (BuildGameCfg) and launches it. One datagram per message, every message with a checksum.
// Two transports carry the same messages: UDP (an address and a port: "Direct (LAN / manual)") and Steam (a SteamID: a friends-only Steam lobby, relayed by Steam,
// so nobody forwards a port; lobby_steam.h). The lobby logic does not know which: an Addr is one or the other. With Steam, the Gate passes on only what members
// of the Steam lobby send, and the games are told each other's SteamIDs (peer=<id>,steam:<steamid64>).
#include <cstdint>
#include <set>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "../proxy/voice/voice.h"

namespace wlobby {
constexpr unsigned kMaxMembers = 16;
constexpr uint32_t kStaleMs = 6000;          // a member not heard from for this long has left
constexpr uint32_t kStateMs = 400, kHelloMs = 400, kStartMs = 3000;
enum Phase : uint8_t { kLobby = 0, kStarting = 1, kInGame = 2 };
enum Mode : uint8_t { kCoop = 0, kRace = 1 };
enum JoinResult : uint8_t { kJoinOk = 0, kBadToken = 1, kFull = 2, kBadVersion = 3, kVersionMismatch = 4, kPending = 255 };      // kBadVersion: another lobby protocol (an old launcher); kVersionMismatch: the same protocol but another MOD version (named in words: HostVersion())

struct Addr {      // UDP: ip and port (host byte order), steam 0. Steam: the member's SteamID64, ip and port 0.
    uint32_t ip = 0; uint16_t port = 0; uint64_t steam = 0;
    bool operator==(const Addr& o) const { return ip == o.ip && port == o.port && steam == o.steam; }
    bool operator<(const Addr& o) const { return steam != o.steam ? steam < o.steam : ip != o.ip ? ip < o.ip : port < o.port; }
    bool valid() const { return steam != 0 || port != 0; }
};
inline Addr SteamAddr(uint64_t id) { Addr a; a.steam = id; return a; }
// A Steam lobby's door: what arrives from a SteamID that is not in the Steam lobby is dropped before the lobby sees it, and nothing is sent to one.
struct Gate {
    std::set<uint64_t> members; uint64_t self = 0;
    bool Allowed(uint64_t id) const {
#ifdef WLB_MUT_GATE_OPEN
        return id != 0;      // mutant: anybody on Steam is let in
#endif
        return id != 0 && id != self && members.count(id) != 0;
    }
};
struct Settings { Mode mode = kCoop; bool newGame = true; std::string raceGoal = "panel:9F7F", progress, randomizer; };      // progress: the new game's spec ("area:X;lasers:N", progress/progress.h), empty: nothing done; randomizer: the built-in randomizer and its seed (common/randomizers.h), empty or "off": the game's own puzzles
struct Member { unsigned id = 0; std::string name; bool host = false, ready = false, inGame = false, speaking = false, playing = false, puzzles = true; uint16_t gamePort = 0; uint32_t ip = 0; uint32_t ackedChat = 0; uint64_t steamId = 0; std::string version; };      // version: the member's mod version (git describe); steamId: in a Steam lobby      // playing: was in the lobby when the game was started: only these are in the game
struct ChatLine { unsigned seq = 0, from = 0; std::string text; };
struct State { uint32_t seq = 0; Phase phase = kLobby; int32_t startInMs = 0; Settings settings; std::vector<Member> members; std::string lastResult; };
struct Launch { unsigned myId = 0; State state; };      // what the game is started from: the same on every member (apart from myId)

using Send = std::function<void(const Addr&, const std::vector<uint8_t>&)>;
using Bytes = std::vector<uint8_t>;

class Lobby {
public:
    // host: Lobby(true, name, gamePort, token, send). client: Lobby(false, ...) then Join(hostAddr).
    Lobby(bool host, const std::string& name, uint16_t gamePort, const std::string& token, Send send);
    void Join(const Addr& host);
    void SetSession(uint32_t s) { session_ = s; }
    void SetInviteOnly(bool b) { inviteOnly_ = b; }       // B9, host: invite only (the default) needs the invite's code to join; open to my networks lets anybody who finds the lobby in, without a code
    void SetModVersion(const std::string& v);              // this launcher's mod version (a build stamp); before Join / the first Tick. Players on different versions cannot play together.
    void SetSteamId(uint64_t id) { steamId_ = id; if (host_) Refresh(); }      // a Steam lobby: this member's own SteamID (the host lists it for the games' cfg)          // host: a number that is different on every start of the lobby (the clock), so clients can tell a restarted host from a late packet
    void Tick(uint64_t now);
    void OnPacket(const Addr& from, const uint8_t* d, size_t n, uint64_t now);
    // what the player does
    void SetReady(bool r);
    void SendChat(const std::string& text);
    void SetSettings(const Settings& s);                 // host only
    bool Start(uint64_t now);                            // host only: false when not in the lobby or somebody is not ready (the host counts as ready)
    void SetInGame(bool b);                              // this launcher's game is running (or has ended)
    void SetResult(const std::string& text);             // host only: shown to everybody in the lobby after the game
    void SetSpeaking(bool b) { speaking_ = b; }
    void SendVoice(const wvoice::Packet& p);             // this member's voice: to the host, which passes it on
    void Leave();
    // what the player sees
    bool joined() const { return joined_; }
    JoinResult refused() const { return refused_; }
    const std::string& hostVersion() const { return hostVersion_; }      // after kVersionMismatch: the version the host runs
    const std::string& modVersion() const { return modVersion_; }
    bool isHost() const { return host_; }
    unsigned myId() const { return id_; }
    const State& state() const { return st_; }
    std::vector<ChatLine> TakeChat();
    bool TakeLaunch(uint64_t now, Launch* out);          // once, when the countdown ends
    std::function<void(const wvoice::Packet&)> onVoice;   // a packet from another member
    // The host's own puzzles ("custom;set:<hash>", common/randomizers.h): a member who does not have that set asks the host for it, a piece at a time,
    // keeps it when its text has that hash, and says so; the host's Start waits until every member has it (Member::puzzles).
    std::function<std::string(const std::string& hash)> setText;                          // host: the text of a set it has ("" when none)
    std::function<bool(const std::string& hash)> haveSet;                                 // client: whether this set is here already
    std::function<void(const std::string& hash, const std::string& text)> storeSet;       // client: a whole set arrived (its hash checked)
    bool HavePuzzles() const;                                                             // this member has the host's puzzles (true when none are needed)
    size_t PuzzleBytes() const { return setBuf_.size(); }                                 // client: how much of the set has arrived
    // bug twins (tests only)
    struct Twins { bool noToken = false, noChatAck = false, noChatDedupe = false, noVoiceRelay = false, voiceEcho = false, noTimeout = false, noStateSeq = false, noVoiceFromCheck = false, startWithoutReady = false, setUnchecked = false; } twins;
private:
    struct Peer { Member m; Addr addr; uint64_t at = 0; uint32_t chatTaken = 0; uint32_t chatSeen = 0; std::map<uint32_t, ChatLine> outbox; uint64_t outboxAt = 0; };
    void Parse(const Addr& from, const uint8_t* d, size_t n, uint64_t now);
    void Broadcast(uint64_t now);
    void SendHello(uint64_t now);
    void PushChat(unsigned from, const std::string& text);
    void Refresh();
    void Rebuild();
    bool host_, inviteOnly_ = true; uint64_t steamId_ = 0; std::string name_, token_, modVersion_ = "dev", hostVersion_; uint16_t gamePort_; Send send_; Addr hostAddr_;
    bool joined_ = false, ready_ = false, inGame_ = false, speaking_ = false, launched_ = false; JoinResult refused_ = kPending; unsigned id_ = 0; State st_; uint64_t startAt_ = 0, nextState_ = 0, nextHello_ = 0, lastHost_ = 0, backSince_ = 0;
    std::map<Addr, Peer> peers_; unsigned nextId_ = 2; uint32_t chatNext_ = 1;         // host
    std::vector<std::pair<uint32_t, std::string>> out_; uint32_t outSeq_ = 0, outAcked_ = 0, seenChat_ = 0, stateSeq_ = 0, session_ = 0, hostSession_ = 0; bool seenInGame_ = false; uint64_t startedAt_ = 0;   // client (session_: the host's)
    std::vector<ChatLine> chatIn_; std::map<uint32_t, ChatLine> pendingIn_;
    std::string NeededSet() const;                                                         // the set the host's choice names, or ""
    void AskForSet(uint64_t now);
    std::string setHash_, setBuf_; uint32_t setTotal_ = 0; uint64_t setAskedAt_ = 0; bool setDone_ = false;      // client: the set being fetched
};

// The cfg of one member's game from the roster (and the host's randomizer, always written: a guest's own earlier choice never applies): id, game port, every other member as peer=<id>,<ip>,<port> (and peer_version=<id>,<version>, so the game can say whose version differs) (or peer=<id>,steam:<steamid64> when the lobby is a Steam lobby: every
// member then has a SteamID), the mode and its goal, the name, the new game's progress. `base` is the member's
// current witness_coop.cfg text (every line it does not set is kept). `dialAddr`: the address the client used to reach the host (the host's own address as the others know it).
std::string BuildGameCfg(const std::string& base, const Launch& l, uint32_t hostDialIp);
std::string IpText(uint32_t ip);
}  // namespace wlobby
