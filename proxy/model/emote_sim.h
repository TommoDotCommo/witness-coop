#pragma once
// Plays an emote on the real character with the production layers (idle clip, emote clip, springs,
// foot IK, life layer) at a fixed step, the way model_gpu.cpp does per frame. Game-free: the quality
// gate (tests/emote_quality.cpp) and the review renderer (tools/emote_render.cpp) both use it.
#include "model.h"
#include "gestures.h"
#include "life.h"
#include <array>
#include <vector>

namespace wm {

inline int JointOf(const Model& m, const char* name) {
    for (size_t j = 0; j < m.joints.size(); ++j) if (m.nodes[m.joints[j]].name == name) return int(j);
    return -1;
}
// A joint's origin in model space under skin matrices (its bind origin carried by its skin matrix).
inline std::array<float, 3> JointPos(const Model& m, const float* mats, int j) {
    const float* ib = m.ibm.data() + j * 16; float p[3];
    for (int r = 0; r < 3; ++r) p[r] = -(ib[r * 4] * ib[12] + ib[r * 4 + 1] * ib[13] + ib[r * 4 + 2] * ib[14]);
    const float* s = mats + j * 16; std::array<float, 3> o;
    for (int r = 0; r < 3; ++r) o[r] = s[r] * p[0] + s[4 + r] * p[1] + s[8 + r] * p[2] + s[12 + r];
    return o;
}

struct EmoteSim {
    const Model& m; LifeRig rig; LifeState life; GestureState g; int idle = -1; bool useLife;
    EmoteSim(const Model& model, bool withLife = true, uint32_t seed = 7) : m(model), life(seed), useLife(withLife) {
        rig.Bind(m, 1.495f); idle = FindClip(m, "idle");
    }
    // One frame: emote `id` at play time t (0: none), the idle under it at idleT. Fills kMaxJoints*16 floats.
    void Frame(int id, float t, float idleT, float dt, float* mats, bool proximity = false, float speed = 0) {
        g.Step(id, t, speed, proximity, dt);
        if (useLife) {
            LifeInput li; li.dt = dt; li.speed = speed; li.emote = g.emote; li.talking = g.talk;
            li.headBusy = g.emote != 0 || g.pose[Head].weight > .05f || g.pose[Neck].weight > .05f;
            life.Step(li);
        }
        PoseLayers layers; layers.gesture = &g; layers.life = useLife ? &life : nullptr; layers.rig = &rig;
        JointMatricesBlendEx(m, -1, 0, idle, idleT, 1, mats, layers);
    }
};

}  // namespace wm
