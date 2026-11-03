// T0: the third-person emote camera (core/thirdperson.h). Mutant: -DTP_MUT_NO_RETURN (the view never swings back) must fail it.
#include "../thirdperson.h"
#include <cstdio>
using namespace witness::tp;
#define REQUIRE(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
static bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) < e; }

// rotate v by q (x y z w), as the game does (exe+0x60060): v' = v + w t + q x t, t = 2 q x v
static V3 Rot(const float q[4], V3 v) {
    V3 u = {q[0], q[1], q[2]}; auto cross = [](V3 a, V3 b) { return V3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; };
    V3 t = Mul(cross(u, v), 2); return Add(Add(v, Mul(t, q[3])), cross(u, t));
}
// run frames of dt until the phase is `want` (or the limit)
static int Run(Cam& c, int frames, bool emote, V3 eye, float yaw, float pitch, bool click = false, bool menu = false, bool enabled = true) {
    for (int i = 0; i < frames; ++i) c.Tick(1 / 30.f, emote, eye, yaw, pitch, click, menu, enabled);
    return 0;
}

int main() {
    // the quaternion turns the camera's +X into the view direction, for every yaw and pitch
    for (float yaw = -3.1f; yaw < 3.2f; yaw += 0.7f) for (float pitch = -1.2f; pitch < 1.3f; pitch += 0.4f) {
        float q[4]; QuatFromYawPitch(yaw, pitch, q); V3 f = Rot(q, {1, 0, 0}), F = Forward(yaw, pitch), u = Rot(q, {0, 0, 1}), U = Up(yaw, pitch);
        REQUIRE(Near(f.x, F.x) && Near(f.y, F.y) && Near(f.z, F.z)); REQUIRE(Near(u.x, U.x) && Near(u.y, U.y) && Near(u.z, U.z));
        REQUIRE(Near(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3], 1));
    }
    const V3 eye = {10, 20, 3.5f}; const float yaw = 0.4f, pitch = 0.05f, vfov = 54, aspect = 16 / 9.f;
    Params P;
    // the probe: five points on screen, the first near the middle; a free view keeps the whole distance
    ProbeSet ps; REQUIRE(MakeProbe(P, eye, yaw, pitch, vfov, aspect, &ps));
    REQUIRE(ps.fx[0] > 0.45f && ps.fx[0] < 0.55f && ps.fy[0] < 0.5f && ps.fy[0] > 0.3f);
    REQUIRE(ps.fx[1] > ps.fx[0] && ps.fx[2] < ps.fx[0] && ps.fy[3] < ps.fy[0] && ps.fy[4] > ps.fy[0]);
    float sky[5] = {-1, -1, -1, -1, -1}; REQUIRE(Near(FreeScale(P, ps, sky), 1));
    // a wall 2 m ahead (view depth 2 m): the camera stops margin short of it, never behind it
    float wall[5]; for (int i = 0; i < 5; ++i) wall[i] = 2.0f;
    float k = FreeScale(P, ps, wall); REQUIRE(k < 1 && k > 0);
    for (int i = 0; i < 5; ++i) REQUIRE(ps.len[i] * k <= wall[i] / ps.cosView[i] - P.margin + 1e-3f);
    // looking at the floor: the spot is off screen, nothing can be measured
    ProbeSet bad; REQUIRE(!MakeProbe(P, eye, yaw, -1.2f, vfov, aspect, &bad));

    // the whole cycle: start, probe, swing in, hold, emote ends, swing back, off
    Cam c; REQUIRE(c.Start(3, eye, yaw, pitch)); REQUIRE(c.phase == Phase::Probe); View v; REQUIRE(!c.ViewFor(eye, yaw, pitch, &v));
    REQUIRE(!c.Start(4, eye, yaw, pitch));      // one at a time
    c.ProbeAnswer(&ps, sky); REQUIRE(c.phase == Phase::In);
    Run(c, 30, true, eye, yaw, pitch); REQUIRE(c.phase == Phase::Hold);
    REQUIRE(c.ViewFor(eye, yaw, pitch, &v) && Near(v.blend, 1));
    V3 off = Sub(v.pos, eye); REQUIRE(Near(Len(V3{off.x, off.y, 0}), P.dist) && Near(off.z, P.height));
    REQUIRE(Dot(V3{off.x, off.y, 0}, V3{std::cos(yaw), std::sin(yaw), 0}) > 0);      // in front of the player ...
    REQUIRE(Dot(Forward(v.yaw, v.pitch), Mul(off, -1)) > 0.9f * Len(off));                // ... looking back at them
    REQUIRE(v.pitch < 0);
    Run(c, 3, false, eye, yaw, pitch); REQUIRE(c.phase == Phase::Out && c.last == Why::EmoteEnded);
    Run(c, 30, false, eye, yaw, pitch); REQUIRE(c.phase == Phase::Off && !c.ViewFor(eye, yaw, pitch, &v));
    REQUIRE(c.starts == 1 && c.returns == 1);

    // half way: between the two, smoothly (no jump on the first frame)
    Cam h; h.Start(1, eye, yaw, pitch); h.ProbeAnswer(&ps, sky); Run(h, 1, true, eye, yaw, pitch);
    REQUIRE(h.ViewFor(eye, yaw, pitch, &v) && v.blend < 0.05f && Len(Sub(v.pos, eye)) < 0.2f);

    // every reason to return, from the hold
    struct Case { V3 eye; float yaw, pitch; bool click, menu, enabled; Why why; } cases[] = {
        {{10.3f, 20, 3.5f}, yaw, pitch, false, false, true, Why::Moved}, {eye, yaw + 0.6f, pitch, false, false, true, Why::Looked},
        {eye, yaw, pitch, true, false, true, Why::Clicked}, {eye, yaw, pitch, false, true, true, Why::Menu}, {eye, yaw, pitch, false, false, false, Why::Disabled}};
    for (const auto& t : cases) {
        Cam d; d.Start(2, eye, yaw, pitch); d.ProbeAnswer(&ps, sky); Run(d, 30, true, eye, yaw, pitch); REQUIRE(d.phase == Phase::Hold);
        d.Tick(1 / 30.f, true, t.eye, t.yaw, t.pitch, t.click, t.menu, t.enabled); REQUIRE(d.phase == Phase::Out && d.last == t.why);
        Run(d, 30, true, t.eye, t.yaw, t.pitch, t.click, t.menu, t.enabled); REQUIRE(d.phase == Phase::Off);
    }
    // the view eases back to the live first-person camera, even after the player moved
    Cam m; m.Start(2, eye, yaw, pitch); m.ProbeAnswer(&ps, sky); Run(m, 30, true, eye, yaw, pitch);
    V3 moved = {10.5f, 20.2f, 3.5f}; m.Tick(1 / 30.f, true, moved, yaw, pitch, false, false, true);
    for (int i = 0; i < 12; ++i) m.Tick(1 / 30.f, true, moved, yaw, pitch, false, false, true);
    if (m.ViewFor(moved, yaw, pitch, &v)) REQUIRE(Len(Sub(v.pos, moved)) < 0.5f);
    // blocked: a wall 1 m ahead leaves no room, so the view stays first person; unmeasurable likewise
    float close[5]; for (int i = 0; i < 5; ++i) close[i] = 1.0f;
    Cam b; b.Start(5, eye, yaw, pitch); b.ProbeAnswer(&ps, close); REQUIRE(b.phase == Phase::Off && b.last == Why::Blocked && !b.ViewFor(eye, yaw, pitch, &v));
    Cam u; u.Start(5, eye, yaw, pitch); u.ProbeAnswer(nullptr, nullptr); REQUIRE(u.phase == Phase::Off && u.last == Why::Unmeasurable);
    // a wall at 2.5 m: a shorter camera, still in front of the wall
    float mid[5]; for (int i = 0; i < 5; ++i) mid[i] = 2.5f;
    Cam s; s.Start(5, eye, yaw, pitch); s.ProbeAnswer(&ps, mid); REQUIRE(s.phase == Phase::In && s.scale < 1);
    Run(s, 30, true, eye, yaw, pitch); REQUIRE(s.ViewFor(eye, yaw, pitch, &v)); REQUIRE(Len(Sub(v.pos, eye)) * ps.cosView[0] < 2.5f - P.margin * ps.cosView[0] + 1e-3f);
    // moving while the probe is out: nothing starts
    Cam pm; pm.Start(5, eye, yaw, pitch); pm.Tick(1 / 30.f, true, moved, yaw, pitch, false, false, true); REQUIRE(pm.phase == Phase::Off);
    pm.ProbeAnswer(&ps, sky); REQUIRE(pm.phase == Phase::Off);
    // a looping emote (Thinking) holds until the player moves
    Cam lp; lp.Start(13, eye, yaw, pitch); lp.ProbeAnswer(&ps, sky); Run(lp, 300, witness::EmoteActive(13, 10.0f), eye, yaw, pitch); REQUIRE(lp.phase == Phase::Hold);
    std::puts("thirdperson: OK");
    return 0;
}
