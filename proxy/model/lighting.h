// The adventurer's light: one place for the numbers that make a remote player look lit by the island's own sun and sky.
// model_gpu.cpp's pixel shader takes these constants (HlslDefs) and the game-free test (tests/lighting_test.cpp) checks the
// same maths in C++ (LitDisplay mirrors the shader's diffuse path), so a rig that goes back to a dim figure is caught without a game.
//
// Measured against the game's daylight (2026-11-05, in-game pictures next to grass and sandstone walls): with the old defaults
// (sun 0.55, sky 0.30) a mid-tone coat came out about a third as bright as the world beside it, and the old straight clip made
// bright felt and skin go flat. The new key is the island's high warm sun, the sky fill is strong and blue from above, the ground
// bounces sandstone from below, and a soft shoulder rolls highlights off instead of clipping them.
#pragma once
#include <cmath>
#include <cstdio>
#include <string>

namespace wl {
constexpr float kSun = 1.30f;                          // model_sun: the key's strength (linear light, before the albedo)
constexpr float kAmbient = 0.90f;                      // model_ambient: the sky/ground hemisphere's strength
constexpr float kSunColour[3] = {1.00f, 0.96f, 0.88f}; // a warm white
constexpr float kSky[3] = {0.50f, 0.58f, 0.70f};       // the sky's blue, from above
constexpr float kGround[3] = {0.34f, 0.30f, 0.24f};    // the sandstone's bounce, from below
constexpr float kRim = 0.16f;                          // the grazing-angle edge light that lifts the silhouette off the scene
constexpr float kShoulder = 0.62f;                     // linear values above this roll off smoothly toward 1 instead of clipping

inline float Shoulder(float x) {      // identity up to kShoulder, then an exponential approach to 1 (continuous, with the same slope at the knee)
    if (x <= kShoulder) return x;
    const float r = 1.0f - kShoulder; return kShoulder + r * (1.0f - std::exp(-(x - kShoulder) / r));
}
inline float ToSrgb(float l) { return l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f; }

// The shader's diffuse path for one grey channel of albedo (linear), a surface whose normal makes nDotL with the sun and has
// vertical component nz: sun + hemisphere, then the shoulder, then the display encoding. wrap: how far the light goes round the terminator.
inline float LitDisplay(float albedo, float nDotL, float nz, float sun = kSun, float ambient = kAmbient, float wrap = 0.12f, float ao = 1.0f) {
    const float diff = std::fmax(0.0f, std::fmin(1.0f, (nDotL + wrap) / (1.0f + wrap)));
    const float up = nz * 0.5f + 0.5f;
    const float sky = kGround[1] + (kSky[1] - kGround[1]) * up;      // the green channel stands for the grey
    const float lin = albedo * (diff * sun * kSunColour[1] + sky * ambient * ao);
    return ToSrgb(Shoulder(lin));
}

// The constants as HLSL, put in front of the shader source so that C++ and the shader cannot disagree.
inline std::string HlslDefs() {
    char b[512];
    snprintf(b, sizeof b,
             "#define SUN_COLOUR float3(%.5f,%.5f,%.5f)\n#define SKY_COLOUR float3(%.5f,%.5f,%.5f)\n#define GROUND_COLOUR float3(%.5f,%.5f,%.5f)\n"
             "#define RIM_STRENGTH %.5f\n#define SHOULDER_KNEE %.5f\n",
             kSunColour[0], kSunColour[1], kSunColour[2], kSky[0], kSky[1], kSky[2], kGround[0], kGround[1], kGround[2], kRim, kShoulder);
    return b;
}
}  // namespace wl
