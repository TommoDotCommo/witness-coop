// Deterministic cable power-off: a cable that was lit (cable 1 is lit from the start, as in the game) goes dark through the game's own
// power-off call on one machine. The other game must make the same cable dark, once, without echo; a fresh receiver must learn it from
// the snapshot; and when both players switch the same cable at once, both worlds must end in the same state.
#include "../sync_core.h"
#include <cstdio>
#include <memory>

struct OffWorld : witness::World {
    bool lit[4] = {false, true, false, false};
    bool refuse = false;
    unsigned calls = 0;
    bool cameraValid() override { return true; }
    bool scan(std::vector<unsigned>& p, std::vector<unsigned>&, uint64_t* t) override { p = {1}; *t = 1; return true; }
    bool scanCables(std::vector<unsigned>& c) override { c = {1, 2, 3}; return true; }
    bool entityExists(unsigned id) override { return id >= 1 && id <= 3; }
    bool isPanel(unsigned id) override { return id == 1; }
    bool isDoor(unsigned) override { return false; }
    bool isCable(unsigned id) override { return id >= 1 && id <= 3; }
    bool panelSolved(unsigned) override { return false; }
    bool doorOpen(unsigned) override { return false; }
    bool cableLit(unsigned id) override { return lit[id]; }
    void applyPanelSolved(unsigned) override {}
    bool applyDoorOpen(unsigned) override { return false; }
    bool applyCableLit(unsigned id) override { if (refuse) return false; lit[id] = true; return true; }
    bool applyCableOff(unsigned id) override { ++calls; if (refuse) return false; lit[id] = false; return true; }
    bool pose(float p[3], float a[2]) override { p[0] = p[1] = p[2] = 1; a[0] = a[1] = 0; return true; }
};

struct Pair {
    OffWorld a, b;
    std::unique_ptr<witness::SyncCore> ca, cb;
    std::vector<witness::Bytes> toA, toB;
    uint64_t now = 0;
    unsigned echoes = 0, applied = 0;
    Pair(witness::Flags fa = {}, witness::Flags fb = {}) {
        witness::Hooks h;
        h.send = [&](const witness::Bytes& d) { toB.push_back(d); };
        ca = std::make_unique<witness::SyncCore>(1, 1, &a, h, fa);
        startB(1, fb);
    }
    void startB(unsigned session, witness::Flags flags = {}) {
        b = OffWorld();
        witness::Hooks h;
        h.send = [&](const witness::Bytes& d) { toA.push_back(d); };
        h.onEmit = [&](uint8_t, unsigned, bool resync) { if (!resync) ++echoes; };
        h.onApplied = [&](uint8_t kind, unsigned, uint64_t, uint32_t) { if (kind == witness::EV_CABLE_OFF) ++applied; };
        cb = std::make_unique<witness::SyncCore>(2, session, &b, h, flags);
    }
    void advance(uint64_t duration) {
        uint64_t end = now + duration;
        while (now < end) {
            now += 50;
            ca->pollState(now); cb->pollState(now);
            ca->tick(now); cb->tick(now);
            auto qa = std::move(toA), qb = std::move(toB); toA.clear(); toB.clear();
            for (auto& d : qa) ca->onPacket(d.data(), d.size(), now);
            for (auto& d : qb) cb->onPacket(d.data(), d.size(), now);
            ca->applyRemote(now); cb->applyRemote(now);
        }
    }
};

// The default-lit cable 1 goes dark on A. The first send is lost, then B refuses the call for longer than the echo guard interval.
bool live(witness::Flags fa = {}, witness::Flags fb = {}) {
    fa.dropFirstEvent = true;
    Pair p(fa, fb); p.advance(30000);
    p.b.refuse = true; p.a.lit[1] = false;
    p.advance(2000);
    bool retained = p.b.lit[1] && p.ca->unacked() == 0 && p.cb->queuedRemote() >= 1 && p.applied == 0;
    p.advance(6000);
    p.b.refuse = false; p.advance(6000);
    return retained && !p.b.lit[1] && !p.a.lit[1] && p.applied == 1 && p.echoes == 0 && p.cb->queuedRemote() == 0 && !p.b.lit[2];
}

