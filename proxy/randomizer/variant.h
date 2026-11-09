#pragma once
// The vendored randomizers ("variants"): each is a fork of the same Witness randomizer, vendored nearly as upstream has it under third_party/ and
// compiled in a C++ namespace of its own (their classes share names: Panel, Generate, Watchdog, ...). The build wraps every vendored .cpp in its
// namespace (proxy/randomizer/Makefile, proxy/Makefile: build/variants/<v>/<file>.cpp), with variant_prelude.h before it; each variant's glue
// (variants/<v>.cpp) resets what upstream keeps between runs, seeds it, runs the mode and hands back its watchdogs as Dogs.
#include <functional>
#include <string>
#include <vector>
#include "../../common/randomizers.h"
#include "../../common/wpuzzle.h"

namespace wrand {
// A watchdog of any variant, as the game runs it on the render thread (upstream: a thread of its own each): what it does, how often, whether it is done.
struct Dog { std::function<void()> act; std::function<float()> interval; std::function<bool()> done; std::function<void()> stop; };
// Makes the puzzles of `spec` through Memory::get() (the calling thread's backend: generate.cpp sets it up); throws when the generator fails.
using VariantRun = std::vector<Dog> (*)(const Spec& spec);
VariantRun FindVariant(Mode m);       // nullptr: no variant makes this mode
// Older upstreams report progress with SetWindowText on their window: the prelude sends it here (the calling thread's status, backend.h)
void StatusW(const wchar_t* text);

std::vector<Dog> RunNsv(const Spec& spec);          // third_party/witness-randomizer: Normal, Expert, Variety
std::vector<Dog> RunVariety(const Spec& spec);      // third_party/variety-randomizer: Ascension
std::vector<Dog> RunDespair(const Spec& spec);      // third_party/despair-randomizer: Symbols, Despair
std::vector<Dog> RunShuffler(const Spec& spec);     // third_party/symbol-shuffler: Symbol Shuffler, Emptiness, Puzzle Pack
std::vector<Dog> RunStar(const Spec& spec);         // third_party/star-randomizer: Star
std::vector<Dog> RunTutorializer(const Spec& spec); // third_party/tutorializer: Tutorializer
std::vector<Dog> RunCustom(const Spec& spec);       // the player's own puzzles (common/wpuzzle.h), written by third_party/witness-randomizer's Panel (variants/nsv.cpp)
// Where the player's puzzle sets come from: the text of the set with this hash, or empty when there is none. The DLL reads the puzzles folder
// (game_randomizer.cpp), the tests a fixture. Set before the first Generate.
extern std::string (*LoadPuzzleSet)(const std::string& hash);
// A panel read back into the editor's model (through the current backend), for the tests: what the game holds.
wpz::Puzzle ReadCustomPuzzle(int id);
}  // namespace wrand

// Inside a variant's namespace: a Dog for one of its watchdogs (any class with action(), sleepTime and terminate; never deleted, as upstream).
#define WRAND_DOG(w) ::wrand::Dog{[w] { w->action(); }, [w] { return (float)w->sleepTime; }, [w] { return (bool)w->terminate; }, [w] { w->terminate = true; }}
