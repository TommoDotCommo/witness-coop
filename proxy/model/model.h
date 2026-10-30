#pragma once
// Game-free character model code: glTF binary (.glb) loading, skeletal animation, CPU skinning (used by the tests), the camera
// maths and the remote player's walk state. Nothing here includes windows.h or d3d11.h, so `make -C proxy/model test` runs it
// without a game. Matrices are column-major (m[col * 4 + row]), like glTF, and act on column vectors.
#include <cstdint>
#include <string>
#include <vector>

namespace wm {

struct GestureState; struct LifeState; struct LifeRig;

#ifdef WM_MUT_MAX_JOINTS
constexpr int kMaxJoints = 128;      // the old limit: a 151-joint character is refused
#else
constexpr int kMaxJoints = 256;      // including the extra identity joint that unskinned geometry is bound to (joint indices are bytes; the shader's bones[] array has the same size)
#endif

struct Vertex { float pos[3], nrm[3], uv[2]; uint8_t joints[4]; float weights[4]; uint8_t col[4]; float appearanceGroup = 0; };      // col: the material's base colour factor; col[3] = 255 when the vertex samples the model's texture, 0 when it is flat colour

struct Node { int parent = -1; float t[3] = {0, 0, 0}, r[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1}; std::string name; };

struct Channel { int node = -1; int path = 0; bool step = false; std::vector<float> times, values; };   // path 0 translation, 1 rotation, 2 scale
struct Clip { std::string name; float duration = 0; std::vector<Channel> channels; };

struct Model {
    std::vector<Vertex> verts; std::vector<uint32_t> idx;
    std::vector<Node> nodes;
    std::vector<int> gestureBone; std::vector<float> gestureForward;
    std::vector<int> joints;          // node index of each joint of the first skin
    std::vector<float> ibm;           // inverse bind matrices, 16 floats per joint
    std::vector<Clip> clips;
    int texW = 0, texH = 0; std::vector<uint8_t> rgba;   // base colour texture of the first textured material (empty: none)
    int nrmW = 0, nrmH = 0; std::vector<uint8_t> nrm;    // that material's tangent-space normal map (glTF: +Y up), and
    int mrW = 0, mrH = 0; std::vector<uint8_t> mr;       // its material map: R occlusion, G roughness, B metalness (empty: flat materials)
    float baseColor[4] = {1, 1, 1, 1};
    float bmin[3] = {0, 0, 0}, bmax[3] = {0, 0, 0};       // bind-pose bounds in model space (glTF: Y up, the front faces +Z)
    int jointCount() const { return (int)joints.size() + 1; }   // + the identity joint
};

bool LoadGlb(const uint8_t* data, size_t n, Model* out, std::string* err);

// Joint matrices (jointCount() * 16 floats) for clip `clip` at `t` seconds (looped). clip < 0: the bind pose.
void JointMatrices(const Model& m, int clip, float t, float* out);
// The same with a cross-fade: pose = (1 - w) * (clip `from` at tFrom) + w * (clip `to` at tTo). from < 0 or w >= 1: just `to`.
void JointMatricesBlend(const Model& m, int from, float tFrom, int to, float tTo, float w, float* out, const GestureState* gesture = nullptr, float radioRaise = 0, float* radioHand = nullptr);
// The layers put over the sampled clips, in this order: the emote gestures, the radio arm, the life layer (life.h: blink, glance, breathing, cloth).
struct PoseLayers { const GestureState* gesture = nullptr; float radioRaise = 0; float* radioHand = nullptr; const LifeState* life = nullptr; const LifeRig* rig = nullptr; };
void JointMatricesBlendEx(const Model& m, int from, float tFrom, int to, float tTo, float w, float* out, const PoseLayers& layers);
inline void JointMatricesBlend(const Model& m, int from, float tFrom, int to, float tTo, float w, float* out, float radioRaise, float* radioHand = nullptr) {
    JointMatricesBlend(m, from, tFrom, to, tTo, w, out, nullptr, radioRaise, radioHand);
}
void SkinPosition(const Model& m, const float* jointMats, const Vertex& v, float out[3]);

// Index of the clip whose name contains `word` (case-insensitive), or -1.
void Slerp(const float* a, const float* b, float t, float* out);       // unit quaternions x y z w
int FindClip(const Model& m, const char* word);
// The clip a model has for an emote (1 wave, 2 dance, 3 cheer, 4 yes, 5 no; others have none), or -1: Wave, Dance / Samba, ThumbsUp / Jump, Yes / agree, No / headShake. "No" must be the whole name.
int EmoteClip(const Model& m, int emote);

// How the model is scaled and put on the ground: feet at the origin, centred, `height` metres tall.
struct Fit { float scale, offset[3]; };
Fit FitToHeight(const Model& m, float height);
constexpr float kNoEye = -1e30f;       // EyeHeight: no joint matched (a real height can be negative: it is measured in the space bmin/bmax are in)
// Bind-pose height (model space, same Y as bmin/bmax) of the eyes: the mean of the joints whose node name contains `nameContains`. kNoEye when none matches.
float EyeHeight(const Model& m, const char* nameContains);
// Scale and put the model so that its eyes (the joints above) are `eye` metres above its feet. No such joints: the model is `eye / 0.92` high.
Fit FitToEye(const Model& m, float eye, const char* nameContains);
// The same height for a pose: `jointMats` are the skin matrices JointMatrices() gave (kMaxJoints * 16 floats). Multiply (height - bmin[1]) by the fit's scale for metres above the feet.
float EyeHeightPosed(const Model& m, const float* jointMats, const char* nameContains);
constexpr float kCameraEye = 1.71f;    // the puppet's feet are this far below the camera of the player it stands for

// ---- camera (the game's camera: position, heading theta, pitch phi, z up, vertical field of view)
struct Cam { float pos[3], theta, phi, vfovDeg, aspect, nearZ, farZ; };
// NDC of a world point as the existing puppet code computes it; false when it is behind the near distance.
bool ProjectPoint(const Cam& c, const float* w, float* nx, float* ny);
void ViewProj(const Cam& c, float out[16]);                 // world -> clip, D3D depth 0..1 (0 at the near plane)
void ModelMatrix(const float feet[3], float yaw, const Fit& f, float yawOffset, float out[16]);   // model space -> world

// ---- the remote player's walk state
struct GaitCfg { float refSpeed = 1.6f; float runSpeed = 4.5f; float idleBelow = 0.25f; float tau = 0.15f; float runRef = 0; float fadeMs = 200; };   // runRef: m/s the run clip was made for (0: the walk's refSpeed); fadeMs: cross-fade between clips
// The pose to draw: `clip` at `time`; while `blend` < 1 it is cross-faded from `from` at `fromTime` (from < 0: no fade).
struct ClipPick { int clip; float time; int from = -1; float fromTime = 0; float blend = 1; };
class Avatar {
public:
    explicit Avatar(GaitCfg g = {}, uint32_t seed = 1) : rng_(seed * 2654435761u | 1u), g_(g) { fidgetDue_ = 6.0f + 10.0f * Rand(); }
    // Feed the latest remote pose every frame; seq changes when a new datagram has arrived. nowMs is a monotonic clock.
    void Update(uint64_t nowMs, bool valid, unsigned seq, const float pos[3], float yaw);
    ClipPick Pick(const Model& m) const;
    // An emote or a seat is playing: no fidget starts, a running one stops.
    void SetBusy(bool b) { busy_ = b; }
    bool fidgeting() const { return fidgetClip_ >= 0; }
    const float* pos() const { return pos_; }
    float yaw() const { return yaw_; }
    float speed() const { return speed_; }
    float phase() const { return phase_; }          // seconds of animation time
private:
    struct Sel { int clip = -1, kind = 0; };      // kind: 0 idle, 1 walk, 2 run, 3 held first frame, 4 a fidget played once: which phase counter times the clip
    Sel Choose(const Model& m) const; float TimeOf(const Model& m, int kind, int clip) const;
    float Rand() const { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return (rng_ & 0xFFFFFF) / 16777216.0f; }
    mutable Sel last_, prev_; mutable uint64_t fadeStartMs_ = 0;
    // fidgets: after fidgetDue_ seconds of standing still a clip named fidget* plays once, then the next one is due 8 to 22 s later
    mutable uint32_t rng_; mutable uint64_t idleSinceMs_ = 0, fidgetStartMs_ = 0; mutable int fidgetClip_ = -1; mutable float fidgetDue_ = 10; bool busy_ = false;
    GaitCfg g_; bool have_ = false; unsigned lastSeq_ = 0; uint64_t lastMs_ = 0, lastSeqMs_ = 0;
    float target_[3] = {0, 0, 0}, pos_[3] = {0, 0, 0}, lastTarget_[3] = {0, 0, 0}, yaw_ = 0, speed_ = 0, phase_ = 0, runPhase_ = 0, idlePhase_ = 0;
};

}  // namespace wm
