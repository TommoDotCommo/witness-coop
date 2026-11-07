// A remote solve is applied with the solver's line and its reactions are not echoed; doors are shared when they are told to move.
//  1. A solves panel 1 by drawing a line: B applies the solve with that same line (the game's reactions read it: which way a sliding bridge goes).
//  2. Applying it runs B's reactions at once (here: door 10 told to open, linked panel 2 solved, cable 20 lit, as A's reactions did): B sends
//     none of that back.
//  3. A's door 11 is told to open and takes 20 s to get there: B gets the open within a second, not after 20 s.
//  4. A solve without a line (a scripted solve) is applied at once, without waiting for a line.
// Twins: noSolveLine (B gets no line), noReactEchoGuard (B echoes the reactions), doorDetectByPosition (the door arrives after it has finished).
#include "../sync_core.h"
#include <cstdio>
#include <memory>

using namespace witness;

struct ReactWorld : World {
    bool solved[5] = {}, powered[5] = {false, true, true, true, true};
    bool doorReq[12] = {}, doorAt[12] = {}, cable[21] = {}, cableReq[21] = {};
    std::map<unsigned, LineOut> lines;
    std::vector<LinePoint> appliedLine; bool applied = false; unsigned appliedCount = 0;
    bool cameraValid() override { return true; }
    bool scan(std::vector<unsigned>& p, std::vector<unsigned>& d, uint64_t* t) override { p = {1, 2, 3, 4}; d = {10, 11}; *t = 1; return true; }
    bool scanCables(std::vector<unsigned>& c) override { c = {20}; return true; }
    bool entityExists(unsigned id) override { return (id >= 1 && id <= 4) || id == 10 || id == 11 || id == 20; }
    bool isPanel(unsigned id) override { return id >= 1 && id <= 4; }
    bool isDoor(unsigned id) override { return id == 10 || id == 11; }
    bool isCable(unsigned id) override { return id == 20; }
    bool panelSolved(unsigned id) override { return solved[id]; }
    bool panelPowered(unsigned id) override { return powered[id]; }
    bool doorOpen(unsigned id) override { return doorAt[id]; }
    bool doorRequested(unsigned id) override { return doorReq[id]; }
    bool cableLit(unsigned id) override { return cable[id]; }
    bool cableRequested(unsigned id) override { return cableReq[id] || cable[id]; }
    void applyPanelSolved(unsigned id) override { std::vector<LinePoint> none; applyPanelSolvedLine(id, none, false); }
    void applyPanelSolvedLine(unsigned id, const std::vector<LinePoint>& line, bool) override {
        solved[id] = true; appliedLine = line; applied = true; ++appliedCount;
        if (id == 1) react();
    }
    void react() { doorReq[10] = true; solved[2] = true; cable[20] = cableReq[20] = true; }     // what the game does when panel 1 is solved
    bool applyDoorOpen(unsigned id) override { doorReq[id] = true; return true; }
    bool slowFade = false;
    bool applyCableLit(unsigned id) override { cableReq[id] = true; if (!slowFade) cable[id] = true; return true; }      // slowFade: lit only when the test says (a cable that takes seconds)
    bool applyPanelPower(unsigned id) override { powered[id] = true; return true; }
    bool scanLines(std::vector<LineOut>& out) override { for (auto& kv : lines) out.push_back(kv.second); return true; }
    bool pose(float p[3], float a[2]) override { p[0] = p[1] = p[2] = 1; a[0] = a[1] = 0; return true; }
};

static LineOut makeLine(unsigned panel) {
    LineOut l; l.panel = panel; l.final = true;
    for (unsigned i = 0; i < 6; ++i) { LinePoint p; p.node = uint16_t(i == 3 ? 10 : i == 4 ? 11 : i); p.d[0] = int16_t(100 * i); l.pts.push_back(p); }
    return l;
}

