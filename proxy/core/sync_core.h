// Game-free sync core: reliable shared-state events (panel solved or un-solved, door opened or closed) among up to 16 players, the stable-world gate, the
// start-up settling window and the echo guard. No Windows headers, no globals: time, transport, entropy and the game world come in
// from outside, so the same code runs in the game (proxy/coop.cpp, proxy/state.cpp) and in the fuzzer (core/tests).
#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "appearance.h"
#include "emotes.h"
#include "line_precision.h"

namespace witness {

using Bytes = std::vector<uint8_t>;
enum EvKind : uint8_t { EV_PANEL_SOLVED = 1, EV_DOOR_OPENED = 2, EV_PANEL_POWERED = 3, EV_CABLE_LIT = 4, EV_CABLE_OFF = 5, EV_PANEL_UNSOLVED = 6, EV_DOOR_CLOSED = 7 };
constexpr unsigned kMaxPlayers = 16;
constexpr unsigned kMaxEntityId = 0x40000;      // the game's entity table size; ids at or above it are refused

// A drawn line as it travels: the nodes of the panel's graph in order, each with its place on the panel in the line's units from the panel's own position
// (so the receiving game needs no idea how a panel's graph is laid out in the world). fine selects 1/20 mm; otherwise mm.
constexpr unsigned kMaxLinePoints = 140;      // a datagram stays under ~1.2 KB: header 28 + 8 per point + checksum
struct LinePoint { uint16_t node = 0; int16_t d[3] = {0, 0, 0}; bool operator==(const LinePoint& o) const { return node == o.node && d[0] == o.d[0] && d[1] == o.d[1] && d[2] == o.d[2]; } };
struct LineOut { unsigned panel = 0; bool final = false; std::vector<LinePoint> pts; bool fine = false; };      // local: a panel whose trace buffer is not empty (final: the panel is solved)
struct RemoteLine { unsigned from = 0, panel = 0, seq = 0; bool final = false; std::vector<LinePoint> pts; uint64_t ageMs = 0; bool fine = false; };

// What the core needs from the game (or a fake of it).
class World {
public:
    virtual ~World() = default;
    virtual bool cameraValid() = 0;                                             // the world exists (the player has a position)
    virtual bool scan(std::vector<unsigned>& panels, std::vector<unsigned>& doors, uint64_t* tableId) = 0;   // false: unreadable
    virtual bool entityExists(unsigned id) = 0;
    // Changes when the world is loaded again (a save restored in the same process re-creates the entities, so their addresses change); 0 when
    // not known. With it a reload is told from progress that undoes itself (timed doors, desert panels that reset): see reloadSeen.
    virtual uint64_t generation() { return 0; }
    virtual bool isPanel(unsigned id) = 0;                                      // type checks, so a wrong-typed event is refused
    virtual bool isDoor(unsigned id) = 0;
    virtual bool panelSolved(unsigned id) = 0;
    virtual bool doorOpen(unsigned id) = 0;
    virtual bool panelPowered(unsigned) { return false; }                       // requested power-on, not the interpolated brightness
    virtual bool applyPanelPower(unsigned) { return false; }                    // render thread only; false: retry later
    virtual void applyPanelSolved(unsigned id) = 0;                             // render thread only
    virtual bool applyDoorOpen(unsigned id) = 0;                                // render thread only (calls the game's door function)
    // Progress that undoes itself and must be undone on the other side too: a timer panel resets (the Shadows door timers), a door shuts.
    virtual bool applyPanelUnsolved(unsigned) { return false; }                 // render thread only; false: retry later
    virtual bool applyDoorClose(unsigned) { return false; }                     // render thread only; false: retry later
    virtual bool doorRequested(unsigned id) { return doorOpen(id); }            // the door was told to open (it may still be moving)
    virtual bool scanCables(std::vector<unsigned>&) { return true; }              // all Power_Cable entities (none by default)
    virtual bool isCable(unsigned) { return false; }
    virtual bool cableLit(unsigned) { return false; }                            // the cable's lit state (+0xC4 and +0xC8 at 1.0)
    virtual bool cableRequested(unsigned id) { return cableLit(id); }            // the cable was switched on (the lit state may still be fading in)
    virtual bool applyCableLit(unsigned) { return false; }                       // render thread only; false: retry later
    virtual bool applyCableOff(unsigned) { return false; }                       // render thread only; false: retry later (the game's own power-off call)
    virtual bool scanLines(std::vector<LineOut>&) { return true; }                // every local panel that has a drawn line (in progress or finished); empty by default
    virtual bool pose(float pos[3], float ang[2]) = 0;                          // local player pose; false when not available
};

struct Flags {   // all off by default
    bool raceMode = false;      // a race, not co-op: nothing of the world is shared (no solved panel, opened door, lit cable, drawn line leaves or enters this game); poses, chat and presence still travel
    bool dropFirstEvent = false, noResend = false, noEchoGuard = false;                 // test switches (witness_coop.cfg)
    // bug twins: each switches one protection off so a test must fail (see core/tests)
    bool noDedupe = false, noEpochReset = false, noResyncOnRestart = false, noGate = false, noSettle = false;
    bool wrongAckEpoch = false, applyOutOfOrder = false, noChecksum = false;
    bool countSkippedResends = false; // twin: an event the 32-per-round budget skipped is counted as resent (a startup burst of 248 events reads as 840 resends)
    bool noEntityOrder = false; // twin: an event retried later is overtaken by a later event about the same entity (a door close lands after the open that followed it)
    bool noApplyRetry = false;  // twin: discard ACKed events when the game cannot apply them yet
    bool noPanelPower = false;  // twin: omit panel power-on progression from live sync and snapshots
    bool noCableSync = false;   // twin: omit cable lighting from live sync and snapshots
    bool noCableOff = false;    // twin: a cable that goes dark stays lit on the peer
    bool noUnsolveSync = false; // twin: a panel that un-solves (a timer ran out) stays solved on the peer
    bool noDoorCloseSync = false; // twin: a door that shuts stays open on the peer
    bool openDedupeByPosition = false; // twin: a door told to shut but still standing open counts as open, so an open right after the close is dropped (one world kept 0x14724 shut)
    bool noOffSnapshot = false; // twin: snapshots do not tell a fresh peer about cables that were switched off
    bool noSnapReassert = false; // twin: a peer's snapshot that relights a cable this player switched off is ignored but not corrected
    bool noCableArbiter = false; // twin: no tie-break when both players switch the same cable at the same moment
    bool noBacklogReset = false; // twin: silently discard new events when an absent peer fills the outbox
    bool noIndependentBudget = false; // twin: one absent peer stalls every other peer after 32 events
    bool noGroupCableIntent = false; // twin: glow/echo bookkeeping hides a genuine new cable command
    bool noGroupVersions = false; // twin: use the two-player cable arbiter in a group
    bool noReloadSync = false;  // twin: ignore a same-process save reload (no re-gate, no resync with the peer)
    bool reloadByProgress = false;  // twin: ignore the world's generation and judge a reload by vanished progress alone (panels that reset themselves stalled sync)
    bool reloadOnDoor = false;  // twin: one closed door alone counts as a save reload (a timed door shutting by itself stalled sync for 10-50 s)
    bool noPingReply = false;   // twin: this game never answers a ping (the peers cannot measure it)
    bool noPingExpiry = false;  // twin: a ping measured once stays on show for ever, even when the peer is gone
    bool noNameSend = false;    // twin: names are not sent
    bool noLineSend = false;    // twin: drawn lines are never sent (the other game shows nothing)
    bool noLineResend = false;  // twin: a line is sent once when it changes and never again (a lost packet is lost for good)
    bool noLineSeqCheck = false; // twin: a delayed older line overwrites a newer one
    bool noSocialResend = false;  // twin: chat, pings and strokes are sent once and never again (a lost packet is lost for good)
    bool noSocialDedupe = false;  // twin: a resent message is delivered again
    bool noSocialOrder = false;   // twin: a message that arrives before an earlier one is taken at once (and the earlier one is then a duplicate)
    bool noSocialEpochReset = false; // twin: a restarted sender's new messages are taken for old ones (its numbers start again)
    bool noPresenceSend = false;  // twin: presence is never sent
    bool noPresenceSeq = false;   // twin: a delayed older presence overwrites a newer one
    bool noPresenceExpiry = false; // twin: typing and speaking stay on for ever when the peer goes quiet
    bool raceEmits = false;     // twin: a race game still announces its own solved panels and doors
    bool raceApplies = false;   // twin: a race game still applies the events it receives
};

struct Hooks {
    std::function<void(unsigned, const Bytes&)> sendTo;      // optional addressed transport (required for groups)
    std::function<void(const Bytes&)> send;                // datagram to the peer
    std::function<void(const char*)> log;                  // optional
    std::function<void()> lock, unlock;                    // optional: the game calls the core from three threads
    // optional taps, used by the tests
    std::function<void(uint8_t kind, unsigned id, bool resync)> onEmit;                              // an event was created
    std::function<void(uint8_t kind, unsigned id, uint64_t epoch, uint32_t seq)> onApplied;          // a remote event changed this world
    std::function<void(uint8_t kind, unsigned id, uint64_t epoch, uint32_t seq, const char* why)> onRejected;
    std::function<void(uint64_t epoch, uint32_t seq)> onAccepted;                                     // an event was taken in order from the peer
    std::function<void()> onRestartSeen;                                                              // the peer (re)started
};

// Social layer: presence (emote, typing, speaking: small unreliable state, repeated), and reliable ordered messages (chat, location pings, drawing strokes).
struct Presence { Appearance appearance; uint8_t emote = 0, emoteSeq = 0; bool typing = false, speaking = false, proximitySpeaking = false, radioEquipped = false, radioTx = false; uint8_t voiceChannel = 0; bool operator==(const Presence& o) const { return appearance == o.appearance && emote == o.emote && emoteSeq == o.emoteSeq && typing == o.typing && speaking == o.speaking && proximitySpeaking == o.proximitySpeaking && radioEquipped == o.radioEquipped && radioTx == o.radioTx && voiceChannel == o.voiceChannel; } };
enum SocialType : uint8_t { SOC_CHAT = 1, SOC_MARK = 2, SOC_STROKE = 3, SOC_STROKE_CLEAR = 4 };
constexpr size_t kMaxChatBytes = 400, kMaxStrokePoints = 60;
struct ChatLine { unsigned from = 0; uint32_t seq = 0; std::string text; };
struct MarkMsg { unsigned from = 0; uint32_t seq = 0; uint8_t kind = 0; float pos[3] = {0, 0, 0}; };
struct StrokeMsg { unsigned from = 0; uint32_t seq = 0, id = 0; bool begin = false, end = false, clear = false; uint8_t color = 0; std::vector<float> pts; };      // pts: x y z per point; clear: erase stroke `id` (0: all of the sender's)

struct RemotePose { Appearance appearance; bool valid = false; float pos[3] = {0, 0, 0}, ang[2] = {0, 0}; uint64_t ageMs = 0; unsigned seq = 0, id = 0; int pingMs = -1; std::string name; uint8_t emote = 0, emoteSeq = 0; bool typing = false, speaking = false, proximitySpeaking = false, radioEquipped = false, radioTx = false; uint8_t voiceChannel = 0; };   // pingMs: round trip in ms, -1 until measured or when the peer went quiet

struct Stats { uint32_t sent = 0, resent = 0, inOrder = 0, dup = 0, ooo = 0, acked = 0, emittedPanels = 0, emittedDoors = 0, appliedPanels = 0, appliedDoors = 0, resyncs = 0, emittedPowers = 0, appliedPowers = 0, reloads = 0, emittedCables = 0, appliedCables = 0; };

class SyncCore {
public:
    // `session` must be larger on every start of this game (the clock in seconds will do): the peer orders restarts by it, so a
    // delayed packet from an earlier run can never be mistaken for a new one.
    SyncCore(int id, uint32_t session, World* world, Hooks hooks, Flags flags = {});
    void tick(uint64_t nowMs);                             // network thread, every few ms: pose at 30 Hz, resends every 150 ms
    void onPacket(const uint8_t* data, size_t n, uint64_t nowMs);
    void pollState(uint64_t nowMs);                        // detector thread, about every 100 ms
    void applyRemote(uint64_t nowMs);                      // render thread, once per frame: apply queued remote events
    bool remotePose(RemotePose* out, uint64_t nowMs);
    std::vector<RemotePose> remotePoses(uint64_t nowMs);
    std::vector<RemoteLine> remoteLines(uint64_t nowMs);    // the latest line each peer has on each panel (an empty line is dropped)
    // Configure the full remote roster before any threads start. Omit this call for legacy two-player discovery.
    bool setPeers(const std::vector<unsigned>& ids);
    void setName(const std::string& name);                  // this player's name, sent with every ping (24 bytes at most)
    // social layer (any thread)
    void setPresence(const Presence& p);                     // this player's emote, typing and speaking flags: sent at once when they change, and every second
    bool sendChat(const std::string& text);                  // reliable, ordered; false when empty (longer than kMaxChatBytes is cut)
    bool sendMark(uint8_t kind, const float pos[3]);         // a location ping
    bool sendStroke(uint32_t id, bool begin, bool end, uint8_t color, const float* pts, size_t count);   // count <= kMaxStrokePoints, pts: x y z each
    bool sendStrokeClear(uint32_t id);                       // 0: every stroke of mine
    std::vector<ChatLine> takeChat();                        // received, in order, each once; taking removes them
    std::vector<MarkMsg> takeMarks();
    std::vector<StrokeMsg> takeStrokes();
    bool ready() const;
    std::set<unsigned> solvedPanels() const;               // the panels known to be solved in this world (the race counts them)
    Stats stats() const;
    size_t unacked() const;                                // events sent but not yet acknowledged
    size_t queuedRemote() const;                           // received in order, not yet applied
    uint32_t session() const { return session_; }
    uint32_t peerSession() const { return peerSession_; }     // the newest session of the peer seen so far (0: none yet)

private:
    using Version = std::pair<uint64_t, unsigned>;  // Lamport clock, then player id
    uint64_t clock_ = 0; std::map<unsigned, Version> cableVersion_;
    struct Sent { uint32_t seq, kind, entity; bool acked, sentOnce; std::set<unsigned> acknowledgers, attempted; Version version; };
    struct Recv { uint8_t kind; uint32_t entity; uint64_t epoch; uint32_t seq; bool snap = false; Version version = {}; };   // snap: part of the sender's full-state snapshot
    struct Guard;                                          // scoped lock
    void emit(uint8_t kind, unsigned id, bool resync);
    void baseline(bool full);
    bool reloadSeen(uint64_t now);
    bool scanWorld();
    uint64_t signature();
    void log(const char* m);
    void send(Bytes b);
    void sendTo(unsigned peer, const Bytes& b);
    struct LineState { uint32_t seq = 0; uint64_t hash = 0; bool final = false, dirty = false, fine = false; std::vector<LinePoint> pts; uint64_t sentAt = 0; };
    struct LineIn { uint32_t seq = 0; uint32_t session = 0; bool final = false, fine = false; std::vector<LinePoint> pts; uint64_t at = 0; };
    std::string name_; uint32_t pingNonce_ = 0; uint64_t nextPing_ = 0;
    void onPing(const uint8_t* data, size_t n, uint64_t now);
    std::map<unsigned, LineState> lines_; size_t lineCursor_ = 0; uint64_t nextLineResend_ = 0;
    struct SocOut { uint32_t seq = 0; uint8_t type = 0; Bytes payload; bool sent = false; uint64_t lastSent = 0; std::set<unsigned> acked; };
    std::vector<SocOut> socOut_; uint64_t socEpoch_ = 0; uint32_t socNext_ = 1, socRestarts_ = 0;
    Presence pres_; bool presDirty_ = true; uint32_t presSeq_ = 0; uint64_t nextPres_ = 0;
    std::vector<ChatLine> chatIn_; std::vector<MarkMsg> markIn_; std::vector<StrokeMsg> strokeIn_;
    bool socialQueue(uint8_t type, Bytes payload);
    void socialTick(uint64_t now);
    void onSocial(const uint8_t* data, size_t n, uint64_t now);
    void onSocialAck(const uint8_t* data, size_t n);
    void onPresence(const uint8_t* data, size_t n, uint64_t now);
    void sendPresence();
    void scanLines(uint64_t now);
    void onLine(const uint8_t* data, size_t n, uint64_t now);
    void sendLine(unsigned panel, const LineState& s);
    struct Peer { uint32_t session = 0, applied = 0; uint64_t epoch = 0, poseAt = 0; RemotePose pose; std::map<unsigned, LineIn> lines; std::string name; uint32_t lastNonce = 0, lastT0 = 0; float ping = -1; uint64_t pongAt = 0; uint64_t socEpoch = 0; uint32_t socApplied = 0, presSeq = 0, presSession = 0; uint64_t presAt = 0; EmoteGate emoteGate; Presence pres; };
    std::map<unsigned, Peer> peers_; bool fixedRoster_ = false;

