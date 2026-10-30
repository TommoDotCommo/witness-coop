// Progress that undoes itself: a timer panel that resets (the Shadows door timers: solved, then 20 s later +0x294 back to 0 in the game
// that ran the timer, while the other game, which had the solve applied, kept it solved for good) and a door that shuts. The other game
// must undo the same, once, without echo; a door that shuts by itself in both games (the timed door) must not bounce; and a save loaded
// again (all progress gone at once, the world's generation changed) is a load, not a run of un-solves.
#include "../sync_core.h"
#include <cstdio>
#include <memory>

struct UndoWorld : witness::World {
    bool solved[3] = {false, false, false}, open[2] = {false, false}, request[2] = {false, false};
    bool refuse = false; uint64_t gen = 1;
    unsigned unsolveCalls = 0, closeCalls = 0;
    bool cameraValid() override { return true; }
    bool scan(std::vector<unsigned>& p, std::vector<unsigned>& d, uint64_t* t) override { p = {1, 2}; d = {10}; *t = 1; return true; }
    bool entityExists(unsigned id) override { return id == 1 || id == 2 || id == 10; }
    uint64_t generation() override { return gen; }
    bool isPanel(unsigned id) override { return id == 1 || id == 2; }
    bool isDoor(unsigned id) override { return id == 10; }
    bool panelSolved(unsigned id) override { return solved[id]; }
    bool doorOpen(unsigned) override { return open[0]; }
    bool doorRequested(unsigned) override { return request[0]; }
    void applyPanelSolved(unsigned id) override { solved[id] = true; }
    bool applyDoorOpen(unsigned) override { open[0] = request[0] = true; return true; }
    bool applyPanelUnsolved(unsigned id) override { ++unsolveCalls; if (refuse) return false; solved[id] = false; return true; }
    bool applyDoorClose(unsigned) override { ++closeCalls; if (refuse) return false; open[0] = request[0] = false; return true; }
    bool pose(float p[3], float a[2]) override { p[0] = p[1] = p[2] = 1; a[0] = a[1] = 0; return true; }
    void openDoor() { open[0] = request[0] = true; }
    void shutDoor() { open[0] = request[0] = false; }
};

