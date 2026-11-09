// Game-free (T0): the player's own puzzles (common/wpuzzle.h) written into the panels of a recorded game (world.h) by the generator's panel
// writer, then read back with its own reader: every puzzle of the test set is on its panel as it was made (size, symbols and their colours,
// polyominoes the right way up, dots, gaps, start and end points (an end in the middle of an edge too), symmetry), only those panels change, a second run makes the same, and a set
// that is missing stops the run with a reason. (The test set names its panels: the recording has what the built-in randomizers read, and
// the first Tutorial panels, which only "Your puzzles" changes, are not fully in it.)
// Mutant (must fail): WR_MUT_SHAPE_FLIPPED (polyominoes written upside down).
#include <cstdio>
#include "../generate.h"
#include "../variant.h"
#include "../world.h"
#include "../../../common/wpuzzle_panels.h"

static std::string g_text;
static std::string Load(const std::string& hash) { return wpz::Hash(g_text) == hash ? g_text : std::string(); }

static wpz::Set TestSet() {
    wpz::Set s; s.name = "test";
    wpz::Puzzle a = wpz::NewPuzzle(4, 4, "every symbol");
    a.at(1, 1) = wpz::Cell{wpz::kSquare, 1}; a.at(3, 1) = wpz::Cell{wpz::kSquare, 2}; a.at(5, 1) = wpz::Cell{wpz::kStar, 10}; a.at(7, 1) = wpz::Cell{wpz::kStar, 10};
    a.at(1, 3) = wpz::Cell{wpz::kNega, 2}; a.at(3, 3) = wpz::Cell{wpz::kTriangle, 10, 1}; a.at(5, 3) = wpz::Cell{wpz::kTriangle, 10, 2}; a.at(7, 3) = wpz::Cell{wpz::kTriangle, 10, 3};
    a.at(1, 5) = wpz::Cell{wpz::kPoly, 8, 0, 0x13 | wpz::kRotBit};      // an L of three, may turn
    a.at(3, 5) = wpz::Cell{wpz::kPoly, 8, 0, 0x017};                    // an L of four, fixed (bits x * 4 + y: (0,0) (0,1) (0,2) (1,0))
    a.at(5, 5) = wpz::Cell{wpz::kYlop, 9, 0, 0x1};
    a.at(7, 7) = wpz::Cell{wpz::kSquare, 4};
    a.at(2, 2).dot = wpz::kDotBlack; a.at(4, 3).dot = wpz::kDotBlack; a.at(5, 4).gap = wpz::kGapBreak; a.at(8, 3).gap = wpz::kGapFull;
    a.panel = 0x00557;
    s.puzzles.push_back(a);
    wpz::Puzzle b = wpz::NewPuzzle(3, 3, "mirrored"); b.sym = wpz::kSymX; b.panel = 0x00022;
    b.at(6, 6).start = true; b.at(0, 0).end = wpz::kEndLeft; b.at(6, 0).end = wpz::kEndRight;
    b.at(2, 4).dot = wpz::kDotBlue; b.at(5, 2).dot = wpz::kDotYellow; b.at(3, 3) = wpz::Cell{wpz::kStar, 5}; b.at(1, 1) = wpz::Cell{wpz::kStar, 5};
    s.puzzles.push_back(b);
    wpz::Puzzle c = wpz::NewPuzzle(5, 2, "wide"); c.at(10, 4).start = true; c.at(5, 3) = wpz::Cell{wpz::kPoly, 7, 0, 0x33};
    c.panel = 0x00469; s.puzzles.push_back(c);
    wpz::Puzzle d = wpz::NewPuzzle(1, 2, "small"); d.panel = 0x0001B; d.at(2, 0).end = wpz::kEndNone; d.at(1, 0).end = wpz::kEndTop; s.puzzles.push_back(d);
    return s;
}
static std::string Shape(wpz::Puzzle p) {      // the puzzle as the set text prints it, without its name and panel and with each polyomino moved to the corner
    p.name.clear(); p.panel = 0;
    for (auto& c : p.g) if (c.kind == wpz::kPoly || c.kind == wpz::kYlop) {
        uint32_t s = c.shape & 0xFFFF; int mx = 4, my = 4;
        for (int x = 0; x < 4; x++) for (int y = 0; y < 4; y++) if (wpz::PolySet(s, x, y)) { mx = std::min(mx, x); my = std::min(my, y); }
        uint32_t n = 0; for (int x = 0; x < 4; x++) for (int y = 0; y < 4; y++) if (wpz::PolySet(s, x, y)) n |= wpz::PolyMask(x - mx, y - my);
        c.shape = n | (c.shape & wpz::kRotBit);
    }
    return wpz::Print(wpz::Set{"", {p}});
}

