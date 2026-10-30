// Deterministic same-process save reload: one player loads an older save while the process and the link stay up. The reloaded
// world has lost progress, shows a start-up transient, and nothing restarted, so no session change tells the peer. The core must
// notice the regression, re-stabilise, take the peer's full state, and not publish the load's transient as player progress.
#include "../sync_core.h"
#include <cstdio>
#include <memory>
#include <string>

struct ReloadWorld : witness::World {
    bool solved[4] = {}, open[4] = {}; uint64_t gen = 1;      // gen: the world's generation (a load re-creates the entities)
    uint64_t generation() override { return gen; }
    bool cameraValid() override { return true; }
    bool scan(std::vector<unsigned>& p, std::vector<unsigned>& d, uint64_t* t) override { p = {1, 2}; d = {3, 4}; *t = 1; return true; }
    bool entityExists(unsigned id) override { return id >= 1 && id <= 4; }
    bool isPanel(unsigned id) override { return id == 1 || id == 2; }
    bool isDoor(unsigned id) override { return id == 3 || id == 4; }
    bool panelSolved(unsigned id) override { return solved[id - 1]; }
    bool doorOpen(unsigned id) override { return open[id - 1]; }
    void applyPanelSolved(unsigned id) override { solved[id - 1] = true; }
    bool applyDoorOpen(unsigned id) override { open[id - 1] = true; return true; }
    bool pose(float p[3], float a[2]) override { p[0] = p[1] = p[2] = 1; a[0] = a[1] = 0; return true; }
};

struct Pair {
    ReloadWorld a, b;
    std::unique_ptr<witness::SyncCore> ca, cb;
    std::vector<witness::Bytes> toA, toB;
    uint64_t now = 0;
    Pair(witness::Flags fb) {
        witness::Hooks ha, hb;
        ha.send = [&](const witness::Bytes& d) { toB.push_back(d); };
        hb.send = [&](const witness::Bytes& d) { toA.push_back(d); };
        ca = std::make_unique<witness::SyncCore>(1, 1, &a, ha);
        cb = std::make_unique<witness::SyncCore>(2, 1, &b, hb, fb);
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

// mode "older save": B loads a save from before the solve. "second reload": the same, twice in a row. "progress only in the save":
// B's loaded save has a panel A never saw, so the reload must also flow B -> A.
bool run(const char* mode, witness::Flags fb) {
    Pair p(fb); p.advance(30000);
    p.a.solved[0] = true;                                    // A solves panel 1; door 3 follows a moment later
    p.advance(400); p.a.open[2] = true; p.advance(20000);
    if (!p.b.solved[0] || !p.b.open[2]) return false;        // live sync worked before the reload
    for (int round = 0; round < (std::string(mode) == "second reload" ? 2 : 1); ++round) {
        for (int i = 0; i < 4; ++i) p.b.solved[i] = p.b.open[i] = false;       // the load: old save, everything solved is gone
        p.b.gen++;                                           // and every entity was created anew
        if (std::string(mode) == "progress only in the save") p.b.solved[1] = true;
        p.advance(1500); p.b.open[3] = true;                 // door 4 flips while the world is still starting up: not player progress
        p.advance(1000); p.b.open[3] = false;
        p.advance(40000);
    }
    bool ok = p.b.solved[0] && p.b.open[2];                  // B caught up from A without any restart
    ok = ok && p.a.solved[0] && p.a.open[2] && !p.a.open[3]; // A is untouched and did not learn the transient door
    if (std::string(mode) == "progress only in the save") ok = ok && p.a.solved[1] && p.b.solved[1];
    return ok;
}

// A timed door (A's door 3) shuts by itself while every panel stays solved; A then solves panel 2. That is no load: B must have panel 2
// within two seconds, as any live solve. (Taken for a load, A re-gates on a stable world and holds every event back for 9 s or more.)
bool timer(witness::Flags fa) {
    witness::Flags none; Pair p(none); witness::Hooks ha; ha.send = [&](const witness::Bytes& d) { p.toB.push_back(d); };
    p.ca = std::make_unique<witness::SyncCore>(1, 1, &p.a, ha, fa);      // A with the flags under test
    p.advance(30000); p.a.solved[0] = true; p.advance(400); p.a.open[2] = true; p.advance(20000);
    if (!p.b.solved[0] || !p.b.open[2]) return false;
    p.a.open[2] = false; p.advance(4000);                    // the timer runs out: the door closes, nothing else changes
    p.a.solved[1] = true; p.advance(2000);                   // A solves the next panel
    return p.b.solved[1];
}

// Two panels reset by themselves (the desert's panels do, as the bot found) while the world stays the same one; A then opens door 4. That is no
// load: B must have the door within two seconds. (Judged by vanished progress, two panels count as a load and A holds everything back for 9 s+.)
bool resetting(witness::Flags fa) {
    witness::Flags none; Pair p(none); witness::Hooks ha; ha.send = [&](const witness::Bytes& d) { p.toB.push_back(d); };
    p.ca = std::make_unique<witness::SyncCore>(1, 1, &p.a, ha, fa);
    p.advance(30000); p.a.solved[0] = p.a.solved[1] = true; p.advance(20000);
    if (!p.b.solved[0] || !p.b.solved[1]) return false;
    p.a.solved[0] = p.a.solved[1] = false; p.advance(4000);      // both reset themselves; the generation does not change
    p.a.open[3] = true; p.advance(2000);
    return p.b.open[3];
}

int main() {
    bool all = true;
    for (const char* mode : {"older save", "second reload", "progress only in the save"}) {
        bool ok = run(mode, {});
        std::printf("%s  %s\n", ok ? "PASS" : "FAIL", mode); all &= ok;
        witness::Flags twin; twin.noReloadSync = true;
        bool caught = !run(mode, twin);
        std::printf("%s  no-reload-sync twin: %s\n", caught ? "PASS" : "FAIL", mode); all &= caught;
    }
    { bool ok = timer({}); std::printf("%s  a timed door shutting is not a load\n", ok ? "PASS" : "FAIL"); all &= ok;
      witness::Flags twin; twin.reloadOnDoor = true; bool caught = !timer(twin);
      std::printf("%s  reload-on-door twin: a timed door stalls sync\n", caught ? "PASS" : "FAIL"); all &= caught; }
    { bool ok = resetting({}); std::printf("%s  panels resetting themselves are not a load\n", ok ? "PASS" : "FAIL"); all &= ok;
      witness::Flags twin; twin.reloadByProgress = true; bool caught = !resetting(twin);
      std::printf("%s  reload-by-progress twin: resetting panels stall sync\n", caught ? "PASS" : "FAIL"); all &= caught; }
    return all ? 0 : 1;
}
