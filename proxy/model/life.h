#pragma once
// The adventurer's life layer: what a character does on its own, with no packet telling it to. It blinks, glances at the player who
// is near and looking at it, carries its head where its own player looks (the remote view pitch), breathes (harder after a run) and
// swings its scarf tails and hanging gear as it moves. Game-free: one LifeState per drawn player, stepped with the frame time and
// applied to the pose by JointMatricesBlendEx (model.cpp). The rig part (which node is the head, which local axis turns it left)
// is found once per model by LifeRig::Bind. Mutants (-DWM_MUT_...) each break one thing on purpose; `make -C proxy/model test` must
// fail for every one of them.
#include <cmath>
#include <cstdint>
#include <cstring>

namespace wm {

struct Model;

// A rotation about local axis `axis` by sign * angle turns the node's probe direction toward the wanted model-space direction (x left, y up, z front).
struct LifeAxis { int axis = -1; float sign = 1; };
struct LifeChain { int node[3] = {-1, -1, -1}; int count = 0; float length[3] = {0.1f, 0.1f, 0.1f}; LifeAxis swing[3], sway[3]; };   // swing: the tail toward +z; sway: toward +x
struct LifeRig {
    int head = -1, neck = -1, chest = -1, eye[2] = {-1, -1}, lid[2] = {-1, -1}, brow[2] = {-1, -1};
    LifeAxis headYaw, headPitch, neckYaw, neckPitch, chestPitch, eyeYaw[2], eyePitch[2], browTilt[2];   // browTilt: a roll that lifts the brow's inner end (the bone runs inner to outer)
    int browUp[2] = {1, 1}; float browUpSign[2] = {1, 1};      // the bone's local axis (and sign) that is 'up' in model space, for lifting the brow
    int hem[3] = {-1, -1, -1}, thigh[2] = {-1, -1};             // the coat's skirt: front left, front right, back; the thighs that drive them
    int lip = -1, lipUp = 1; float lipUpSign = 1;                // the lower lip and its parent-frame 'up' axis: it drops while the player speaks
    LifeAxis hemSwing[3], hemSway[3], thighSwing[2];            // swing: the hem's bottom toward +z (front); thighSwing: the thigh's foot toward +z
    LifeChain scarfA, scarfB, canteen, satchel;
    float lidClose = 1.0f;          // the lid bone's scale along its length that closes the eye (the model's .glb.txt: lid_close=); 1: no lids
    bool ok = false;                // the head was found: the layer does something
    void Bind(const Model& m, float lidCloseScale);
};

struct LifeInput {
    float dt = 0;                   // seconds since the last step (clamped to 0.1)
    float speed = 0;                // the player's ground speed, m/s
    float viewPitch = 0;            // the remote player's own view pitch, radians, up positive
    bool viewer = false;            // there is a local camera that could be glanced at
    float viewerDir[3] = {0, 0, 1}; // unit vector from the puppet's head to that camera, model space
    float viewerDist = 100;         // metres
    bool headBusy = false;          // an emote owns the head: the layer lets go of it
    int emote = 0;                  // the emote playing (1..24, 0 none): the brows act with it
    float talking = 0;              // 0..1: the player's proximity voice is active (the social layer's smoothed activity): the mouth moves
    float viewYawOffset = 0;        // where the player looks minus where the body faces (radians, left positive): the head turns there ahead of the body
    float accel[3] = {0, 0, 0};     // the puppet's acceleration, model space, m/s^2 (y: the gait's bounce)
};

struct LifePendulum { float a = 0, va = 0, b = 0, vb = 0; };     // swing (a) and sway (b) angles and their rates, radians

struct LifeState {
    // outputs, read by the pose code
    float blink = 0;                // 0 open .. 1 closed
    float headYaw = 0, headPitch = 0, headWeight = 1;   // radians, model space: yaw > 0 looks left, pitch > 0 up
    float browLift = 0, browTilt = 0, browAsym = 0;      // the face acting with an emote: lift in metres (up), tilt in radians (inner ends up), asym: the right brow's share of the lift (thinking)
    float lipOpen = 0;                                   // how far the lower lip has dropped, metres (speech)
    float eyeYaw = 0, eyePitch = 0;
    float breath = 0;               // -1 .. 1 (times the exertion gain)
    float exertion = 0;             // 0 rested .. 1 just after a run
    LifePendulum scarfA[3], scarfB[2], canteen, satchel, hemSway[3];
    float hemFollow[3] = {0, 0, 0}, hemAngle[3] = {0, 0, 0};    // the coat's skirt: what the legs ask of it (front left, front right, back; radians, front positive) and what is shown
    mutable float thighSwing[2] = {0, 0};                       // the thighs' forward swing in the pose last applied (written by the pose code, read by the next Step)
    bool glancing = false;
    // state
    float t = 0, nextBlink = 1.5f, blinkT = -1, blinkSlow = 1, glanceGap = 1.0f, glanceLeft = 0, nextSac = 0.4f, sacYaw = 0, sacPitch = 0, breathPhase = 0;      // blinkSlow: a forced blink for pictures runs this many times slower
    float headYawV = 0, headPitchV = 0, eyeYawV = 0, eyePitchV = 0; bool hadViewer = false;
    uint32_t rng = 2463534242u;

