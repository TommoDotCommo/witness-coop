// Game-free (T0) test of local body-overlap fading (ghost.h).
#include "ghost.h"
#include <cstdio>
using namespace wghost;
static int fails = 0, checks = 0;
static void check(const char* what, bool ok) { ++checks; if (!ok) { ++fails; printf("FAILED: %s\n", what); } }
int main() {
    Params p; const float cam[3] = {0, 0, 1.7f}, fwd[3] = {1, 0, 0}; const float panel[3] = {10, 0, 1.5f};
    auto at = [&](float x, float y, float z) { float f[3] = {x, y, z}; return Alpha(cam, fwd, f, panel, 1, p); };
    check("bodies occupying the viewer fade", at(0, 0, 0) == p.minAlpha);
    check("a narrow body intersection fades", at(0.25f, 0, 0) == p.minAlpha);
    check("the fade edge is partial and smooth", at(0.47f, 0, 0) > p.minAlpha && at(0.47f, 0, 0) < 1.0f);
    check("ordinary shoulder-to-shoulder distance is fully solid", at(0.65f, 0, 0) == 1.0f && at(0, 0.65f, 0) == 1.0f);
    check("standing between the camera and a puzzle never fades", at(2, 0, 0) == 1.0f && at(5, 0, 0) == 1.0f && at(9, 0, 0) == 1.0f);
    check("standing beside the camera without overlapping never fades", at(0, 1.0f, 0) == 1.0f && at(-0.7f, 0.3f, 0) == 1.0f);
    float f[3] = {2, 0, 0};
    check("panels and view direction are irrelevant", Alpha(cam, fwd, f, nullptr, 0, p) == 1.0f);
    const float back[3] = {-1, 0, 0};
    check("looking away does not change alpha", Alpha(cam, back, f, panel, 1, p) == at(2, 0, 0));
    check("a body above the viewer stays solid", at(0, 0, 2.0f) == 1.0f);
    check("a body below the viewer stays solid", at(0, 0, -2.0f) == 1.0f);
    check("vertical overlap still fades", at(0, 0, 0.5f) == p.minAlpha && at(0, 0, -0.5f) == p.minAlpha);
    float upper = cam[2] + p.headroom, lower = cam[2] - p.eyeHeight - p.bodyHeight;
    check("vertical grazing is partial on both ends", at(0, 0, upper - .08f) > p.minAlpha && at(0, 0, upper - .08f) < 1 && at(0, 0, lower + .08f) > p.minAlpha && at(0, 0, lower + .08f) < 1);
    check("vertical boundaries are continuous", std::fabs(at(0, 0, upper - .001f) - at(0, 0, upper + .001f)) < .001f && std::fabs(at(0, 0, lower - .001f) - at(0, 0, lower + .001f)) < .001f);
    float prev = 0.0f; bool mono = true;
    for (float x = 0; x <= 0.8f; x += 0.01f) { float a = at(x, 0, 0); if (a < prev - 1e-6f) mono = false; prev = a; }
    check("alpha rises monotonically out of overlap", mono);
    check("Approach fades and restores without overshoot", Approach(1.0f, p.minAlpha, 0.1f, 5.0f) == 0.5f && Approach(0.3f, p.minAlpha, 0.1f, 5.0f) == p.minAlpha && Approach(p.minAlpha, 1.0f, 0.1f, 5.0f) > 0.6f);
    check("Approach restores opacity quickly", Approach(p.minAlpha, 1.0f, 0.2f, 5.0f) == 1.0f);
    { float s; float a[3] = {0, 0, 0}, b[3] = {1, 0, 0}, c[3] = {0.5f, 1, 0}, d[3] = {0.5f, 1, 5}; check("segment distance: parallel and crossing cases", std::fabs(SegmentDistance(a, b, c, d, &s) - 1.0f) < 1e-5f && std::fabs(s - 0.5f) < 1e-4f); }
    printf("%d checks, %d failed\n", checks, fails); return fails ? 1 : 0;
}
