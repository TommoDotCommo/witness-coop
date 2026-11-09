// T0 test of common/wpuzzle.h and wpuzzle_json.h (game-free): the rules and the solver against jbzdarkid's own engine on the fixture
// (tests/wpuzzle_fixture.txt, tools/wpuzzle_fixture.py), a drawn line checked, the set text and witnesspuzzles.com's JSON both ways.
// Mutants (Makefile wpuzzle-test): a rule switched off, the solver's early region check blind to eliminators, the set text without rotation.
#include "wpuzzle_json.h"
#include <fstream>
#include <iostream>
#include <sstream>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; if (fails < 20) { printf("FAIL %s:%d: %s  ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static std::string Positions(const std::vector<wpz::Pos>& l) {
    std::string s; for (size_t i = 0; i < l.size(); i++) s += (i ? " " : "") + std::to_string(l[i].x) + "," + std::to_string(l[i].y);
    return s;
}
static std::vector<wpz::Pos> ParsePositions(const std::string& s) {
    std::vector<wpz::Pos> out; std::istringstream in(s); std::string t;
    while (in >> t) { size_t c = t.find(','); out.push_back({atoi(t.substr(0, c).c_str()), atoi(t.substr(c + 1).c_str())}); }
    return out;
}

static void Fixture(const char* path) {
    std::ifstream f(path); std::string line; int n = 0, solvable = 0, same = 0;
    CHECK(f.good(), "no fixture %s", path);
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t a = line.find('\t'), b = line.find('\t', a + 1);
        int want = atoi(line.substr(0, a).c_str()); std::string first = line.substr(a + 1, b - a - 1), json = line.substr(b + 1);
        wpz::Imported im = wpz::ImportJson(json); n++;
        CHECK(im.ok && im.problems.empty(), "fixture %d does not import cleanly: %s", n, im.ok && !im.problems.empty() ? im.problems[0].c_str() : im.error.c_str());
        wpz::Solutions s = wpz::Solve(im.puzzle, 200, 0);
        bool ok = (int)s.lines.size() == want && (want == 0 || Positions(s.lines[0]) == first);
        CHECK(ok, "fixture %d: %zu solutions (his %d), first %s (his %s)", n, s.lines.size(), want, s.lines.empty() ? "-" : Positions(s.lines[0]).c_str(), first.c_str());
        same += ok;
        if (want > 0) {
            solvable++;
            auto his = ParsePositions(first);
            wpz::Verdict v = wpz::Check(im.puzzle, his);
            CHECK(v.ok, "fixture %d: his solution is not accepted (%s)", n, v.why.c_str());
            auto shorter = his; shorter.pop_back();
            CHECK(!wpz::Check(im.puzzle, shorter).ok, "fixture %d: a line that stops short of the end is accepted", n);
        }
        // both texts keep the puzzle
        wpz::Set set{"fixture", {im.puzzle}}; wpz::Set back;
        std::string err; CHECK(wpz::Parse(wpz::Print(set), &back, &err) && back == set, "fixture %d: the set text does not keep it (%s)", n, err.c_str());
        wpz::Imported again = wpz::ImportJson(wpz::ExportJson(im.puzzle));
        CHECK(again.ok && again.puzzle == im.puzzle, "fixture %d: witnesspuzzles.com's JSON does not keep it", n);
    }
    printf("fixture: %d puzzles (%d solvable), %d the same as jbzdarkid's engine\n", n, solvable, same);
    CHECK(n >= 500 && solvable >= 300, "the fixture is too small (%d, %d solvable)", n, solvable);
}

