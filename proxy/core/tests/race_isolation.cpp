// A race is not co-op: with Flags::raceMode nothing of the world is shared (solved panels, opened doors, drawn lines), but poses, chat and presence still travel. Compared with the
// same run in co-op (where it all arrives), in a mixed pair (one racer, one co-op player: the co-op sender must not be blocked) and with the two bug twins that must fail.
#include "../sync_core.h"
#include <cstdio>
#include <memory>
#include <string>

using namespace witness;
struct RW : World {
    bool solved[8] = {}, open[4] = {}; bool line = false; unsigned appliedPanels = 0, appliedDoors = 0;
    bool cameraValid() override { return true; }
    bool scan(std::vector<unsigned>& p, std::vector<unsigned>& d, uint64_t* t) override { p = {1, 2, 3}; d = {1, 2}; *t = 1; return true; }
    bool entityExists(unsigned id) override { return id >= 1 && id <= 3; }
    bool isPanel(unsigned id) override { return id >= 1 && id <= 3; }
    bool isDoor(unsigned id) override { return id == 1 || id == 2; }
    bool panelSolved(unsigned id) override { return solved[id]; }
    bool doorOpen(unsigned id) override { return open[id]; }
    void applyPanelSolved(unsigned id) override { solved[id] = true; ++appliedPanels; }
    bool applyDoorOpen(unsigned id) override { open[id] = true; ++appliedDoors; return true; }
    bool scanLines(std::vector<LineOut>& out) override { if (line) { LineOut l; l.panel = 2; l.pts = {{1, {0, 0, 0}}, {2, {10, 0, 0}}, {3, {20, 0, 0}}}; out.push_back(l); } return true; }
    bool pose(float p[3], float a[2]) override { p[0] = 10; p[1] = 20; p[2] = 1; a[0] = a[1] = 0; return true; }
};
struct Pair {
    unsigned emits = 0;
    RW a, b; std::unique_ptr<SyncCore> ca, cb; std::vector<Bytes> toA, toB; uint64_t now = 0;
    Pair(Flags fa, Flags fb) {
        Hooks ha, hb; ha.onEmit = [&](uint8_t, unsigned, bool) { ++emits; }; ha.send = [&](const Bytes& d) { toB.push_back(d); }; hb.send = [&](const Bytes& d) { toA.push_back(d); };
        ca = std::make_unique<SyncCore>(1, 1, &a, ha, fa); cb = std::make_unique<SyncCore>(2, 1, &b, hb, fb); cb->setName("Beta"); ca->setName("Alpha");
    }
    void advance(uint64_t ms) {
        uint64_t end = now + ms;
        while (now < end) {
            now += 50; ca->pollState(now); cb->pollState(now); ca->tick(now); cb->tick(now);
            auto qa = std::move(toA), qb = std::move(toB); toA.clear(); toB.clear();
            for (auto& d : qa) ca->onPacket(d.data(), d.size(), now);
            for (auto& d : qb) cb->onPacket(d.data(), d.size(), now);
            ca->applyRemote(now); cb->applyRemote(now);
        }
    }
};
static int fails = 0, checks = 0;
static void check(const char* what, bool ok) { ++checks; if (!ok) { ++fails; std::printf("FAILED: %s\n", what); } }
struct Out { bool panel, door, line; unsigned emitted, unacked; bool pose; bool chat; };
// A (id 1) solves panel 2 and opens door 1 and draws a line; B (id 2) watches
static Out run(Flags fa, Flags fb) {
    Pair p(fa, fb); p.advance(40000);      // both worlds become stable and settle
    p.a.solved[2] = true; p.a.open[1] = true; p.a.line = true; p.cb->sendChat("hi"); p.ca->sendChat("hello");
    Presence pr; pr.character = "frog"; pr.emote = 3; pr.emoteSeq = 1; p.ca->setPresence(pr);
    p.advance(12000);
    Out o{}; o.panel = p.b.solved[2]; o.door = p.b.open[1]; o.line = !p.cb->remoteLines(p.now).empty(); o.emitted = p.emits; o.unacked = unsigned(p.ca->unacked());
    auto rps = p.cb->remotePoses(p.now); RemotePose rp; if (!rps.empty()) rp = rps[0]; o.pose = rps.size() == 1 && rp.valid && rp.character == "frog" && rp.emote == 3;
    auto ca_ = p.cb->takeChat(), cb_ = p.ca->takeChat(); o.chat = ca_.size() == 1 && cb_.size() == 1;
    std::printf("    [pose %d (valid %d char '%s' emote %d) chat %zu/%zu emitted %u]\n", o.pose, rp.valid, rp.character.c_str(), rp.emote, ca_.size(), cb_.size(), o.emitted);
    return o;
}
int main() {
    Flags co, race; race.raceMode = true;
    Out c = run(co, co);
    check("co-op (the control): a solved panel, an opened door and a drawn line arrive in the other game", c.panel && c.door && c.line && c.emitted >= 2);
    check("co-op: poses, presence and chat arrive", c.pose && c.chat);
    Out r = run(race, race);
    check("race: the other game's panel stays unsolved, its door shut, no line shown", !r.panel && !r.door && !r.line);
    check("race: nothing is even announced (no events made)", r.emitted == 0);
    check("race: the other player is still seen (pose, character, emote) and chat still works", r.pose && r.chat);
    Out m = run(co, race);
    check("mixed (a co-op sender, a racer): the racer's world is untouched", !m.panel && !m.door && !m.line);
    check("mixed: the co-op sender is not blocked (everything it sent was acknowledged)", m.unacked == 0);
    Flags emits = race; emits.raceEmits = true; Out te = run(emits, race);
    check("twin raceEmits: a racer that still announces its progress is caught (events were made)", te.emitted > 0);
    Flags applies = race; applies.raceApplies = true; Out ta = run(co, applies);
    check("twin raceApplies: a racer that still applies what it receives is caught (its world changed)", ta.panel || ta.door);
    std::printf("%d checks, %d failed\n", checks, fails); return fails ? 1 : 0;
}
