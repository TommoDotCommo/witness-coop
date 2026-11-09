// The variant third_party/star-randomizer (Parker's (MrP4rK3r2) Star Randomizer), compiled in namespace v_star (variant.h); this is its glue.
#include "variant_prelude.h"
namespace v_star {
#include "../../../third_party/star-randomizer/src/Randomizer.h"
#include "../../../third_party/star-randomizer/src/Panel.h"
#include "../../../third_party/star-randomizer/src/Generate.h"
#include "../../../third_party/star-randomizer/src/Special.h"
#include "../../../third_party/star-randomizer/src/Watchdog.h"
#include "../../../third_party/star-randomizer/src/Random.h"

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
    PanelExtractionTests::Reset(); Point::pillarWidth = 0;
#endif
    Random::seed((int)(spec.seed & 0x7FFFFFFFu));
    Randomizer rz;
    rz.seed = (int)(spec.seed & 0x7FFFFFFFu); rz.seedIsRNG = false;
    if (spec.mode == ::wrand::kStar) rz.GenerateNormal(nullptr);
    else throw std::logic_error("v_star: not one of its modes");
    if (spec.fast) rz.AdjustSpeed();
    std::vector<::wrand::Dog> dogs;
    for (Watchdog* w : g_started) dogs.push_back(WRAND_DOG(w));
    return dogs;
}
}  // namespace v_star

std::vector<wrand::Dog> wrand::RunStar(const Spec& spec) { return v_star::Run(spec); }
