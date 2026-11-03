#pragma once
#include <algorithm>
#include <cmath>

namespace wnear {
// Positions are camera/remote eyes, in metres. The overlap cylinder encloses the
// head and torso, including the case where projection is behind the near plane.
struct State {
    float distance = -1;
    bool central = false;
    float step(const float* cam, float yaw, float pitch, float vfov, float aspect,
               const float* eye, float start = 1.4f, float end = 0.6f, bool enabled = true) {
        if (!enabled) { distance = -1; central = false; return 1; }
        if (!std::isfinite(start) || !std::isfinite(end) || end < 0 || start <= end) { start = 1.4f; end = 0.6f; }
        float x=eye[0]-cam[0], y=eye[1]-cam[1], z=eye[2]-cam[2];
        float d=std::sqrt(x*x+y*y+z*z);
        bool inside=x*x+y*y < 0.35f*0.35f && z > -0.22f && z < 1.71f;
        float along=std::cos(yaw)*x+std::sin(yaw)*y;
        float f=std::cos(pitch)*along+std::sin(pitch)*z;
        float r=std::sin(yaw)*x-std::cos(yaw)*y;
        float u=-std::sin(pitch)*along+std::cos(pitch)*z;
        float tv=std::tan(vfov*0.00872664626f), limit=central ? 0.75f : 0.65f;
        central=f > 0.01f && std::fabs(r) < limit*f*tv*aspect && std::fabs(u) < limit*f*tv;
        if (inside) { distance=d; return 0; }
        if (!central || d >= start) { distance=d; return 1; }
        if (d <= end) { distance=d; return 0; }
        // A 4 cm distance deadband holds opacity while neighbours shuffle.
        if (distance < 0 || std::fabs(d-distance) > 0.04f) distance=d;
        return std::clamp((distance-end)/(start-end),0.f,1.f);
    }
};
// The body's opacity when the overlap ghost (ghost/ghost.h) and this fade both apply: the stronger of the two, never their product
// (a body both overlapping and close would otherwise be faded twice). Mutant NF_MUT_PRODUCT: the product.
inline float BodyOpacity(float ghost, float nearAlpha) {
#ifdef NF_MUT_PRODUCT
    return ghost * nearAlpha;
#else
    return std::min(ghost, nearAlpha);
#endif
}
} // namespace wnear
