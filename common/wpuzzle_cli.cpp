// wpuzzle-cli: the puzzle rules of common/wpuzzle.h on puzzles from witnesspuzzles.com, for tools/wpuzzle_diff.py (which compares them with
// jbzdarkid's own JavaScript). Reads one puzzle's JSON per line on stdin; prints per puzzle: the number of solutions (up to MAX) and the first one
// as positions "x,y x,y ...", or "skip <why>" for a puzzle the game cannot have (other than its size).
//   wpuzzle-cli MAX [MS]      MS: time per puzzle (default 2000); a solve cut short prints its count with a "+"
// For the in-game test (tools/qa_custom.py), on a set's text (common/wpuzzle.h Print):
//   wpuzzle-cli set FILE              "hash H", then per puzzle "puzzle K panel 0xID W H problems N solutions N line x,y ..." (its panel from Placement)
//   wpuzzle-cli print FILE            the set's text as the editor writes it
//   wpuzzle-cli solutions FILE K [N]  up to N (100) solutions of puzzle K, one per line
//   wpuzzle-cli check FILE K x,y ...  "ok", or "no <why>": that line on puzzle K, by the rules
#include "wpuzzle_json.h"
#include "wpuzzle_panels.h"
#include <fstream>
#include <iostream>
#include <sstream>
static bool ReadSet(const char* file, wpz::Set* set) {
    std::ifstream f(file, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); std::string err;
    if (!wpz::Parse(ss.str(), set, &err)) { printf("error %s\n", err.c_str()); return false; }
    return true;
}
int main(int argc, char** argv) {
    if (argc > 2 && std::string(argv[1]) == "set") {
        wpz::Set set; if (!ReadSet(argv[2], &set)) return 1;
        printf("hash %s\n", wpz::Hash(set).c_str()); auto at = wpz::Placement(set);
        for (size_t k = 0; k < set.puzzles.size(); k++) {
            const wpz::Puzzle& p = set.puzzles[k]; wpz::Solutions s = wpz::Solve(p, 1000, 0, 5000);
            printf("puzzle %zu panel 0x%x %d %d problems %zu solutions %zu%s line", k, at[k], p.W, p.H, wpz::Problems(p).size(), s.lines.size(), s.complete ? "" : "+");
            if (!s.lines.empty()) for (auto& q : s.lines[0]) printf(" %d,%d", q.x, q.y);
            printf("\n");
        }
        return 0;
    }
    if (argc > 2 && std::string(argv[1]) == "print") {      // print FILE: the set's text as the editor writes it (the text its hash is of)
        wpz::Set set; if (!ReadSet(argv[2], &set)) return 1; fputs(wpz::Print(set).c_str(), stdout); return 0;
    }
    if (argc > 3 && std::string(argv[1]) == "solutions") {      // solutions FILE K [N]: up to N solutions of puzzle K, one per line
        wpz::Set set; if (!ReadSet(argv[2], &set)) return 1; size_t k = (size_t)atoi(argv[3]);
        if (k >= set.puzzles.size()) { printf("error no puzzle %zu\n", k); return 1; }
        wpz::Solutions s = wpz::Solve(set.puzzles[k], argc > 4 ? (size_t)atoi(argv[4]) : 100, 0, 5000);
        for (auto& l : s.lines) { for (size_t i = 0; i < l.size(); i++) printf("%s%d,%d", i ? " " : "", l[i].x, l[i].y); printf("\n"); }
        return 0;
    }
    if (argc > 3 && std::string(argv[1]) == "check") {
        wpz::Set set; if (!ReadSet(argv[2], &set)) return 1; size_t k = (size_t)atoi(argv[3]);
        if (k >= set.puzzles.size()) { printf("error no puzzle %zu\n", k); return 1; }
        std::vector<wpz::Pos> path; for (int i = 4; i < argc; i++) { int x, y; if (sscanf(argv[i], "%d,%d", &x, &y) == 2) path.push_back({x, y}); }
        wpz::Verdict v = wpz::Check(set.puzzles[k], path); printf(v.ok ? "ok\n" : "no %s\n", v.why.c_str()); return 0;
    }
    size_t max = argc > 1 ? (size_t)atoi(argv[1]) : 100; int ms = argc > 2 ? atoi(argv[2]) : 2000; std::string line;
    while (std::getline(std::cin, line)) {
        wpz::Imported im = wpz::ImportJson(line);
        if (!im.ok) { printf("skip %s\n", im.error.c_str()); fflush(stdout); continue; }
        bool sizeOnly = true; for (auto& p : im.problems) if (p.find("1 to 7 cells") == std::string::npos) sizeOnly = false;      // (larger grids are still compared)
        if (!sizeOnly) { printf("skip %s\n", im.problems[0].c_str()); fflush(stdout); continue; }
        wpz::Solutions s = wpz::Solve(im.puzzle, max, 0, ms);
        printf("%zu%s", s.lines.size(), s.complete ? "" : "+");
        if (!s.lines.empty()) { printf(" "); for (size_t i = 0; i < s.lines[0].size(); i++) printf("%s%d,%d", i ? " " : "", s.lines[0][i].x, s.lines[0][i].y); }
        printf("\n"); fflush(stdout);
    }
}
