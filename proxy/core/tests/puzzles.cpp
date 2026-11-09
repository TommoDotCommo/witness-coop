// Each player tells the others what its puzzles are (the built-in randomizer's fingerprint, "WTR1"), so players whose randomizer made other
// puzzles than theirs can be told. A says fingerprint X ready; B says Y working then ready; each must see the other's, newest state.
// Mutant WR_MUT_PUZZLES_ZERO (the fingerprint is never sent) must fail.
#include "../sync_core.h"
#include <cstdio>

struct W : witness::World {
    bool cameraValid() override { return true; }
    bool scan(std::vector<unsigned>& p, std::vector<unsigned>& d, uint64_t* t) override { p = {1}; d = {2}; *t = 1; return true; }
    bool entityExists(unsigned id) override { return id == 1 || id == 2; }
    bool isPanel(unsigned id) override { return id == 1; }
    bool isDoor(unsigned id) override { return id == 2; }
    bool panelSolved(unsigned) override { return false; }
    bool doorOpen(unsigned) override { return false; }
    void applyPanelSolved(unsigned) override {}
    bool applyDoorOpen(unsigned) override { return true; }
    bool pose(float p[3], float a[2]) override { p[0] = p[1] = p[2] = 1; a[0] = a[1] = 0; return true; }
};

int main() {
    W a, b; std::vector<witness::Bytes> toA, toB;
    witness::Hooks ha, hb;
    ha.send = [&](const witness::Bytes& d) { toB.push_back(d); };
    hb.send = [&](const witness::Bytes& d) { toA.push_back(d); };
    witness::SyncCore ca(1, 1, &a, ha), cb(2, 1, &b, hb);
    uint64_t now = 0;
    auto advance = [&](uint64_t ms) {
        for (uint64_t end = now + ms; now < end;) {
            now += 50; ca.pollState(now); cb.pollState(now); ca.tick(now); cb.tick(now);
            auto qa = std::move(toA), qb = std::move(toB); toA.clear(); toB.clear();
            for (auto& d : qa) ca.onPacket(d.data(), d.size(), now);
            for (auto& d : qb) cb.onPacket(d.data(), d.size(), now);
        }
    };
    const uint64_t X = 0x1122334455667788ull, Y = 0x99AABBCCDDEEFF00ull;
    ca.setPuzzles(X, witness::PUZ_READY); cb.setPuzzles(Y, witness::PUZ_WORKING);
    advance(5000);
    int fails = 0;
    auto pa = ca.peerPuzzles(), pb = cb.peerPuzzles();
    if (pa.size() != 1 || pa[0].fingerprint != Y || pa[0].state != witness::PUZ_WORKING) { std::printf("A does not see B's puzzles\n"); fails++; }
    if (pb.size() != 1 || pb[0].fingerprint != X || pb[0].state != witness::PUZ_READY) { std::printf("B does not see A's puzzles\n"); fails++; }
    cb.setPuzzles(X, witness::PUZ_READY); advance(500);
    pa = ca.peerPuzzles();
    if (pa.size() != 1 || pa[0].fingerprint != X || pa[0].state != witness::PUZ_READY) { std::printf("A does not see B's change at once\n"); fails++; }
    std::printf(fails ? "puzzles: FAILED\n" : "puzzles: ok\n");
    return fails ? 1 : 0;
}
