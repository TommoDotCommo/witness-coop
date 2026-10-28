// Protocol migration contract: inspect actual output and deliver it to the real decoder.
#include "../sync_core.h"
#include <cstdio>
using namespace witness;
struct W : World {
    bool cameraValid() override { return true; }
    bool scan(std::vector<unsigned>& p, std::vector<unsigned>& d, uint64_t* t) override { p.clear(); d.clear(); *t = 1; return true; }
    bool entityExists(unsigned) override { return false; }
    bool isPanel(unsigned) override { return false; }
    bool isDoor(unsigned) override { return false; }
    bool panelSolved(unsigned) override { return false; }
    bool doorOpen(unsigned) override { return false; }
    void applyPanelSolved(unsigned) override {}
    bool applyDoorOpen(unsigned) override { return false; }
    bool pose(float p[3], float a[2]) override { p[0] = 1; p[1] = 2; p[2] = 3; a[0] = a[1] = 0; return true; }
};
static void checksum(Bytes& b) {
    unsigned h = 2166136261u;
    for (size_t i = 0; i < b.size() - 4; ++i) { h ^= b[i]; h *= 16777619u; }
    for (int i = 0; i < 4; ++i) b[b.size() - 4 + i] = uint8_t(h >> (8 * i));
}
int main() {
    W w; std::vector<Bytes> wire;
    Hooks h; h.send = [&](const Bytes& b) { wire.push_back(b); };
    SyncCore a(1, 1, &w, h), b(2, 1, &w, {});
    Presence p; p.emote = 7; p.emoteSeq = 9; p.typing = p.speaking = true; a.setPresence(p); a.tick(1000);
    bool pose = false, pres = false, ok = true;
    for (const auto& d : wire) {
        ok &= d.size() >= 4 && d[3] == '6';
        if (d[2] == 'P') { pose = true; ok &= d.size() == 40; }
        if (d[2] == 'S') { pres = true; ok &= d.size() == 36; }
        b.onPacket(d.data(), d.size(), 1000);
    }
    auto v = b.remotePoses(1000);
    ok &= pose && pres && v.size() == 1;
    if (v.size() != 1) return 1;
    ok &= v[0].emote == 7 && v[0].emoteSeq == 9 && v[0].typing && v[0].speaking;
    for (auto d : wire) {
        // A correctly checksummed v5 peer with a newer session cannot replace v6 state.
        d[3] = '5'; d[8] = 2; checksum(d); b.onPacket(d.data(), d.size(), 1100);
        if (d[2] == 'S') {
            // Even under the new magic an old model-name payload is rejected by length.
            d[3] = '6'; d[16] = 1; d.insert(d.end() - 4, 24, 'X'); checksum(d);
            b.onPacket(d.data(), d.size(), 1100);
        }
    }
    v = b.remotePoses(1200); ok &= v.size() == 1 && v[0].ageMs == 200 && v[0].emote == 7;
    wire.clear(); h.sendTo = [&](unsigned, const Bytes& d) { wire.push_back(d); };
    SyncCore ga(1, 1, &w, h), gb(2, 1, &w, h);
    ok &= ga.setPeers({2}) && gb.setPeers({1}); ga.tick(1000);
    bool groupPose = false;
    for (const auto& d : wire) if (d[2] == 'P') {
        groupPose = true; ok &= d[3] == '7';
        gb.onPacket(d.data(), d.size(), 1000);
    }
    ok &= groupPose && gb.remotePoses(1000).size() == 1;
    std::printf("protocol v6: compact presence, legacy rejection, distinct group dispatch: %s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
