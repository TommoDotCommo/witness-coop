// Game-free sync core: reliable shared-state events (panel solved, door opened) between two players, the stable-world gate, the
// start-up settling window and the echo guard. No Windows headers, no globals: time, transport, entropy and the game world come in
// from outside, so the same code runs in the game (proxy/coop.cpp, proxy/state.cpp) and in the fuzzer (core/tests).
#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace witness {

using Bytes = std::vector<uint8_t>;
enum EvKind : uint8_t { EV_PANEL_SOLVED = 1, EV_DOOR_OPENED = 2, EV_PANEL_POWERED = 3 };
constexpr unsigned kMaxEntityId = 0x40000;      // the game's entity table size; ids at or above it are refused

// What the core needs from the game (or a fake of it).
class World {
public:
    virtual ~World() = default;
    virtual bool cameraValid() = 0;                                             // the world exists (the player has a position)
    virtual bool scan(std::vector<unsigned>& panels, std::vector<unsigned>& doors, uint64_t* tableId) = 0;   // false: unreadable
    virtual bool entityExists(unsigned id) = 0;
    virtual bool isPanel(unsigned id) = 0;                                      // type checks, so a wrong-typed event is refused
    virtual bool isDoor(unsigned id) = 0;
    virtual bool panelSolved(unsigned id) = 0;
    virtual bool doorOpen(unsigned id) = 0;
    virtual bool panelPowered(unsigned) { return false; }                       // requested power-on, not the interpolated brightness
    virtual bool applyPanelPower(unsigned) { return false; }                    // render thread only; false: retry later
    virtual void applyPanelSolved(unsigned id) = 0;                             // render thread only
    virtual bool applyDoorOpen(unsigned id) = 0;                                // render thread only (calls the game's door function)
    virtual bool pose(float pos[3], float ang[2]) = 0;                          // local player pose; false when not available
};

struct Flags {   // all off by default
    bool dropFirstEvent = false, noResend = false, noEchoGuard = false;                 // test switches (witness_coop.cfg)
    // bug twins: each switches one protection off so a test must fail (see core/tests)
    bool noDedupe = false, noEpochReset = false, noResyncOnRestart = false, noGate = false, noSettle = false;
    bool wrongAckEpoch = false, applyOutOfOrder = false, noChecksum = false;
    bool noApplyRetry = false;  // twin: discard ACKed events when the game cannot apply them yet
    bool noPanelPower = false;  // twin: omit panel power-on progression from live sync and snapshots
    bool noReloadSync = false;  // twin: ignore a same-process save reload (no re-gate, no resync with the peer)
};

struct Hooks {
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

struct RemotePose { bool valid = false; float pos[3] = {0, 0, 0}, ang[2] = {0, 0}; uint64_t ageMs = 0; unsigned seq = 0, id = 0; };

struct Stats { uint32_t sent = 0, resent = 0, inOrder = 0, dup = 0, ooo = 0, acked = 0, emittedPanels = 0, emittedDoors = 0, appliedPanels = 0, appliedDoors = 0, resyncs = 0, emittedPowers = 0, appliedPowers = 0, reloads = 0; };

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
    bool ready() const { return ready_; }
    Stats stats() const;
    size_t unacked() const;                                // events sent but not yet acknowledged
    size_t queuedRemote() const;                           // received in order, not yet applied
    uint32_t session() const { return session_; }
    uint32_t peerSession() const { return peerSession_; }     // the newest session of the peer seen so far (0: none yet)

private:
    struct Sent { uint32_t seq, kind, entity; bool acked, sentOnce; };
    struct Recv { uint8_t kind; uint32_t entity; uint64_t epoch; uint32_t seq; };
    struct Guard;                                          // scoped lock
    void emit(uint8_t kind, unsigned id, bool resync);
    void baseline(bool full);
    bool reloadSeen(uint64_t now);
    bool scanWorld();
    uint64_t signature();
    void log(const char* m);
    void send(Bytes b);

    int id_; uint32_t session_; World* w_; Hooks h_; Flags f_;
    // reliable stream, mine and the peer's
    std::vector<Sent> sent_; uint64_t epoch_ = 0; uint32_t nextSeq_ = 1, restartsSeen_ = 0;    // epoch = session << 16 | restarts seen: only ever grows
    uint32_t peerSession_ = 0, peerApplied_ = 0; uint64_t peerEpoch_ = 0; bool resync_ = false;
    std::vector<Recv> recvQ_; RemotePose remote_; uint64_t remoteAt_ = 0; uint32_t poseSeq_ = 0; uint64_t nextPose_ = 0, nextResend_ = 0;
    Stats st_;
    // detector
    std::vector<unsigned> panels_, doors_; uint64_t tableId_ = 0;
    std::set<unsigned> knownPanel_, knownDoor_; std::map<unsigned, uint64_t> suppressPanel_, suppressDoor_;
    std::set<unsigned> knownPower_; std::map<unsigned, uint64_t> suppressPower_;
    uint64_t lastLocalSolveAt_ = 0; bool anyLocalSolve_ = false;     // a door opening soon after a local solve is that solve's cascade, not a start-up flip
    bool ready_ = false, gateStarted_ = false; uint64_t nextGate_ = 0, settleUntil_ = 0, nextScan_ = 0, nextStats_ = 0;
    size_t gateLastPanels_ = 0, gateLastDoors_ = 0; uint64_t gateLastSig_ = 0; int gateSame_ = 0;
};

}  // namespace witness