    explicit LifeState(uint32_t seed = 1) { rng = 2463534242u ^ (seed * 2654435761u); if (!rng) rng = 1; for (int i = 0; i < 4; i++) Rand(); nextBlink = 0.8f + 2.5f * Rand(); glanceGap = 0.5f + 1.5f * Rand(); }
    float Rand() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / 16777216.0f; }      // 0 .. 1

    static void Spring(float& x, float& v, float target, float omega, float dt) {      // critically damped x'' = -omega^2 (x - target) - 2 omega x', implicit Euler (stable at any step)
        float f = 1 + 2 * dt * omega, oo = omega * omega, hoo = dt * oo, hhoo = dt * hoo, inv = 1 / (f + hhoo);
        float nx = (f * x + dt * v + hhoo * target) * inv, nv = (v + hoo * (target - x)) * inv; x = nx; v = nv;
    }
    static float Clamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

    void StepPendulum(LifePendulum& p, float L, float az, float ax, float ay, float damping, float swingLo, float swingHi, float swayLo, float swayHi, float restSwing, float dt) {
#ifdef WM_MUT_NO_CLOTH
        (void)L; (void)az; (void)ax; (void)ay; (void)damping; (void)swingLo; (void)swingHi; (void)swayLo; (void)swayHi; (void)restSwing; (void)dt; p = LifePendulum{}; return;
#endif
#ifdef WM_MUT_CLOTH_UNDAMPED
        damping = 0;
#endif
        const float g = 9.81f + ay;      // a bouncing pivot makes the hang heavier and lighter
        int n = (int)ceilf(dt / 0.008f); if (n < 1) n = 1; float h = dt / n;
        for (int i = 0; i < n; i++) {
            // the tail hangs down; a pivot pushed forward (+z) leaves the tail behind (swing negative), a pivot pushed left leaves it to the right
            float aa = -(g / L) * sinf(p.a - restSwing) - (az / L) * cosf(p.a) - damping * p.va;
            float ab = -(g / L) * sinf(p.b) - (ax / L) * cosf(p.b) - damping * p.vb;
            p.va += aa * h; p.a += p.va * h; p.vb += ab * h; p.b += p.vb * h;
            if (p.a < swingLo) { p.a = swingLo; if (p.va < 0) p.va = -0.3f * p.va; } if (p.a > swingHi) { p.a = swingHi; if (p.va > 0) p.va = -0.3f * p.va; }
            if (p.b < swayLo) { p.b = swayLo; if (p.vb < 0) p.vb = -0.3f * p.vb; } if (p.b > swayHi) { p.b = swayHi; if (p.vb > 0) p.vb = -0.3f * p.vb; }
        }
    }

