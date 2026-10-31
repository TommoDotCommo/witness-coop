// Game-free (T0) test of the second line of a symmetry panel: the table of Symmetry Island Right 1 and a panel whose surface is tilted.
#include "mirror.h"
#include <cstdio>
using namespace wmirror;
static int fails = 0, checks = 0;
static void check(const char* what, bool ok) { ++checks; if (!ok) { ++fails; printf("FAILED: %s\n", what); } }
static float dist(const float* a, const float* b) { return std::sqrt((a[0]-b[0])*(a[0]-b[0]) + (a[1]-b[1])*(a[1]-b[1]) + (a[2]-b[2])*(a[2]-b[2])); }
int main() {
    // Symmetry Island Right 1 (panel 0x22), read from a real game: 20 nodes, v flips (node k <-> k^3 inside a column), two stubs on the right edge
    const float uv[40] = {.08f,.92f, .08f,.64f, .08f,.36f, .08f,.08f, .36f,.92f, .36f,.64f, .36f,.36f, .36f,.08f, .64f,.92f, .64f,.64f, .64f,.36f, .64f,.08f, .92f,.92f, .92f,.64f, .92f,.36f, .92f,.08f, .97f,.64f, .97f,.36f, .5f,.92f, .5f,.08f};
    const int mir[20] = {3,2,1,0,7,6,5,4,11,10,9,8,15,14,13,12,17,16,19,18};
    Flip fl = DetectFlip(20, uv, mir);
    check("the table of panel 0x22 is a flip of v about 0.5", fl.ok && !fl.u && fl.v && std::fabs(fl.cv - .5f) < 1e-3f);
    { const int rot[4] = {3,2,1,0}; const float g[8] = {0,0, 1,0, 0,1, 1,1}; const int rr[4] = {3,2,1,0}; Flip r = DetectFlip(4, g, rr); (void)rot;
      check("a point reflection (node k <-> 3-k on a square) is both flips", r.ok && r.u && r.v); }
    { const int bad[4] = {1,0,3,2}; const float g[8] = {0,0, 1,0, 0,1, 1,1}; const int shuffled[4] = {2,3,0,1}; Flip a = DetectFlip(4, g, bad), b = DetectFlip(4, g, shuffled);
      check("neighbouring pairs on a square are a flip of u", a.ok && a.u && !a.v); check("opposite pairs the other way are a flip of v", b.ok && !b.u && b.v); }
    // the real panel's map in its local frame, as fitted from a trace: o, a, b (b is tilted 4.3 degrees out of the surface plane)
    Fit truth; truth.ok = true; truth.o[0] = .2269f; truth.o[1] = -.2988f; truth.o[2] = -.3475f; truth.a[1] = .5992f; truth.b[0] = .0451f; truth.b[2] = .6f;
    // a line drawn on it: nodes 1,0,4,18,8,12,13,16 (the real trace)
    int path[8] = {1, 0, 4, 18, 8, 12, 13, 16}; std::vector<Sample> s; for (int k : path) { Sample q{uv[2*k], uv[2*k+1], {}}; At(truth, q.u, q.v, q.p); s.push_back(q); }
    Fit f = FitSamples(s, nullptr);
    check("the fit finds the tilted surface exactly from the nodes of the line", f.ok && f.exact && dist(f.b, truth.b) < 1e-3f && dist(f.a, truth.a) < 1e-3f && dist(f.o, truth.o) < 1e-3f);
    float worst = 0; for (int i = 0; i < 8; ++i) { float r[3], want[3]; At(truth, uv[2*mir[path[i]]], uv[2*mir[path[i]]+1], want); if (!Reflect(f, fl, s[i].p, r)) { worst = 9; break; } worst = std::fmax(worst, dist(r, want)); }
    check("every node of the line is reflected onto its mirror node (within 1 mm)", worst < 1e-3f);
    { float tip[3], r[3], want[3]; float u = .08f + .5f * (.36f - .08f), v = .92f; At(truth, u, v, tip); At(truth, u, 1 - v, want); check("a tip half way along an edge is reflected half way along the mirror edge", Reflect(f, fl, tip, r) && dist(r, want) < 1e-3f); }
    { float p0[3], r[3]; At(truth, uv[2 * 1], uv[2 * 1 + 1], p0); Reflect(f, fl, p0, r); check("the reflected line is not the line (mirror image is a different place)", dist(p0, r) > .1f); }
    // the start of a line: two nodes on one grid line, so the surface is not known; a fit remembered from before is used, else a flat one
    std::vector<Sample> two = {s[0], s[1]}; Fit g = FitSamples(two, &f);
    float r2[3], want2[3]; At(truth, uv[2*mir[1]], uv[2*mir[1]+1], want2);
    check("with two nodes and a remembered fit the reflection is still exact", g.ok && !g.exact && Reflect(g, fl, s[0].p, r2) && dist(r2, want2) < 1e-3f);
    Fit h = FitSamples(two, nullptr); float r3[3]; At(truth, uv[2*mir[1]], uv[2*mir[1]+1], want2);
    check("with two nodes and nothing remembered the flat guess stays within the tilt (5 cm) of the right place", h.ok && Reflect(h, fl, s[0].p, r3) && dist(r3, want2) < .05f);
    printf("%d checks, %d failed\n", checks, fails); return fails ? 1 : 0;
}
