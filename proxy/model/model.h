#pragma once
// Game-free character model code: glTF binary (.glb) loading, skeletal animation, CPU skinning (used by the tests), the camera
// maths and the remote player's walk state. Nothing here includes windows.h or d3d11.h, so `make -C proxy/model test` runs it
// without a game. Matrices are column-major (m[col * 4 + row]), like glTF, and act on column vectors.
#include <cstdint>
#include <string>
#include <vector>

namespace wm {

constexpr int kMaxJoints = 128;      // including the extra identity joint that unskinned geometry is bound to

struct Vertex { float pos[3], nrm[3], uv[2]; uint8_t joints[4]; float weights[4]; };

struct Node { int parent = -1; float t[3] = {0, 0, 0}, r[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1}; std::string name; };

struct Channel { int node = -1; int path = 0; bool step = false; std::vector<float> times, values; };   // path 0 translation, 1 rotation, 2 scale
struct Clip { std::string name; float duration = 0; std::vector<Channel> channels; };

struct Model {
    std::vector<Vertex> verts; std::vector<uint32_t> idx;
    std::vector<Node> nodes;
    std::vector<int> joints;          // node index of each joint of the first skin
    std::vector<float> ibm;           // inverse bind matrices, 16 floats per joint
    std::vector<Clip> clips;
    int texW = 0, texH = 0; std::vector<uint8_t> rgba;   // base colour texture of the first textured material (empty: none)
    float baseColor[4] = {1, 1, 1, 1};
    float bmin[3] = {0, 0, 0}, bmax[3] = {0, 0, 0};       // bind-pose bounds in model space (glTF: Y up, the front faces +Z)
    int jointCount() const { return (int)joints.size() + 1; }   // + the identity joint
};

bool LoadGlb(const uint8_t* data, size_t n, Model* out, std::string* err);

// Joint matrices (jointCount() * 16 floats) for clip `clip` at `t` seconds (looped). clip < 0: the bind pose.
void JointMatrices(const Model& m, int clip, float t, float* out);
void SkinPosition(const Model& m, const float* jointMats, const Vertex& v, float out[3]);

// Index of the clip whose name contains `word` (case-insensitive), or -1.
void Slerp(const float* a, const float* b, float t, float* out);       // unit quaternions x y z w
int FindClip(const Model& m, const char* word);

// How the model is scaled and put on the ground: feet at the origin, centred, `height` metres tall.
struct Fit { float scale, offset[3]; };
Fit FitToHeight(const Model& m, float height);

// ---- camera (the game's camera: position, heading theta, pitch phi, z up, vertical field of view)
struct Cam { float pos[3], theta, phi, vfovDeg, aspect, nearZ, farZ; };
// NDC of a world point as the existing puppet code computes it; false when it is behind the near distance.
bool ProjectPoint(const Cam& c, const float* w, float* nx, float* ny);
void ViewProj(const Cam& c, float out[16]);                 // world -> clip, D3D depth 0..1 (0 at the near plane)
void ModelMatrix(const float feet[3], float yaw, const Fit& f, float yawOffset, float out[16]);   // model space -> world

// ---- the remote player's walk state
struct GaitCfg { float refSpeed = 1.6f; float runSpeed = 4.5f; float idleBelow = 0.25f; float tau = 0.15f; };
struct ClipPick { int clip; float time; };
class Avatar {
public:
    explicit Avatar(GaitCfg g = {}) : g_(g) {}
    // Feed the latest remote pose every frame; seq changes when a new datagram has arrived. nowMs is a monotonic clock.
    void Update(uint64_t nowMs, bool valid, unsigned seq, const float pos[3], float yaw);
    ClipPick Pick(const Model& m) const;
    const float* pos() const { return pos_; }
    float yaw() const { return yaw_; }
    float speed() const { return speed_; }
    float phase() const { return phase_; }          // seconds of animation time
private:
    GaitCfg g_; bool have_ = false; unsigned lastSeq_ = 0; uint64_t lastMs_ = 0, lastSeqMs_ = 0;
    float target_[3] = {0, 0, 0}, pos_[3] = {0, 0, 0}, lastTarget_[3] = {0, 0, 0}, yaw_ = 0, speed_ = 0, phase_ = 0, idlePhase_ = 0;
};

}  // namespace wm
