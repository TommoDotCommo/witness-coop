#pragma once
#include <algorithm>
#include <cmath>

struct UiColor { float r, g, b, a; };
inline UiColor Rgba(float r, float g, float b, float a = 1) { return {r, g, b, a}; }

namespace uitheme {
constexpr UiColor Canvas{0.030f, 0.040f, 0.037f, 0.64f};
constexpr UiColor Surface{0.070f, 0.088f, 0.078f, 0.95f};
constexpr UiColor Raised{0.105f, 0.125f, 0.105f, 0.94f};
constexpr UiColor Inset{0.040f, 0.058f, 0.052f, 0.90f};
constexpr UiColor Border{0.52f, 0.49f, 0.39f, 0.62f};
constexpr UiColor Text{0.95f, 0.93f, 0.86f, 1.0f};
constexpr UiColor Muted{0.76f, 0.75f, 0.68f, 1.0f};
constexpr UiColor Accent{0.96f, 0.78f, 0.47f, 1.0f};
constexpr UiColor Hover{0.19f, 0.23f, 0.17f, 0.97f};
constexpr UiColor Selected{0.31f, 0.28f, 0.17f, 0.98f};
constexpr UiColor Good{0.67f, 0.85f, 0.66f, 1.0f};
constexpr UiColor Danger{1.0f, 0.67f, 0.60f, 1.0f};
constexpr UiColor Cool{0.65f, 0.82f, 0.81f, 1.0f};
inline float Luminance(UiColor c) {
    auto linear = [](float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    return 0.2126f * linear(c.r) + 0.7152f * linear(c.g) + 0.0722f * linear(c.b);
}
inline float Contrast(UiColor a, UiColor b) {
    float x = Luminance(a), y = Luminance(b);
    return (std::max(x, y) + 0.05f) / (std::min(x, y) + 0.05f);
}
} // namespace uitheme
