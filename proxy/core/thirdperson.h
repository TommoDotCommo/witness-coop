#pragma once
// Third-person view while emoting (game-free; proxy/thirdperson.cpp is the game side). While the local player plays an emote the view
// swings round to the front of the player, a little above, looking back at them, so they see their own figure do it; it swings back
// when the emote ends, the player moves or looks away, clicks (a puzzle) or opens a menu.
// Walls: the camera spot lies on a ray from the player's eye into the view the game has just drawn, so the game's own depth buffer says
// how far that ray (and four rays round it, the camera's clearance) is free. The distance shrinks to fit; below minDist it stays first person.
// Camera convention (build 11370412): z up, yaw about z, pitch up positive; the game's quaternion is x y z w and rotates the camera's
// local +X (forward) into the view direction: q = Rz(yaw) * Ry(-pitch).
#include <cmath>
#include "emotes.h"

namespace witness::tp {

struct V3 { float x, y, z; };
inline V3 Add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 Sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 Mul(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float Len(V3 a) { return std::sqrt(Dot(a, a)); }
constexpr float kPi = 3.14159265358979f;
inline float WrapAngle(float a) { while (a > kPi) a -= 2 * kPi; while (a <= -kPi) a += 2 * kPi; return a; }

// The game's camera quaternion (x y z w) for a yaw and pitch.
inline void QuatFromYawPitch(float yaw, float pitch, float q[4]) {
    float cy = std::cos(yaw * 0.5f), sy = std::sin(yaw * 0.5f), cp = std::cos(-pitch * 0.5f), sp = std::sin(-pitch * 0.5f);
    // Rz(yaw) = (0,0,sy,cy), Ry(-pitch) = (0,sp,0,cp); product (Hamilton, a*b): x = -sy*sp, y = cy*sp, z = sy*cp, w = cy*cp
    q[0] = -sy * sp; q[1] = cy * sp; q[2] = sy * cp; q[3] = cy * cp;
}
inline V3 Forward(float yaw, float pitch) { return {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)}; }
inline V3 Right(float yaw) { return {std::sin(yaw), -std::cos(yaw), 0}; }
inline V3 Up(float yaw, float pitch) { return {-std::sin(pitch) * std::cos(yaw), -std::sin(pitch) * std::sin(yaw), std::cos(pitch)}; }

struct Params {
    float dist = 2.6f;        // metres in front of the eye (horizontal)
    float height = 0.35f;     // above the eye
    float aimDrop = 0.55f;    // the camera looks at a point this far below the eye (the chest: the whole figure fits at 54 degrees)
    float minDist = 1.2f;     // closer than this: stay first person
    float margin = 0.45f;     // kept free between the camera and the first surface
    float clearance = 0.35f;  // the four extra rays sit this far round the camera spot
    float inSec = 0.6f, outSec = 0.45f;     // swing times
    float moveCancel = 0.08f; // metres the eye may drift before the view returns (walking)
    float lookCancel = 0.35f; // radians of mouse look before the view returns
};

enum class Phase { Off, Probe, In, Hold, Out };
enum class Why { None, EmoteEnded, Moved, Looked, Clicked, Menu, Disabled, Blocked, Unmeasurable };
inline const char* WhyName(Why w) {
    switch (w) { case Why::EmoteEnded: return "emote_ended"; case Why::Moved: return "moved"; case Why::Looked: return "looked";
        case Why::Clicked: return "clicked"; case Why::Menu: return "menu"; case Why::Disabled: return "disabled"; case Why::Blocked: return "blocked";
        case Why::Unmeasurable: return "unmeasurable"; default: return "none"; }
}

// The five sample points (the camera spot, then right, left, up, down of it) as screen positions (0..1, y down) of the current view.
// false: a point is behind the view or off screen, so the space cannot be measured (the caller stays first person).
struct ProbeSet { float fx[5], fy[5]; float len[5]; float cosView[5]; };
inline bool MakeProbe(const Params& p, V3 eye, float yaw, float pitch, float vfovDeg, float aspect, ProbeSet* out) {
    V3 fh = {std::cos(yaw), std::sin(yaw), 0}, z = {0, 0, 1}, r = Right(yaw);
    V3 spot = Add(eye, Add(Mul(fh, p.dist), Mul(z, p.height)));
    V3 pts[5] = {spot, Add(spot, Mul(r, p.clearance)), Sub(spot, Mul(r, p.clearance)), Add(spot, Mul(z, p.clearance)), Sub(spot, Mul(z, p.clearance))};
    V3 f = Forward(yaw, pitch), rv = Right(yaw), u = Up(yaw, pitch);
    float tv = std::tan(vfovDeg * kPi / 360.0f), th = tv * aspect;
    for (int i = 0; i < 5; ++i) {
        V3 d = Sub(pts[i], eye); float L = Len(d), xf = Dot(d, f);
        if (!(L > 1e-3f) || xf < 0.05f * L) return false;
        float nx = Dot(d, rv) / (xf * th), ny = Dot(d, u) / (xf * tv);
        if (std::fabs(nx) > 0.95f || std::fabs(ny) > 0.95f) return false;
        out->fx[i] = 0.5f + 0.5f * nx; out->fy[i] = 0.5f - 0.5f * ny; out->len[i] = L; out->cosView[i] = xf / L;
    }
    return true;
}
// The share (0..1) of the planned offset that stays `margin` short of the first surface on every ray. viewDepth: distance along the view
// direction of the game's geometry at each sample point (<= 0: sky, nothing there).
inline float FreeScale(const Params& p, const ProbeSet& s, const float viewDepth[5]) {
    float k = 1.0f;
    for (int i = 0; i < 5; ++i) {
        if (!(viewDepth[i] > 0) || !std::isfinite(viewDepth[i])) continue;
        float along = viewDepth[i] / s.cosView[i];       // the free length along the ray
        float ki = (along - p.margin) / s.len[i]; if (ki < k) k = ki;
    }
    return k < 0 ? 0 : k;
}

struct View { V3 pos; float yaw, pitch, blend; };

class Cam {
public:
    Params p;
    Phase phase = Phase::Off; float s = 0; Why last = Why::None;
    V3 eye0{0, 0, 0}; float yaw0 = 0, pitch0 = 0, scale = 1; int emote = 0;
    unsigned starts = 0, returns = 0;

