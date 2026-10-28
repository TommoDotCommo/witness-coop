// Shared drawn lines: the nodes (and their places on the panel) of the line one player draws reach the others, newest wins, lost packets are
// repaired by the periodic resend, a restarted peer catches up, and a reset panel loses its line on the other side.
#include "../sync_core.h"
#include <cstdio>
#include <memory>

using namespace witness;

struct LineWorld : World {
    std::map<unsigned, LineOut> lines;
    bool cameraValid() override { return true; }
    bool scan(std::vector<unsigned>& p, std::vector<unsigned>&, uint64_t* t) override { p = {5, 7}; *t = 1; return true; }
    bool entityExists(unsigned id) override { return id == 5 || id == 7; }
    bool isPanel(unsigned id) override { return id == 5 || id == 7; }
    bool isDoor(unsigned) override { return false; }
    bool panelSolved(unsigned) override { return false; }
    bool doorOpen(unsigned) override { return false; }
    void applyPanelSolved(unsigned) override {}
    bool applyDoorOpen(unsigned) override { return false; }
    bool scanLines(std::vector<LineOut>& out) override { for (auto& kv : lines) out.push_back(kv.second); return true; }
    bool pose(float p[3], float a[2]) override { p[0] = p[1] = p[2] = 1; a[0] = a[1] = 0; return true; }
};

static bool fineLines = false;
static LineOut makeLine(unsigned panel, unsigned nodes, bool final) {
    LineOut l; l.panel = panel; l.final = final; l.fine = fineLines;
    for (unsigned i = 0; i < nodes; ++i) { LinePoint p; p.node = uint16_t(i * 3 + 1); p.d[0] = int16_t(100 * i - 400); p.d[1] = int16_t(-50 * i); p.d[2] = int16_t(7); l.pts.push_back(p); }
    return l;
}

struct Pair {
    LineWorld a, b; std::unique_ptr<SyncCore> ca, cb; std::vector<Bytes> toA, toB; uint64_t now = 0; unsigned lossEvery = 0, linePackets = 0, dropNext = 0; std::vector<Bytes> held; bool hold = false;
    Pair(Flags fa = {}, Flags fb = {}) { Hooks h; h.send = [&](const Bytes& d) { if (fineLines && isLine(d) && d[3] == '6') return; if (isLine(d) && dropNext) { --dropNext; return; } if (isLine(d) && lossEvery && (++linePackets % lossEvery) != 0) return; if (isLine(d) && hold) { held.push_back(d); return; } toB.push_back(d); }; ca = std::make_unique<SyncCore>(1, 1, &a, h, fa); startB(1, fb); }
    static bool isLine(const Bytes& d) { return d.size() > 4 && d[0] == 'W' && d[1] == 'T' && d[2] == 'L'; }
    void startB(unsigned session, Flags f = {}) { b = LineWorld(); Hooks h; h.send = [&](const Bytes& d) { toA.push_back(d); }; cb = std::make_unique<SyncCore>(2, session, &b, h, f); }
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
    const RemoteLine* get(unsigned panel) { static std::vector<RemoteLine> keep; keep = cb->remoteLines(now); for (auto& r : keep) if (r.panel == panel && r.from == 1) return &r; return nullptr; }
};

static bool same(const RemoteLine* r, const LineOut& l) { return r && r->fine == l.fine && r->final == l.final && r->pts == l.pts; }

