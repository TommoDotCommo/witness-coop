#pragma once
// A race, game-free (T0 test: race_test.cpp). Everybody plays their own copy of the world (the sync core shares nothing of it in race mode, see Flags::raceMode) and sees the others
// run about. This module decides when the race starts and who has won: nobody starts before everybody's world is loaded and ready; a countdown follows, and its end (GO) is the same
// moment for all (within the link's delay); progress counts only what is solved after GO; the first to reach the goal wins. Each player sends a small report of its state (WTR1) several
// times a second to everybody; a report is state, not an event: a lost one is replaced by the next, the newest wins.
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace wrace {
enum Phase : uint8_t { kWaiting = 0, kReady = 1, kCountdown = 2, kRunning = 3, kFinished = 4 };
//   kWaiting    the world is not loaded yet          kReady     loaded, waiting for the others       kCountdown  everybody is ready: GO in a few seconds
//   kRunning    the race is on                       kFinished  this player reached the goal
constexpr uint32_t kCountdownMs = 5000;          // from everybody ready to GO
constexpr uint32_t kReportMs = 250;              // how often a report is sent when nothing changes
constexpr uint32_t kStaleMs = 3000;              // a player not heard from for this long is shown as gone
constexpr uint32_t kGraceMs = 1500;              // after the first finish is known, how long to wait for a faster finish whose report is on its way

struct Goal { enum Kind { kPanel, kCount } kind = kPanel; unsigned panel = 0x9F7F; unsigned count = 0; };
//   panel:<hex id>  the first to solve that panel wins (default panel:9F7F, the mountain's last box); count:<n> the first to solve n panels after GO wins
bool ParseGoal(const char* text, Goal* g);
std::string GoalText(const Goal& g);
uint32_t GoalHash(const Goal& g);

struct Report { unsigned from = 0; uint32_t session = 0, seq = 0; Phase phase = kWaiting; uint16_t solved = 0; uint32_t finishMs = 0; int32_t msToGo = 0; uint32_t goal = 0; };
std::vector<uint8_t> MakePacket(const Report& r);
bool ParsePacket(const uint8_t* d, size_t n, Report* r);

struct Standing { unsigned id = 0; bool me = false, gone = false; Phase phase = kWaiting; unsigned solved = 0; uint32_t finishMs = 0; int place = 0; };

class Race {
public:
    Race(unsigned self, const Goal& goal, uint32_t session, std::function<void(const std::vector<uint8_t>&)> send, uint32_t countdownMs = kCountdownMs);
    void SetRoster(const std::vector<unsigned>& others);                      // the other players (before the first Tick)
    void Tick(uint64_t now, bool worldReady, const std::set<unsigned>& solved);   // solved: the panels solved in this world now
    void OnPacket(const uint8_t* d, size_t n, uint64_t now);
    Phase phase() const { return phase_; }
    int64_t msToGo(uint64_t now) const { return phase_ == kCountdown ? int64_t(goAt_) - int64_t(now) : 0; }      // countdown only
    uint64_t goAt() const { return goAt_; }                                  // the local time of GO (0: not scheduled)
    uint32_t elapsedMs(uint64_t now) const;                                   // since GO (0 before it); stops at this player's finish
    unsigned solvedSinceGo() const { return solvedDelta_; }
    std::vector<Standing> standings(uint64_t now) const;                      // everybody, best first
    unsigned winner() const;                                                  // the id of the fastest finisher known so far (0: none)
    bool decided(uint64_t now) const;                                         // the winner is certain: all have finished, or the grace period has passed
    unsigned waitingFor(uint64_t now) const;                                  // while kReady: how many players are not ready yet (or silent)
private:
    struct Peer { Report r; uint64_t at = 0; bool any = false; uint64_t goAt = 0; };
    void Send(uint64_t now);
    unsigned self_; Goal goal_; uint32_t session_, seq_ = 0, countdownMs_; std::function<void(const std::vector<uint8_t>&)> send_;
    std::map<unsigned, Peer> peers_; Phase phase_ = kWaiting; uint64_t goAt_ = 0, nextSend_ = 0, finishAt_ = 0, firstFinishAt_ = 0; uint32_t finishMs_ = 0; unsigned solvedDelta_ = 0;
    std::set<unsigned> baseline_; Phase sentPhase_ = kWaiting; unsigned sentSolved_ = 0;
};
}  // namespace wrace
