#pragma once
// Players become translucent only where their vertical body capsule intersects the local viewer's capsule.
// Distance from a puzzle, view direction and ordinary proximity do not affect opacity.
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace wghost {
struct Params {
    float bodyHeight = 1.8f;       // remote capsule: feet to head, metres
    float eyeHeight = 1.62f;       // local capsule below the camera
    float headroom = 0.18f;        // local capsule above the camera
    float radius = 0.38f;          // full fade inside this horizontal body radius
    float margin = 0.17f;          // smooth edge; fully solid at radius + margin (0.55 m)
    float verticalMargin = 0.17f; // fade only inside the shared vertical interval
    float minAlpha = 0.22f;
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

inline float OverlapAlpha(float distance, const Params& p) {
#ifdef WG_MUT_BROAD_NEAR
    return p.minAlpha + (1.0f - p.minAlpha) * Smooth((distance - 0.5f) / 1.1f);
#endif
#ifdef WG_MUT_NOFADE
    return 1.0f;
#endif
    return p.minAlpha + (1.0f - p.minAlpha) * Smooth((distance - p.radius) / p.margin);
}

// fwd and panels remain in the interface for the renderer, but deliberately do not participate in opacity.
inline float Alpha(const float cam[3], const float fwd[3], const float feet[3], const float* panels, size_t n, const Params& p) {
    (void)fwd; (void)panels; (void)n;
#ifdef WG_MUT_PANEL
    if (n && feet[0] * cam[0] >= 0) return p.minAlpha;
#endif
    float localBottom[3] = {cam[0], cam[1], cam[2] - p.eyeHeight};
    float localTop[3] = {cam[0], cam[1], cam[2] + p.headroom};
    float remoteTop[3] = {feet[0], feet[1], feet[2] + p.bodyHeight};
#ifndef WG_MUT_NOVERTICAL
    if (localBottom[2] > remoteTop[2] || feet[2] > localTop[2]) return 1.0f;
#endif
    float d = std::hypot(feet[0] - cam[0], feet[1] - cam[1]);
    float alpha = OverlapAlpha(d, p);
#ifndef WG_MUT_NOVERTICAL
#ifndef WG_MUT_VERTICAL_STEP
    float penetration = std::min(localTop[2] - feet[2], remoteTop[2] - localBottom[2]);
    float vertical = p.minAlpha + (1.0f - p.minAlpha) * (1.0f - Smooth(penetration / p.verticalMargin));
    alpha = std::max(alpha, vertical);
#endif
#endif
    return alpha;
}

// Move toward the target without popping; at 5 units/s a fully clear body is solid again in about 0.16 s.
inline float Approach(float cur, float target, float dt, float perSecond) {
#ifdef WG_MUT_NOSMOOTH
    return target;
#endif
    float step = perSecond * dt; return cur < target ? std::min(target, cur + step) : std::max(target, cur - step);
}
}  // namespace wghost