    void Step(const LifeInput& in) {
        float dt = std::isfinite(in.dt) ? Clamp(in.dt, 0, 0.1f) : 0; t += dt;
        const bool moving = in.speed > 0.35f;
        // ---- blink: every 2 to 6 seconds, 75 ms to close and 120 ms to open, now and then a double blink; a glance begins with one
        nextBlink -= dt;
        if (nextBlink <= 0 && blinkT < 0) { blinkT = 0; nextBlink = Rand() < 0.15f ? 0.35f : 2.0f + 4.0f * Rand(); }
        if (blinkT >= 0) {
            blinkT += dt; const float close = 0.075f * blinkSlow, open = 0.12f * blinkSlow;
            float b = blinkT < close ? blinkT / close : blinkT < close + open ? 1 - (blinkT - close) / open : 0;
            blink = b * b * (3 - 2 * b);
            if (blinkT >= close + open) { blink = 0; blinkT = -1; blinkSlow = 1; }
        } else blink = 0;
#ifdef WM_MUT_NO_BLINK
        blink = 0;
#endif
        // ---- the glance: a nearby viewer in front of the character gets looked at for a few seconds, then left alone for a few
        float dir[3] = {in.viewerDir[0], in.viewerDir[1], in.viewerDir[2]}; float dl = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]); if (dl > 1e-6f) for (float& v : dir) v /= dl;
        float tyaw = atan2f(dir[0], dir[2]), tpitch = asinf(Clamp(dir[1], -1, 1));
        bool canGlance = in.viewer && dl > 1e-6f && in.viewerDist < 7.0f && fabsf(tyaw) < 1.75f && !in.headBusy;
        if (canGlance && !hadViewer) glanceGap = fminf(glanceGap, 0.25f + 0.5f * Rand());      // somebody has just come near: look at them soon
        hadViewer = canGlance;
        if (glanceLeft > 0) glanceLeft -= dt;
        if (glancing) { if (!canGlance || glanceLeft <= 0) { glancing = false; glanceGap = 2.5f + 4.0f * Rand(); } }
        else { glanceGap -= dt; if (canGlance && glanceGap <= 0) { glancing = true; glanceLeft = 1.8f + 2.5f * Rand() + (in.viewerDist < 2.5f ? 2.0f : 0.0f); if (blinkT < 0 && Rand() < 0.5f) { blinkT = 0; } } }
#ifdef WM_MUT_NO_LOOK
        glancing = false;
#endif
        float yawLimit = moving ? 0.45f : 1.05f;
#ifdef WM_MUT_LOOK_UNCLAMPED
        yawLimit = 3.2f;
#endif
        float viewTurn = Clamp(in.viewYawOffset * 0.85f, -1.0f, 1.0f);
#ifdef WM_MUT_NO_LOOK
        viewTurn = 0;
#endif
        float headYawT = glancing ? Clamp(tyaw, -yawLimit, yawLimit) : viewTurn;
        float headPitchT = glancing ? Clamp(tpitch, -0.5f, 0.4f) : Clamp(in.viewPitch * 0.7f, -0.6f, 0.35f);
        // ---- saccades: small eye movements every second or two; during a glance the eyes lead the head toward the viewer
        nextSac -= dt; if (nextSac <= 0) { nextSac = 0.6f + 2.5f * Rand(); sacYaw = (Rand() - 0.5f) * 0.14f; sacPitch = (Rand() - 0.5f) * 0.08f; }
        float eyeYawT = glancing ? Clamp(tyaw - headYaw, -0.26f, 0.26f) : sacYaw + Clamp(viewTurn - headYaw, -0.26f, 0.26f), eyePitchT = glancing ? Clamp(tpitch - headPitch, -0.18f, 0.18f) : sacPitch;
        Spring(headYaw, headYawV, headYawT, 5.5f, dt); Spring(headPitch, headPitchV, headPitchT, 5.5f, dt);
        Spring(eyeYaw, eyeYawV, eyeYawT, 20.0f, dt); Spring(eyePitch, eyePitchV, eyePitchT, 20.0f, dt);
        float wT = in.headBusy ? 0.0f : 1.0f; headWeight += (wT - headWeight) * (dt > 0 ? 1 - expf(-dt / 0.25f) : 0);
        // ---- the face acts with the emote: sad and thinking draw the inner brows up, laughter and panic and a cheer raise them, a facepalm or a 'no' knits them
        float liftT = 0, tiltT = 0, asymT = 0;
        switch (in.emote) {
        case 19: liftT = 0.002f; tiltT = 0.14f; break;                 // sad
        case 13: liftT = 0.004f; tiltT = 0.06f; asymT = 1.0f; break;  // thinking: one brow up
        case 20: case 18: case 3: case 21: liftT = 0.005f; break;      // laugh, celebrate, cheer, panic: raised
        case 11: case 5: liftT = -0.003f; tiltT = -0.12f; break;       // facepalm, no: knitted
        case 8: liftT = 0.003f; tiltT = 0.08f; break;                  // shrug
        default: break;
        }
#ifdef WM_MUT_NO_BROWS
        liftT = tiltT = asymT = 0;