static void Basics() {
    wpz::Puzzle p = wpz::NewPuzzle(2, 2, "two");
    CHECK(wpz::Problems(p).empty(), "a new puzzle has problems: %s", wpz::Problems(p)[0].c_str());
    wpz::Solutions s = wpz::Solve(p, 1000, 0);
    CHECK(s.lines.size() == 12 && s.complete, "an empty 2x2 corner to corner has 12 lines, got %zu", s.lines.size());      // (self-avoiding paths on 3x3 nodes)
    // black and white squares split
    p.at(1, 1) = wpz::Cell{wpz::kSquare, 1}; p.at(3, 3) = wpz::Cell{wpz::kSquare, 2};
    s = wpz::Solve(p, 1000, 0);
    CHECK(!s.lines.empty() && s.lines.size() < 12, "squares of two colours rule some lines out: %zu", s.lines.size());
    for (auto& l : s.lines) CHECK(wpz::Check(p, l).ok, "a solution is not accepted");
    // a line through a gap is refused
    wpz::Puzzle g = wpz::NewPuzzle(1, 1); g.at(0, 1).gap = wpz::kGapBreak;
    CHECK(wpz::Solve(g, 10, 0).lines.size() == 1, "a gap leaves one line");
    CHECK(!wpz::Check(g, {{0, 2}, {0, 1}, {0, 0}, {1, 0}, {2, 0}}).ok, "a line through a gap is accepted");
    // problems
    wpz::Puzzle q = wpz::NewPuzzle(3, 3); q.at(0, 6).start = false;
    CHECK(!wpz::Problems(q).empty(), "no start point is not a problem");
    { wpz::Puzzle g = wpz::NewPuzzle(3, 3); g.at(4, 0).gap = wpz::kGapBreak; CHECK(!wpz::Problems(g).empty(), "a gap on a corner is not a problem (the game shows none there)");
      g.at(4, 0).gap = wpz::kGapNone; g.at(3, 0).gap = wpz::kGapBreak; CHECK(wpz::Problems(g).empty(), "a gap on a line is a problem"); }
    q = wpz::NewPuzzle(3, 3); q.sym = wpz::kSymX;
    CHECK(!wpz::Problems(q).empty(), "a symmetry puzzle without mirrored start and end is not a problem");
    q.at(6, 6).start = true; q.at(0, 0).end = wpz::kEndLeft;
    CHECK(wpz::Problems(q).empty(), "a mirrored symmetry puzzle has problems: %s", wpz::Problems(q).empty() ? "" : wpz::Problems(q)[0].c_str());
    CHECK(!wpz::Solve(q, 5, 0).lines.empty(), "the empty symmetry puzzle has a solution");
    // resize keeps symbols, ends stay on the edge
    wpz::Puzzle r = wpz::NewPuzzle(4, 4); r.at(1, 1) = wpz::Cell{wpz::kStar, 10};
    wpz::Puzzle r2 = wpz::Resized(r, 2, 3);
    CHECK(r2.W == 2 && r2.H == 3 && r2.at(1, 1).kind == wpz::kStar, "resize lost the star");
    // the set text
    wpz::Set set{"My set", {wpz::NewPuzzle(3, 2, "first"), r}}; set.puzzles[1].panel = 0x64; set.puzzles[1].at(3, 3) = wpz::Cell{wpz::kPoly, 8, 0, 0x33 | wpz::kRotBit};
    wpz::Set back; std::string err;
    CHECK(wpz::Parse(wpz::Print(set), &back, &err) && back == set, "the set text does not keep the set (%s)", err.c_str());
    std::string h = wpz::Hash(set); set.puzzles[1].at(3, 3).shape &= ~wpz::kRotBit;
    CHECK(h.size() == 16 && h != wpz::Hash(set), "the hash does not change with the puzzle");
    CHECK(!wpz::Parse("witness-coop puzzles 1\nset x\npuzzle 9 9 none 0x0 big\n", &back, &err), "a 9x9 grid is read");
    CHECK(!wpz::Parse("witness-coop puzzles 1\nset x\npuzzle 2 2 none 0x0 t\nat 1 1 triangle orange 4\n", &back, &err), "a triangle of 4 is read");
    CHECK(!wpz::Parse("something else\n", &back, &err), "a file that is not a set is read");
    // witnesspuzzles.com: colours, a play page
    CHECK(wpz::NearestColour("#FFA500")->index == 10 && wpz::NearestColour("rgb(0, 0, 250)")->index == 9 && wpz::NearestColour("white")->index == 2, "colours");
    std::string page = "<script>puzzle = Puzzle.deserialize(\"{\\\"name\\\":\\\"x\\\",\\\"grid\\\":[]}\")</script>";
    CHECK(wpz::JsonFromPlayPage(page) == "{\"name\":\"x\",\"grid\":[]}", "the play page's JSON: %s", wpz::JsonFromPlayPage(page).c_str());
    wpz::Imported im = wpz::ImportJson("{\"grid\":[[{\"type\":\"line\",\"line\":0,\"start\":true},{\"type\":\"line\",\"line\":0},{\"type\":\"line\",\"line\":0}],"
                                       "[{\"type\":\"line\",\"line\":0},{\"type\":\"bridge\",\"color\":\"red\"},{\"type\":\"line\",\"line\":0}],"
                                       "[{\"type\":\"line\",\"line\":0},{\"type\":\"line\",\"line\":0},{\"type\":\"line\",\"line\":0,\"end\":\"right\"}]],\"pillar\":false}");
    CHECK(im.ok && !im.problems.empty(), "a custom symbol is not reported");
    CHECK(!wpz::ImportJson("{\"grid\":[[0,0,0],[0,null,0],[0,0,0]],\"pillar\":true}").ok, "a pillar puzzle is imported");
}

int main(int argc, char** argv) {
    Basics();
    Fixture(argc > 1 ? argv[1] : "tests/wpuzzle_fixture.txt");
    if (fails) { printf("wpuzzle-test: %d FAILED\n", fails); return 1; }
    printf("wpuzzle-test: ok\n"); return 0;
}
