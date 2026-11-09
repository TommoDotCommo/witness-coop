// The overlay sentence for a peer on another mod version (version_notice.h), game-free.
// Mutants (build flags): WN_MUT_SILENT (the packet is dropped without a word), WN_MUT_REPEAT (said again for every packet), WN_MUT_ANY_MAGIC (anything that is not this mod's is called a version mismatch).
#include "../version_notice.h"
#include <cstdio>
#include <vector>
static int fails = 0, checks = 0;
static void check(const char* what, bool ok) { ++checks; if (!ok) { ++fails; printf("FAILED: %s\n", what); } }
static std::vector<uint8_t> pkt(const char* magic, unsigned from, size_t len = 40) { std::vector<uint8_t> b(len, 0); memcpy(b.data(), magic, 4); for (int i = 0; i < 4; ++i) b[4 + i] = uint8_t(from >> (8 * i)); return b; }
int main() {
    using wpeers::VersionNotice;
    VersionNotice vn; auto say = [&](const char* m, unsigned from, const char* theirs = "") { auto p = pkt(m, from); return vn.Observe(p.data(), p.size(), "v0.2.0", theirs); };
    for (const char* ok : {"WTP6", "WTE6", "WTA6", "WTQ6", "WTX6", "WTK6", "WTS6", "WTL6", "WTL7", "WTP7", "WTE7", "WTA7", "WTT1", "WTU1", "WTD2", "WTV4", "WTO5", "WTR1", "WTR4", "WTR6"})
        check((std::string("this build's own magic ") + ok + " raises nothing").c_str(), say(ok, 2).empty());
    std::string s = say("WTP5", 2, "v0.1.0");
    check("an older pose magic from a player whose version the roster names: one sentence naming the player and both versions", s == "Player 2 has a different version (v0.1.0)");
    check("the same player again (any class, any version): nothing more (once)", say("WTP5", 2).empty() && say("WTE5", 2).empty() && say("WTP8", 2).empty());
    std::string u = say("WTE9", 3);
    check("another player on a newer magic whose version is not known: the same sentence without a version", u == "Player 3 has a different version");
    check("a third player gets its own sentence", !say("WTA2", 4).empty());
    VersionNotice w; auto g = pkt("ABCD", 2); check("another protocol's bytes are not a version mismatch", w.Observe(g.data(), g.size(), "v", "").empty());
    auto z = pkt("WTZ6", 2); check("an unknown class letter is not this mod's", w.Observe(z.data(), z.size(), "v", "").empty());
    auto sh = pkt("WTP5", 2, 6); check("a datagram too short to name its sender is ignored", w.Observe(sh.data(), sh.size(), "v", "").empty());
    check("garbage that merely starts with W is ignored", w.Observe(g.data(), 3, "v", "").empty());
    printf("%d checks, %d failed\n", checks, fails); return fails ? 1 : 0;
}
