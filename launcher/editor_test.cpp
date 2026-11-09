// Game-free test of the puzzle editor's logic (editor_core.h): clicks land where each tool works, the tools change the puzzle as the editor says,
// a drawn line follows the grid and is checked by the rules. Mutants (Makefile editor-test): a start is not mirrored, a drawn line crosses gaps.
#include <cstdio>
#include "editor_core.h"

static int fails = 0;
#define CHECK(c, what) do { if (!(c)) { fails++; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, what); } } while (0)

int main() {
    wpz::Puzzle p = wpz::NewPuzzle(4, 4);
    ped::Geometry g = ped::Geometry::Fit(p, 0, 0, 400, 400, 40);
    CHECK(g.step == 40 && g.ox == 40 && g.oy == 40, "a 4x4 puzzle fills 320 pixels of a 400 pixel canvas");
    wpz::Pos a;
    CHECK(ped::Pick(g, p, ped::kSquare, g.px(3) + 5, g.py(5) - 5, &a) && a.x == 3 && a.y == 5, "a click inside a cell picks the cell for a symbol");
    CHECK(ped::Pick(g, p, ped::kStart, g.px(0) + 25, g.py(1) + 9, &a) && a.x == 0 && a.y == 2, "a start goes to the nearest corner");
    CHECK(ped::Pick(g, p, ped::kGap, g.px(2) + 30, g.py(2), &a) && a.x == 3 && a.y == 2, "a gap goes to the nearest edge");
    CHECK(!ped::Pick(g, p, ped::kDot, 5, 5, &a), "a click far outside the grid picks nothing");

    ped::Brush b; b.tool = ped::kSquare; b.colour = 2;
    CHECK(ped::Apply(p, b, {1, 1}) && p.at(1, 1).kind == wpz::kSquare && p.at(1, 1).colour == 2, "a white square");
    CHECK(ped::Apply(p, b, {1, 1}) && p.at(1, 1).kind == wpz::kEmpty, "the same square again takes it away");
    b.tool = ped::kTriangle; b.colour = 10;
    ped::Apply(p, b, {3, 3}); ped::Apply(p, b, {3, 3});
    CHECK(p.at(3, 3).kind == wpz::kTriangle && p.at(3, 3).count == 2, "a triangle again counts two");
    ped::Apply(p, b, {3, 3}); ped::Apply(p, b, {3, 3});
    CHECK(p.at(3, 3).kind == wpz::kEmpty, "after three it is gone");
    b.tool = ped::kPoly; b.shape = 0x13; b.rotate = true; b.negative = false; b.colour = 8;
    CHECK(ped::Apply(p, b, {5, 5}) && p.at(5, 5).kind == wpz::kPoly && p.at(5, 5).shape == (0x13 | wpz::kRotBit), "a polyomino that may turn");
    b.negative = true; ped::Apply(p, b, {5, 5});
    CHECK(p.at(5, 5).kind == wpz::kYlop, "a negative one replaces it");
    CHECK(!ped::Apply(p, b, {2, 2}), "a symbol cannot go on a line");
    b.tool = ped::kEnd;
    CHECK(p.at(8, 0).end == wpz::kEndRight, "a new puzzle ends at the top right, pointing right");
    ped::Apply(p, b, {8, 0});
    CHECK(p.at(8, 0).end == wpz::kEndTop || p.at(8, 0).end == wpz::kEndNone, "an end again points the next way out, or goes");
    b.tool = ped::kGap; CHECK(!ped::Apply(p, b, {2, 2}), "a gap cannot go on a corner");
    b.tool = ped::kClear; p.at(4, 4).dot = wpz::kDotBlack; ped::Apply(p, b, {4, 4});
    CHECK(p.at(4, 4).dot == wpz::kDotNone, "clear takes the dot away");

    // symmetry: starts and ends are mirrored
    wpz::Puzzle s = wpz::NewPuzzle(3, 3); s.sym = wpz::kSymX;
    ped::MirrorStartsAndEnds(s);
    CHECK(wpz::Problems(s).empty(), "mirroring the starts and ends makes a symmetry puzzle whole");
    b.tool = ped::kStart; ped::Apply(s, b, {2, 6});
    CHECK(s.at(2, 6).start && s.at(4, 6).start, "a start on a symmetry puzzle is mirrored");

    // trying a puzzle: a line drawn along the grid
    wpz::Puzzle t = wpz::NewPuzzle(2, 2);
    ped::Trace tr;
    CHECK(!tr.begin(t, {2, 2}) && tr.begin(t, {0, 4}), "a line begins only at a start");
    tr.pull(t, {0, 0}); tr.pull(t, {4, 0});
    CHECK(tr.atEnd(t) && tr.path.size() == 9, "pulled up and across, the line reaches the end");
    CHECK(wpz::Check(t, tr.path).ok, "the empty puzzle is solved by it");
    tr.pull(t, {0, 0});
    CHECK(tr.path.back().x == 0 && tr.path.back().y == 0 && tr.path.size() == 5, "pulled back, it is taken back");
    wpz::Puzzle gap = wpz::NewPuzzle(2, 2); gap.at(0, 3).gap = wpz::kGapBreak;
    ped::Trace tg; tg.begin(gap, {0, 4}); tg.pull(gap, {0, 0});
    CHECK(tg.path.size() == 1, "a line does not cross a gap");
    wpz::Puzzle sq = wpz::NewPuzzle(2, 2); sq.at(1, 1) = wpz::Cell{wpz::kSquare, 1}; sq.at(3, 3) = wpz::Cell{wpz::kSquare, 2};
    ped::Trace ts; ts.begin(sq, {0, 4}); ts.pull(sq, {0, 0}); ts.pull(sq, {4, 0});
    CHECK(ts.atEnd(sq) && !wpz::Check(sq, ts.path).ok, "a line that leaves black and white squares together is refused");
    wpz::Solutions sol = wpz::Solve(sq, 100, 0);
    CHECK(ped::CountText(sol, 100).find("solution") != std::string::npos && !sol.lines.empty(), "the editor's count");

    if (fails) { std::printf("editor-test: %d FAILED\n", fails); return 1; }
    std::printf("editor-test: ok\n"); return 0;
}
