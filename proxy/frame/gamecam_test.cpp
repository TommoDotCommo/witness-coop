// Game-free test of the field of view taken from the game's projection matrix. An independent reference projects points with the
// matrix form the game uses (x scale cot(half fov) / aspect, y scale cot(half fov)); the model's camera, set up from only the two
// scale terms, must land every point on the same normalized screen position, for several fields of view, aspects and view angles.
#include "../gamecam.h"
#include "../model/model.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s: ", #c); printf(__VA_ARGS__); printf("\n"); ++g_fail; } } while (0)
int main() {
    const float pi = 3.14159265f;
    const float fovs[] = {30, 40, 54, 60, 70, 90}, aspects[] = {1.0f, 4.0f / 3, 16.0f / 9, 21.0f / 9, 1366.0f / 768};
    const float angs[][2] = {{0, 0}, {1.2f, -0.3f}, {-2.5f, 0.4f}, {3.0f, -1.0f}, {0.5f, 1.2f}};
    const float cam[3] = {-188.1f, -155.6f, 3.7f};
    unsigned n = 0; float worst = 0;
    for (float fov : fovs) for (float asp : aspects) {
        float cot = 1.0f / tanf(fov * pi / 360.0f), sy = cot, sx = -cot / asp;      // the game's matrix carries a negative x scale
        GameProj g; CHECK(GameProjFromScales(sx, sy, &g), "scales for %g degrees were refused", fov);
#ifdef WG_MUT_OLD
        g.vfovDeg = 60.0f;
#endif
#ifdef WG_MUT_SWAP
        GameProjFromScales(sy, sx, &g);
#endif
        CHECK(std::fabs(g.vfovDeg - fov) < 0.01f || false, "fov %g read as %g", fov, g.vfovDeg);
        CHECK(std::fabs(g.matrixAspect - asp) < 1e-3f, "aspect %g read as %g", asp, g.matrixAspect);
        for (const auto& a : angs) for (int i = 0; i < 12; ++i) {
            float th = a[0], ph = a[1];
            float fwd[3] = {cosf(ph) * cosf(th), cosf(ph) * sinf(th), sinf(ph)}, right[3] = {sinf(th), -cosf(th), 0}, up[3] = {-sinf(ph) * cosf(th), -sinf(ph) * sinf(th), cosf(ph)};
            float f = 3.0f + 4.0f * i, r = (i % 5 - 2) * 0.35f * f * tanf(fov * pi / 360.0f) * asp, u = (i % 3 - 1) * 0.6f * f * tanf(fov * pi / 360.0f);
            float w[3]; for (int k = 0; k < 3; ++k) w[k] = cam[k] + fwd[k] * f + right[k] * r + up[k] * u;
            float refx = (-sx * r) / f, refy = (sy * u) / f;      // clip / w with the sign flip the game's x axis has undone
            wm::Cam c = {{cam[0], cam[1], cam[2]}, th, ph, g.vfovDeg, asp, 0.1f, 2000.0f}; float nx, ny;
            CHECK(wm::ProjectPoint(c, w, &nx, &ny), "point behind the camera");
            worst = std::fmax(worst, std::fmax(std::fabs(nx - refx), std::fabs(ny - refy))); ++n;
        }
    }
    CHECK(worst < 1e-4f, "screen position differs from the matrix reference by %g (NDC)", worst);
    GameProj g;
    CHECK(!GameProjFromScales(0, 0, &g) && !GameProjFromScales(NAN, 1.9f, &g) && !GameProjFromScales(1.1f, 100.0f, &g) && !GameProjFromScales(1.1f, 0.1f, &g), "implausible matrices must be refused");
    if (g_fail) return 1;
    printf("PASS game fov from matrix scales: %u points over %zu fovs, %zu aspects, worst NDC error %.2g\n", n, sizeof fovs / sizeof *fovs, sizeof aspects / sizeof *aspects, worst);
}
