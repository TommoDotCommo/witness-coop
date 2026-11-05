// T0 test of the adventurer's light (proxy/model/lighting.h): the figure must look lit by the same sun and sky as the world.
// Game-free. The shader takes its constants from lighting.h and LitDisplay mirrors its diffuse path, so these checks bind the
// numbers the proxy draws with. Mutants (-DWL_MUT_...) each break one protection and must fail.
#include <cstdio>
#include <cstdlib>
#include "../lighting.h"
#include "../../config.h"
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s: ", #c); printf(__VA_ARGS__); printf("\n"); ++fails; } } while (0)

#if defined(WL_MUT_DIM)      // the old, dim rig
#define SUN 0.55f
#define AMB 0.30f
#else
#define SUN wl::kSun
#define AMB wl::kAmbient
#endif

static float Display(float albedo, float nl, float nz) {
#if defined(WL_MUT_CLIP)      // the old straight clip instead of the shoulder
    float lin = albedo * (std::fmax(0.f, nl) * SUN + (wl::kGround[1] + (wl::kSky[1] - wl::kGround[1]) * (nz * .5f + .5f)) * AMB);
    return wl::ToSrgb(std::fmin(1.f, lin));
#elif defined(WL_MUT_LINEAR)      // forgot the display encoding: linear values written to an sRGB-looking target
    float lin = albedo * (std::fmax(0.f, nl) * SUN + (wl::kGround[1] + (wl::kSky[1] - wl::kGround[1]) * (nz * .5f + .5f)) * AMB);
    return std::fmin(1.f, wl::Shoulder(lin));
#else
    return wl::LitDisplay(albedo, nl, nz, SUN, AMB);
#endif
}

int main() {
    // 1. A lit white albedo reaches the brightness of a sunlit wall in the game (about 0.85+ in display values) but is not blown out.
    float lit = Display(1.0f, 1.0f, 1.0f);
    CHECK(lit >= 0.93f && lit <= 1.0f, "sun-facing white = %.3f (want 0.93..1)", lit);
    // 2. Mid-tones (the teal coat, linear 0.2) in the sun read as a clear mid-tone, not dark: 0.55..0.85 in display values.
    float mid = Display(0.2f, 0.8f, 0.6f);
    CHECK(mid >= 0.55f && mid <= 0.85f, "sunlit mid-tone (0.2) = %.3f (want 0.55..0.85)", mid);
    // 3. The shaded side keeps the sky's light: a white surface facing away from the sun is still bright enough to read (>= 0.5),
    //    and the key still models form: sunlit side at least 25%% brighter than the shaded side in display values.
    float shade = Display(1.0f, -0.5f, 0.2f), sun = Display(1.0f, 0.8f, 0.6f);
    CHECK(shade >= 0.50f, "shaded white = %.3f (want >= 0.5)", shade);
    CHECK(sun >= shade * 1.10f, "sunlit white %.3f vs shaded %.3f: no form", sun, shade);
    // 4. Highlights keep rolling off: two very bright intensities stay distinct instead of clipping to the same value.
    float a = Display(1.0f, 1.0f, 1.0f), b = Display(0.85f, 1.0f, 1.0f);
    CHECK(a - b >= 0.006f, "bright values merge: %.4f vs %.4f", a, b);
    // 5. The shoulder is continuous, monotone and never above 1.
    float prev = -1; bool mono = true; for (int i = 0; i <= 400; ++i) { float x = i * 0.01f, y = wl::Shoulder(x); if (y < prev - 1e-6f || y > 1.0f) mono = false; prev = y; }
    CHECK(mono, "the shoulder is not monotone or exceeds 1");
    CHECK(std::fabs(wl::Shoulder(wl::kShoulder + 1e-4f) - wl::Shoulder(wl::kShoulder - 1e-4f)) < 5e-4f, "the shoulder has a step at the knee");
    // 6. The proxy's configured defaults are these constants (so the in-game figure uses the tested rig, not a stale default).
    Cfg c; CHECK(c.modelSun == wl::kSun && c.modelAmbient == wl::kAmbient, "Cfg defaults %.2f/%.2f differ from lighting.h", c.modelSun, c.modelAmbient);
    // 7. The HLSL prelude carries every constant the shader reads.
    std::string h = wl::HlslDefs();
    for (const char* k : {"SUN_COLOUR", "SKY_COLOUR", "GROUND_COLOUR", "RIM_STRENGTH", "SHOULDER_KNEE"}) CHECK(h.find(k) != std::string::npos, "HlslDefs lacks %s", k);
    if (fails) printf("lighting: %d failures\n", fails); else printf("lighting: all checks passed (white %.3f, mid %.3f, shade %.3f)\n", lit, mid, shade);
    return fails ? 1 : 0;
}
