// Game-free (T0) test of the see-through geometry (ghost.h).
#include "ghost.h"
#include <cstdio>
using namespace wghost;
static int fails = 0, checks = 0;
static void check(const char* what, bool ok) { ++checks; if (!ok) { ++fails; printf("FAILED: %s\n", what); } }
int main() {
    Params p; const float cam[3] = {0, 0, 1.7f}, fwd[3] = {1, 0, 0}; const float panel[3] = {10, 0, 1.5f};
    auto at = [&](float x, float y, float z) { float f[3] = {x, y, z}; return InTheWay(cam, fwd, f, panel, 1, p); };      // feet at (x, y, z)
    check("on the line, halfway to the panel: a ghost", std::fabs(at(5, 0, 0.0f) - p.minAlpha) < 1e-4f);
    check("2 m to the side: solid", at(5, 2.0f, 0.0f) == 1.0f);
    check("1 m to the side (within the body and the margin): fading but not gone", at(5, 0.8f, 0.0f) > p.minAlpha + 0.05f && at(5, 0.8f, 0.0f) < 1.0f);
    float prev = 0; bool mono = true; for (float y = 0; y <= 2.0f; y += 0.05f) { float a = at(5, y, 0.0f); if (a < prev - 1e-6f) mono = false; prev = a; }
    check("the further from the line, the more solid, never the other way", mono);
    check("behind the panel (further than it): solid", at(14, 0, 0.0f) == 1.0f);
    check("standing at the panel itself: solid", at(9.9f, 0, 0.0f) == 1.0f);
    check("just in front of the panel (1 m): a ghost", at(9.0f, 0, 0.0f) < 0.3f);
    check("behind the camera: solid", at(-3, 0, 0.0f) == 1.0f);
    check("on the line but with the feet below and the head above it: still a ghost (a whole body, not a point)", at(5, 0, -0.5f) < 0.3f);
    { const float turned[3] = {-1, 0, 0}; float f[3] = {5, 0, 0}; check("the panel is behind where the camera points: nobody is in the way", InTheWay(cam, turned, f, panel, 1, p) == 1.0f); }
    { const float far_[3] = {60, 0, 1.5f}; float f[3] = {30, 0, 0}; check("a panel 60 m away is not looked at", InTheWay(cam, fwd, f, far_, 1, p) == 1.0f); }
    { const float two[6] = {10, 0, 1.5f, 10, 30, 1.5f}; float f[3] = {5, 0, 0}; check("with more panels the worst one counts", InTheWay(cam, fwd, f, two, 2, p) < 0.3f); }
    { float f[3] = {5, 0, 0}; check("no panels: solid", InTheWay(cam, fwd, f, nullptr, 0, p) == 1.0f); }
    check("a player in your face fades: a ghost at 0.4 m, solid at 2 m", NearAlpha(0.4f, p) == p.minAlpha && NearAlpha(2.0f, p) == 1.0f && NearAlpha(1.0f, p) > p.minAlpha && NearAlpha(1.0f, p) < 1.0f);
    { float f[3] = {0.5f, 0.0f, 0.0f}, g[3] = {4.0f, 3.0f, 0.0f}; check("Alpha combines: a player beside the camera is a ghost with no panel at all, one 5 m away is solid", Alpha(cam, fwd, f, nullptr, 0, p) < 0.5f && Alpha(cam, fwd, g, nullptr, 0, p) == 1.0f); }
    check("Approach moves a step at a time and stops on the target", Approach(1.0f, 0.2f, 0.1f, 4.0f) == 0.6f && Approach(0.3f, 0.2f, 0.1f, 4.0f) == 0.2f && Approach(0.2f, 1.0f, 0.1f, 4.0f) == 0.6f);
    { float s; float a[3] = {0, 0, 0}, b[3] = {1, 0, 0}, c[3] = {0.5f, 1, 0}, d[3] = {0.5f, 1, 5}; check("segment distance: parallel and crossing cases", std::fabs(SegmentDistance(a, b, c, d, &s) - 1.0f) < 1e-5f && std::fabs(s - 0.5f) < 1e-4f); }
    printf("%d checks, %d failed\n", checks, fails); return fails ? 1 : 0;
}
