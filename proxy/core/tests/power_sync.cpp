#include "../sync_core.h"
#include <cstdio>
#include <memory>

struct PowerWorld : witness::World {
    bool solved[4] = {}, powered[4] = {false, true, false, false};
    bool refuse = false;
    unsigned calls = 0;
    bool cameraValid() override { return true; }
    bool scan(std::vector<unsigned>& p, std::vector<unsigned>&, uint64_t* t) override { p = {1, 2, 3}; *t = 1; return true; }
    bool entityExists(unsigned id) override { return id >= 1 && id <= 3; }
    bool isPanel(unsigned id) override { return entityExists(id); }
    bool isDoor(unsigned) override { return false; }
    bool panelSolved(unsigned id) override { return solved[id]; }
    bool doorOpen(unsigned) override { return false; }
    bool panelPowered(unsigned id) override { return powered[id]; }
    void applyPanelSolved(unsigned id) override { solved[id] = true; }
    bool applyDoorOpen(unsigned) override { return false; }
    bool applyPanelPower(unsigned id) override { ++calls; if (refuse) return false; powered[id] = true; return true; }
    bool pose(float p[3], float a[2]) override { p[0] = p[1] = p[2] = 1; a[0] = a[1] = 0; return true; }
};

struct Pair {
    PowerWorld a, b;
    std::unique_ptr<witness::SyncCore> ca, cb;
    std::vector<witness::Bytes> toA, toB;
    uint64_t now = 0;
    unsigned echoes = 0, powersApplied = 0;
    Pair(witness::Flags fa = {}, witness::Flags fb = {}) {
        witness::Hooks h;
        h.send = [&](const witness::Bytes& d) { toB.push_back(d); };
        ca = std::make_unique<witness::SyncCore>(1, 1, &a, h, fa);
        startB(1, fb);
    }
    void startB(unsigned session, witness::Flags flags = {}) {
        b = PowerWorld();
        witness::Hooks h;
        h.send = [&](const witness::Bytes& d) { toA.push_back(d); };
        h.onEmit = [&](uint8_t, unsigned, bool resync) { if (!resync) ++echoes; };
        h.onApplied = [&](uint8_t kind, unsigned, uint64_t, uint32_t) { if (kind == witness::EV_PANEL_POWERED) ++powersApplied; };
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

bool live(witness::Flags fa = {}, witness::Flags fb = {}) {
    fa.dropFirstEvent = true;
    Pair p(fa, fb); p.advance(30000);
    p.b.refuse = true;
    p.a.solved[1] = true; p.a.powered[2] = true;
    p.advance(2000);
    bool retained = p.b.solved[1] && !p.b.powered[2] && p.ca->unacked() == 0 && p.cb->queuedRemote() == 1 && p.powersApplied == 0;
    p.advance(6000); // unavailable for longer than the echo guard interval
    p.b.refuse = false; p.advance(6000);
    return retained && p.b.powered[2] && !p.b.solved[2] && p.powersApplied == 1 && p.echoes == 0 && p.cb->queuedRemote() == 0;
}

bool restart(witness::Flags fa = {}) {
    Pair p(fa); p.advance(30000);
    p.a.solved[1] = true; p.a.powered[2] = true; p.advance(2000);
    if (!p.b.powered[2] || p.ca->unacked()) return false;
    p.startB(2); p.advance(30000);
    return p.b.powered[2] && !p.b.solved[2] && p.echoes == 0 && p.ca->unacked() == 0;
}

void word(witness::Bytes& d, size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) d[at+i] = uint8_t(v >> (8*i)); }
void checksum(witness::Bytes& d) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < d.size()-4; ++i) { h ^= d[i]; h *= 16777619u; }
    word(d, d.size()-4, h);
}
bool legacyRejected() {
    Pair p; p.advance(30000);
    p.a.solved[3] = true; p.ca->pollState(30150); p.ca->tick(30150);
    bool event = false;
    witness::Bytes currentPose;
    for (auto d : p.toB) {
        if (d.size() == 32) {
            auto legacy = d; word(legacy, 0, 0x45435457); checksum(legacy); // v1 WTCE, valid checksum and sequence
            p.cb->onPacket(legacy.data(), legacy.size(), 30150); p.cb->applyRemote(30150);
            if (p.b.solved[3] || p.cb->queuedRemote()) return false;
            p.cb->onPacket(d.data(), d.size(), 30150); p.cb->applyRemote(30150);
            event = p.b.solved[3]; // identical event with v2 magic must apply: the fixture can detect acceptance
        } else if (d.size() == 40) {
            word(d, 32, 999); checksum(d);
            auto legacy = d; word(legacy, 0, 0x50435457); checksum(legacy); // v1 WTCP must not trigger restart
            p.cb->onPacket(legacy.data(), legacy.size(), 30150);
            if (p.cb->peerSession() != 1) return false;
            // Deliver the v2 control after the event so its session change cannot affect the event fixture.
            currentPose = d;
        }
    }
    if (currentPose.empty()) return false;
    p.cb->onPacket(currentPose.data(), currentPose.size(), 30150);
    return event && p.cb->peerSession() == 999;
}

int main() {
    bool a = live(), b = restart();
    std::printf("%s  power-on: loss, refused call, no echo, target remains unsolved\n", a ? "PASS" : "FAIL");
    std::printf("%s  power-on: fresh receiver recovers after original ACK\n", b ? "PASS" : "FAIL");
    witness::Flags disabled, noRetry, noEcho, noResync;
    disabled.noPanelPower = true; noRetry.noApplyRetry = true;
    noEcho.noEchoGuard = true; noResync.noResyncOnRestart = true;
    bool c = !live(disabled), d = !live({}, noRetry), e = !live({}, noEcho), f = !restart(noResync);
    std::printf("%s  disabled-power twin caught\n", c ? "PASS" : "FAIL");
    std::printf("%s  no-apply-retry twin caught for power\n", d ? "PASS" : "FAIL");
    std::printf("%s  no-echo-guard twin caught for power\n", e ? "PASS" : "FAIL");
    std::printf("%s  no-resync twin caught for power\n", f ? "PASS" : "FAIL");
    bool g = legacyRejected();
    std::printf("%s  v1 event/pose ignored, v2 event control accepted\n", g ? "PASS" : "FAIL");
    return a && b && c && d && e && f && g ? 0 : 1;
}
