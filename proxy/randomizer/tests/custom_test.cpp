// Game-free (T0): the player's own puzzles (common/wpuzzle.h) written into the panels of a recorded game (world.h) by the generator's panel
// writer, then read back with its own reader: every puzzle of the test set is on its panel as it was made (size, symbols and their colours,
// polyominoes the right way up, dots, gaps, start and end points (an end in the middle of an edge too), symmetry), only those panels change, a second run makes the same, and a set
// that is missing stops the run with a reason. The first two Tutorial panels are not grids (a straight line, a bend): they take a grid
// panel's frame (nsv.cpp IsGridPanel; mutant READ_ANY_PANEL: read as they are, which crashed both games).
// Mutants (must fail): WR_MUT_SHAPE_FLIPPED (polyominoes written upside down), WR_MUT_READ_ANY_PANEL.
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
    a.at(2, 2).dot = wpz::kDotBlack; a.at(4, 3).dot = wpz::kDotBlack; a.at(3, 6).dot = wpz::kDotBlack;      // dots on a corner, a vertical and a horizontal line a.at(5, 4).gap = wpz::kGapBreak; a.at(8, 3).gap = wpz::kGapFull;
    a.panel = 0x00557;
    s.puzzles.push_back(a);
    wpz::Puzzle b = wpz::NewPuzzle(3, 3, "mirrored"); b.sym = wpz::kSymX; b.panel = 0x00022;
    b.at(6, 6).start = true; b.at(0, 0).end = wpz::kEndLeft; b.at(6, 0).end = wpz::kEndRight;
    b.at(2, 4).dot = wpz::kDotBlue; b.at(5, 2).dot = wpz::kDotYellow; b.at(3, 3) = wpz::Cell{wpz::kStar, 5}; b.at(1, 1) = wpz::Cell{wpz::kStar, 5};
    s.puzzles.push_back(b);
    wpz::Puzzle c = wpz::NewPuzzle(5, 2, "wide"); c.at(10, 4).start = true; c.at(5, 3) = wpz::Cell{wpz::kPoly, 7, 0, 0x33};
    c.panel = 0x00469; s.puzzles.push_back(c);
    wpz::Puzzle d = wpz::NewPuzzle(1, 2, "small"); d.panel = 0x0001B; d.at(2, 0).end = wpz::kEndNone; d.at(1, 0).end = wpz::kEndTop; s.puzzles.push_back(d);
    wpz::Puzzle e = wpz::NewPuzzle(3, 3, "first"); e.panel = 0x00064; e.at(1, 1) = wpz::Cell{wpz::kSquare, 1}; e.at(5, 5) = wpz::Cell{wpz::kSquare, 2}; s.puzzles.push_back(e);      // panels that are not grids
    wpz::Puzzle f = wpz::NewPuzzle(2, 2, "bend"); f.panel = 0x00182; f.at(1, 1) = wpz::Cell{wpz::kStar, 10}; f.at(3, 3) = wpz::Cell{wpz::kStar, 10}; s.puzzles.push_back(f);
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
        uint8_t bg[16];      // the recording lacks Tutorial Straight's background colour (+0x110: the reader reads it; no randomizer did): Bend's stands in
        if (!r.overlay->readField(0x64, 0x110, bg, 16) && r.overlay->readField(0x182, 0x110, bg, 16)) r.overlay->writeField(0x64, 0x110, bg, 16);
        for (size_t k = 0; k < set.puzzles.size(); k++) {
            wpz::Puzzle back;
            try { back = wrand::ReadCustomPuzzle((int)where[k]); }
            catch (const std::exception& ex) {
                int near = -1; uintptr_t base = 0;
                for (auto& e : world.ents) if (e.second <= world.missAddr && e.second > base) { base = e.second; near = e.first; }
                std::printf("FAIL: %s on %x: %s (the recording does not have entity %x +%llx)\n", set.puzzles[k].name.c_str(), where[k], ex.what(), near, (unsigned long long)(world.missAddr - base)); fails++; continue;
            }
            bool same = Shape(back) == Shape(set.puzzles[k]);
            if (pass == 1) std::printf("  %-14s on %-36s %s\n", set.puzzles[k].name.c_str(), wpz::SlotName(where[k]).c_str(), same ? "read back the same" : "DIFFERENT");
            auto flags = wrand::CustomGridFlags((int)where[k]); const wpz::Puzzle& q = set.puzzles[k];      // each dot with the generator's own flags (the game reads more than the reader keeps)
            for (int x = 0; x < q.width(); x++) for (int y = 0; y < q.height(); y++) if (q.at(x, y).dot) {
                int want = wrand::DotFlags(x % 2 == 0 && y % 2 == 0 ? 0 : x % 2 ? 1 : 2), got = x < (int)flags.size() && y < (int)flags[x].size() ? flags[x][y] : 0;
                if ((got & want) != want) { std::printf("FAIL: %s: the dot at %d,%d has flags %x, not the generator's %x\n", q.name.c_str(), x, y, got, want); fails++; }
            }
            if (!same) { fails++; if (pass == 1) std::printf("--- made\n%s--- read back\n%s", Shape(set.puzzles[k]).c_str(), Shape(back).c_str()); }
        }
    }
    {      // every panel the editor offers takes a puzzle and reads it back (a panel the reader cannot read crashed the game)
        wpz::Set all; all.name = "every panel";
        for (auto& slot : wpz::Slots()) { wpz::Puzzle q = wpz::NewPuzzle(2, 2, slot.name); q.panel = slot.id; q.at(1, 1) = wpz::Cell{wpz::kSquare, 1}; q.at(3, 3) = wpz::Cell{wpz::kStar, 10}; all.puzzles.push_back(q); }
        std::string keep = g_text; g_text = wpz::Print(all); wrand::Arrays aa; world.misses = 0;
        auto r = wrand::Generate(wrand::ParseSpec("custom;set:" + wpz::Hash(g_text)), &world, &aa);
        if (!r.ok) {
            int near = -1; uintptr_t base = 0;
            for (auto& e : world.ents) if (e.second <= world.missAddr && e.second > base) { base = e.second; near = e.first; }
            std::printf("FAIL: every panel: %s (entity %x +%llx)\n", r.error.c_str(), near, (unsigned long long)(world.missAddr - base)); fails++;
        } else {
            wrand::ScopedBackend use(r.overlay.get()); int same = 0, lacking = 0;
            uint8_t bg[16]; if (!r.overlay->readField(0x64, 0x110, bg, 16) && r.overlay->readField(0x182, 0x110, bg, 16)) r.overlay->writeField(0x64, 0x110, bg, 16);
            for (auto& q : all.puzzles) {
                try { if (Shape(wrand::ReadCustomPuzzle((int)q.panel)) == Shape(q)) same++; else { std::printf("FAIL: on %s (%x) the puzzle reads back different\n", wpz::SlotName(q.panel).c_str(), q.panel); fails++; } }
                catch (const std::exception&) { lacking++; std::printf("  (%s, %x: the recording lacks a field the reader reads)\n", wpz::SlotName(q.panel).c_str(), q.panel); }
            }
            std::printf("every panel: %zu panels written, %d read back the same, %d not readable from the recording\n", all.puzzles.size(), same, lacking);
            if (r.overlay->changes().size() != all.puzzles.size()) { std::printf("FAIL: every panel: %zu entities changed\n", r.overlay->changes().size()); fails++; }
        }
        g_text = keep;
    }
    {      // the generator's own puzzles (four recorded runs), read into the editor's model and written back by the editor's writer: what the game
           // judges (symbols, dots with the bit a dot needs, gaps, starts, ends) must come back bit for bit (a dot without 0x40000 was judged wrong
           // in the game). Not compared: structure the editor does not need (points nothing connects to, 0x8; extra nodes on lines; how a corner
           // is tagged) and panels with what the editor has not (arrows and the like: Problems).
        int compared = 0, differ = 0, runs = 0;
        for (const auto& rec : recorded) {
            if (!rec.fingerprint || rec.spec.find("seed:4242") == std::string::npos) continue;
            std::string m = rec.spec.substr(0, rec.spec.find(';'));
            if (m != "normal" && m != "expert" && m != "variety" && m != "puzzle-pack") continue;
            wrand::Arrays ga; world.misses = 0; auto r = wrand::Generate(wrand::ParseSpec(rec.spec), &world, &ga);
            if (!r.ok) { std::printf("FAIL: %s: %s\n", rec.spec.c_str(), r.error.c_str()); fails++; continue; }
            runs++; wrand::ScopedBackend use(r.overlay.get()); auto ch = r.overlay->changes();
            for (auto& sl : wpz::Slots()) {
                if (!ch.count((int)sl.id)) continue;
                std::vector<std::vector<int>> before; wpz::Puzzle q;
                try { before = wrand::CustomGridFlags((int)sl.id); if (before.empty()) continue; q = wrand::ReadCustomPuzzle((int)sl.id); } catch (const std::exception&) { continue; }
                if (!wpz::Problems(q).empty()) continue;
                bool unknown = false;      // a symbol the editor has not (arrows 0x700, and the like): its reader drops it, so it is not the editor's puzzle
                for (int x = 1; x < q.width(); x += 2) for (int y = 1; y < q.height(); y += 2) { int v = x < (int)before.size() && y < (int)before[x].size() ? before[x][y] : 0, sh = v & 0xF00; if (v && sh != 0x100 && sh != 0x200 && sh != 0x400 && sh != 0x500 && sh != 0x600) unknown = true; }
                if (unknown) continue;
                wrand::WriteCustomPuzzle((int)sl.id, q); auto after = wrand::CustomGridFlags((int)sl.id); compared++; int shown = 0; bool same = true;
                for (int x = 0; x < q.width(); x++) for (int y = 0; y < q.height(); y++) {
                    int g = x < (int)before.size() && y < (int)before[x].size() ? before[x][y] : 0, e = x < (int)after.size() && y < (int)after[x].size() ? after[x][y] : 0;
                    bool cell = x % 2 && y % 2;
                    int mask = cell ? ~0 : (0x20 | 0x100 | 0x200 | 0x1000 | 0x100000 | 0x3 | ((g & 0x20) ? 0x40000 : 0));      // (on lines and corners: dots and their colours, gaps, start, end)
                    if (!cell && (g & 0x3) == 0x3) continue;      // (OPEN, a line that is not there: written as the editor's missing line)
                    if ((g & mask) != (e & mask)) { same = false; if (shown++ < 3) std::printf("FAIL: %s, %s: at %d,%d the generator has %x, the editor's writer %x\n", rec.spec.c_str(), wpz::SlotName(sl.id).c_str(), x, y, g, e); }
                }
                if (!same) { differ++; fails++; }
            }
        }
        std::printf("generator's puzzles written back: %d runs, %d panels compared, %d different\n", runs, compared, differ);
        if (runs < 4 || compared < 100) { std::printf("FAIL: too little compared (%d runs, %d panels)\n", runs, compared); fails++; }
    }
    wrand::Arrays a3; auto missing = wrand::Generate(wrand::ParseSpec("custom;set:0000000000000001"), &world, &a3);
    if (missing.ok || missing.error.find("not on this PC") == std::string::npos) { std::printf("FAIL: a missing set: %s\n", missing.error.c_str()); fails++; }
    if (fails) std::printf("custom: %d FAILED\n", fails); else std::printf("custom: ok\n");
    return fails ? 1 : 0;
}
