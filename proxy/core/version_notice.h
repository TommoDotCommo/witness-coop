#pragma once
// A packet from a configured peer whose magic belongs to this mod but to another version's protocol ("WTP5" where this build speaks "WTP6"), game-free (tests/version_notice.cpp).
// The sync core ignores such packets (older peers are ignored by design), and that used to be silent: the two players saw each other not at all and no message said why. Now
// the first one from each player is turned into one sentence for the overlay and the log, never repeated for that player.
// A magic is "WT" + a class letter + a digit; the digit is that class's protocol version. A class this build does not know, or other bytes, are not this mod's: no notice.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

namespace wpeers {
class VersionNotice {
public:
    // The digits this build speaks per class (the group form of pose, events and acks uses 7; lines have a fine form 7).
    static const char* Digits(char cls) {
        switch (cls) {
            case 'P': case 'E': case 'A': case 'L': return "67";
            case 'Q': case 'X': case 'K': case 'S': return "6";
            case 'T': case 'U': return "1";
            case 'D': return "2";
            case 'V': return "4";
            case 'O': return "5";
            case 'R': return "14";
            default: return nullptr;
        }
    }
    // `d`/`n`: a datagram already accepted as coming from that peer's own endpoint. Returns the sentence the first time this player's packets are of another version, else "".
    // `mine`: this build's version; `theirs`: the version the launcher's roster names for that player ("" when not known).
    std::string Observe(const uint8_t* d, size_t n, const std::string& mine, const std::string& theirs) {
#ifdef WN_MUT_SILENT
        return "";      // mutant: the packet is dropped without a word, as before
#endif
        if (n < 8 || d[0] != 'W' || d[1] != 'T') return "";
        const char* ok = Digits((char)d[2]);
#ifndef WN_MUT_ANY_MAGIC
        if (!ok) return "";
#endif
        char dg = (char)d[3];
        if (ok && dg && strchr(ok, dg)) return "";
        unsigned sender = unsigned(d[4]) | unsigned(d[5]) << 8 | unsigned(d[6]) << 16 | unsigned(d[7]) << 24;
#ifndef WN_MUT_REPEAT
        if (!told_.insert(sender).second) return "";
#endif
        char b[256];
        if (!theirs.empty()) snprintf(b, sizeof b, "Player %u is on %s, you are on %s: update both", sender, theirs.c_str(), mine.c_str());
        else snprintf(b, sizeof b, "Player %u runs another version of the mod (its packets are %c%c%c%c, yours are not), you are on %s: update both", sender, d[0], d[1], d[2], dg >= 32 && dg < 127 ? dg : '?', mine.c_str());
        return b;
    }
private:
    std::set<unsigned> told_;
};
}  // namespace wpeers
