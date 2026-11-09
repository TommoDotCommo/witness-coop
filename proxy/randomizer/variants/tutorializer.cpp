// The variant third_party/tutorializer (hatkirby's Tutorializer: almost every panel becomes the first tutorial puzzle), compiled in namespace
// v_tutorializer (variant.h) with its own Memory (tutorializer_inside.h); this is its glue. It has no seed and no watchdogs.
#include "variant_prelude.h"
namespace v_tutorializer {
#include "tutorializer_inside.h"
#include "../../../third_party/tutorializer/src/Randomizer.h"

static std::vector<::wrand::Dog> Run(const ::wrand::Spec& spec) {
    if (spec.mode != ::wrand::kTutorializer) throw std::logic_error("v_tutorializer: not one of its modes");
    Randomizer r(std::make_shared<Memory>(L"witness64_d3d11.exe"));
    r.Randomize();
    return {};
}
}  // namespace v_tutorializer

std::vector<wrand::Dog> wrand::RunTutorializer(const Spec& spec) { return v_tutorializer::Run(spec); }