// A draws in three steps over a link that loses 2 of every 3 line packets; B ends with the finished line.
static bool lossy(Flags fa = {}) {
    Pair p(fa); p.advance(500); p.lossEvery = 3;
    p.a.lines[5] = makeLine(5, 4, false); p.advance(300);
    p.a.lines[5] = makeLine(5, 9, false); p.advance(300);
    p.lossEvery = 0; p.dropNext = 1; LineOut fin = makeLine(5, 14, true); p.a.lines[5] = fin; p.advance(6000);
    return same(p.get(5), fin);
}
// A's finished line is overtaken: a delayed older packet arrives after the newer one and must not replace it.
static bool stale(Flags fb = {}) {
    Pair p({}, fb); p.advance(500);
    p.a.lines[5] = makeLine(5, 4, false); p.hold = true; p.advance(300);      // this version is held back in the network
    p.hold = false; LineOut fin = makeLine(5, 12, true); p.a.lines[5] = fin; p.advance(600);
    for (auto& d : p.held) p.toB.push_back(d);                                   // ... and arrives late
    p.advance(50);
    return same(p.get(5), fin);
}
// B restarts after A finished drawing: the periodic resend brings the line back.
static bool restart(Flags fa = {}) {
    Pair p(fa); p.advance(500);
    LineOut fin = makeLine(5, 11, true); p.a.lines[5] = fin; p.advance(1500);
    if (!same(p.get(5), fin)) return false;
    p.startB(2); p.advance(5000);
    return same(p.get(5), fin);
}
// A line that was sent while B was away is only delivered by the resend; with it off the first (lost) packet is never repaired.
static bool firstLost(Flags fa = {}) {
    Pair p(fa); p.advance(500); p.lossEvery = 2; p.linePackets = 0;       // the first line packet is dropped, the second would pass
    LineOut fin = makeLine(7, 6, true); p.a.lines[7] = fin; p.advance(5000);
    return same(p.get(7), fin);
}
// The panel is reset on A: B drops the line. Another panel's line stays.
static bool reset() {
    Pair p; p.advance(500);
    LineOut x = makeLine(5, 6, true), y = makeLine(7, 5, false); p.a.lines[5] = x; p.a.lines[7] = y; p.advance(3000);
    if (!same(p.get(5), x) || !same(p.get(7), y)) return false;
    p.a.lines.erase(5); p.advance(3000);
    return !p.get(5) && same(p.get(7), y);
}
// Hostile packets: a wrong panel type, a count that does not match the length, a bad checksum: nothing is stored.
static bool hostile() {
    Pair p; p.advance(500);
    Bytes good; { LineOut l = makeLine(5, 3, true); p.a.lines[5] = l; p.advance(400); }
    p.cb = nullptr; p.startB(1); p.advance(100);
    // craft: panel 9 (not a panel), n = 2 but length for 1 point, flipped checksum
    auto craft = [](uint32_t panel, uint32_t cnt, size_t points, bool badSum) { Bytes b; auto put = [&](uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); };
        put(0x364C5457); put(1); put(1); put(panel); put(1); put(1); put(cnt); for (size_t i = 0; i < points * 8; ++i) b.push_back(1);
        uint32_t h = 2166136261u; for (uint8_t c : b) { h ^= c; h *= 16777619u; } if (badSum) h ^= 1; put(h); return b; };
    for (auto& d : {craft(9, 1, 1, false), craft(5, 2, 1, false), craft(5, 1, 1, true), craft(5, 5000, 1, false)}) p.cb->onPacket(d.data(), d.size(), p.now);
    return p.cb->remoteLines(p.now).empty();
}

int main() {
    Flags noResend, noSeq; noResend.noLineResend = true; noSeq.noLineSeqCheck = true;
    bool a = lossy(), b = stale(), c = restart(), d = firstLost(), e = reset(), f = hostile();
    std::printf("%s  lines: three steps over a link losing 2 of 3 packets end as the finished line\n", a ? "PASS" : "FAIL");
    std::printf("%s  lines: a delayed older line never replaces a newer one\n", b ? "PASS" : "FAIL");
    std::printf("%s  lines: a restarted peer gets the finished line back\n", c ? "PASS" : "FAIL");
    std::printf("%s  lines: a lost first packet is repaired by the resend\n", d ? "PASS" : "FAIL");
    std::printf("%s  lines: a reset panel drops its line, another panel keeps its\n", e ? "PASS" : "FAIL");
    std::printf("%s  lines: wrong panel, wrong length and bad checksum are refused\n", f ? "PASS" : "FAIL");
    bool g = !lossy(noResend), h = !stale(noSeq), i = !restart(noResend), j = !firstLost(noResend);
    std::printf("%s  twin no-resend caught by loss\n", g ? "PASS" : "FAIL");
    std::printf("%s  twin no-sequence-check caught by the stale packet\n", h ? "PASS" : "FAIL");
    std::printf("%s  twin no-resend caught by the restart\n", i ? "PASS" : "FAIL");
    std::printf("%s  twin no-resend caught by the lost first packet\n", j ? "PASS" : "FAIL");
    bool ok = a && b && c && d && e && f && g && h && i && j; std::printf("%s\n", ok ? "ALL OK" : "FAILED"); return ok ? 0 : 1;
}
