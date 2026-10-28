// Exercise the actual presence encoder/decoder; malformed records must be atomic.
#define main wire_contract_main
#include "protocol.cpp"
#undef main
#include <cstdlib>
#define REQUIRE(x) do { if (!(x)) { std::printf("appearance FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
    REQUIRE(wire_contract_main() == 0);
    Appearance original, a; auto encoded = EncodeAppearance(original);
    REQUIRE(encoded[0] == 1);
    for (size_t i = 1; i < encoded.size(); ++i) REQUIRE(encoded[i] == 0);
    for (size_t feature = 0; feature < kAppearanceFeatures; ++feature) {
        for (unsigned value = 0; value < 256; ++value) {
            auto b = encoded; b[feature + 1] = uint8_t(value); Appearance got;
            REQUIRE(DecodeAppearance(b.data(), b.size(), &got));
            REQUIRE(got.choice[feature] == std::min(value, 3u));
            REQUIRE(DecodeAppearance(EncodeAppearance(got).data(), kAppearanceBytes, &a) && a == got);
        }
    }
    for (size_t n : {size_t(0),size_t(11),size_t(13)}) {
        a.choice.fill(2); REQUIRE(!DecodeAppearance(encoded.data(), n, &a)); REQUIRE(a.choice[0] == 2);
    }
    for (unsigned version : {0u,2u,255u}) { auto b = encoded; b[0] = version; REQUIRE(!DecodeAppearance(b.data(), b.size(), &a)); }
    for (size_t i = 9; i < 12; ++i) { auto b = encoded; b[i] = 1; REQUIRE(!DecodeAppearance(b.data(), b.size(), &a)); }
    for (const char* name : kAppearanceNames) {
        REQUIRE(SetAppearanceChoice(&a, name, "3"));
        for (const char* invalid : {"", "4", "-1", "256", "1junk", "1 2", " 1", "01"}) REQUIRE(!SetAppearanceChoice(&a, name, invalid));
    }
    REQUIRE(!SetAppearanceChoice(&a, "unknown", "1"));
    a = {};
    for (const char* legacy : {"character=fox", "model=none", "model=outside.glb", "model_by_player=1", "key_character=C", "no_model_select=1"}) {
        REQUIRE(!SetAppearanceConfigLine(&a, legacy)); REQUIRE(a == Appearance{});
    }
    REQUIRE(SetAppearanceConfigLine(&a, "appearance_coat=2\r\n") && a.choice[3] == 2);
    for (const char* bad : {"appearance_unknown=1", "appearance_coat=2junk", "appearance_coat=1 2", "appearance_coat", "appearance_coat=", "appearance_coat=-1"}) {
        auto before = a; REQUIRE(!SetAppearanceConfigLine(&a, bad)); REQUIRE(a == before);
    }
    // Every independent palette has finite, bounded multipliers; defaults preserve
    // the authored atlas exactly, including materials outside the tint regions.
    for (unsigned group = 0; group < 11; ++group) for (unsigned choice = 0; choice < 4; ++choice) {
        a.choice.fill(uint8_t(choice)); float tint[3]; AppearanceTint(a, group, tint);
        for (float t : tint) REQUIRE(std::isfinite(t) && t >= .2f && t <= 2.6f);
        if (!choice || !group || group > 8) for (float t : tint) REQUIRE(t == 1);
        else REQUIRE(tint[0] != 1 || tint[1] != 1 || tint[2] != 1);
    }
    W world; std::vector<Bytes> wire; Hooks h; h.send = [&](const Bytes& b) { wire.push_back(b); };
    SyncCore sender(1, 1, &world, h), receiver(2, 1, &world, {});
    Presence p; p.appearance.choice = {0,1,2,3,0,1,2,3}; sender.setPresence(p); sender.tick(1000);
    Bytes pres;
    for (const auto& b : wire) { receiver.onPacket(b.data(), b.size(), 1000); if (b[2] == 'S') pres = b; }
    REQUIRE(!pres.empty()); auto rem = receiver.remotePoses(1000);
    REQUIRE(rem.size() == 1 && rem[0].appearance == p.appearance);
    witness::RemotePose single; REQUIRE(receiver.remotePose(&single,1000) && single.appearance == p.appearance);
    // Correct checksums, newer sequence, invalid schema: reject the WHOLE presence,
    // without poisoning its sequence. Then accept a valid packet with the same seq.
    for (size_t offset : {size_t(20),size_t(29),size_t(30),size_t(31)}) {
        auto bad = pres; bad[12] = 2; bad[offset] = 255; bad[16] = 8; checksum(bad);
        receiver.onPacket(bad.data(), bad.size(), 1100);
        REQUIRE(receiver.remotePoses(1100)[0].appearance == p.appearance);
        REQUIRE(receiver.remotePoses(1100)[0].emote == 0);
    }
    auto newer = pres; newer[12] = 2; for (int i = 21; i < 29; ++i) newer[i] = 255; checksum(newer);
    receiver.onPacket(newer.data(), newer.size(), 1200);
    a.choice.fill(3); REQUIRE(receiver.remotePoses(1200)[0].appearance == a);
    receiver.onPacket(pres.data(), pres.size(), 1300); // stale update cannot undo it
    REQUIRE(receiver.remotePoses(1300)[0].appearance == a);
    // A corrupt packet never changes any descriptor byte.
    auto corrupt = newer; corrupt[12] = 3; corrupt[21] = 0;
    receiver.onPacket(corrupt.data(), corrupt.size(), 1400);
    REQUIRE(receiver.remotePoses(1400)[0].appearance == a);
    // Late join/lost first update: the repeated heartbeat restores appearance.
    SyncCore late(2, 1, &world, {}); wire.clear(); sender.tick(3000);
    for (const auto& b : wire) late.onPacket(b.data(), b.size(), 3000);
    REQUIRE(late.remotePoses(3000).size() == 1 && late.remotePoses(3000)[0].appearance == p.appearance);
    // Local API values are sanitized too, before equality/dirty tracking.
    p.appearance.choice.fill(255); sender.setPresence(p); wire.clear(); sender.tick(3100);
    for (const auto& b : wire) receiver.onPacket(b.data(), b.size(), 3100);
    REQUIRE(receiver.remotePoses(3100)[0].appearance == a);
    // Two senders with independent palettes in a fixed group, including updates
    // while other presence fields are identical.
    std::vector<std::pair<unsigned, Bytes>> groupWire;
    Hooks gh; gh.sendTo = [&](unsigned to, const Bytes& b) { groupWire.push_back({to,b}); };
    SyncCore one(1,1,&world,gh), two(2,1,&world,gh), three(3,1,&world,gh);
    REQUIRE(one.setPeers({2,3}) && two.setPeers({1,3}) && three.setPeers({1,2}));
    Presence first, second; first.appearance.choice.fill(1); second.appearance.choice.fill(2);
    one.setPresence(first); two.setPresence(second); one.tick(1000); two.tick(1000);
    for (const auto& packet : groupWire) if (packet.first == 3) three.onPacket(packet.second.data(),packet.second.size(),1000);
    rem = three.remotePoses(1000); REQUIRE(rem.size()==2);
    REQUIRE(rem[0].id==1 && rem[0].appearance==first.appearance && rem[1].id==2 && rem[1].appearance==second.appearance);
    first.appearance.choice[0]=3; groupWire.clear(); one.setPresence(first); one.tick(1010);
    bool immediate = false;
    for (const auto& packet : groupWire) if (packet.first == 3 && packet.second[2]=='S') {
        immediate = true; three.onPacket(packet.second.data(),packet.second.size(),1010);
    }
    rem=three.remotePoses(1010); REQUIRE(immediate && rem[0].appearance==first.appearance && rem[1].appearance==second.appearance);
    std::puts("appearance: round trip, all byte clamps, strict text, atomic validation, palettes, stale/corrupt rejection and heartbeat passed");
}
