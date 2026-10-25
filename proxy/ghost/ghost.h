#pragma once
// See-through players, game-free (T0 test: ghost_test.cpp). A player standing between your camera and a puzzle panel you are looking towards fades to a ghost so the puzzle can be read
// through them; so does a player standing almost in your face. How: the line from the camera to each panel in front of you (within range and a wide cone around where you look) is
// compared with the player's body (a vertical segment from the feet up): if the body is nearer than the panel and close to the line, the player is in the way, the closer to the line the more.
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace wghost {
struct Params {
    float bodyHeight = 1.8f, radius = 0.45f;     // the player as a vertical capsule: metres
    float margin = 0.55f;                        // beyond the body, how much further from the line counts as partly in the way
    float minAlpha = 0.22f;                      // how see-through a player in the way becomes (1: not at all)
    float maxTargetDist = 35.0f;                 // panels further than this from the camera are not looked at
    float coneCos = 0.55f;                       // a panel is looked towards when the angle to where the camera points is under acos(coneCos), about 57 degrees
    float panelClear = 0.30f;                    // the player must be at least this much nearer to the camera than the panel (someone standing at the panel is not in front of it)
    float nearDist = 1.6f;                       // closer than this to the camera a player starts to fade, ...
    float nearFull = 0.5f;                       // ... and at this distance has reached minAlpha
};
inline float Smooth(float x) { x = x < 0 ? 0 : (x > 1 ? 1 : x); return x * x * (3 - 2 * x); }
inline float Dot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// Closest distance between the segments p0-p1 and q0-q1; *s is where on the first segment (0..1) the closest points are.
inline float SegmentDistance(const float* p0, const float* p1, const float* q0, const float* q1, float* s) {
    float d1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]}, d2[3] = {q1[0] - q0[0], q1[1] - q0[1], q1[2] - q0[2]}, r[3] = {p0[0] - q0[0], p0[1] - q0[1], p0[2] - q0[2]};
    float a = Dot(d1, d1), e = Dot(d2, d2), f = Dot(d2, r), sn, t; const float eps = 1e-9f;
    if (a <= eps && e <= eps) { sn = t = 0; }
    else if (a <= eps) { sn = 0; t = std::min(1.0f, std::max(0.0f, f / e)); }
    else {
        float c = Dot(d1, r);
        if (e <= eps) { t = 0; sn = std::min(1.0f, std::max(0.0f, -c / a)); }
        else {
            float b = Dot(d1, d2), den = a * e - b * b;
            sn = den > eps ? std::min(1.0f, std::max(0.0f, (b * f - c * e) / den)) : 0.0f;
            t = (b * sn + f) / e;
            if (t < 0) { t = 0; sn = std::min(1.0f, std::max(0.0f, -c / a)); } else if (t > 1) { t = 1; sn = std::min(1.0f, std::max(0.0f, (b - c) / a)); }
        }
    }
    float cp[3] = {p0[0] + d1[0] * sn - (q0[0] + d2[0] * t), p0[1] + d1[1] * sn - (q0[1] + d2[1] * t), p0[2] + d1[2] * sn - (q0[2] + d2[2] * t)};
    *s = sn; return std::sqrt(Dot(cp, cp));
}

// The alpha (1: solid .. minAlpha: ghost) of a player whose feet are at `feet`, seen from `cam` looking along the unit vector `fwd`, with `n` panels at panels[3 * i ..].
inline float InTheWay(const float cam[3], const float fwd[3], const float feet[3], const float* panels, size_t n, const Params& p) {
    float top[3] = {feet[0], feet[1], feet[2] + p.bodyHeight}; float worst = 0;
    for (size_t i = 0; i < n; ++i) {
        const float* P = panels + 3 * i; float v[3] = {P[0] - cam[0], P[1] - cam[1], P[2] - cam[2]}; float len = std::sqrt(Dot(v, v)); if (len < 0.5f) continue;
#ifndef WG_MUT_FAR
        if (len > p.maxTargetDist) continue;
#endif
#ifndef WG_MUT_NOCONE
        if (Dot(v, fwd) / len < p.coneCos) continue;
#endif
        float s, dist = SegmentDistance(cam, P, feet, top, &s);
#ifndef WG_MUT_BEHIND
        if ((1.0f - s) * len < p.panelClear) continue;      // the closest point of the line is at the panel itself: the player is not in front of it
#endif
        if (s * len < 0.2f) continue;                       // (and not behind the camera)
#ifdef WG_MUT_NOMARGIN
        float w = dist < p.radius ? 1.0f : 0.0f;
#else
        float w = 1.0f - Smooth((dist - p.radius) / p.margin);
#endif
        worst = std::max(worst, w);
    }
    return 1.0f - (1.0f - p.minAlpha) * worst;
}
// A player in your face fades: 1 beyond nearDist, minAlpha at nearFull and closer.
inline float NearAlpha(float distance, const Params& p) {
#ifdef WG_MUT_NONEAR
    return 1.0f;
#endif
    float k = Smooth((distance - p.nearFull) / (p.nearDist - p.nearFull)); return p.minAlpha + (1.0f - p.minAlpha) * k;
}
inline float Alpha(const float cam[3], const float fwd[3], const float feet[3], const float* panels, size_t n, const Params& p) {
    float top[3] = {feet[0], feet[1], feet[2] + p.bodyHeight}, s, d = SegmentDistance(cam, cam, feet, top, &s);      // how near the nearest part of the body is to the camera
    return std::min(InTheWay(cam, fwd, feet, panels, n, p), NearAlpha(d, p));
}
// Moves `cur` towards `target` at most `perSecond` per second (so a player that steps in front of a puzzle fades instead of popping).
inline float Approach(float cur, float target, float dt, float perSecond) {
#ifdef WG_MUT_NOSMOOTH
    return target;
#endif
    float step = perSecond * dt; return cur < target ? std::min(target, cur + step) : std::max(target, cur - step);
}
}  // namespace wghost
