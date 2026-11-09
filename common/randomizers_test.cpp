// Game-free (T0): the randomizer choice as text (common/randomizers.h), which the launcher, the lobby, the game and the save's sidecar all read.
// Mutant RZ_MUT_SEED_LOST (the seed is not written) must fail.
#include "randomizers.h"
#include <cstdio>
static int failures;
static void check(bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); ++failures; } }
using namespace wrand;
int main() {
    for (const auto& i : Randomizers()) {
        Spec s; s.mode = i.mode; s.seed = 48213; s.shuffle = true; s.colorblind = true; s.fast = true;
        Spec b = ParseSpec(SpecText(s));
        check(b.mode == s.mode && b.seed == 48213 && b.colorblind && b.fast && b.shuffle == i.canShuffle, "every randomizer and option survives the text");
        check(!i.credits.empty() && !CreditNames(i.mode).empty(), "every randomizer names who made it");
    }
    check(SpecText(ParseSpec("expert;seed:7;fast")) == "expert;seed:7;fast", "a spec reads back the same");
    check(!ParseSpec("").on() && !ParseSpec("off").on() && !ParseSpec("sudoku;seed:1").on() && SpecText(Spec()) == "off", "empty, off and unknown are no randomizer");
    check(ParseSpec("normal;seed:2147483647").seed == 2147483647u && ParseSpec("normal;seed:2147483648").seed == 0 && ParseSpec("normal;seed:12x").seed == 0, "a seed is 0 to 2^31-1, anything else 0");
    check(!ParseSpec("variety;seed:3;shuffle").shuffle, "Variety has no panel shuffle");
    check(ParseSpec(" normal ; seed:5 ; wobble ; fast\r").fast && ParseSpec(" normal ; seed:5 ; wobble ; fast\r").seed == 5, "spaces and unknown words are ignored");
    check(SpecSummary(ParseSpec("expert;seed:48213;fast")) == "Sigma's Randomizer: Expert, seed 48213, faster doors" && SpecSummary(Spec()) == "No randomizer", "the summary");
    check(CreditNames(kNormal) == "sigma144 and jbzdarkid" && CreditNames(kVariety) == "Penguin12335, sigma144 and jbzdarkid", "credit names");
    std::printf(failures ? "randomizers: %d FAILED\n" : "randomizers: all checks passed\n", failures);
    return failures ? 1 : 0;
}
