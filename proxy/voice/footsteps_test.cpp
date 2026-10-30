// T0 test of the footsteps (footsteps.h): plants from the gait phase, and what a step sounds like where. Exit 0 = all passed; -DWV_MUT_FS_xxx must fail.
#include "footsteps.h"
#include <cmath>
#include <cstdio>
#include <vector>

static int g_fail, g_n;
#define CHECK(c, ...) do { g_n++; if (!(c)) { g_fail++; printf("FAIL line %d: %s | ", __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

// renders `ms` milliseconds of the foley alone and returns per-20 ms-frame RMS of left and right
static void Render(wvoice::Foley& f, int ms, std::vector<float>* rmsL, std::vector<float>* rmsR, float* peak) {
    int frames = ms / 20; *peak = 0;
    for (int k = 0; k < frames; k++) {
        float st[wvoice::kFrame * 2] = {}; f.Render(st, wvoice::kFrame);
        double l = 0, r = 0; for (int i = 0; i < wvoice::kFrame; i++) { l += st[i * 2] * st[i * 2]; r += st[i * 2 + 1] * st[i * 2 + 1]; *peak = std::max(*peak, std::max(std::fabs(st[i * 2]), std::fabs(st[i * 2 + 1]))); }
        rmsL->push_back((float)std::sqrt(l / wvoice::kFrame)); rmsR->push_back((float)std::sqrt(r / wvoice::kFrame));
    }
}

int main() {
    // ---- the tracker: a 1 s walk cycle at 60 fps gives two plants a cycle, alternating feet; standing gives none; a reversed cycle still steps
    {
        wvoice::FootstepTracker t; int steps = 0, lefts = 0, rights = 0, foot = -1, maxPer = 0;
        for (int fr = 0; fr < 600; fr++) { float phase = fr / 60.0f; int n = t.Step(phase, true, &foot); steps += n; maxPer = std::max(maxPer, n); if (n) (foot ? rights : lefts)++; }
        CHECK(steps >= 19 && steps <= 20 && lefts >= 9 && rights >= 9 && maxPer == 1, "10 s of walking: %d plants (%d left, %d right), at most %d a frame", steps, lefts, rights, maxPer);
        wvoice::FootstepTracker s; int still = 0; for (int fr = 0; fr < 600; fr++) still += s.Step(0.3f, false, nullptr);
        CHECK(still == 0, "standing still made %d plants", still);
        wvoice::FootstepTracker r; int rev = 0; for (int fr = 0; fr < 600; fr++) rev += r.Step(5.0f - fr / 60.0f, true, nullptr);
        CHECK(rev >= 19 && rev <= 20, "a reversed walk cycle: %d plants", rev);
        wvoice::FootstepTracker run; int runs = 0; for (int fr = 0; fr < 600; fr++) runs += run.Step(fr / 60.0f / 0.7f, true, nullptr);
        CHECK(runs >= 27 && runs <= 29, "10 s of running (0.7 s cycle): %d plants", runs);
        // the first frame of a walk does not step at once; a frame across the right plant counts it, one across the wrap counts the left
        wvoice::FootstepTracker j; int first = j.Step(0.4f, true, &foot); int one = j.Step(0.6f, true, &foot); int rightFoot = foot; int wrap = j.Step(0.05f, true, &foot);
        CHECK(first == 0 && one == 1 && rightFoot == 1 && wrap == 1 && foot == 0, "walk start %d, across 0.5: %d (foot %d), across the wrap: %d (foot %d)", first, one, rightFoot, wrap, foot);
    }
    // ---- the sound: a step 2 m to the right is a short burst, louder than one at 40 m, panned right; beyond the range it is silent; many steps never clip
    {
        wvoice::Settings s; s.range = 25; s.volume = 1;
        wvoice::Foley f; f.Plant(2.0f, 1.2f, 1.3f, false, s);
        std::vector<float> L, R; float peak; Render(f, 400, &L, &R, &peak);
        float first = std::max(L[0], R[0]), late = std::max(L[15], R[15]);
        CHECK(peak > 0.04f && peak < 0.7f, "peak %.3f of a step 2 m away", peak);
        CHECK(first > 0.01f && late < first * 0.02f, "a step rings on: frame 0 rms %.4f, 300 ms later %.5f", first, late);
        int loud = 0; for (size_t k = 0; k < L.size(); k++) if (std::max(L[k], R[k]) > first * 0.05f) loud++;
        CHECK(loud >= 3 && loud <= 10, "the burst lasts %d frames of 20 ms", loud);
        CHECK(R[0] > L[0] * 1.5f, "a walker to the right: left rms %.4f right %.4f", L[0], R[0]);
        CHECK(f.active.empty() && f.played == 1, "the step was retired (%zu active, %u played)", f.active.size(), f.played);
        wvoice::Foley far; far.Plant(40.0f, 0, 1.3f, false, s); std::vector<float> fl, fr2; float fpeak; Render(far, 100, &fl, &fr2, &fpeak);
        CHECK(fpeak < peak * 0.1f, "a step 40 m away peaks at %.4f, 2 m away at %.4f", fpeak, peak);
        wvoice::Foley gone; gone.Plant(80.0f, 0, 1.3f, false, s); CHECK(gone.active.empty() && gone.dropped == 1, "a step beyond the range was still played");
        wvoice::Foley left; left.Plant(2.0f, -1.2f, 1.3f, false, s); std::vector<float> ll, lr; float lp; Render(left, 40, &ll, &lr, &lp);
        CHECK(ll[0] > lr[0] * 1.5f, "a walker to the left: left rms %.4f right %.4f", ll[0], lr[0]);
        wvoice::Foley many; for (int i = 0; i < 12; i++) many.Plant(0.5f, 0, 3.0f, true, s); float voicesAndSteps[wvoice::kFrame * 2]; for (auto& v : voicesAndSteps) v = 0.9f; many.Render(voicesAndSteps, wvoice::kFrame);
        float mx = 0; for (float v : voicesAndSteps) mx = std::max(mx, std::fabs(v)); CHECK(mx <= 1.0f, "twelve steps over a loud voice peaked at %.3f", mx);
        wvoice::Foley runner; runner.Plant(2.0f, 0, 3.0f, true, s); std::vector<float> rl, rr; float rpeak; Render(runner, 40, &rl, &rr, &rpeak);
        CHECK(rpeak > peak * 1.1f, "a running step (%.3f) is louder than a walking one (%.3f)", rpeak, peak);
    }
    printf("%s: %d checks, %d failed\n", g_fail ? "FAILED" : "ok", g_n, g_fail);
    return g_fail ? 1 : 0;
}
