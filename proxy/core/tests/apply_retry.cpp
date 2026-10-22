// Deterministic receiver regression: transport ACKs must not lose deferred game writes.
#include "../sync_core.h"
#include <cstdio>
#include <map>
#include <string>

struct TestWorld : witness::World {
    struct Entity { bool door = false, present = true, changed = false; };
    std::map<unsigned, Entity> entities{{1, {}}, {2, {true}}, {3, {true}}};
    bool refuse = false;
    unsigned doorCalls = 0;
    bool cameraValid() override { return true; }
    bool scan(std::vector<unsigned>& p, std::vector<unsigned>& d, uint64_t* t) override {
        for (auto& [id, e] : entities) if (e.present) (e.door ? d : p).push_back(id);
        *t = 1; return true;
    }
    bool entityExists(unsigned id) override { return entities.count(id) && entities.at(id).present; }
    bool isPanel(unsigned id) override { return entityExists(id) && !entities.at(id).door; }
    bool isDoor(unsigned id) override { return entityExists(id) && entities.at(id).door; }
    bool panelSolved(unsigned id) override { return isPanel(id) && entities.at(id).changed; }
    bool doorOpen(unsigned id) override { return isDoor(id) && entities.at(id).changed; }
    void applyPanelSolved(unsigned id) override { entities.at(id).changed = true; }
    bool applyDoorOpen(unsigned id) override { ++doorCalls; if (refuse && id == 2) return false; entities.at(id).changed = true; return true; }
    bool pose(float[3], float[2]) override { return false; }
};

bool run(const std::string& mode, witness::Flags flags) {
    TestWorld a, b;
    witness::SyncCore* receiver = nullptr;
    witness::SyncCore* sender = nullptr;
    uint64_t now = 0;
    witness::Hooks ha, hb;
    ha.send = [&](const witness::Bytes& d) { receiver->onPacket(d.data(), d.size(), now); };
    hb.send = [&](const witness::Bytes& d) { sender->onPacket(d.data(), d.size(), now); };
    witness::SyncCore ca(1, 1, &a, ha), cb(2, 1, &b, hb, flags);
    sender = &ca; receiver = &cb;
    for (now = 0; now <= 30000; now += 100) { ca.pollState(now); cb.pollState(now); }
    if (!ca.ready() || !cb.ready()) return false;
    unsigned id = mode == "missing panel" ? 1 : 2;
    if (mode == "refused door") b.refuse = true;
    else if (mode == "wrong type") b.entities.at(id).door = false;
    else b.entities.at(id).present = false;
    a.entities.at(id).changed = true;
    a.entities.at(3).changed = true; // a deferred entity must not block unrelated progress
    ca.pollState(now); ca.tick(now);
    if (ca.unacked() || cb.queuedRemote() != 2) return false; // both accepted and ACKed before application
    cb.applyRemote(now);
    bool ok = b.doorOpen(3) && !b.entities.at(id).changed && cb.stats().appliedDoors == 1;
    if (mode == "wrong type") return ok && cb.queuedRemote() == 0;
    ok = ok && cb.queuedRemote() == 1;
    // No new packets arrive. The receiver must retain the acknowledged event itself.
    now += 6000;
    cb.applyRemote(now);
    ok = ok && cb.queuedRemote() == 1;
    b.refuse = false; b.entities.at(id).present = true;
    cb.applyRemote(++now);
    cb.pollState(now); cb.applyRemote(++now);
    auto s = cb.stats();
    ok = ok && b.entities.at(id).changed && cb.queuedRemote() == 0;
    ok = ok && s.appliedPanels == (id == 1 ? 1u : 0u) && s.appliedDoors == (id == 1 ? 1u : 2u);
    return ok && s.emittedPanels == 0 && s.emittedDoors == 0;
}

int main() {
    bool all = true;
    for (const char* mode : {"missing panel", "missing door", "refused door", "wrong type"}) {
        bool ok = run(mode, {});
        std::printf("%s  %s\n", ok ? "PASS" : "FAIL", mode); all &= ok;
        if (std::string(mode) != "wrong type") {
            witness::Flags twin; twin.noApplyRetry = true;
            bool caught = !run(mode, twin);
            std::printf("%s  no-apply-retry twin: %s\n", caught ? "PASS" : "FAIL", mode); all &= caught;
        }
    }
    return all ? 0 : 1;
}
