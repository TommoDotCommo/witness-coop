#pragma once
// Who draws another player's finished line on this game's panel: the game, from the panel's own trace, or the overlay (game-free, tests/native_line.cpp).
// A remote solve writes the solver's line into the panel's trace (state.cpp WriteLine). The game draws a panel's line from a picture it builds when
// the panel comes into view (the render object at +0x4A8; it is freed when the panel goes out of range and built again when it comes back), and that
// picture is not rebuilt when the trace is written from outside. So a player who watched the panel while the other solved it saw the panel solved
// with no line (owner's playtest, 2026-11-10; trying to draw on it then reset the panel). A player who came later saw the line: the picture was
// built after the write. The rule: when the line is written into a panel whose picture already exists, the overlay keeps drawing the line until
// the game's picture of that panel changes (freed, or another one); then the game draws it and the overlay leaves it out (no doubled line).
#include <cstdint>
#include <unordered_map>

namespace wline {
class NativeLines {
public:
    // The line was written into `panel`'s trace while the game's picture of the panel was `picture` (0: none yet, the next one has the line).
    void Written(unsigned panel, uint64_t picture) {
#ifdef WNL_MUT_TRUST
        picture = 0;      // mutant: the game is trusted to draw a line written into a panel already in view (as 2026-11-07 to 11-10)
#endif
        lines_[panel] = picture ? picture : kBuilt;
    }
    // The trace is this player's own again (they drew over it), or the panel was reset: nothing of the remote line is left.
    void Dropped(unsigned panel) { lines_.erase(panel); }
    // Does the game draw the written line now, with its picture of the panel `picture`? (False also when no line was written.)
    bool GameDraws(unsigned panel, uint64_t picture) {
        auto it = lines_.find(panel); if (it == lines_.end()) return false;
        if (it->second == kBuilt) return true;
        if (picture == it->second) return false;      // the picture is the one made before the line: it shows none
#ifndef WNL_MUT_NEVER_HANDED
        it->second = kBuilt;      // freed or built again: from now on the game's picture has the line
#else
        return false;      // mutant: the overlay never hands the line back (doubled once the game draws it)
#endif
        return true;
    }
    bool Written(unsigned panel) const { return lines_.count(panel) != 0; }
private:
    static constexpr uint64_t kBuilt = 1;      // (no picture is at address 1)
    std::unordered_map<unsigned, uint64_t> lines_;
};
}  // namespace wline
