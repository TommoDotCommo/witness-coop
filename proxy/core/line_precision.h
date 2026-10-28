#pragma once
#include <cmath>
#include <cstdint>
namespace witness {
// WTL6 uses millimetres; WTL7 uses 1/20 mm for panels within +/-1.6 m.
// The format is per line: never mix units within one polyline.
inline float LineUnit(bool fine) { return fine ? 0.00005f : 0.001f; }
inline bool LineFitsFine(const float p[3]) {
    for (int k = 0; k < 3; ++k) if (!std::isfinite(p[k]) || std::fabs(p[k]) > 1.6f) return false;
    return true;
}
inline int16_t LineQuantize(float p, bool fine) {
#ifdef WL_MUT_COARSE
    if (fine) return int16_t(std::lround(p * 1000.f) * 20);
#endif
    const float v = p / LineUnit(fine);
    return int16_t(std::lround(v > 32000 ? 32000 : v < -32000 ? -32000 : v));
}
inline int16_t LineLegacy(int16_t p, bool fine) {
    return fine ? int16_t(std::lround(double(p) / 20.0)) : p;
}
}
