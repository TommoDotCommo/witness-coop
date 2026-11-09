// Game-free test of native_line.h: who draws another player's finished line (the game or the overlay). Mutants: TRUST (the game trusted to
// draw a line written into a panel in view) and NEVER_HANDED (the overlay keeps drawing after the game's picture was rebuilt).
#include "../native_line.h"
#include <cstdio>

static int fails = 0;
static void check(bool ok, const char* what) { std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; }

int main() {
    wline::NativeLines n;
    check(!n.GameDraws(0x293, 0x5000), "no line written: the game draws none of it");
    n.Written(0x293, 0x5000);      // the watcher stood at the panel: its picture existed when the line was written
    check(!n.GameDraws(0x293, 0x5000), "written into a panel in view: the overlay draws it (the game's picture has no line)");
    check(!n.GameDraws(0x293, 0x5000), "  and keeps drawing it while that picture lasts");
    check(n.GameDraws(0x293, 0), "the picture is freed (out of range): the game's next one has the line");
    check(n.GameDraws(0x293, 0x5000), "  even when the new picture is at the old address");
    n.Written(0x64, 0x7000);
    check(n.GameDraws(0x64, 0x7100), "another picture of the panel: the game draws the line");
    n.Written(0x182, 0);      // nobody looked at the panel: the picture is built later, from the trace
    check(n.GameDraws(0x182, 0x8000), "written before the panel came into view: the game draws it");
    n.Dropped(0x182);
    check(!n.GameDraws(0x182, 0x8000) && !n.Written(0x182), "the player draws over it: no remote line is left");
    n.Written(0x293, 0x9000);
    check(!n.GameDraws(0x293, 0x9000), "a second remote solve of the same panel in view: the overlay draws it again");
    std::printf(fails ? "native-line: %d failed\n" : "native-line: all passed\n", fails);
    return fails ? 1 : 0;
}