struct Pair {
    UndoWorld a, b;
    std::unique_ptr<witness::SyncCore> ca, cb;
    std::vector<witness::Bytes> toA, toB;
    uint64_t now = 0;
    unsigned echoesB = 0, undoEmitsA = 0;
    Pair(witness::Flags fa = {}, witness::Flags fb = {}) {
        witness::Hooks h;
        h.send = [&](const witness::Bytes& d) { toB.push_back(d); };
        h.onEmit = [&](uint8_t kind, unsigned, bool resync) { if (!resync && (kind == witness::EV_PANEL_UNSOLVED || kind == witness::EV_DOOR_CLOSED)) ++undoEmitsA; };
        ca = std::make_unique<witness::SyncCore>(1, 1, &a, h, fa);
        witness::Hooks hb;
        hb.send = [&](const witness::Bytes& d) { toA.push_back(d); };
        hb.onEmit = [&](uint8_t, unsigned, bool resync) { if (!resync) ++echoesB; };
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

// A solves both timer panels; the timer runs out on A only. B must un-solve them too (the first send is lost, then B refuses the call
// for a while), and say nothing back.
bool timer(witness::Flags fa = {}, witness::Flags fb = {}) {
    fa.dropFirstEvent = true;
    Pair p(fa, fb); p.advance(30000);
    p.a.solved[1] = p.a.solved[2] = true; p.advance(3000);
    if (!p.b.solved[1] || !p.b.solved[2]) return false;
    p.b.refuse = true; p.a.solved[1] = p.a.solved[2] = false; p.advance(3000);
    bool held = p.b.solved[1] && p.b.unsolveCalls >= 1;
    p.b.refuse = false; p.advance(6000);
    return held && !p.b.solved[1] && !p.b.solved[2] && p.echoesB == 0 && p.cb->queuedRemote() == 0 && p.ca->unacked() == 0;
}

// A opens the door, later it shuts on A: it shuts on B.
bool door(witness::Flags fa = {}) {
    Pair p(fa); p.advance(30000);
    p.a.openDoor(); p.advance(3000);
    if (!p.b.open[0]) return false;
    p.a.shutDoor(); p.advance(3000);
    return !p.b.open[0] && !p.b.request[0] && p.b.closeCalls == 1 && p.echoesB == 0;
}

// The timed door: opened on A (B has it applied), then it shuts by itself in both games in the same frame. No call is needed on B, and
// neither game opens it again.
bool timedDoor() {
    Pair p; p.advance(30000);
    p.a.openDoor(); p.advance(3000);
    p.a.shutDoor(); p.b.shutDoor(); p.advance(5000);
    return !p.a.open[0] && !p.b.open[0] && p.b.closeCalls == 0 && p.a.closeCalls == 0;
}

// A door shuts and opens again within a moment on A while B refuses the close call for a while: the close is kept and retried, and the open
// that follows it must not be judged against the door as it is now (still open, so nothing to do) and then overtaken by the retried close.
// Both worlds end with the door open. (Seen in a 2-bot playthrough: a door that closed and re-opened while a save was settling.)
bool doorOrder(witness::Flags fb = {}) {
    Pair p({}, fb); p.advance(30000);
    p.a.openDoor(); p.advance(3000);
    if (!p.b.open[0]) return false;
    p.b.refuse = true;
    p.a.shutDoor(); p.advance(100); p.a.openDoor(); p.advance(500);
    p.b.refuse = false; p.advance(6000);
    return p.a.open[0] && p.b.open[0] && p.cb->queuedRemote() == 0;
}

// A save loaded again: the generation changes and the progress is gone at once. That is a load (the peer is asked for its state again),
// not a run of un-solves to share.
bool reload(witness::Flags fa = {}) {
    Pair p(fa); p.advance(30000);
    p.a.solved[1] = p.a.solved[2] = true; p.a.openDoor(); p.advance(3000);
    p.a.gen = 2; p.a.solved[1] = p.a.solved[2] = false; p.a.shutDoor(); p.advance(500);
    return p.undoEmitsA == 0;
}

int main() {
    bool a = timer(), b = door(), c = timedDoor(), d = reload(), o = doorOrder();
    std::printf("%s  timer panels reset on one game: reset on the other (loss, refused call, no echo)\n", a ? "PASS" : "FAIL");
    std::printf("%s  a door that shuts on one game shuts on the other\n", b ? "PASS" : "FAIL");
    std::printf("%s  a timed door shutting in both games at once: no call, no bounce\n", c ? "PASS" : "FAIL");
    std::printf("%s  a save loaded again is not shared as un-solves\n", d ? "PASS" : "FAIL");
    std::printf("%s  a close kept for a retry is not overtaken by the open that followed it\n", o ? "PASS" : "FAIL");
    witness::Flags noUnsolve, noClose, noRetry, noReload;
    noUnsolve.noUnsolveSync = true; noClose.noDoorCloseSync = true; noRetry.noApplyRetry = true; noReload.noReloadSync = true;
    witness::Flags noOrder; noOrder.noEntityOrder = true;
    bool e = !timer(noUnsolve), f = !door(noClose), g = !timer({}, noRetry), h = !reload(noReload), k = !doorOrder(noOrder);
    std::printf("%s  no-unsolve-sync twin caught\n", e ? "PASS" : "FAIL");
    std::printf("%s  no-door-close-sync twin caught\n", f ? "PASS" : "FAIL");
    std::printf("%s  no-apply-retry twin caught for un-solves\n", g ? "PASS" : "FAIL");
    std::printf("%s  no-reload-sync twin caught: a load goes out as un-solves\n", h ? "PASS" : "FAIL");
    std::printf("%s  no-entity-order twin caught: the retried close lands after the open\n", k ? "PASS" : "FAIL");
    return o && k && a && b && c && d && e && f && g && h ? 0 : 1;
}
