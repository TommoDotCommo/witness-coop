// The stable-world gate waits for World::worldReady: while the mod itself still sets up a loaded world (the built-in randomizer writes its
// puzzles into the panels), nothing from the other player may be applied to it: a solve drawn on the randomized panel must not land on the
// panel as the game loaded it, to be overwritten a moment later. B's randomizer takes until 40 s; A solves panel 1 at 20 s. B must apply it
// only after its world is ready, and must still apply it. Twin noWorldReady (the gate as before) must fail.
#include "../sync_core.h"
#include <cstdio>
#include <memory>

struct W : witness::World {
    bool solved[2] = {}, open[2] = {}; bool ready = true;
    bool worldReady() override { return ready; }
    bool cameraValid() override { return true; }
    bool scan(std::vector<unsigned>& p, std::vector<unsigned>& d, uint64_t* t) override { p = {1, 2}; d = {3, 4}; *t = 1; return true; }
    bool entityExists(unsigned id) override { return id >= 1 && id <= 4; }
    bool isPanel(unsigned id) override { return id == 1 || id == 2; }
    bool isDoor(unsigned id) override { return id == 3 || id == 4; }
    bool panelSolved(unsigned id) override { return solved[id - 1]; }
    bool doorOpen(unsigned id) override { return open[id - 3]; }
    uint64_t* clock = nullptr; uint64_t appliedAt = 0;
    void applyPanelSolved(unsigned id) override { solved[id - 1] = true; if (clock && !appliedAt) appliedAt = *clock; }
    bool applyDoorOpen(unsigned id) override { open[id - 3] = true; return true; }
    bool pose(float p[3], float a[2]) override { p[0] = p[1] = p[2] = 1; a[0] = a[1] = 0; return true; }
};

static bool run(witness::Flags fb) {
    W a, b; b.ready = false;
    std::vector<witness::Bytes> toA, toB;
    witness::Hooks ha, hb;
    ha.send = [&](const witness::Bytes& d) { toB.push_back(d); };
    hb.send = [&](const witness::Bytes& d) { toA.push_back(d); };
    witness::SyncCore ca(1, 1, &a, ha), cb(2, 1, &b, hb, fb);
    uint64_t now = 0; b.clock = &now;
    auto advance = [&](uint64_t ms) {
        for (uint64_t end = now + ms; now < end;) {
            now += 50;
            if (now == 20000) a.solved[0] = true;         // A solves panel 1
            if (now == 40000) b.ready = true;             // B's randomizer is done
            ca.pollState(now); cb.pollState(now); ca.tick(now); cb.tick(now);
            auto qa = std::move(toA), qb = std::move(toB); toA.clear(); toB.clear();
            for (auto& d : qa) ca.onPacket(d.data(), d.size(), now);
            for (auto& d : qb) cb.onPacket(d.data(), d.size(), now);
            ca.applyRemote(now); cb.applyRemote(now);
        }
    };
    advance(90000);
    if (!b.solved[0]) { std::printf("A's solve never reached B\n"); return false; }
    if (b.appliedAt < 40000) { std::printf("B applied A's solve at %llu ms, before its randomizer was done (40000)\n", (unsigned long long)b.appliedAt); return false; }
    return true;
}

int main() {
    bool ok = run(witness::Flags());
    witness::Flags twin; twin.noWorldReady = true;
    bool twinOk = run(twin);
    std::printf("world-ready: clean %s, twin noWorldReady %s\n", ok ? "pass" : "FAIL", twinOk ? "SURVIVED" : "killed");
    return ok && !twinOk ? 0 : 1;
}
