// The variant third_party/witness-randomizer (NewSoupVi's Witness Random Puzzle Generator for Archipelago): Sigma's Normal and Expert, and Variety.
// Its files are compiled in namespace v_nsv (variant.h); this is its glue.
#include "variant_prelude.h"
namespace v_nsv {
#include "../../../third_party/witness-randomizer/src/Randomizer.h"
#include "../../../third_party/witness-randomizer/src/Panel.h"
#include "../../../third_party/witness-randomizer/src/Generate.h"
#include "../../../third_party/witness-randomizer/src/Special.h"
#include "../../../third_party/witness-randomizer/src/Watchdog.h"
#include "../../../third_party/witness-randomizer/src/Random.h"

// Upstream keeps state in statics between runs (one run per process there). A second run in the same game (after a save is loaded) must start
// from the same state as the first, or it would make other puzzles than a partner's first run. Panel and Generate name these two classes as
// friends (upstream's panel test and Archipelago's watchdog, neither of which is built here), which lets the reset reach their private statics
// without changing upstream's files.
class PanelExtractionTests { public: static void Reset() { Panel::generatedPanels.clear(); Panel::arrowPuzzles.clear(); } };
class APWatchdog { public: static void Reset() { Generate::_SHAPEDIRECTIONS.clear(); } };

std::mt19937 Random::gen = std::mt19937(0);        // upstream's Random.cpp seeds it from the clock; every run here seeds it from the Spec
static std::vector<Watchdog*> g_started;
#ifndef WR_MUT_WATCHDOG_THREAD
void Watchdog::start() { g_started.push_back(this); }      // upstream: a thread of its own (Watchdog.cpp, left out by WITNESS_COOP)
#else
void Watchdog::start() {}      // mutant: the watchdogs are lost
#endif
void Watchdog::run() {}

static std::vector<::wrand::Dog> Run(const ::wrand::Spec& spec) {
    g_started.clear();
#ifndef WR_MUT_NO_RESET
    PanelExtractionTests::Reset(); APWatchdog::Reset(); Point::pillarWidth = 0; Special::correctShapesById.clear();
#endif
    Random::seed((int)(spec.seed & 0x7FFFFFFFu));
    Randomizer rz;
    rz.seed = (int)(spec.seed & 0x7FFFFFFFu); rz.seedIsRNG = false; rz.colorblind = spec.colorblind; rz.doubleMode = spec.shuffle;
    if (spec.mode == ::wrand::kNormal) rz.GenerateNormal();
    else if (spec.mode == ::wrand::kExpert) rz.GenerateHard();
    else if (spec.mode == ::wrand::kVariety) rz.GenerateVariety();
    else throw std::logic_error("v_nsv: not one of its modes");
    if (spec.fast) rz.AdjustSpeed();
    std::vector<::wrand::Dog> dogs;
    for (Watchdog* w : g_started) dogs.push_back(WRAND_DOG(w));
    return dogs;
}
}  // namespace v_nsv

std::vector<wrand::Dog> wrand::RunNsv(const Spec& spec) { return v_nsv::Run(spec); }
