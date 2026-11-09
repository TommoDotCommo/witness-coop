#pragma once
// One run of a built-in randomizer, game-free: the vendored generator (third_party/witness-randomizer) makes the puzzles of a Spec into an Overlay over
// the game's memory (Raw), from the game's own panel data, without writing the game. The caller applies the overlay on the render thread and then
// ticks the watchdogs there (game_randomizer.cpp). The same Spec over the same game data gives the same fingerprint on every PC.
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "backend.h"
#include "../../common/randomizers.h"
#include "variant.h"

namespace wrand {
struct Result {
    bool ok = false;
    std::string error;                          // why not, in words (an exception from the generator)
    std::unique_ptr<Overlay> overlay;
    std::vector<Dog> watchdogs;                 // started by the generator (upstream: one thread each); the render thread runs them
    uint64_t fingerprint = 0;
    size_t entities = 0, bytes = 0, arrays = 0;
    std::string lastStatus;
};
// status: upstream's progress line ("Randomizing Swamp"), from the calling thread. Only one Generate runs at a time (upstreams keep statics).
// The variant that makes the spec's mode does the work (variant.h).
// cancel: when set (from another thread), the generator's next read throws and the run ends with an error (Ascension's mountaintop can loop for ever).
Result Generate(const Spec& spec, Raw* raw, Arrays* arrays, const std::function<void(const std::string&)>& status = nullptr, const std::atomic<bool>* cancel = nullptr);
}  // namespace wrand
