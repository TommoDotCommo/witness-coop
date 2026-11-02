// Deterministic same-process save reload: one player loads an older save while the process and the link stay up. The reloaded
// world has lost progress, shows a start-up transient, and nothing restarted, so no session change tells the peer. The core must
// notice the regression, re-stabilise, take the peer's full state, and not publish the load's transient as player progress.
#include "../sync_core.h"
#include <cstdio>
#include <memory>
#include <string>

struct ReloadWorld : witness::World {
    bool solved[4] = {}, open[4] = {}; uint64_t gen = 1;      // gen: the world's generation (a load re-creates the entities)
    bool gap = false; uint64_t table = 1;                     // gap: a load is under way (the entities are missing, the generation reads 0); table: the entity table's address
    bool half = false;                                        // half: the load is building the world: its entities exist in their default state, the generation is still unreadable
    uint64_t generation() override { return gap || half ? 0 : gen; }
    bool cameraValid() override { return true; }
    bool scan(std::vector<unsigned>& p, std::vector<unsigned>& d, uint64_t* t) override { if (gap) { p.clear(); d.clear(); } else { p = {1, 2}; d = {3, 4}; } *t = table; return true; }
    bool entityExists(unsigned id) override { return !gap && id >= 1 && id <= 4; }
    bool isPanel(unsigned id) override { return !gap && (id == 1 || id == 2); }
    bool isDoor(unsigned id) override { return !gap && (id == 3 || id == 4); }
    bool panelSolved(unsigned id) override { return !gap && solved[id - 1]; }
    bool doorOpen(unsigned id) override { return !gap && open[id - 1]; }
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

// A save point restored into a fresh game (tools/suite.py 'save point restore'): A loads a save with MORE progress than its world has. The game
// takes the world down first (entities missing, generation 0, a new entity table) and builds it again; the core's rescan of the entity
// table runs every 3 s and, before the fix, a rescan that fell into that gap took a silent new baseline (generation 0), and the next one folded
// the loaded progress in as A's start-up world: B never got it (live: A had 16 panels, B none, A emitted nothing). gapMs 4000 always puts a
// rescan into the gap; 300 ms usually does not (the old rule caught that case through the changed generation). B solved panel 2 before the
// load and A's save lacks it: it must come back to A (a load merges, it never takes B's progress away).
bool restoreIntoFresh(witness::Flags fa, uint64_t gapMs) {
    witness::Flags none; Pair p(none); witness::Hooks ha; ha.send = [&](const witness::Bytes& d) { p.toB.push_back(d); };
    p.ca = std::make_unique<witness::SyncCore>(1, 1, &p.a, ha, fa);
    p.advance(30000);
    p.b.solved[1] = true; p.advance(5000);                   // B's own progress, shared live before the load
    if (!p.a.solved[1]) return false;
    p.a.gap = true; p.a.table++; p.advance(gapMs);           // the load takes A's world down
    p.a.gap = false; p.a.gen++; p.a.table++;                 // and builds the saved one: panel 1 solved, door 3 open, panel 2 not (the save is older)
    for (int i = 0; i < 4; ++i) p.a.solved[i] = p.a.open[i] = false;
    p.a.solved[0] = true; p.a.open[2] = true;
    p.advance(40000);
    return p.b.solved[0] && p.b.open[2] && p.a.solved[0] && p.a.open[2] && p.a.solved[1] && p.b.solved[1];
}

// A moment's unreadable generation in a game that loads nothing (seen live on B on 2026-11-02, one poll): detection waits for it and goes on, it is
// no load. A then opens door 4: B must have it within two seconds and A must not have re-gated. (Taken for a load, A re-gates and its new start-up
// window folds the door into the baseline: B never gets it.)
bool blip(witness::Flags fa) {
    witness::Flags none; Pair p(none); witness::Hooks ha; ha.send = [&](const witness::Bytes& d) { p.toB.push_back(d); };
    p.ca = std::make_unique<witness::SyncCore>(1, 1, &p.a, ha, fa);
    p.advance(30000);
    p.a.gap = true; p.advance(100); p.a.gap = false;          // one unreadable poll, the same world after it
    p.advance(500); p.a.open[3] = true; p.advance(2000);
    return p.b.open[3] && p.ca->stats().reloads == 0;
}

// The other live failure of the same gap: the half-built world (entities there, generation still unreadable) has door 4 standing open by default,
// and A sent it as a live opening before the new generation showed the load (27 such doors stayed open on B for good: the load's snapshot lists
// only what is open, and the opening had been acknowledged). B must end with A's loaded world: panel 1 and door 3, door 4 shut.
bool halfBuilt(witness::Flags fa) {
    witness::Flags none; Pair p(none); witness::Hooks ha; ha.send = [&](const witness::Bytes& d) { p.toB.push_back(d); };
    p.ca = std::make_unique<witness::SyncCore>(1, 1, &p.a, ha, fa);
    p.advance(30000);
    p.a.half = true; p.a.open[3] = true; p.advance(1500);                    // the world being built: door 4 open by default
    p.a.half = false; p.a.gen++; p.a.table++; p.a.open[3] = false; p.a.solved[0] = true; p.a.open[2] = true;      // the loaded save
    p.advance(40000);
    return p.b.solved[0] && p.b.open[2] && !p.b.open[3] && !p.a.open[3];
}

// A save point restored without its game save (tools/suite.py 'MUTANT: save point restore without the save file'), and any progress list applied
// after a load: A loads a world with nothing solved, and the mod then applies the list (panel 1). The game's own scripts react later: door 3
// opens 3 s after the list and door 4 6 s after it. Live (2026-11-02) the list landed just as the reloaded world became stable, so the panel
// reached B with the full state, but the doors came inside the start-up window and were folded into A's baseline: 8 doors stayed open on A only.
// early: the list is applied 2 s after the load, before the world is stable (its panel and door 3 are then in the stable world's baseline).
// B must end with panel 1 and both doors; and a start-up flip on A after a load with no list (door 4 opening and shutting again) is still folded.
bool listAfterLoad(witness::Flags fa, bool early) {
    witness::Flags none; Pair p(none); witness::Hooks ha; ha.send = [&](const witness::Bytes& d) { p.toB.push_back(d); };
    p.ca = std::make_unique<witness::SyncCore>(1, 1, &p.a, ha, fa);
    p.advance(30000);
    p.a.gap = true; p.a.table++; p.advance(300);             // the load takes A's world down
    p.a.gap = false; p.a.gen++; p.a.table++;                 // and builds a fresh one (nothing solved, as before)
    if (early) p.advance(2000);
    else { p.advance(500); for (int i = 0; i < 400 && !p.ca->ready(); ++i) p.advance(50); if (!p.ca->ready()) return false; }
    p.a.solved[0] = true; p.ca->modProgressApplied(p.now);   // the mod applies the list
    p.advance(early ? 1000 : 3000); p.a.open[2] = true;      // the game's script opens door 3
    if (early) { for (int i = 0; i < 400 && !p.ca->ready(); ++i) p.advance(50); p.advance(2000); }
    else p.advance(3000);
    p.a.open[3] = true;                                      // and door 4 (early: 2 s after the world became stable)
    p.advance(40000);
    return p.b.solved[0] && p.b.open[2] && p.b.open[3];
}

// The rule above must not share an ordinary start-up flip: A loads again with no list applied, door 4 (open since the list) shuts and opens
// once more while the world settles. B must keep it open (no close was sent) and must not see A re-solving anything.
bool loadWithoutList(witness::Flags fa) {
    witness::Flags none; Pair p(none); witness::Hooks ha; ha.send = [&](const witness::Bytes& d) { p.toB.push_back(d); };
    p.ca = std::make_unique<witness::SyncCore>(1, 1, &p.a, ha, fa);
    p.advance(30000);
    p.a.gap = true; p.a.table++; p.advance(300);
    p.a.gap = false; p.a.gen++; p.a.table++; p.a.open[3] = true;          // the load: door 4 stands open in the saved world
    p.advance(500); for (int i = 0; i < 400 && !p.ca->ready(); ++i) p.advance(50);
    p.advance(2000); p.a.open[3] = false; p.advance(1000); p.a.open[3] = true;      // a start-up flip in the settling window
    p.advance(40000);
    return p.b.open[3] && p.ca->stats().emittedDoors == 0;
}

int main() {
    bool all = true;
    { bool ok = halfBuilt({}); std::printf("%s  the half-built world's default-open door is not sent as progress\n", ok ? "PASS" : "FAIL"); all &= ok;
      witness::Flags twin; twin.reloadGapIgnored = true; bool caught = !halfBuilt(twin);
      std::printf("%s  reload-gap-ignored twin: the default-open door stays open on B\n", caught ? "PASS" : "FAIL"); all &= caught; }
    { bool ok = blip({}); std::printf("%s  a moment's unreadable generation is no load: a door opened after it reaches B at once\n", ok ? "PASS" : "FAIL"); all &= ok;
      witness::Flags twin; twin.gapIsLoad = true; bool caught = !blip(twin);
      std::printf("%s  gap-is-load twin: the blip re-gates A and the door is folded into its start-up baseline\n", caught ? "PASS" : "FAIL"); all &= caught; }
    for (bool early : {false, true}) {
        bool ok = listAfterLoad({}, early);
        std::printf("%s  a progress list applied after a load (%s): the doors the game opens in response reach B\n", ok ? "PASS" : "FAIL", early ? "before the world is stable" : "as it becomes stable"); all &= ok;
        witness::Flags twin; twin.noModApplyShare = true; bool caught = !listAfterLoad(twin, early);
        std::printf("%s  no-mod-apply-share twin: the doors are folded into the start-up baseline (%s)\n", caught ? "PASS" : "FAIL", early ? "early list" : "list at stable"); all &= caught;
    }
    { bool ok = loadWithoutList({}); std::printf("%s  a load with no list still folds its start-up flips (nothing emitted)\n", ok ? "PASS" : "FAIL"); all &= ok;
      witness::Flags twin; twin.noSettle = true; bool caught = !loadWithoutList(twin);
      std::printf("%s  no-settle twin: the start-up flip goes out as a close and an open\n", caught ? "PASS" : "FAIL"); all &= caught; }
    for (uint64_t gap : {4000u, 300u}) {
        bool ok = restoreIntoFresh({}, gap);
        std::printf("%s  save point restored into a fresh game (load gap %llu ms): B gets the loaded progress, A gets B's back\n", ok ? "PASS" : "FAIL", (unsigned long long)gap); all &= ok;
        witness::Flags twin; twin.reloadGapIgnored = true; bool caught = !restoreIntoFresh(twin, gap);
        std::printf("%s  reload-gap-ignored twin: the loaded progress is folded into a silent baseline (gap %llu ms)\n", caught ? "PASS" : "FAIL", (unsigned long long)gap); all &= caught;
    }
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