// B (a fresh process) must learn that a default-lit cable is dark, long after the original event was acknowledged.
bool restart(witness::Flags fa = {}) {
    Pair p(fa); p.advance(30000);
    p.a.lit[1] = false; p.advance(2000);
    if (p.b.lit[1] || p.ca->unacked()) return false;
    p.startB(2); p.advance(30000);
    return !p.b.lit[1] && p.echoes == 0 && p.ca->unacked() == 0;
}

// A switches the cable off; at the same moment B switches it off and straight back on. The lower player id wins: both end dark.
bool conflict(witness::Flags fa = {}) {
    Pair p(fa); p.advance(30000);
    p.a.lit[1] = false; p.b.lit[1] = false; p.advance(50);
    p.b.lit[1] = true; p.advance(20000);
    return !p.a.lit[1] && !p.b.lit[1] && p.ca->unacked() == 0 && p.cb->unacked() == 0;
}

// The opposite way round: A relights the cable that B just switched off.
bool conflictRelight(witness::Flags fa = {}) {
    Pair p(fa); p.advance(30000);
    p.a.lit[1] = false; p.advance(2000);                 // both dark
    p.a.lit[1] = true; p.b.lit[1] = true; p.advance(50); // both relight at once, no disagreement
    p.b.lit[1] = false; p.advance(20000);                // B alone switches it off again; A's relight came first, so A (id 1) keeps it
    return p.a.lit[1] == p.b.lit[1];
}

int main() {
    witness::Flags viaReassert; viaReassert.noOffSnapshot = true;    // A sends no off-snapshot: B only learns through A's answer to B's own snapshot
    bool a = live(), b = restart(), c = conflict(), d = conflictRelight(), r = restart(viaReassert);
    std::printf("%s  cable off: loss, refused call, no echo, other cable untouched\n", a ? "PASS" : "FAIL");
    std::printf("%s  cable off: fresh receiver learns a default-lit cable is dark\n", b ? "PASS" : "FAIL");
    std::printf("%s  cable off: simultaneous opposite switches end the same on both\n", c ? "PASS" : "FAIL");
    std::printf("%s  cable off: later switch-off and relight end the same on both\n", d ? "PASS" : "FAIL");
    witness::Flags noOff, noSnap, noArb, noRetry, noReassert, noArbNoSnap;
    noOff.noCableOff = true; noSnap.noOffSnapshot = true; noArb.noCableArbiter = true; noSnap.noSnapReassert = true; noReassert = viaReassert; noReassert.noSnapReassert = true; noArbNoSnap = viaReassert; noArbNoSnap.noCableArbiter = true; noRetry.noApplyRetry = true;
    bool e = !live(noOff), f = !restart(noSnap), g = !conflict(noArb), h = !restart(noArbNoSnap), j = !restart(noReassert), i = !live({}, noRetry);
    std::printf("%s  cable off: a default-lit peer's snapshot does not relight, and the peer is corrected\n", r ? "PASS" : "FAIL");
    std::printf("%s  no-cable-off twin caught\n", e ? "PASS" : "FAIL");
    std::printf("%s  no-off-snapshot twin caught\n", f ? "PASS" : "FAIL");
    std::printf("%s  no-arbiter twin caught\n", g ? "PASS" : "FAIL");
    std::printf("%s  no-arbiter twin caught: a fresh peer's snapshot must not relight\n", h ? "PASS" : "FAIL");
    std::printf("%s  no-reassert twin caught\n", j ? "PASS" : "FAIL");
    std::printf("%s  no-apply-retry twin caught for cable off\n", i ? "PASS" : "FAIL");
    return a && b && c && d && e && f && g && h && i && j && r ? 0 : 1;
}
