#pragma once
// The lobby, game-free (T0 test: lobby_test.cpp). Before a game starts the players meet in the launcher: one hosts, the others join with an invite, everybody sees who is there, chats by
// text and voice, and the host picks what to play (co-op or race, a new game with parts of the map done) and starts it. When the game ends everybody is back in the lobby with the result.
// The design is a star: the host is the authority and the voice relay, so only the host needs a reachable port. The room state is repeated (the newest wins, nothing needs to be reliable);
// chat is a small acknowledged stream both ways. Starting the game is a state too: a countdown that every launcher counts down by itself, then each one writes the cfg of its game from
// the same roster (BuildGameCfg) and launches it. UDP, one datagram per message, every message with a checksum.
#include <cstdint>
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
enum JoinResult : uint8_t { kJoinOk = 0, kBadToken = 1, kFull = 2, kBadVersion = 3, kPending = 255 };

struct Addr { uint32_t ip = 0; uint16_t port = 0; bool operator==(const Addr& o) const { return ip == o.ip && port == o.port; } bool operator<(const Addr& o) const { return ip != o.ip ? ip < o.ip : port < o.port; } };   // host byte order
struct Settings { Mode mode = kCoop; bool newGame = true; std::string raceGoal = "panel:9F7F", progress; };      // progress: the new game's spec ("area:X;lasers:N", progress/progress.h), empty: nothing done
struct Member { unsigned id = 0; std::string name; bool host = false, ready = false, inGame = false, speaking = false, playing = false; uint16_t gamePort = 0; uint32_t ip = 0; uint32_t ackedChat = 0; };      // playing: was in the lobby when the game was started: only these are in the game
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
    void SetSession(uint32_t s) { session_ = s; }          // host: a number that is different on every start of the lobby (the clock), so clients can tell a restarted host from a late packet
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
    bool isHost() const { return host_; }
    unsigned myId() const { return id_; }
    const State& state() const { return st_; }
    std::vector<ChatLine> TakeChat();
    bool TakeLaunch(uint64_t now, Launch* out);          // once, when the countdown ends
    std::function<void(const wvoice::Packet&)> onVoice;   // a packet from another member
    // bug twins (tests only)
    struct Twins { bool noToken = false, noChatAck = false, noChatDedupe = false, noVoiceRelay = false, voiceEcho = false, noTimeout = false, noStateSeq = false, noVoiceFromCheck = false, startWithoutReady = false; } twins;
private:
    struct Peer { Member m; Addr addr; uint64_t at = 0; uint32_t chatTaken = 0; uint32_t chatSeen = 0; std::map<uint32_t, ChatLine> outbox; uint64_t outboxAt = 0; };
    void Parse(const Addr& from, const uint8_t* d, size_t n, uint64_t now);
    void Broadcast(uint64_t now);
    void SendHello(uint64_t now);
    void PushChat(unsigned from, const std::string& text);
    void Refresh();
    void Rebuild();
    bool host_; std::string name_, token_; uint16_t gamePort_; Send send_; Addr hostAddr_;
    bool joined_ = false, ready_ = false, inGame_ = false, speaking_ = false, launched_ = false; JoinResult refused_ = kPending; unsigned id_ = 0; State st_; uint64_t startAt_ = 0, nextState_ = 0, nextHello_ = 0, lastHost_ = 0, backSince_ = 0;
    std::map<Addr, Peer> peers_; unsigned nextId_ = 2; uint32_t chatNext_ = 1;         // host
    std::vector<std::pair<uint32_t, std::string>> out_; uint32_t outSeq_ = 0, outAcked_ = 0, seenChat_ = 0, stateSeq_ = 0, session_ = 0, hostSession_ = 0; bool seenInGame_ = false; uint64_t startedAt_ = 0;   // client (session_: the host's)
    std::vector<ChatLine> chatIn_; std::map<uint32_t, ChatLine> pendingIn_;
};

// The cfg of one member's game from the roster: id, game port, every other member as peer=<id>,<ip>,<port>, the mode and its goal, the name, the new game's progress. `base` is the member's
// current witness_coop.cfg text (every line it does not set is kept). `dialAddr`: the address the client used to reach the host (the host's own address as the others know it).
std::string BuildGameCfg(const std::string& base, const Launch& l, uint32_t hostDialIp);
std::string IpText(uint32_t ip);
}  // namespace wlobby
