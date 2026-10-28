// Names and ping: each player sends its name with a ping once a second, the peer answers, and the round trip (smoothed) is what the other side shows.
// It must come out near twice the one-way delay, survive loss, go back to "unknown" when the peer goes quiet, and ignore a pong that is not for the ping in flight.
#include "../sync_core.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace witness;

struct W : World {
    bool cameraValid() override { return true; }
    bool scan(std::vector<unsigned>& p, std::vector<unsigned>&, uint64_t* t) override { p = {5}; *t = 1; return true; }
    bool entityExists(unsigned id) override { return id == 5; }
    bool isPanel(unsigned id) override { return id == 5; }
    bool isDoor(unsigned) override { return false; }
    bool panelSolved(unsigned) override { return false; }
    bool doorOpen(unsigned) override { return false; }
    void applyPanelSolved(unsigned) override {}
    bool applyDoorOpen(unsigned) override { return false; }
    bool pose(float p[3], float a[2]) override { p[0] = p[1] = p[2] = 1; a[0] = a[1] = 0; return true; }
};
struct Msg { uint64_t at; Bytes d; };
struct Pair {
    W wa, wb; std::unique_ptr<SyncCore> ca, cb; std::vector<Msg> toA, toB; uint64_t now = 0, delay = 100; unsigned lossPct = 0, seed = 7; bool aAlive = true;
    bool lose() { seed = seed * 1103515245u + 12345u; return ((seed >> 16) % 100) < lossPct; }
    Pair(Flags fa = {}, Flags fb = {}) {
        Hooks ha; ha.send = [&](const Bytes& d) { if (!lose()) toB.push_back({now + delay, d}); };
        Hooks hb; hb.send = [&](const Bytes& d) { if (!lose()) toA.push_back({now + delay, d}); };
        ca = std::make_unique<SyncCore>(1, 1, &wa, ha, fa); cb = std::make_unique<SyncCore>(2, 1, &wb, hb, fb); ca->setName("Alpha"); cb->setName("Beta");
    }
    void advance(uint64_t ms) {
        uint64_t end = now + ms;
        while (now < end) {
            now += 10; if (aAlive) { ca->pollState(now); ca->tick(now); } cb->pollState(now); cb->tick(now);
            auto qa = std::move(toA), qb = std::move(toB); toA.clear(); toB.clear();      // replies sent while delivering must not be lost
            for (auto& m : qa) { if (m.at <= now) { if (aAlive) ca->onPacket(m.d.data(), m.d.size(), now); } else toA.push_back(m); }
            for (auto& m : qb) { if (m.at <= now) cb->onPacket(m.d.data(), m.d.size(), now); else toB.push_back(m); }
        }
    }
    RemotePose seenByB() { auto v = cb->remotePoses(now); return v.empty() ? RemotePose() : v[0]; }
    RemotePose seenByA() { auto v = ca->remotePoses(now); return v.empty() ? RemotePose() : v[0]; }
};

static bool basic(Flags fa = {}) {
    Pair p(fa); p.advance(8000);
    RemotePose b = p.seenByB(), a = p.seenByA();
    return b.name == "Alpha" && a.name == "Beta" && b.pingMs >= 170 && b.pingMs <= 260 && a.pingMs >= 170 && a.pingMs <= 260;
}
static bool lossy() { Pair p; p.lossPct = 35; p.advance(30000); RemotePose b = p.seenByB(); return b.name == "Alpha" && b.pingMs >= 170 && b.pingMs <= 260; }
static bool expiry(Flags fb = {}) { Pair p({}, fb); p.advance(6000); if (p.seenByB().pingMs < 0) return false; p.aAlive = false; p.advance(12000); return p.seenByB().pingMs == -1; }
static bool followsDelay() { Pair p; p.delay = 400; p.advance(10000); RemotePose b = p.seenByB(); return b.pingMs >= 780 && b.pingMs <= 880; }
// a pong for another nonce is ignored: the ping must not jump
static bool forged() {
    Pair p; p.advance(6000); int before = p.seenByB().pingMs;
    Bytes b; auto put = [&](uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); };
    put(0x36515457); put(1); put(2); put(0x7777); put(uint32_t(p.now - 1)); for (int i = 0; i < 24; ++i) b.push_back(0);
    uint32_t h = 2166136261u; for (uint8_t c : b) { h ^= c; h *= 16777619u; } put(h);
    p.cb->onPacket(b.data(), b.size(), p.now);
    return before >= 170 && p.seenByB().pingMs == before;
}

int main() {
    Flags noReply, noExpiry, noName; noReply.noPingReply = true; noExpiry.noPingExpiry = true; noName.noNameSend = true;
    bool a = basic(), b = lossy(), c = expiry(), d = followsDelay(), e = forged();
    std::printf("%s  ping: names arrive and the round trip is twice the one-way delay (100 ms)\n", a ? "PASS" : "FAIL");
    std::printf("%s  ping: 35%% loss still gives a name and a ping\n", b ? "PASS" : "FAIL");
    std::printf("%s  ping: unknown again when the peer goes quiet\n", c ? "PASS" : "FAIL");
    std::printf("%s  ping: follows a 400 ms delay\n", d ? "PASS" : "FAIL");
    std::printf("%s  ping: a pong that is not for the ping in flight is ignored\n", e ? "PASS" : "FAIL");
    bool f = !basic(noReply), g = !expiry(noExpiry), h = !basic(noName);
    std::printf("%s  twin no-pong-reply caught\n", f ? "PASS" : "FAIL");
    std::printf("%s  twin no-expiry caught\n", g ? "PASS" : "FAIL");
    std::printf("%s  twin no-names caught\n", h ? "PASS" : "FAIL");
    bool ok = a && b && c && d && e && f && g && h; std::printf("%s\n", ok ? "ALL OK" : "FAILED"); return ok ? 0 : 1;
}
