// Game-free (T0) test of the panel frame: a line kept in the panel's frame follows the panel when it turns and moves, and the tip of a line being drawn is part of the way along its edge.
#include "panelframe.h"
#include <cstdio>
using namespace wframe;
static int fails = 0, checks = 0;
static void check(const char* what, bool ok) { ++checks; if (!ok) { ++fails; printf("FAILED: %s\n", what); } }
static bool near3(const float* a, const float* b, float eps = 1e-3f) { return std::fabs(a[0] - b[0]) < eps && std::fabs(a[1] - b[1]) < eps && std::fabs(a[2] - b[2]) < eps; }
int main() {
    // the numbers of the first tutorial panel, read from a real game: its trace in world coordinates, the panel's pose before and after the door has swung open
    const float pos0[3] = {-187.157f, -158.502f, 3.700f}; const Q q0 = {0, 0, -0.811f, -0.587f};
    const float posN[3] = {-186.245f, -158.905f, 3.700f}; const Q qN = {0, 0, -0.950f, 0.316f};
    const float start[3] = {-186.962f, -158.437f, 3.700f}, end[3] = {-187.352f, -158.566f, 3.700f};
    float ls[3], le[3]; ToLocal(pos0, q0, start, ls); ToLocal(pos0, q0, end, le);
    float back[3]; ToWorld(pos0, q0, ls, back); check("a world point turned into the panel's frame and back is the same point", near3(back, start));
    float len0 = std::sqrt((end[0] - start[0]) * (end[0] - start[0]) + (end[1] - start[1]) * (end[1] - start[1])), lenL = std::sqrt((le[0] - ls[0]) * (le[0] - ls[0]) + (le[1] - ls[1]) * (le[1] - ls[1]));
    check("the line keeps its length in the panel's frame (0.41 m)", std::fabs(len0 - lenL) < 1e-3f && std::fabs(len0 - 0.41f) < 0.02f);
    // the door has swung: the same local line, drawn with the panel's new pose
    float ws[3], we[3]; ToWorld(posN, qN, ls, ws); ToWorld(posN, qN, le, we);
    float lenN = std::sqrt((we[0] - ws[0]) * (we[0] - ws[0]) + (we[1] - ws[1]) * (we[1] - ws[1]));
    check("after the panel has turned and moved the line has the same length and is where the panel is", std::fabs(lenN - len0) < 2e-3f && std::fabs(ws[0] - posN[0]) < 0.6f && std::fabs(ws[1] - posN[1]) < 0.6f);
    float olx = ls[0], oly = ls[1]; float dN = std::sqrt((ws[0] - posN[0]) * (ws[0] - posN[0]) + (ws[1] - posN[1]) * (ws[1] - posN[1])), d0 = std::sqrt(olx * olx + oly * oly);
    check("its distance from the panel's origin does not change when the panel moves (it moves with it)", std::fabs(dN - d0) < 2e-3f);
    float old[3]; { float pold[3] = {pos0[0], pos0[1], pos0[2]}; for (int k = 0; k < 3; ++k) old[k] = start[k] - pold[k]; }
    float stale[3]; for (int k = 0; k < 3; ++k) stale[k] = posN[k] + old[k];      // what a translation-only ribbon (the old way) would show: the old offset on the new position
    check("the old way (world offsets added to the new position) shows the line somewhere else: more than 0.3 m off", !near3(stale, ws, 0.3f));
    { float p[3] = {1, 2, 3}, l[3], w[3]; Q q = Normalize({0.2f, -0.4f, 0.5f, 0.7f}); ToLocal(p, q, (const float[3]){2, 3, 5}, l); ToWorld(p, q, l, w); check("a tilted panel (all four parts of the quaternion) also round-trips", near3(w, (const float[3]){2, 3, 5})); }
    // the tip of a line being drawn
    float a[3] = {0, 0, 0}, b[3] = {1, 2, 0}, t[3];
    Tip(a, b, 0.25f, t); check("a quarter drawn: the tip is a quarter of the way along the edge", near3(t, (const float[3]){0.25f, 0.5f, 0}));
    Tip(a, b, 0.0f, t); check("just started: the tip is at the start", near3(t, a));
    Tip(a, b, 1.0f, t); check("finished (1.0): the tip is the end", near3(t, b));
    Tip(a, b, 7.0f, t); check("a value that is not a fraction: the whole edge", near3(t, b));
    printf("%d checks, %d failed\n", checks, fails); return fails ? 1 : 0;
}