struct Pair {
    ReactWorld a, b; std::unique_ptr<SyncCore> ca, cb; std::vector<Bytes> toA, toB; uint64_t now = 0; unsigned echoes = 0; uint64_t doorAt = 0;
    Pair(Flags fa = {}, Flags fb = {}) {
        Hooks h; h.send = [&](const Bytes& d) { toB.push_back(d); };
        ca = std::make_unique<SyncCore>(1, 1, &a, h, fa);
        Hooks hb; hb.send = [&](const Bytes& d) { toA.push_back(d); };
        hb.onEmit = [&](uint8_t, unsigned, bool resync) { if (!resync) ++echoes; };
        hb.onApplied = [&](uint8_t kind, unsigned id, uint64_t, uint32_t) { if (kind == EV_DOOR_OPENED && id == 11 && !doorAt) doorAt = now; };
        cb = std::make_unique<SyncCore>(2, 1, &b, hb, fb);
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

struct Result { bool line = false, noEcho = false, doorSoon = false, scriptedSoon = false, fadeNoEcho = false; };

static Result run(Flags fa = {}, Flags fb = {}) {
    Result r; Pair p(fa, fb); p.advance(30000);
    // 1, 2: A draws a line on panel 1 and solves it; A's own reactions happen with it
    // (A's own reactions are picked up one poll after the solve, so their events reach B a little after it: B must not send its own first)
    p.a.lines[1] = makeLine(1); p.a.solved[1] = true; p.advance(50); p.a.react();
    p.advance(3000);
    r.line = p.b.applied && p.b.appliedLine.size() == 6 && p.b.appliedLine[3].node == 10 && p.b.appliedLine[4].node == 11;
    r.noEcho = p.b.solved[2] && p.b.doorReq[10] && p.b.cable[20] && p.echoes == 0;
    // 3: a slow door: told to open now, there in 20 s
    uint64_t told = p.now; p.a.doorReq[11] = true; p.advance(20000); p.a.doorAt[11] = true; p.advance(3000);
    r.doorSoon = p.doorAt && p.doorAt - told <= 1000;
    // 4: a scripted solve, no line: applied within 300 ms
    uint64_t at = p.now; unsigned before = p.b.appliedCount; p.a.solved[3] = true;
    for (int i = 0; i < 6 && p.b.appliedCount == before; ++i) p.advance(50);
    r.scriptedSoon = p.b.appliedCount > before && p.now - at <= 300;
    // 5: A lights a cable that fades in over 9 s on B: B must not report it as its own when it is finally lit
    p.b.slowFade = true; unsigned echoesBefore = p.echoes;
    p.a.cable[20] = false; p.a.cableReq[20] = false; p.b.cable[20] = false; p.b.cableReq[20] = false; p.advance(6000);      // (both dark again: A's off reached B)
    echoesBefore = p.echoes;
    p.a.cable[20] = p.a.cableReq[20] = true; p.advance(9000); p.b.cable[20] = true; p.advance(3000);
    r.fadeNoEcho = p.b.cableReq[20] && p.echoes == echoesBefore;
    return r;
}

int main() {
    Result c = run();
    bool ok = true;
    auto out = [&](bool v, const char* what) { std::printf("%s  %s\n", v ? "PASS" : "FAIL", what); ok = ok && v; };
    out(c.line, "a remote solve is applied with the solver's line (nodes 10-11 used)");
    out(c.noEcho, "the solve's reactions on B (door told to open, linked panel, cable) are not sent back");
    out(c.doorSoon, "a slow door reaches the peer when it is told to open, not when it gets there");
    out(c.scriptedSoon, "a solve without a line is applied without waiting");
    out(c.fadeNoEcho, "a remote cable that takes 9 s to fade in is not sent back when it is lit");
    Flags noLine; noLine.noSolveLine = true;
    Flags noGuard; noGuard.noReactEchoGuard = true;
    Flags byPos; byPos.doorDetectByPosition = true;
    out(!run({}, noLine).line, "twin noSolveLine caught (B gets no line)");
    out(!run({}, noGuard).noEcho, "twin noReactEchoGuard caught (B echoes the reactions)");
    out(!run(byPos, {}).doorSoon, "twin doorDetectByPosition caught (the door arrives after it finished)");
    Flags noFade; noFade.noCableFadeHold = true;
    out(!run({}, noFade).fadeNoEcho, "twin noCableFadeHold caught (the faded-in cable is sent back)");
    return ok ? 0 : 1;
}
