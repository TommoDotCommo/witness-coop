#pragma once
// A panel's own frame, game-free (T0 test: frame_test.cpp). A drawn line is kept by the game as world positions that are written while the line is drawn and never updated, but the panel
// can move afterwards (the first panels are part of a door that swings open): the game draws its own line from the panel's local nodes, so it follows the panel. A line that is shared must do
// the same: positions are turned into the panel's frame (a quaternion and a position) at the time they were valid, travel like that, and are turned back with the frame the other game's panel
// has now. The tip of a line that is being drawn is a fraction of the way along its edge (the game stores the whole edge and the fraction apart).
#include <cmath>

namespace wframe {
struct Q { float x, y, z, w; };
inline void Rotate(const Q& q, const float v[3], float out[3]) {      // q v q^-1 for a unit quaternion
    float tx = 2 * (q.y * v[2] - q.z * v[1]), ty = 2 * (q.z * v[0] - q.x * v[2]), tz = 2 * (q.x * v[1] - q.y * v[0]);
    out[0] = v[0] + q.w * tx + (q.y * tz - q.z * ty); out[1] = v[1] + q.w * ty + (q.z * tx - q.x * tz); out[2] = v[2] + q.w * tz + (q.x * ty - q.y * tx);
}
inline Q Conj(const Q& q) { return {-q.x, -q.y, -q.z, q.w}; }
inline Q Normalize(const Q& q) { float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w); if (n < 1e-6f) return {0, 0, 0, 1}; return {q.x / n, q.y / n, q.z / n, q.w / n}; }
// world -> the panel's frame (position `pos`, rotation `q`), in metres
inline void ToLocal(const float pos[3], const Q& q, const float world[3], float local[3]) {
#ifdef WF_MUT_NO_ROTATION
    for (int k = 0; k < 3; ++k) local[k] = world[k] - pos[k]; return; (void)q;
#endif
    float d[3] = {world[0] - pos[0], world[1] - pos[1], world[2] - pos[2]}; Rotate(Conj(Normalize(q)), d, local);
}
inline void ToWorld(const float pos[3], const Q& q, const float local[3], float world[3]) {
#ifdef WF_MUT_NO_ROTATION
    for (int k = 0; k < 3; ++k) world[k] = pos[k] + local[k]; return; (void)q;
#endif
    float r[3]; Rotate(Normalize(q), local, r); for (int k = 0; k < 3; ++k) world[k] = pos[k] + r[k];
}
// the tip of the segment from `a` to `b` of which the fraction `t` is drawn; t outside 0..1 (not a fraction: another kind of record) means the whole segment
inline void Tip(const float a[3], const float b[3], float t, float out[3]) {
#ifdef WF_MUT_WHOLE_EDGE
    for (int k = 0; k < 3; ++k) out[k] = b[k]; return; (void)t; (void)a;
#endif
    if (!(t >= 0.0f && t < 0.9999f)) { for (int k = 0; k < 3; ++k) out[k] = b[k]; return; }
    for (int k = 0; k < 3; ++k) out[k] = a[k] + (b[k] - a[k]) * t;
}
}  // namespace wframe