    int id_; uint32_t session_; World* w_; Hooks h_; Flags f_;
    // reliable stream, mine and the peer's
    std::vector<Sent> sent_; uint64_t epoch_ = 0; uint32_t nextSeq_ = 1, restartsSeen_ = 0;    // epoch = session << 16 | restarts seen: only ever grows
    uint32_t peerSession_ = 0; bool resync_ = false;
    std::vector<Recv> recvQ_; RemotePose remote_; uint64_t remoteAt_ = 0; uint32_t poseSeq_ = 0; uint64_t nextPose_ = 0, nextResend_ = 0;
    Stats st_;
    // detector
    std::vector<unsigned> panels_, doors_; uint64_t tableId_ = 0;
    std::set<unsigned> knownPanel_, knownDoor_; std::map<unsigned, uint64_t> suppressPanel_, suppressDoor_;
    std::set<unsigned> knownPower_; std::map<unsigned, uint64_t> suppressPower_;
    std::vector<unsigned> cables_; std::set<unsigned> knownCable_; std::map<unsigned, uint64_t> suppressCable_; std::set<unsigned> everOff_; std::map<unsigned, uint64_t> cableToggledAt_;
    uint64_t lastLocalSolveAt_ = 0; bool anyLocalSolve_ = false;     // a door opening soon after a local solve is that solve's cascade, not a start-up flip
    bool settledLogged_ = false;
    std::string vanished_;      // what reloadSeen found gone (for the log)
    uint64_t gen_ = 0;          // the world's generation at the last baseline (0: not known)
    bool ready_ = false, gateStarted_ = false; uint64_t nextGate_ = 0, settleUntil_ = 0, nextScan_ = 0, nextStats_ = 0;
    size_t gateLastPanels_ = 0, gateLastDoors_ = 0; uint64_t gateLastSig_ = 0; int gateSame_ = 0;
};

}  // namespace witness
