// Deterministic cable sync: a solved panel lights its cable in the game's own code on the machine that solved it. The other game
// must light the same cable through the same call, once, without echo, and a fresh receiver must get it from the snapshot.
#include "../sync_core.h"
#include <cstdio>
#include <memory>

struct CableWorld : witness::World {
    bool solved[2] = {}, lit[4] = {false, true, false, false};     // cable 1 is lit from the start, like the game's default-lit cables
    bool refuse = false;
    unsigned calls = 0;
    bool cameraValid() override { return true; }
    bool scan(std::vector<unsigned>& p, std::vector<unsigned>&, uint64_t* t) override { p = {1}; *t = 1; return true; }
    bool scanCables(std::vector<unsigned>& c) override { c = {1, 2, 3}; return true; }
    bool entityExists(unsigned id) override { return id >= 1 && id <= 3; }
    bool isPanel(unsigned id) override { return id == 1; }
    bool isDoor(unsigned) override { return false; }
    bool isCable(unsigned id) override { return id >= 1 && id <= 3; }
    bool panelSolved(unsigned id) override { return id == 1 && solved[1]; }
    bool doorOpen(unsigned) override { return false; }
    bool cableLit(unsigned id) override { return lit[id]; }
    void applyPanelSolved(unsigned id) override { if (id == 1) solved[1] = true; }
    bool applyDoorOpen(unsigned) override { return false; }
    bool applyCableLit(unsigned id) override { ++calls; if (refuse) return false; lit[id] = true; return true; }
    bool pose(float p[3], float a[2]) override { p[0] = p[1] = p[2] = 1; a[0] = a[1] = 0; return true; }
};

struct Pair {
    CableWorld a, b;
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
        b = CableWorld();
        witness::Hooks h;
        h.send = [&](const witness::Bytes& d) { toA.push_back(d); };
        h.onEmit = [&](uint8_t, unsigned, bool resync) { if (!resync) ++echoes; };
        h.onApplied = [&](uint8_t kind, unsigned, uint64_t, uint32_t) { if (kind == witness::EV_CABLE_LIT) ++applied; };
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

// A solve lights cable 2 on A. The first send is lost, then B refuses the call for longer than the echo guard interval.
bool live(witness::Flags fa = {}, witness::Flags fb = {}) {
    fa.dropFirstEvent = true;
    Pair p(fa, fb); p.advance(30000);
    p.b.refuse = true;
    p.a.solved[1] = true; p.a.lit[2] = true;
    p.advance(2000);
    bool retained = !p.b.lit[2] && p.ca->unacked() == 0 && p.cb->queuedRemote() >= 1 && p.applied == 0;
    p.advance(6000);
    p.b.refuse = false; p.advance(6000);
    return retained && p.b.lit[2] && !p.b.lit[3] && p.applied == 1 && p.echoes == 0 && p.cb->queuedRemote() == 0 && p.b.solved[1];
}

bool restart(witness::Flags fa = {}) {
    Pair p(fa); p.advance(30000);
    p.a.solved[1] = true; p.a.lit[2] = true; p.advance(2000);
    if (!p.b.lit[2] || p.ca->unacked()) return false;
    p.startB(2); p.advance(30000);
    return p.b.lit[2] && !p.b.lit[3] && p.echoes == 0 && p.ca->unacked() == 0;
}

int main() {
    bool a = live(), b = restart();
    std::printf("%s  cable: loss, refused call, no echo, other cable untouched\n", a ? "PASS" : "FAIL");
    std::printf("%s  cable: fresh receiver recovers after original ACK\n", b ? "PASS" : "FAIL");
    witness::Flags disabled, noRetry, noEcho, noResync;
    disabled.noCableSync = true; noRetry.noApplyRetry = true; noEcho.noEchoGuard = true; noResync.noResyncOnRestart = true;
    bool c = !live(disabled), d = !live({}, noRetry), e = !live({}, noEcho), f = !restart(noResync);
    std::printf("%s  disabled-cable twin caught\n", c ? "PASS" : "FAIL");
    std::printf("%s  no-apply-retry twin caught for cable\n", d ? "PASS" : "FAIL");
    std::printf("%s  no-echo-guard twin caught for cable\n", e ? "PASS" : "FAIL");
    std::printf("%s  no-resync twin caught for cable\n", f ? "PASS" : "FAIL");
    return a && b && c && d && e && f ? 0 : 1;
}
