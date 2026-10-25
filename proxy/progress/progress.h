#pragma once
// The natural order of The Witness' puzzles, for "start a new game with these parts of the map completed". The data (progression.txt, built by tools/gen_progression.py from the
// Archipelago randomizer's logic file, MIT) gives every puzzle panel with the game's own entity id, its area and the full set of panels the game needs solved before it can be
// reached and solved: the panels in front of it in its own area, the panel behind every door on the way in, the panels that open the way between areas. Completing a part of the
// map is therefore: the panels chosen plus whatever the game needs before them, nothing else, prerequisites first. Game-free (T0 test: progress_test.cpp).
#include <map>
#include <string>
#include <vector>

namespace wprog {
struct Panel { unsigned id = 0; int area = 0; std::string name; std::vector<unsigned> needs; bool optional = false; };      // optional: not part of finishing its area (it waits for far more than its area, like the tutorial's closing panel)      // needs: every panel to be solved before this one (transitive), without itself
struct Data {
    std::vector<std::string> areas;      // "Symmetry Island", ...
    std::vector<Panel> panels;           // in file order
    struct Door { unsigned id = 0; std::vector<std::vector<unsigned>> opensWith; };      // opens when every panel of any one alternative is solved
    std::vector<Door> doors;
    std::vector<unsigned> lasers;        // the panel that lights each of the 11 lasers
    std::map<unsigned, size_t> index;    // panel id -> position in `panels`
    int AreaByName(const std::string& name) const;       // case and spaces/underscores/punctuation ignored: "symmetry island", "Mountain (Inside)"; -1: none
    size_t AreaCount(int area) const;
};
const char* BuiltinText();               // progress_data.cpp
bool Parse(const std::string& text, Data* out, std::string* err = nullptr);
const Data& Builtin();                   // the embedded data, parsed once

// The panels to solve, prerequisites first: every panel of the chosen areas, the single panels, and what the game needs before any of them.
std::vector<unsigned> Complete(const Data& d, const std::vector<int>& areas, const std::vector<unsigned>& panels = {}, bool withPrerequisites = true);      // (without: only a game-level test mutant asks for that)
// The cheapest way to light n of the 11 lasers: their panels and everything before them (one after the other, the cheapest next).
std::vector<unsigned> Lasers(const Data& d, int n);
// A choice written as text (the cfg key start_progress=, the launcher, the dev menu): "area:Desert;area:Symmetry Island;lasers:3;panel:0x293;preset:seven-lasers". Empty on an error (err says why).
std::vector<unsigned> Resolve(const Data& d, const std::string& spec, std::string* err = nullptr, bool withPrerequisites = true);
std::vector<std::string> PresetNames();                 // "tutorial", "three-lasers", "seven-lasers", "all-lasers", "everything"
std::string PresetSpec(const std::string& name);        // its spec text, empty if unknown
}  // namespace wprog
