#pragma once
// The emote layer at run time: the keyframed clips (emote_clip.h, assets/emotes/*.emote) sampled
// at the emote's play time, then a spring per joint (follow-through and settle, the same on any
// frame rate) and the pose filter that blends in and out of the gait. model.cpp applies the result
// to the rig: bind-relative joint angles, the hips' offset, the turn, and the foot IK.
#include "../core/emotes.h"
#include "emote_clip.h"
#include "emote_data.h"
#include <array>
#include <algorithm>
#include <cmath>
namespace wm {
struct GestureJoint { float a[3] = {}; float weight = 0; };
using GesturePose = std::array<GestureJoint,GestureBones>;
// Not on the wire: the proximity-speech hand is a clip too.
inline constexpr int kTalkClip = 100;

// The clip library. It starts with the embedded copy of assets/emotes; a dev command can replace
// it from files (ReloadEmoteClips), which keeps the old library when the new text does not parse.
inline EmoteLibrary& EmoteClips() {
    static EmoteLibrary lib = [] { EmoteLibrary l; std::string err; if (!l.Parse(kEmoteClipText, &err, "embedded")) l.clips.clear(); return l; }();
    return lib;
}
inline bool ReloadEmoteClips(const std::string& text, std::string* err) {
    EmoteLibrary l; if (!l.Parse(text, err, "emotes")) return false;
    for (int id = 1; id <= witness::kEmoteCount; ++id) if (!l.ById(id)) { if (err) *err = "emotes: no clip for id " + std::to_string(id); return false; }
    EmoteClips() = std::move(l); return true;
}

// What the clip asks for at one instant, before springs and the pose filter.
struct GestureTarget {
    GesturePose pose{}; float hips[3] = {}, steps[6] = {}, hipsWeight = 0, turn = 0, plant = 1;
};
inline GestureTarget GestureSampleFull(int id, float t, float envelope) {
    GestureTarget g; const EmoteAnim* c = EmoteClips().ById(id); if (!c || envelope <= 0) return g;
    float v[kClipChannels]; SampleClip(*c, t, v);
    for (int b = 0; b < GestureBones; ++b) if (c->Drives(b)) g.pose[b] = {{v[b*3], v[b*3+1], v[b*3+2]}, envelope};
    if (c->Drives(kHipsBit)) { for (int k = 0; k < 3; ++k) g.hips[k] = v[kHipsChannel + k]; for (int k = 0; k < 6; ++k) g.steps[k] = v[kStepChannel + k]; g.hipsWeight = envelope; }
    g.turn = v[kTurnChannel] * 0.01745329252f;
    g.plant = std::max(0.f, std::min(1.f, v[kPlantChannel]));
#ifdef WE_MUT_LIMIT
    g.pose[ElbowR] = {{190, 0, 0}, envelope};
#endif
    return g;
}
inline GesturePose GestureSample(int id, float t) { return GestureSampleFull(id, t, witness::EmoteEnvelope(id, t)).pose; }

// Spring frequency (Hz) and damping ratio per bone: the torso is heavy and damped, the hands
// are light and settle with a little overshoot. Legs are stiff (the IK does their work).
inline constexpr float kSpringHz[GestureBones]   = {2.6f, 3.0f, 3.4f, 3.8f, 2.8f, 3.2f, 4.2f, 2.8f, 3.2f, 4.2f, 6, 6, 6, 6, 6, 6, 3.0f};
inline constexpr float kSpringZeta[GestureBones] = {.90f, .80f, .75f, .70f, .72f, .66f, .58f, .72f, .66f, .58f, 1, 1, 1, 1, 1, 1, .90f};

// Persistent per avatar: input loss, cancellation, switching and speech conflicts
// all use the same bounded filter. No frame-time allocations or clock reads.
struct GestureState {
    GesturePose pose{}; float talk = 0, turn = 0; int emote = 0, cancelled = 0; float lastT = -1;
    float hips[3] = {}, steps[6] = {}, hipsWeight = 0, plant = 1, talkT = 0, stiffness = 1;
    float vel[GestureBones][3] = {}, hipsVel[3] = {}, stepVel[6] = {}, turnVel = 0;
    void Step(int id, float t, float speed, bool proximity, float dt) {
        dt = std::isfinite(dt) ? std::max(0.f, std::min(.1f, dt)) : 0;
        if (!witness::EmoteActive(id, t)) id = 0;
        if (!id || id != cancelled || t < lastT) cancelled = 0;
        if (id && speed > .35f && !witness::kEmotes[id].mobile) cancelled = id;
#ifndef WE_MUT_MOVE
        if (id == cancelled) id = 0;
#endif
        lastT = t;
        int was = emote; emote = id;
        GestureTarget target = GestureSampleFull(id, t, witness::EmoteEnvelope(id, t));
#ifndef WE_MUT_CONFLICT
        proximity = proximity && !id;
#endif
        float k = 1 - std::exp(-dt / .09f);
#ifdef WE_MUT_BLEND
        k = 1;
#endif
        talk += ((proximity ? 1.f : 0.f) - talk) * k;
        if (talk > .0001f && !id) {
            talkT += dt;
            GestureTarget speech = GestureSampleFull(kTalkClip, talkT, talk);
            for (int b = 0; b < GestureBones; ++b) if (speech.pose[b].weight > 0) target.pose[b] = speech.pose[b];
        } else if (talk <= .0001f) talkT = 0;
        if (const EmoteAnim* c = id ? EmoteClips().ById(id) : nullptr) stiffness = c->stiffness;
        // Follow the shortest arc on interruption; a completed turn is identity.
        float targetTurn = id ? target.turn : 0;
        for (int b = 0; b < GestureBones; ++b) {
            pose[b].weight += (target.pose[b].weight - pose[b].weight) * k;
            if (target.pose[b].weight <= 0) continue;      // keep the outgoing pose while its influence fades out
            if (pose[b].weight < .02f) { for (int a = 0; a < 3; ++a) { pose[b].a[a] = target.pose[b].a[a]; vel[b][a] = 0; } continue; }
            for (int a = 0; a < 3; ++a) SpringTo(pose[b].a[a], vel[b][a], target.pose[b].a[a], kSpringHz[b] * stiffness, kSpringZeta[b], dt);
        }
        hipsWeight += (target.hipsWeight - hipsWeight) * k;
        if (target.hipsWeight > 0) {
            for (int a = 0; a < 3; ++a) SpringTo(hips[a], hipsVel[a], target.hips[a], 3.f * stiffness, .9f, dt);
            for (int a = 0; a < 6; ++a) SpringTo(steps[a], stepVel[a], target.steps[a], 5.f * stiffness, .95f, dt);
        } else if (hipsWeight < .001f) { for (int a = 0; a < 3; ++a) hips[a] = hipsVel[a] = 0; for (int a = 0; a < 6; ++a) steps[a] = stepVel[a] = 0; }
        // The plant follows its keys closely (a landing must not sink while it catches up); entering and leaving use the pose filter.
        plant += ((id ? target.plant : 1.f) - plant) * (id && was == id ? 1 - std::exp(-dt / .02f) : k);
        float err = std::remainder(targetTurn - turn, 6.2831853f), goal = turn + err;
        SpringTo(turn, turnVel, goal, 2.2f * stiffness, .9f, dt);
        turn = std::remainder(turn, 6.2831853f);
    }
    // A damped spring toward `target`, integrated in sub-steps so any frame rate gives the same motion.
    static void SpringTo(float& x, float& v, float target, float hz, float zeta, float dt) {
#ifdef WE_MUT_NO_SPRING
        x += (target - x) * (1 - std::exp(-dt / .09f)); v = 0; (void)hz; (void)zeta; return;
#endif
        if (dt <= 0) return;
        float w = 6.2831853f * hz; int n = int(std::ceil(dt * 240)); float h = dt / n;
        for (int i = 0; i < n; ++i) { v += (w * w * (target - x) - 2 * zeta * w * v) * h; x += v * h; }
    }
};
}
