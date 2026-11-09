// wpuzzle-cli: the puzzle rules of common/wpuzzle.h on puzzles from witnesspuzzles.com, for tools/wpuzzle_diff.py (which compares them with
// jbzdarkid's own JavaScript). Reads one puzzle's JSON per line on stdin; prints per puzzle: the number of solutions (up to MAX) and the first one
// as positions "x,y x,y ...", or "skip <why>" for a puzzle the game cannot have (other than its size).
//   wpuzzle-cli MAX [MS]      MS: time per puzzle (default 2000); a solve cut short prints its count with a "+"
#include "wpuzzle_json.h"
#include <iostream>
int main(int argc, char** argv) {
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
