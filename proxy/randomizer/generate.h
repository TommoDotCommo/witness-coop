#pragma once
// One run of a built-in randomizer, game-free: the vendored generator (third_party/witness-randomizer) makes the puzzles of a Spec into an Overlay over
// the game's memory (Raw), from the game's own panel data, without writing the game. The caller applies the overlay on the render thread and then
// ticks the watchdogs there (watchdogs.h). The same Spec over the same game data gives the same fingerprint on every PC.
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "backend.h"
#include "../../common/randomizers.h"

class Watchdog;          // upstream's (third_party/witness-randomizer/src/Watchdog.h)

namespace wrand {
struct Result {
    bool ok = false;
    std::string error;                          // why not, in words (an exception from the generator)
    std::unique_ptr<Overlay> overlay;
    std::vector<Watchdog*> watchdogs;           // started by the generator (upstream: one thread each); never deleted (upstream's classes have no virtual destructor)
    uint64_t fingerprint = 0;
    size_t entities = 0, bytes = 0, arrays = 0;
    std::string lastStatus;
};
// status: upstream's progress line ("Randomizing Swamp"), from the calling thread. Only one Generate runs at a time (upstream keeps statics).
Result Generate(const Spec& spec, Raw* raw, Arrays* arrays, const std::function<void(const std::string&)>& status = nullptr);
}  // namespace wrand
