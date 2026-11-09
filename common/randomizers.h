#pragma once
// The randomizers built into the mod, and the one text that names a choice of them (header-only, game-free; T0: make -C common randomizers-test).
// The launcher's Randomizers tab lists kRandomizers (with their credits), the lobby carries the host's choice to every player as a Spec's text, the
// game reads it from witness_coop.cfg (randomizer=...) and the save's sidecar keeps it, so a continued game gets the same puzzles.
//
// Spec text: "<mode>;seed:<n>[;shuffle][;colorblind][;fast]", e.g. "expert;seed:48213;fast". "off" (or empty) is no randomizer.
#include <cstdint>
#include <string>
#include <vector>

namespace wrand {

enum Mode : uint8_t { kOff = 0, kNormal = 1, kExpert = 2, kVariety = 3 };

struct Credit { const char* who; const char* what; const char* link; };
struct Info {
    Mode mode;
    const char* key;            // the spec's word
    const char* name;           // what the player sees
    const char* blurb;
    bool canShuffle;            // upstream's "double mode": shuffle which panel is where (Normal and Expert)
    std::vector<Credit> credits;
};

inline const std::vector<Info>& Randomizers() {
    static const std::vector<Info> v = {
        {kNormal, "normal", "Sigma's Randomizer: Normal",
         "New puzzles on most logic panels, with each area keeping its own kind of symbols. The usual choice for a first randomized game.", true,
         {{"sigma144", "the puzzle generator, Normal and Expert", "https://github.com/sigma144/witness-randomizer"},
          {"jbzdarkid", "the original Witness randomizer", "https://github.com/jbzdarkid/witness-randomizer"}}},
        {kExpert, "expert", "Sigma's Randomizer: Expert",
         "Much harder puzzles, some needing approaches the base game never asks for. Made for players who know the game well.", true,
         {{"sigma144", "the puzzle generator, Normal and Expert", "https://github.com/sigma144/witness-randomizer"},
          {"jbzdarkid", "the original Witness randomizer", "https://github.com/jbzdarkid/witness-randomizer"}}},
        {kVariety, "variety", "Variety Randomizer",
         "Each area gets its own combination of symbols, including new ones such as arrows.", false,
         {{"Penguin12335", "Variety", "https://github.com/Penguin12335/Variety-Randomiser"},
          {"sigma144", "the puzzle generator it builds on", "https://github.com/sigma144/witness-randomizer"},
          {"jbzdarkid", "the original Witness randomizer", "https://github.com/jbzdarkid/witness-randomizer"}}},
    };
    return v;
}
// Everyone whose work is in the built-in randomizers (the tab's credits and THIRD-PARTY-NOTICES.txt say the same).
inline const char* RandomizerSourceCredit() {
    return "The randomizers are the Witness Random Puzzle Generator (MIT licence, copyright 2018 jbzdarkid), as maintained for Archipelago by "
           "NewSoupVi, built into the mod unchanged apart from how it reaches the game. Written by sigma144, jbzdarkid, NewSoupVi, Penguin12335, "
           "blastron, Jarno458, hatkirby, ReverM and others.";
}
constexpr const char* kRandomizerSourceLink = "https://github.com/NewSoupVi/The-Witness-Randomizer-for-Archipelago";

inline const Info* FindRandomizer(Mode m) { for (const auto& i : Randomizers()) if (i.mode == m) return &i; return nullptr; }

struct Spec {
    Mode mode = kOff;
    uint32_t seed = 0;          // 0 .. 2^31-1 (upstream's seed is an int)
    bool shuffle = false, colorblind = false, fast = false;
    bool on() const { return mode != kOff; }
    bool operator==(const Spec& o) const { return mode == o.mode && seed == o.seed && shuffle == o.shuffle && colorblind == o.colorblind && fast == o.fast; }
    bool operator!=(const Spec& o) const { return !(*this == o); }
};

inline std::string SpecText(const Spec& s) {
    const Info* i = FindRandomizer(s.mode);
    if (!i) return "off";
    std::string t = std::string(i->key) + ";seed:" + std::to_string(s.seed & 0x7FFFFFFFu);
    if (s.shuffle && i->canShuffle) t += ";shuffle";
    if (s.colorblind) t += ";colorblind";
    if (s.fast) t += ";fast";
    return t;
}
// Anything unknown is ignored; a mode it does not know, or none, is off. A missing seed is 0.
inline Spec ParseSpec(const std::string& text) {
    Spec s;
    size_t a = 0;
    bool first = true;
    while (a <= text.size()) {
        size_t b = text.find(';', a); if (b == std::string::npos) b = text.size();
        std::string w = text.substr(a, b - a);
        while (!w.empty() && (w.back() == ' ' || w.back() == '\r' || w.back() == '\t')) w.pop_back();
        while (!w.empty() && (w.front() == ' ' || w.front() == '\t')) w.erase(w.begin());
        if (first) { for (const auto& i : Randomizers()) if (w == i.key) s.mode = i.mode; first = false; }
        else if (w.compare(0, 5, "seed:") == 0) {
            uint64_t v = 0; bool ok = w.size() > 5;
            for (size_t k = 5; k < w.size() && ok; k++) { if (w[k] < '0' || w[k] > '9') ok = false; else { v = v * 10 + (uint64_t)(w[k] - '0'); if (v > 0x7FFFFFFFu) ok = false; } }
            if (ok) s.seed = (uint32_t)v;
        }
        else if (w == "shuffle") s.shuffle = true;
        else if (w == "colorblind") s.colorblind = true;
        else if (w == "fast") s.fast = true;
        a = b + 1;
    }
    if (s.mode == kOff) return Spec();
    const Info* i = FindRandomizer(s.mode);
    if (i && !i->canShuffle) s.shuffle = false;
    return s;
}
// A one-line description for the lobby, the save list and the game: "Sigma's Randomizer: Expert, seed 48213, faster doors"
inline std::string SpecSummary(const Spec& s) {
    const Info* i = FindRandomizer(s.mode);
    if (!i) return "No randomizer";
    std::string t = std::string(i->name) + ", seed " + std::to_string(s.seed);
    if (s.shuffle) t += ", panels shuffled";
    if (s.colorblind) t += ", colour-blind colours";
    if (s.fast) t += ", faster doors";
    return t;
}
}  // namespace wrand