    // A local emote began (eye, yaw, pitch: the first-person camera now). Only from first person: the depth must be of the player's own view.
    bool Start(int emoteId, V3 eye, float yaw, float pitch) {
        if (phase != Phase::Off) return false;
        emote = emoteId; eye0 = eye; yaw0 = yaw; pitch0 = pitch; scale = 1; s = 0; phase = Phase::Probe; return true;
    }
    // The answer of the depth probe (null: no measurement possible).
    void ProbeAnswer(const ProbeSet* set, const float* viewDepth) {
        if (phase != Phase::Probe) return;
        if (!set || !viewDepth) { phase = Phase::Off; last = Why::Unmeasurable; return; }
        float k = FreeScale(p, *set, viewDepth); k = k > 1 ? 1 : k;
        if (p.dist * k < p.minDist) { phase = Phase::Off; last = Why::Blocked; return; }
        scale = k; phase = Phase::In; last = Why::None; ++starts;
    }
    // Once per frame. emoteActive: the local emote still plays; eye/yaw/pitch: the first-person camera now.
    void Tick(float dt, bool emoteActive, V3 eye, float yaw, float pitch, bool clicked, bool menu, bool enabled) {
        if (phase == Phase::Off) return;
        Why w = Why::None;
        if (!enabled) w = Why::Disabled;
        else if (menu) w = Why::Menu;
        else if (clicked) w = Why::Clicked;
        else if (Len(Sub(eye, eye0)) > p.moveCancel) w = Why::Moved;
        else if (std::fabs(WrapAngle(yaw - yaw0)) > p.lookCancel || std::fabs(pitch - pitch0) > p.lookCancel) w = Why::Looked;
        else if (!emoteActive) w = Why::EmoteEnded;
        if (phase == Phase::Probe) { if (w != Why::None) { phase = Phase::Off; last = w; } return; }
#ifndef TP_MUT_NO_RETURN
        if (w != Why::None && phase != Phase::Out) { phase = Phase::Out; last = w; ++returns; }
#endif
        if (phase == Phase::In || phase == Phase::Hold) { s += dt / p.inSec; if (s >= 1) { s = 1; phase = Phase::Hold; } }
        else if (phase == Phase::Out) { s -= dt / p.outSec; if (s <= 0) { s = 0; phase = Phase::Off; } }
    }
    bool Active() const { return phase == Phase::In || phase == Phase::Hold || phase == Phase::Out; }
    // The camera to draw the world from, given the live first-person camera. false: draw first person.
    bool ViewFor(V3 eye, float yaw, float pitch, View* v) const {
        if (!Active() || s <= 0) return false;
        float e = Ease(s), D = p.dist * scale, H = p.height * scale;
        V3 fh = {std::cos(yaw0), std::sin(yaw0), 0};
        V3 offset = Add(Mul(fh, D), V3{0, 0, H});
        float targetPitch = std::atan2(-(H + p.aimDrop), D);
        float dYaw = WrapAngle(yaw0 + kPi - yaw); if (dYaw < 0) dYaw += 2 * kPi;      // always swing the same way round (to the left)
        v->pos = Add(eye, Mul(offset, e)); v->yaw = WrapAngle(yaw + dYaw * e); v->pitch = pitch + (targetPitch - pitch) * e; v->blend = e;
        return true;
    }
};

} // namespace witness::tp