int main(int argc, char** argv) {
    int fails = 0;
    if (argc < 2) { std::printf("FAIL: no recorded world given\n"); return 1; }
    wrand::ReplayRaw world; std::vector<wrand::RecordedRun> recorded;
    if (!wrand::LoadWorld(argv[1], &world, &recorded)) { std::printf("FAIL %s: not a recorded world\n", argv[1]); return 1; }
    wrand::LoadPuzzleSet = Load;
    wpz::Set set = TestSet(); g_text = wpz::Print(set);
    for (auto& p : set.puzzles) if (!wpz::Problems(p).empty()) { std::printf("FAIL: test puzzle '%s': %s\n", p.name.c_str(), wpz::Problems(p)[0].c_str()); fails++; }
    wrand::Spec spec = wrand::ParseSpec("custom;set:" + wpz::Hash(g_text));
    std::vector<unsigned> where = wpz::Placement(set);
    uint64_t first = 0;
    for (int pass = 0; pass < 2; pass++) {
        wrand::Arrays arrays; world.misses = 0;
        auto r = wrand::Generate(spec, &world, &arrays);
        std::printf("custom pass %d: %s, %zu entities, fingerprint %016llx, misses %u\n", pass + 1, r.ok ? "ok" : r.error.c_str(), r.entities, (unsigned long long)r.fingerprint, world.misses);
        if (!r.ok) {
            int near = -1; uintptr_t base = 0;
            for (auto& e : world.ents) if (e.second <= world.missAddr && e.second > base) { base = e.second; near = e.first; }
            std::printf("  (the recording does not have: entity %x +%llx)\n", near, (unsigned long long)(world.missAddr - base)); fails++; break;
        }
        if (pass == 0) first = r.fingerprint; else if (first != r.fingerprint) { std::printf("FAIL: the second run made other puzzles\n"); fails++; }
        for (const auto& kv : r.overlay->changes()) {
            bool placed = false; for (unsigned w : where) placed = placed || w == kv.first;
            if (!placed) { std::printf("FAIL: entity %x changed, which is not one of the set's panels\n", kv.first); fails++; }
        }
        if (r.overlay->changes().size() != where.size()) { std::printf("FAIL: %zu entities changed, %zu panels placed\n", r.overlay->changes().size(), where.size()); fails++; }
        wrand::ScopedBackend use(r.overlay.get());      // (the run's own view: the recorded game with its writes and its arrays)
        for (size_t k = 0; k < set.puzzles.size(); k++) {
            wpz::Puzzle back = wrand::ReadCustomPuzzle((int)where[k]);
            bool same = Shape(back) == Shape(set.puzzles[k]);
            if (pass == 1) std::printf("  %-14s on %-36s %s\n", set.puzzles[k].name.c_str(), wpz::SlotName(where[k]).c_str(), same ? "read back the same" : "DIFFERENT");
            if (!same) { fails++; if (pass == 1) std::printf("--- made\n%s--- read back\n%s", Shape(set.puzzles[k]).c_str(), Shape(back).c_str()); }
        }
    }
    wrand::Arrays a3; auto missing = wrand::Generate(wrand::ParseSpec("custom;set:0000000000000001"), &world, &a3);
    if (missing.ok || missing.error.find("not on this PC") == std::string::npos) { std::printf("FAIL: a missing set: %s\n", missing.error.c_str()); fails++; }
    if (fails) std::printf("custom: %d FAILED\n", fails); else std::printf("custom: ok\n");
    return fails ? 1 : 0;
}
