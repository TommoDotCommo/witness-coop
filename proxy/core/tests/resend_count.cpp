// A startup burst larger than one resend round (32 events) on a perfect link must not be counted as resends:
// events that the per-round budget skipped were never sent, so they cannot be "re-sent". Real resends (a lost
// first send) must still be counted, and they must stop once everything is acknowledged.
#include "../sync_core.h"
#include <cstdio>
#include <deque>
#include <map>

constexpr unsigned kPanels = 248;

struct BurstWorld : witness::World {
    std::map<unsigned, bool> solved;
    BurstWorld() { for (unsigned i = 1; i <= kPanels; ++i) solved[i] = false; }
    bool cameraValid() override { return true; }
    bool scan(std::vector<unsigned>& p, std::vector<unsigned>&, uint64_t* t) override { for (auto& kv : solved) p.push_back(kv.first); *t = 1; return true; }
    bool entityExists(unsigned id) override { return solved.count(id); }
    bool isPanel(unsigned id) override { return solved.count(id); }
    bool isDoor(unsigned) override { return false; }
    bool panelSolved(unsigned id) override { return solved.count(id) && solved[id]; }
    bool doorOpen(unsigned) override { return false; }
    void applyPanelSolved(unsigned id) override { solved[id] = true; }
    bool applyDoorOpen(unsigned) override { return true; }
    bool pose(float[3], float[2]) override { return false; }
};

struct Result { bool ok; unsigned resentAfterBurst, resentFinal, acked, appliedOnB; };

Result run(witness::Flags flags, bool loseFirstSends) {
    BurstWorld a, b; uint64_t now = 0;
    witness::SyncCore* ca = nullptr; witness::SyncCore* cb = nullptr;
    witness::Hooks ha, hb;
    struct Pkt { uint64_t at; bool toB; witness::Bytes d; };
    std::deque<Pkt> link;      // 20 ms one way, like a real network: a packet is never delivered inside the call that sent it
    ha.send = [&](const witness::Bytes& d) { link.push_back({now + 20, true, d}); };
    hb.send = [&](const witness::Bytes& d) { link.push_back({now + 20, false, d}); };
    auto deliver = [&] { while (!link.empty() && link.front().at <= now) { Pkt p = std::move(link.front()); link.pop_front(); (p.toB ? cb : ca)->onPacket(p.d.data(), p.d.size(), now); } };
    witness::SyncCore sa(1, 1, &a, ha, flags), sb(2, 1, &b, hb);
    if (loseFirstSends) { witness::Flags f = flags; f.dropFirstEvent = true; new (&sa) witness::SyncCore(1, 1, &a, ha, f); }
    ca = &sa; cb = &sb;
    for (now = 0; now <= 30000; now += 100) { sa.pollState(now); sb.pollState(now); }
    if (!sa.ready() || !sb.ready()) return {false, 0, 0, 0, 0};
    for (auto& kv : a.solved) kv.second = true;      // the burst: every panel changes at once
    sa.pollState(now);
    unsigned atBurstEnd = 0;
    uint64_t end = now + 10000;
    for (uint64_t t = now; t <= end; t += 5) {
        now = t; deliver(); sa.tick(now); sb.tick(now); sb.applyRemote(now);
        if (sa.unacked() == 0 && !atBurstEnd) atBurstEnd = sa.stats().resent + 1;      // +1: zero is a valid value
    }
    unsigned after = atBurstEnd ? atBurstEnd - 1 : 0;
    for (uint64_t t = end; t <= end + 3000; t += 5) { now = t; deliver(); sa.tick(now); sb.tick(now); sb.applyRemote(now); }
    return {atBurstEnd != 0, after, sa.stats().resent, sa.stats().acked, sb.stats().appliedPanels};
}

int main() {
    bool all = true;
    Result r = run({}, false);
    bool clean = r.ok && r.appliedOnB == kPanels && r.acked == kPanels && r.resentAfterBurst == 0 && r.resentFinal == 0;
    std::printf("%s  perfect link, %u-event burst: acked %u applied %u resent %u (must be 0)\n", clean ? "PASS" : "FAIL", kPanels, r.acked, r.appliedOnB, r.resentFinal); all &= clean;
    witness::Flags twin; twin.countSkippedResends = true;
    Result t = run(twin, false);
    bool caught = t.resentFinal > 0;
    std::printf("%s  count-skipped-resends twin is caught (resent %u)\n", caught ? "PASS" : "FAIL", t.resentFinal); all &= caught;
    Result l = run({}, true);
    bool lost = l.ok && l.appliedOnB == kPanels && l.resentAfterBurst >= kPanels && l.resentFinal == l.resentAfterBurst;
    std::printf("%s  every first send lost: resent %u >= %u, all applied, resends stop once acked (final %u)\n", lost ? "PASS" : "FAIL", l.resentAfterBurst, kPanels, l.resentFinal); all &= lost;
    return all ? 0 : 1;
}
