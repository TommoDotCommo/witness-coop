// How long a solve takes to reach the other game, over ten minutes of play on a link that loses 20% of the packets each way, while the
// world does what the island does by itself: the Shadows timer panels reset 20 s after they are solved (in the game that ran the timer)
// and the timed door shuts with them; a hedge maze is walked again (its panel un-solves and its exit door shuts, then both come back).
// The playthrough bot measured p95 12-13 s and a maximum of 36 s with 4 bots: the receiving game took each of those self-undoing changes
// for a save load and re-gated (no remote event applied until its world had looked the same on 4 scans 3 s apart). Bound: every solve
// arrives, p95 under 1 s, none over 2.5 s. Twins that must break it: judging a load by vanished progress, a shut door counting as a load,
// and no resends on the lossy link.
#include "../sync_core.h"
#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <vector>

namespace {
constexpr unsigned kTimerA = 50, kTimerB = 51, kTimedDoor = 60, kHedge = 70, kHedgeExit = 71;

struct PlayWorld : witness::World {
    bool solved[128] = {}, open[128] = {}; uint64_t gen = 1, now = 0;
    std::map<unsigned, uint64_t> applied;      // panel -> when a remote solve was applied here
    bool cameraValid() override { return true; }
    bool scan(std::vector<unsigned>& p, std::vector<unsigned>& d, uint64_t* t) override {
        p.clear(); d = {kTimedDoor, kHedgeExit};
        for (unsigned i = 1; i <= 40; ++i) p.push_back(i);
        p.push_back(kTimerA); p.push_back(kTimerB); p.push_back(kHedge); *t = 1; return true;
    }
    bool entityExists(unsigned id) override { return (id >= 1 && id <= 40) || id == kTimerA || id == kTimerB || id == kHedge || id == kTimedDoor || id == kHedgeExit; }
    uint64_t generation() override { return gen; }
    bool isPanel(unsigned id) override { return entityExists(id) && id != kTimedDoor && id != kHedgeExit; }
    bool isDoor(unsigned id) override { return id == kTimedDoor || id == kHedgeExit; }
    bool panelSolved(unsigned id) override { return solved[id]; }
    bool doorOpen(unsigned id) override { return open[id]; }
    void applyPanelSolved(unsigned id) override { solved[id] = true; applied.emplace(id, now); }
    bool applyDoorOpen(unsigned id) override { open[id] = true; return true; }
    bool applyPanelUnsolved(unsigned id) override { solved[id] = false; return true; }
    bool applyDoorClose(unsigned id) override { open[id] = false; return true; }
    bool pose(float p[3], float a[2]) override { p[0] = p[1] = p[2] = 1; a[0] = a[1] = 0; return true; }
};

struct Link {      // one direction: a fixed delay and seeded loss
    struct Pkt { uint64_t at; witness::Bytes d; };
    std::vector<Pkt> q; uint32_t rng; unsigned lossPct;
    Link(uint32_t seed, unsigned loss) : rng(seed), lossPct(loss) {}
    void push(const witness::Bytes& d, uint64_t now) { rng = rng * 1664525u + 1013904223u; if ((rng >> 8) % 100 < lossPct) return; q.push_back({now + 30, d}); }
    std::vector<witness::Bytes> due(uint64_t now) {
        std::vector<witness::Bytes> out; std::vector<Pkt> keep;
        for (auto& p : q) { if (p.at <= now) out.push_back(p.d); else keep.push_back(p); }
        q = std::move(keep); return out;
    }
};

struct Result { std::vector<double> lat; unsigned missing = 0; double p95() const { auto v = lat; std::sort(v.begin(), v.end()); return v.empty() ? 0 : v[size_t(0.95 * (v.size() - 1))]; } double max() const { return lat.empty() ? 0 : *std::max_element(lat.begin(), lat.end()); } };

Result play(witness::Flags flags) {
    PlayWorld a, b; uint64_t now = 0;
    Link ab(7, 20), ba(11, 20);
    witness::Hooks ha, hb;
    ha.send = [&](const witness::Bytes& d) { ab.push(d, now); };
    hb.send = [&](const witness::Bytes& d) { ba.push(d, now); };
    witness::SyncCore ca(1, 1, &a, ha, flags), cb(2, 1, &b, hb, flags);
    struct Solve { PlayWorld* by; PlayWorld* to; unsigned panel; uint64_t at; };
    std::vector<Solve> solves;
    unsigned next = 1; uint64_t timerAt = 0, hedgeAt = 0;
    const uint64_t end = 600000;
    for (now = 50; now <= end + 10000; now += 50) {
        a.now = b.now = now;
        if (now >= 30000 && now <= end) {
            if (now % 9000 == 0 && next <= 40) {      // a solve every 9 s, the two players in turn
                PlayWorld* by = next % 2 ? &a : &b; by->solved[next] = true; solves.push_back({by, by == &a ? &b : &a, next, now}); ++next;
            }
            if (now % 60000 == 0) { a.solved[kTimerA] = a.solved[kTimerB] = true; a.open[kTimedDoor] = true; timerAt = now; }
            if (timerAt && now == timerAt + 20000) { a.solved[kTimerA] = a.solved[kTimerB] = false; a.open[kTimedDoor] = false; }      // A's timer ran out
            if (now % 90000 == 45000) { b.solved[kHedge] = true; b.open[kHedgeExit] = true; hedgeAt = now; }
            if (hedgeAt && now == hedgeAt + 30000) { b.solved[kHedge] = false; b.open[kHedgeExit] = false; }      // the maze walked again
            if (hedgeAt && now == hedgeAt + 35000) { b.solved[kHedge] = true; b.open[kHedgeExit] = true; }
        }
        ca.pollState(now); cb.pollState(now); ca.tick(now); cb.tick(now);
        for (auto& d : ab.due(now)) cb.onPacket(d.data(), d.size(), now);
        for (auto& d : ba.due(now)) ca.onPacket(d.data(), d.size(), now);
        ca.applyRemote(now); cb.applyRemote(now);
    }
    Result r;
    for (auto& s : solves) {
        auto it = s.to->applied.find(s.panel);
        if (it == s.to->applied.end() || it->second < s.at) ++r.missing;
        else r.lat.push_back((it->second - s.at) / 1000.0);
    }
    return r;
}

bool within(const Result& r) { return r.missing == 0 && r.lat.size() >= 30 && r.p95() < 1.0 && r.max() <= 2.5; }
}  // namespace

int main() {
    Result clean = play({});
    std::printf("%s  %zu solves over a 20%% lossy link with timers and a hedge maze undoing themselves: p95 %.2f s, max %.2f s, %u missing (bound p95 < 1 s, max 2.5 s)\n",
                within(clean) ? "PASS" : "FAIL", clean.lat.size(), clean.p95(), clean.max(), clean.missing);
    bool ok = within(clean);
    witness::Flags byProgress, onDoor, noResend; byProgress.reloadByProgress = true; onDoor.reloadOnDoor = true; noResend.noResend = true;
    for (auto& [name, f] : std::vector<std::pair<const char*, witness::Flags>>{{"reload-by-progress", byProgress}, {"reload-on-door", onDoor}, {"no-resend", noResend}}) {
        Result m = play(f); bool caught = !within(m); ok = ok && caught;
        std::printf("%s  %s twin caught: p95 %.2f s, max %.2f s, %u missing\n", caught ? "PASS" : "FAIL", name, m.p95(), m.max(), m.missing);
    }
    return ok ? 0 : 1;
}
