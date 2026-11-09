// The variant third_party/despair-randomizer (chuukunn's Despair Randomizer: Symbols and Despair), compiled in namespace v_despair (variant.h); this is its glue.
#include "variant_prelude.h"
namespace v_despair {
#include "../../../third_party/despair-randomizer/src/Randomizer.h"
#include "../../../third_party/despair-randomizer/src/Panel.h"
#include "../../../third_party/despair-randomizer/src/Generate.h"
#include "../../../third_party/despair-randomizer/src/Special.h"
#include "../../../third_party/despair-randomizer/src/Watchdog.h"
#include "../../../third_party/despair-randomizer/src/Random.h"

// What upstream keeps in statics between runs, back to its start (a second run in one game must make what a partner's first run made). Panel
// names PanelExtractionTests as a friend (upstream's test, not built here), which reaches its private statics without changing upstream's files.
class PanelExtractionTests { public: static void Reset() { Panel::generatedPanels.clear(); Panel::arrowPuzzles.clear(); } };

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
    PanelExtractionTests::Reset(); Point::pillarWidth = 0;
#endif
    Random::seed((int)(spec.seed & 0x7FFFFFFFu));
    Randomizer rz;
    rz.seed = (int)(spec.seed & 0x7FFFFFFFu); rz.seedIsRNG = false; rz.colorblind = spec.colorblind; rz.doubleMode = spec.shuffle;
    if (spec.mode == ::wrand::kDespairSymbols) rz.GenerateNormal(nullptr);
    else if (spec.mode == ::wrand::kDespair) rz.GenerateHard(nullptr);
    else throw std::logic_error("v_despair: not one of its modes");
    if (spec.fast) rz.AdjustSpeed();
    std::vector<::wrand::Dog> dogs;
    for (Watchdog* w : g_started) dogs.push_back(WRAND_DOG(w));
    return dogs;
}
}  // namespace v_despair

std::vector<wrand::Dog> wrand::RunDespair(const Spec& spec) { return v_despair::Run(spec); }