#endif
        float kb = dt > 0 ? 1 - expf(-dt / 0.12f) : 0; browLift += (liftT - browLift) * kb; browTilt += (tiltT - browTilt) * kb; browAsym += (asymT - browAsym) * kb;
        // ---- speech: the lower lip drops and flutters at a syllable's pace (about 5 a second, unevenly) while the voice is active
        float syll = 0.5f + 0.5f * sinf(6.2831853f * 5.2f * t) * (0.6f + 0.4f * sinf(6.2831853f * 1.3f * t + 0.7f)) ;
        float lipT = Clamp(in.talking, 0, 1) * 0.0045f * (0.25f + 0.75f * syll);
#ifdef WM_MUT_NO_LIPS
        lipT = 0;
#endif
        lipOpen += (lipT - lipOpen) * (dt > 0 ? 1 - expf(-dt / 0.04f) : 0);
        // ---- breathing: 13 breaths a minute at rest, up to 46 after a run, deeper too; the exertion builds over a run and fades over half a minute
        float exT = in.speed > 2.2f ? 1.0f : 0.0f; exertion += (exT - exertion) * (dt > 0 ? 1 - expf(-dt / (exT > exertion ? 6.0f : 18.0f)) : 0);
        breathPhase += 6.2831853f * (0.22f + 0.55f * exertion) * dt; if (breathPhase > 6.2831853f) breathPhase -= 6.2831853f;
        breath = sinf(breathPhase) * (1 + 1.6f * exertion);
#ifdef WM_MUT_NO_BREATH
        breath = 0;
#endif
        // ---- cloth and gear: pendulums driven by the body's acceleration, the gait's bounce, the air at speed and a little wind
        float wind = (0.35f * sinf(1.3f * t) + 0.22f * sinf(2.9f * t + 1.7f) + 0.15f * sinf(7.1f * t + 0.4f)) * (0.25f + 0.6f * in.speed);
        float drag = -atanf(0.05f * in.speed * in.speed);      // the air at speed lays hanging things back
        float az = in.accel[2], ax = in.accel[0], ay = in.accel[1];
        for (int i = 0; i < 3; i++) StepPendulum(scarfA[i], 0.10f, az + (i ? scarfA[i - 1].va * 0.3f : 0.0f), ax + wind * (1 + 0.4f * i), ay, 3.0f, -0.10f, 0.55f, -0.35f, 0.35f, drag * 0.15f, dt);
        for (int i = 0; i < 2; i++) StepPendulum(scarfB[i], 0.075f, az + (i ? scarfB[i - 1].va * 0.3f : 0.0f), ax + wind * (1.2f + 0.4f * i), ay, 3.0f, -0.10f, 0.55f, -0.35f, 0.35f, drag * 0.15f, dt);
        StepPendulum(canteen, 0.17f, az, ax + wind * 0.2f, ay, 1.4f, -0.5f, 0.5f, -0.25f, 0.12f, drag * 0.5f, dt);
        StepPendulum(satchel, 0.18f, az, ax + wind * 0.15f, ay, 1.6f, -0.45f, 0.45f, -0.08f, 0.30f, drag * 0.4f, dt);
        // ---- the coat's skirt: each front panel follows the leg on its side forward, the back follows whichever leg trails (a fast follow, 60 ms,
        // so the cloth stays ahead of the thigh); on top of that a slow pendulum adds the lag and sway of heavy wool
        float tl = thighSwing[0], tr = thighSwing[1];
        float want[3] = {fmaxf(0, tl) * 0.95f + fminf(0, tl) * 0.25f, fmaxf(0, tr) * 0.95f + fminf(0, tr) * 0.25f, -fmaxf(0, fmaxf(-tl, -tr)) * 0.95f};
#ifdef WM_MUT_NO_HEM
        want[0] = want[1] = want[2] = 0;
#endif
        float kh = dt > 0 ? 1 - expf(-dt / 0.06f) : 0;
        for (int k = 0; k < 3; k++) {
            hemFollow[k] += (want[k] - hemFollow[k]) * kh;
            StepPendulum(hemSway[k], 0.43f, az * 0.5f, ax * 0.5f + wind * 0.3f, ay, 2.5f, -0.2f, 0.2f, -0.15f, 0.15f, 0, dt);
            hemAngle[k] = Clamp(hemFollow[k] + hemSway[k].a, -0.8f, 0.8f);
        }
    }
};

}  // namespace wm
