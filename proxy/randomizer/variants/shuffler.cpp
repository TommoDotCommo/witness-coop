// The variant third_party/symbol-shuffler (Dawgup's (KemSpeedruns) Symbol Shuffler: Symbol Shuffler, Emptiness, Puzzle Pack), compiled in namespace v_shuffler (variant.h); this is its glue.
#include "variant_prelude.h"
namespace v_shuffler {
#include "../../../third_party/symbol-shuffler/src/Randomizer.h"
#include "../../../third_party/symbol-shuffler/src/Panel.h"
#include "../../../third_party/symbol-shuffler/src/Generate.h"
#include "../../../third_party/symbol-shuffler/src/Special.h"
#include "../../../third_party/symbol-shuffler/src/Watchdog.h"
#include "../../../third_party/symbol-shuffler/src/Random.h"

// What upstream keeps in statics between runs, back to its start (a second run in one game must make what a partner's first run made). Panel
// names PanelExtractionTests as a friend (upstream's test, not built here), which reaches its private statics without changing upstream's files.
class PanelExtractionTests { public: static void Reset() { Panel::generatedPanels.clear(); } };

std::mt19937 Random::gen = std::mt19937(0);        // upstream's Random.cpp seeds it from the clock; every run here seeds it from the Spec
static std::vector<Watchdog*> g_started;
#ifndef WR_MUT_WATCHDOG_THREAD
void Watchdog::start() { g_started.push_back(this); }      // upstream: a thread of its own (Watchdog.cpp, left out by WITNESS_COOP)
#else
void Watchdog::start() {}
#endif
void Watchdog::run() {}

static std::vector<::wrand::Dog> Run(const ::wrand::Spec& spec) {
    g_started.clear();
#ifndef WR_MUT_NO_RESET
    PanelExtractionTests::Reset(); Point::pillarWidth = 0; Special::writeInt.clear(); Special::writeFloat.clear(); Special::writeColor.clear(); Special::writeIntVec.clear(); Special::writeFloatVec.clear(); Special::writeColorVec.clear();
#endif
    Random::seed((int)(spec.seed & 0x7FFFFFFFu));
    Randomizer rz;
    rz.seed = (int)(spec.seed & 0x7FFFFFFFu); rz.seedIsRNG = false;
    if (spec.mode == ::wrand::kShuffler) rz.GenerateNormal(nullptr);
    else if (spec.mode == ::wrand::kEmptiness) rz.GenerateHard(nullptr);
    else if (spec.mode == ::wrand::kPuzzlePack) rz.GeneratePuzzlePack(nullptr);
    else throw std::logic_error("v_shuffler: not one of its modes");
    if (spec.fast) rz.AdjustSpeed();
    std::vector<::wrand::Dog> dogs;
    for (Watchdog* w : g_started) dogs.push_back(WRAND_DOG(w));
    return dogs;
}
}  // namespace v_shuffler

std::vector<wrand::Dog> wrand::RunShuffler(const Spec& spec) { return v_shuffler::Run(spec); }
