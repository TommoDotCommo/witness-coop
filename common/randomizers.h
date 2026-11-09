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

enum Mode : uint8_t { kOff = 0, kNormal = 1, kExpert = 2, kVariety = 3, kAscension = 4, kDespairSymbols = 5, kDespair = 6, kShuffler = 7, kEmptiness = 8,
                      kPuzzlePack = 9, kStar = 10, kTutorializer = 11 };

struct Credit { const char* who; const char* what; const char* link; };
struct Info {
    Mode mode;
    const char* key;            // the spec's word
    const char* name;           // what the player sees
    const char* blurb;
    bool canShuffle;            // upstream's "double mode": shuffle which panel is where
    std::vector<Credit> credits;
    const char* project = "";   // the randomizer it comes from, for the list's groups
    bool canColorblind = true;  // the colour-blind option exists in that randomizer
    bool seeded = true;         // the seed changes the puzzles (Tutorializer's are always the same)
    const char* warning = "";   // its author's own warning, shown with it
};

inline const std::vector<Info>& Randomizers() {
    static const std::vector<Info> v = {
        {kNormal, "normal", "Sigma's Randomizer: Normal",
         "New puzzles on most logic panels, with each area keeping its own kind of symbols. The usual choice for a first randomized game.", true,
         {{"sigma144", "the puzzle generator, Normal and Expert", "https://github.com/sigma144/witness-randomizer"},
          {"jbzdarkid", "the original Witness randomizer", "https://github.com/jbzdarkid/witness-randomizer"}}, "Sigma's Randomizer"},
        {kExpert, "expert", "Sigma's Randomizer: Expert",
         "Much harder puzzles, some needing approaches the base game never asks for. Made for players who know the game well.", true,
         {{"sigma144", "the puzzle generator, Normal and Expert", "https://github.com/sigma144/witness-randomizer"},
          {"jbzdarkid", "the original Witness randomizer", "https://github.com/jbzdarkid/witness-randomizer"}}, "Sigma's Randomizer"},
        {kVariety, "variety", "Variety Randomizer",
         "Each area gets its own combination of symbols, including new ones such as arrows.", false,
         {{"Penguin12335", "Variety", "https://github.com/Penguin12335/Variety-Randomiser"},
          {"sigma144", "the puzzle generator it builds on", "https://github.com/sigma144/witness-randomizer"},
          {"jbzdarkid", "the original Witness randomizer", "https://github.com/jbzdarkid/witness-randomizer"}}, "Variety Randomizer"},
        {kAscension, "ascension", "Variety Randomizer: Ascension",
         "New symbols from other Witness fan games and the Despair Randomizer, in puzzles of the Variety Randomizer's kind.", true,
         {{"Penguin12335", "Variety and Ascension", "https://github.com/Penguin12335/Variety-Randomiser"},
          {"chuukunn", "the Despair Randomizer's symbols", "https://github.com/chuukunn/witness-randomizer"},
          {"Looksy", "symbols from a Witness fan game", ""},
          {"sigma144", "the puzzle generator it builds on", "https://github.com/sigma144/witness-randomizer"},
          {"jbzdarkid", "the original Witness randomizer", "https://github.com/jbzdarkid/witness-randomizer"}}, "Variety Randomizer"},
        {kDespairSymbols, "despair-symbols", "Despair Randomizer: Symbols",
         "New symbols (faces, mushrooms and ghosts) in puzzles of Sigma's kind.", true,
         {{"chuukunn", "Despair and its symbols", "https://github.com/chuukunn/witness-randomizer"},
          {"sigma144", "the puzzle generator it builds on", "https://github.com/sigma144/witness-randomizer"},
          {"jbzdarkid", "the original Witness randomizer", "https://github.com/jbzdarkid/witness-randomizer"}}, "Despair Randomizer", true, true,
         "Its author's warning: ghost puzzles are buggy, so the game may not be finishable without glitches."},
        {kDespair, "despair", "Despair Randomizer: Despair",
         "Bigger grids (mostly 6x6 to 8x8) and harder combinations of symbols and mechanics: the step beyond Expert.", true,
         {{"chuukunn", "Despair and its symbols", "https://github.com/chuukunn/witness-randomizer"},
          {"sigma144", "the puzzle generator it builds on", "https://github.com/sigma144/witness-randomizer"},
          {"jbzdarkid", "the original Witness randomizer", "https://github.com/jbzdarkid/witness-randomizer"}}, "Despair Randomizer"},
        {kShuffler, "shuffler", "Symbol Shuffler",
         "New puzzles with as close as possible to the same number of each symbol as the game's own panel.", false,
         {{"Dawgup (KemSpeedruns)", "the Symbol Shuffler", "https://github.com/KemSpeedruns/witness-randomizer"},
          {"sigma144", "the puzzle generator it builds on", "https://github.com/sigma144/witness-randomizer"},
          {"jbzdarkid", "the original Witness randomizer", "https://github.com/jbzdarkid/witness-randomizer"}}, "Symbol Shuffler", false},
        {kEmptiness, "emptiness", "Symbol Shuffler: Emptiness",
         "Every puzzle emptied out as much as possible.", false,
         {{"Dawgup (KemSpeedruns)", "the Symbol Shuffler", "https://github.com/KemSpeedruns/witness-randomizer"},
          {"sigma144", "the puzzle generator it builds on", "https://github.com/sigma144/witness-randomizer"},
          {"jbzdarkid", "the original Witness randomizer", "https://github.com/jbzdarkid/witness-randomizer"}}, "Symbol Shuffler", false},
        {kPuzzlePack, "puzzle-pack", "Symbol Shuffler: Puzzle Pack",
         "Completely random puzzles, with few limits on what each panel gets.", false,
         {{"Dawgup (KemSpeedruns)", "the Symbol Shuffler", "https://github.com/KemSpeedruns/witness-randomizer"},
          {"sigma144", "the puzzle generator it builds on", "https://github.com/sigma144/witness-randomizer"},
          {"jbzdarkid", "the original Witness randomizer", "https://github.com/jbzdarkid/witness-randomizer"}}, "Symbol Shuffler", false, true,
         "Its author's warning: playable, but issues may arise."},
        {kStar, "star", "Star Randomizer",
         "Each panel keeps its theme, with at least two extra stars.", false,
         {{"Parker (MrP4rK3r2)", "the Star Randomizer", "https://github.com/MrP4rK3r2/witness-randomizer"},
          {"sigma144", "the puzzle generator it builds on", "https://github.com/sigma144/witness-randomizer"},
          {"jbzdarkid", "the original Witness randomizer", "https://github.com/jbzdarkid/witness-randomizer"}}, "Star Randomizer", false},
        {kTutorializer, "tutorializer", "Tutorializer",
         "Almost every panel becomes the first tutorial puzzle, a straight line. A joke mode.", false,
         {{"hatkirby", "the Tutorializer", "https://code.fourisland.com/witness-tutorializer/about/"},
          {"jbzdarkid", "the randomizer it builds on", "https://github.com/jbzdarkid/witness-randomizer"}}, "Tutorializer", false, false,
         "Its author's note: some panels (the tutorial gate, every Bunker puzzle) behave strangely and are solved by double-clicking in the middle."},
    };
    return v;
}
// Everyone whose work is in the built-in randomizers (the tab's credits and THIRD-PARTY-NOTICES.txt say the same).
inline const char* RandomizerSourceCredit() {
    return "The randomizers are the Witness Random Puzzle Generator (MIT licence, copyright 2018 jbzdarkid) and its forks, built into the mod "
           "unchanged apart from how they reach the game: NewSoupVi's version for Archipelago (Sigma's Normal and Expert, Variety), Penguin12335's "
           "Variety Randomizer (Ascension), chuukunn's Despair Randomizer, Dawgup's Symbol Shuffler, Parker's Star Randomizer and hatkirby's "
           "Tutorializer. Written by sigma144, jbzdarkid, NewSoupVi, Penguin12335, chuukunn, Dawgup, Parker, hatkirby, blastron, Jarno458, ReverM "
           "and others.";
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
#ifdef RZ_MUT_SEED_LOST
    std::string t = std::string(i->key);      // mutant: the seed is not written (every player would roll their own)
#else
    std::string t = std::string(i->key) + ";seed:" + std::to_string(s.seed & 0x7FFFFFFFu);
#endif
    if (s.shuffle && i->canShuffle) t += ";shuffle";
    if (s.colorblind && i->canColorblind) t += ";colorblind";
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
    if (i && !i->canColorblind) s.colorblind = false;
    return s;
}
// The people a randomizer is by, for the game's notice: "sigma144 and jbzdarkid"
inline std::string CreditNames(Mode m) {
    const Info* i = FindRandomizer(m); if (!i) return "";
    std::string t; for (size_t k = 0; k < i->credits.size(); ++k) t += (k ? (k + 1 == i->credits.size() ? " and " : ", ") : "") + std::string(i->credits[k].who);
    return t;
}
// A one-line description for the lobby, the save list and the game: "Sigma's Randomizer: Expert, seed 48213, faster doors"
inline std::string SpecSummary(const Spec& s) {
    const Info* i = FindRandomizer(s.mode);
    if (!i) return "No randomizer";
    std::string t = std::string(i->name) + (i->seeded ? ", seed " + std::to_string(s.seed) : std::string());
    if (s.shuffle) t += ", panels shuffled";
    if (s.colorblind) t += ", colour-blind colours";
    if (s.fast) t += ", faster doors";
    return t;
}
}  // namespace wrand
