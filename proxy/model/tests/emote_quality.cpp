// The emote quality gate (T0, no game): plays every clip through the production pose code
// (emote_sim.h: idle, clip, springs, foot IK) at 120 Hz on the real character and checks what an
// animation reviewer would look for, as numbers. docs/ANIMATION.md lists the checks.
//   emote-quality <glb> [clips file|-] [--json report.json] [--expect check] [--only id]
// Exit 0 when every clip passes. With --expect, exit 0 only when that check fails (bad-clip fixtures).
#include "../emote_sim.h"
#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>
using namespace wm;

static float Dist(const std::array<float, 3>& a, const std::array<float, 3>& b) { float d = 0; for (int k = 0; k < 3; ++k) d += (a[k] - b[k]) * (a[k] - b[k]); return sqrtf(d); }

// A body part's surface in bind space: its vertices and normals in a 2 cm grid. A posed point is
// carried back into bind space by the inverse skin matrix of the part's joint (so the test follows
// the bending body), then its signed distance is taken to the nearest surface vertex along that
// vertex's normal: negative is inside. Accurate to the mesh's vertex spacing (a few millimetres).
struct Surface {
    static constexpr float Cell = .02f;
    std::vector<std::array<float, 6>> pts; std::vector<int> start; int n[3] = {0, 0, 0}; float lo[3] = {0, 0, 0};
    void Add(const float p[3], const float q[3]) { pts.push_back({p[0], p[1], p[2], q[0], q[1], q[2]}); }
    int CellOf(const float* p, int k) const { return int(floorf((p[k] - lo[k]) / Cell)); }
    void Finish() {      // a dense grid: the points sorted by cell, start[] indexes each cell's run
        for (int k = 0; k < 3; ++k) { float a = 1e9f, b = -1e9f; for (auto& q : pts) { a = std::min(a, q[k]); b = std::max(b, q[k]); } lo[k] = a - Cell; n[k] = int((b - lo[k]) / Cell) + 2; }
        auto idx = [&](const std::array<float, 6>& q) { return (CellOf(q.data(), 0) * n[1] + CellOf(q.data(), 1)) * n[2] + CellOf(q.data(), 2); };
        std::sort(pts.begin(), pts.end(), [&](const std::array<float, 6>& x, const std::array<float, 6>& y) { return idx(x) < idx(y); });
        start.assign(size_t(n[0]) * n[1] * n[2] + 1, 0); for (auto& q : pts) start[idx(q) + 1]++;
        for (size_t i = 1; i < start.size(); ++i) start[i] += start[i - 1];
    }
    float Signed(const float p[3]) const {      // 1 when no surface is within about 4 cm
        int c[3]; for (int k = 0; k < 3; ++k) c[k] = CellOf(p, k);
        float best = 1e9f, sd = 1;
        for (int dx = -2; dx <= 2; ++dx) for (int dy = -2; dy <= 2; ++dy) for (int dz = -2; dz <= 2; ++dz) {
            int x = c[0] + dx, y = c[1] + dy, z = c[2] + dz; if (x < 0 || y < 0 || z < 0 || x >= n[0] || y >= n[1] || z >= n[2]) continue;
            int cell = (x * n[1] + y) * n[2] + z;
            for (int i = start[cell]; i < start[cell + 1]; ++i) {
                const auto& q = pts[i]; float d[3] = {p[0] - q[0], p[1] - q[1], p[2] - q[2]}, d2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
                if (d2 < best) { best = d2; float dot = d[0] * q[3] + d[1] * q[4] + d[2] * q[5]; sd = dot < 0 ? dot : sqrtf(d2); }
            }
        }
        return sd;
    }
};
static void ToBind(const float* skin, const float p[3], float out[3]) {      // inverse of a rigid skin matrix
    float d[3] = {p[0] - skin[12], p[1] - skin[13], p[2] - skin[14]};
    for (int c = 0; c < 3; ++c) out[c] = skin[c * 4] * d[0] + skin[c * 4 + 1] * d[1] + skin[c * 4 + 2] * d[2];
}

struct Fail { int id; std::string check, detail; };
static std::vector<Fail> g_fails;
static void Flag(int id, const char* check, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
static void Flag(int id, const char* check, const char* fmt, ...) {
    char buf[300]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    for (auto& f : g_fails) if (f.id == id && f.check == check) return;      // the first instance of each check is enough
    g_fails.push_back({id, check, buf});
}

// Anatomical ranges (degrees; left bones in the same anatomical sense as the right). The pose is
// the filtered pose the renderer draws, springs and overshoot included.
struct Range { float lo[3], hi[3]; };
static const Range kRange[GestureBones] = {
    {{-25, -35, -25}, {45, 35, 25}},   {{-30, -40, -25}, {40, 40, 25}},  {{-40, -50, -30}, {45, 50, 30}},  {{-45, -70, -35}, {50, 70, 35}},
    {{-60, -100, -30}, {180, 100, 175}}, {{0, -15, -25}, {150, 15, 25}},   {{-80, -90, -45}, {80, 90, 45}},
    {{-60, -100, -30}, {180, 100, 175}}, {{0, -15, -25}, {150, 15, 25}},   {{-80, -90, -45}, {80, 90, 45}},
    {{-40, -30, -45}, {120, 30, 45}},  {{-150, -5, -5}, {0, 5, 5}},       {{-45, -20, -20}, {45, 20, 20}},
    {{-40, -30, -45}, {120, 30, 45}},  {{-150, -5, -5}, {0, 5, 5}},       {{-45, -20, -20}, {45, 20, 20}},
    {{-25, -40, -20}, {40, 40, 20}},
    {{-110, -20, -110}, {110, 20, 110}}, {{-80, -60, -80}, {80, 60, 80}},      // fingers (one curl), thumb
    {{-110, -20, -110}, {110, 20, 110}}, {{-80, -60, -80}, {80, 60, 80}},
};

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: emote-quality <glb> [clips|-] [--json out] [--expect check] [--only id]\n"); return 2; }
    std::string clipsPath = argc > 2 && argv[2][0] != '-' ? argv[2] : "-", jsonPath, expect; int only = 0;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--json") && i + 1 < argc) jsonPath = argv[++i];
        else if (!strcmp(argv[i], "--expect") && i + 1 < argc) expect = argv[++i];
        else if (!strcmp(argv[i], "--only") && i + 1 < argc) only = atoi(argv[++i]);
    }
    std::ifstream f(argv[1], std::ios::binary); std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
    Model m; std::string err; if (!LoadGlb(bytes.data(), bytes.size(), &m, &err)) { fprintf(stderr, "model: %s\n", err.c_str()); return 3; }
    if (clipsPath != "-") {
        std::ifstream c(clipsPath); std::string text((std::istreambuf_iterator<char>(c)), {});
        EmoteLibrary lib; if (!lib.Parse(text, &err, clipsPath.c_str())) { printf("FAIL parse: %s\n", err.c_str()); return expect == "parse" ? 0 : 1; }
        // A fixture may hold a single clip: it replaces the embedded one with the same id.
        for (auto& clip : lib.clips) { bool put = false; for (auto& e : EmoteClips().clips) if (e.id == clip.id) { e = clip; put = true; } if (!put) EmoteClips().clips.push_back(clip); }
        if (lib.clips.size() == 1 && !only) only = lib.clips[0].id;
    }
    // ---- the catalog: every wire id has its clip, with the catalog's name, duration and loop flag
    for (int id = 1; id <= witness::kEmoteCount; ++id) {
        const EmoteAnim* c = EmoteClips().ById(id);
        if (!c) { Flag(id, "catalog", "no clip"); continue; }
        if (c->name != witness::kEmotes[id].name) Flag(id, "catalog", "name '%s' is '%s' in core/emotes.h", c->name.c_str(), witness::kEmotes[id].name);
        if (fabsf(c->duration - witness::kEmotes[id].duration) > 1e-3f) Flag(id, "catalog", "duration %.2f is %.2f in core/emotes.h", c->duration, witness::kEmotes[id].duration);
        if (c->Loops() != witness::kEmotes[id].loop) Flag(id, "catalog", "loop flag differs from core/emotes.h");
        if (witness::kEmotes[id].mobile) for (int b : {int(ThighL), int(ShinL), int(FootL), int(ThighR), int(ShinR), int(FootR), int(Pelvis), int(kHipsBit), int(kTurnBit)})
            if (c->Drives(b)) Flag(id, "mobile", "plays while walking but drives %s", b < GestureBones ? kGestureBones[b] : b == kHipsBit ? "the hips" : "the turn");
    }
    if (!EmoteClips().ById(kTalkClip)) Flag(kTalkClip, "catalog", "no Talk clip (id 100)");

    // ---- the body's shapes, from the mesh in bind pose
    std::vector<float> bind(kMaxJoints * 16); JointMatrices(m, -1, 0, bind.data());
    std::map<std::string, int> J; for (size_t j = 0; j < m.joints.size(); ++j) J[m.nodes[m.joints[j]].name] = int(j);
    auto dominant = [&](const Vertex& v) { int b = 0; for (int k = 1; k < 4; ++k) if (v.weights[k] > v.weights[b]) b = k; return v.weights[b] > .5f ? int(v.joints[b]) : -1; };
    Surface torsoS[2], headS;      // the torso in the spine's frame (below the chest) and the chest's; the head (with the hat)
    std::vector<size_t> handV[2], foreV[2], footV, thighV[2];
    for (size_t i = 0; i < m.verts.size(); ++i) {
        int d = dominant(m.verts[i]); if (d < 0) continue; const std::string& n = m.nodes[m.joints[d]].name;
        float p[3]; SkinPosition(m, bind.data(), m.verts[i], p); const float* nr = m.verts[i].nrm;
        if ((n == "pelvis" || n == "spine" || n == "chest") && p[1] >= .98f) { if (p[1] < 1.26f) torsoS[0].Add(p, nr); if (p[1] > 1.14f) torsoS[1].Add(p, nr); }
        else if (n == "head" || n == "neck" || n.rfind("eye", 0) == 0 || n.rfind("lid", 0) == 0 || n.rfind("brow", 0) == 0 || n == "lip_lower") headS.Add(p, nr);
        else if (n == "hand.R" && i % 6 == 0) handV[1].push_back(i); else if (n == "hand.L" && i % 6 == 0) handV[0].push_back(i);
        else if (n == "forearm.R" && i % 6 == 0) foreV[1].push_back(i); else if (n == "forearm.L" && i % 6 == 0) foreV[0].push_back(i);
        else if ((n == "foot.L" || n == "foot.R") && i % 3 == 0) footV.push_back(i);
        else if (n == "thigh.L" && i % 6 == 0) thighV[0].push_back(i); else if (n == "thigh.R" && i % 6 == 0) thighV[1].push_back(i);
    }
    torsoS[0].Finish(); torsoS[1].Finish(); headS.Finish();
    const int jSpine = J["spine"], jChest = J["chest"], jHead = J["head"];
    // the face: a point in front of the eyes, carried by the head (a nod or a shake moves it, the neck joint hardly)
    auto face = [&](const float* mats) { const float b[3] = {0, 1.66f, .14f}; const float* s = mats + jHead * 16; std::array<float, 3> o; for (int r = 0; r < 3; ++r) o[r] = s[r] * b[0] + s[4 + r] * b[1] + s[8 + r] * b[2] + s[12 + r]; return o; };
    const int jHand[2] = {J["hand.L"], J["hand.R"]}, jFoot[2] = {J["foot.L"], J["foot.R"]};
    // A posed point's signed distance outside the torso (in the frame of the nearer of spine and chest) and outside the head.
    auto torsoOut = [&](const float* mats, const float p[3]) {
        float a[3], b[3]; ToBind(mats + jSpine * 16, p, a); ToBind(mats + jChest * 16, p, b);
        float da = a[1] < 1.22f ? torsoS[0].Signed(a) : 1.f, db = b[1] >= 1.18f ? torsoS[1].Signed(b) : 1.f; return std::min(da, db);
    };
    auto headOut = [&](const float* mats, const float p[3]) { float a[3]; ToBind(mats + jHead * 16, p, a); return headS.Signed(a); };

    // ---- every clip
    struct Report { int id; std::string name; float peak = 0, accel = 0, slide = 0, sink = 0, sinkT = 0, pen = 0, balance = 0, hold = 1e9f, twin = 0, eyeDrop = 0; std::vector<float> touch; };
    std::vector<Report> reports; std::vector<std::vector<std::array<float, 3>>> features;
    std::vector<int> ids; for (int id = 1; id <= witness::kEmoteCount; ++id) ids.push_back(id); ids.push_back(kTalkClip);
    const float H = 1.f / 120;
    for (int id : ids) {
        if (only && id != only) continue;
        const EmoteAnim* c = EmoteClips().ById(id); if (!c) continue;
        bool talk = id == kTalkClip, loops = c->Loops();
        float length = loops ? c->loopB + 2 * (c->loopB - c->loopA) : c->duration + 1.0f;
        Report R; R.id = id; R.name = c->name; R.touch.assign(c->touches.size(), 1e9f);
        EmoteSim sim(m, false), base(m, false);
        std::vector<float> mats(kMaxJoints * 16), bm(kMaxJoints * 16), prev;
        std::array<float, 3> pv[3] = {}, pv2[3] = {}; int have = 0;
        std::vector<std::array<float, 3>> feat;
        float eyeBase = EyeHeightPosed(m, bind.data(), "eye");
        std::vector<std::array<float, 3>> holdWin; float lastMove = 0; float holdStart = -1;
        GesturePose lastPose{};
        float twinMax = 0; bool bothArms = c->Drives(ArmL) && c->Drives(ArmR);
        for (int fr = 0; fr * H < length; ++fr) {
            float t = fr * H;
            if (talk) sim.Frame(0, 0, t, H, mats.data(), true); else sim.Frame(id, t, t, H, mats.data());
            base.Frame(0, 0, t, H, bm.data());
            const GestureState& g = sim.g;
            float env = talk ? g.talk : witness::EmoteEnvelope(id, t);
            // limits on the filtered pose
            for (int b = 0; b < GestureBones; ++b) if (g.pose[b].weight > .01f) for (int a = 0; a < 3; ++a) {
                float v = g.pose[b].a[a] * (a && IsLeftBone(b) ? -1.f : 1.f);
                if (!std::isfinite(v) || v < kRange[b].lo[a] - .5f || v > kRange[b].hi[a] + .5f) Flag(id, "limits", "%s axis %d at %.1f deg (t %.2f), range %.0f..%.0f", kGestureBones[b], a, v, t, kRange[b].lo[a], kRange[b].hi[a]);
            }
            for (int k = 0; k < 3; ++k) if (fabsf(g.hips[k] * g.hipsWeight) > (k == 1 ? 50.f : 12.f)) Flag(id, "limits", "hips axis %d at %.1f cm", k, g.hips[k] * g.hipsWeight);
            for (size_t j = 0; j < m.joints.size() * 16; ++j) if (!std::isfinite(mats[j])) { Flag(id, "finite", "joint matrix not finite at t %.2f", t); break; }
            // joints: speed and acceleration (pops), the readable peak, features for distinctness
            std::array<float, 3> P[3] = {JointPos(m, mats.data(), jHand[1]), JointPos(m, mats.data(), jHand[0]), face(mats.data())};
            std::array<float, 3> B[3] = {JointPos(m, bm.data(), jHand[1]), JointPos(m, bm.data(), jHand[0]), face(bm.data())};
            for (int k = 0; k < 3; ++k) {
                R.peak = std::max(R.peak, Dist(P[k], B[k]) * (k == 2 ? 2.f : 1.f));      // the face moves less but reads as much
                if (have >= 1 && Dist(P[k], pv[k]) > .075f) Flag(id, "pop", "%s moved %.3f m in one 120 Hz frame (over 9 m/s) at t %.2f", k == 2 ? "the face" : k ? "hand.L" : "hand.R", Dist(P[k], pv[k]), t);
                if (have >= 2) { float a = 0; for (int x = 0; x < 3; ++x) { float d = (P[k][x] - 2 * pv[k][x] + pv2[k][x]) / (H * H); a += d * d; } R.accel = std::max(R.accel, sqrtf(a)); }
                pv2[k] = pv[k]; pv[k] = P[k];
            }
            have++;
            if (fr % 12 == 0) for (int k = 0; k < 3; ++k) { float w = k == 2 ? 2.f : 1.f; feat.push_back({(P[k][0] - B[k][0]) * w, (P[k][1] - B[k][1]) * w, (P[k][2] - B[k][2]) * w}); }
            // feet: planted soles stay where the gait put them, and never sink
            float minY = 1e9f, baseMinY = 1e9f, fx0 = 1e9f, fx1 = -1e9f, fz0 = 1e9f, fz1 = -1e9f;
            for (size_t v : footV) {
                float p[3], q[3]; SkinPosition(m, mats.data(), m.verts[v], p); SkinPosition(m, bm.data(), m.verts[v], q);
                minY = std::min(minY, p[1]); baseMinY = std::min(baseMinY, q[1]);
                fx0 = std::min(fx0, q[0]); fx1 = std::max(fx1, q[0]); fz0 = std::min(fz0, q[2]); fz1 = std::max(fz1, q[2]);
            }
            if (baseMinY - minY > R.sink) { R.sink = baseMinY - minY; R.sinkT = t; }
            if (g.plant > .99f && !talk) for (int s = 0; s < 2; ++s) {      // planted feet pivot with the body's turn (about the root, at the origin)
                auto q = JointPos(m, bm.data(), jFoot[s]); float ct = cosf(g.turn), st = sinf(g.turn);
                const float* sp = g.steps + s * 3; float w = .01f * g.hipsWeight;      // and take the clip's steps
                std::array<float, 3> turned = {q[0] * ct + q[2] * st + (sp[0] * ct + sp[2] * st) * w, q[1] + sp[1] * w, -q[0] * st + q[2] * ct + (-sp[0] * st + sp[2] * ct) * w};
                R.slide = std::max(R.slide, Dist(JointPos(m, mats.data(), jFoot[s]), turned));
            }
            // balance: the centre of mass over the feet while they are planted
            if (g.plant > .99f && env > .5f) {
                static const std::pair<const char*, float> mass[] = {{"pelvis", .30f}, {"spine", .12f}, {"chest", .20f}, {"head", .08f}, {"upper_arm.L", .03f}, {"upper_arm.R", .03f}, {"forearm.L", .02f}, {"forearm.R", .02f}, {"hand.L", .01f}, {"hand.R", .01f}, {"thigh.L", .07f}, {"thigh.R", .07f}, {"shin.L", .02f}, {"shin.R", .02f}};
                float cx = 0, cz = 0, wsum = 0; for (auto& ms : mass) { auto p = JointPos(m, mats.data(), J[ms.first]); cx += p[0] * ms.second; cz += p[2] * ms.second; wsum += ms.second; }
                cx /= wsum; cz /= wsum; float out = std::max({fx0 - cx, cx - fx1, fz0 - cz, cz - fz1, 0.f});      // past the soles' outline: falling
                R.balance = std::max(R.balance, out);
            }
            // penetration: hands and forearms through the torso or the head; contacts declared by `touch`
            for (int s = 0; s < 2; ++s) {
                int hb = s ? HandR : HandL, fb = s ? ElbowR : ElbowL;
                bool allowedT = false, allowedH = false;
                for (auto& tc : c->touches) if (tc.a == hb && t >= tc.from && t <= tc.to) { if (tc.b == Head || tc.b == Neck) allowedH = true; else if (tc.b == Chest || tc.b == Spine || tc.b == Pelvis) allowedT = true; }
                for (int part = 0; part < 2; ++part) for (size_t v : part ? foreV[s] : handV[s]) {
                    float p[3]; SkinPosition(m, mats.data(), m.verts[v], p);
                    float tol = part ? .015f : .01f;      // a sleeve or a hand may press 1-1.5 cm into the soft coat or the scarf before it reads as clipping
                    float dt = torsoOut(mats.data(), p), dh = headOut(mats.data(), p);
                    float lt = allowedT ? .03f : tol, lh = allowedH ? .025f : tol;
                    R.pen = std::max({R.pen, -dt - (allowedT ? .02f : 0.f), -dh - (allowedH ? .015f : 0.f)});
                    if (-dt > lt || -dh > lh) {      // where, in the body part's rest pose (x left, y up, z front): tells a brim from a cheek
                        float q[3]; bool head = -dh > lh; ToBind(mats.data() + (head ? jHead : p[1] > 1.2f ? jChest : jSpine) * 16, p, q);
                        Flag(id, "penetration", "%s through the %s by %.1f cm at t %.2f (rest-pose point %.2f %.2f %.2f%s)", kGestureBones[part ? fb : hb], head ? "head" : "torso", (head ? -dh : -dt) * 100, t, q[0], q[1], q[2],
                             head && q[1] > 1.69f && q[1] < 1.77f && sqrtf(q[0] * q[0] + q[2] * q[2]) > .13f ? ": the hat brim" : "");
                    }
                }
            }
            for (size_t i = 0; i < c->touches.size(); ++i) {
                const ClipTouch& tc = c->touches[i]; if (t < tc.from || t > tc.to) continue;
                int s = tc.a == HandR ? 1 : 0; float best = 1e9f;
                for (size_t v : handV[s]) {
                    float p[3]; SkinPosition(m, mats.data(), m.verts[v], p);
                    if (tc.b == Head || tc.b == Neck) best = std::min(best, headOut(mats.data(), p));
                    else if (tc.b == Chest || tc.b == Spine || tc.b == Pelvis) best = std::min(best, torsoOut(mats.data(), p));
                    else if (tc.b == HandL || tc.b == HandR) for (size_t w : handV[tc.b == HandR]) { float q[3]; SkinPosition(m, mats.data(), m.verts[w], q); best = std::min(best, sqrtf((p[0]-q[0])*(p[0]-q[0])+(p[1]-q[1])*(p[1]-q[1])+(p[2]-q[2])*(p[2]-q[2]))); }
                    else if (tc.b == ThighL || tc.b == ThighR) for (size_t w : thighV[tc.b == ThighR]) { float q[3]; SkinPosition(m, mats.data(), m.verts[w], q); best = std::min(best, sqrtf((p[0]-q[0])*(p[0]-q[0])+(p[1]-q[1])*(p[1]-q[1])+(p[2]-q[2])*(p[2]-q[2]))); }
                }
                R.touch[i] = std::min(R.touch[i], best);
            }
            // hands through each other (a clap): the palms' centres stay apart
            if (c->Drives(HandL) && c->Drives(HandR)) {
                float cl[3] = {}, cr[3] = {};
                for (size_t v : handV[0]) { float p[3]; SkinPosition(m, mats.data(), m.verts[v], p); for (int k = 0; k < 3; ++k) cl[k] += p[k] / handV[0].size(); }
                for (size_t v : handV[1]) { float p[3]; SkinPosition(m, mats.data(), m.verts[v], p); for (int k = 0; k < 3; ++k) cr[k] += p[k] / handV[1].size(); }
                float d = sqrtf((cl[0]-cr[0])*(cl[0]-cr[0])+(cl[1]-cr[1])*(cl[1]-cr[1])+(cl[2]-cr[2])*(cl[2]-cr[2]));
                if (d < .045f) Flag(id, "penetration", "the hands pass through each other (palm centres %.1f cm apart) at t %.2f", d * 100, t);
            }
            // moving holds: while the emote is fully in, something always moves
            if (env > .99f) {
                float mv = 0; for (int b = 0; b < GestureBones; ++b) for (int a = 0; a < 3; ++a) mv = std::max(mv, fabsf(g.pose[b].a[a] - lastPose[b].a[a]) * g.pose[b].weight);
                if (holdStart < 0) { holdStart = t; lastMove = t; }
                if (mv > 0.004f) lastMove = t;      // 0.004 deg per 120 Hz frame: 0.5 deg/s
                R.hold = std::min(R.hold, -(t - lastMove));
                if (t - lastMove > .6f) Flag(id, "dead-hold", "nothing moves for %.2f s from t %.2f", t - lastMove, lastMove);
            } else holdStart = -1;
            lastPose = g.pose;
            // twinning: both arms in exact mirror the whole time looks mechanical
            if (bothArms) for (int pr = 0; pr < 3; ++pr) for (int a = 0; a < 3; ++a) {
                int r = ArmR + pr, l = ArmL + pr; float lv = g.pose[l].a[a] * (a ? -1.f : 1.f);
                twinMax = std::max(twinMax, fabsf(g.pose[r].a[a] - lv) * std::min(g.pose[r].weight, g.pose[l].weight));
            }
            if (c->eyeDrop[0] >= 0 && env > .99f && t > c->duration * .5f) R.eyeDrop = std::max(R.eyeDrop, eyeBase - EyeHeightPosed(m, mats.data(), "eye"));
            prev = mats;
        }
        R.twin = twinMax;
        // ---- the verdicts that need the whole clip
        if (!talk && !loops) for (auto& j : sim.g.pose) if (j.weight > .001f) { Flag(id, "exit", "the pose has not let go %.1f s after the end", 1.0f); break; }
        if (R.slide > .012f) Flag(id, "foot-slide", "a planted foot slid %.1f cm", R.slide * 100);
        if (R.sink > .012f) Flag(id, "ground", "a sole went %.1f cm into the ground at t %.2f", R.sink * 100, R.sinkT);
        if (R.balance > .01f) Flag(id, "balance", "the centre of mass leaves the feet by %.1f cm", R.balance * 100);
        if (R.accel > 260) Flag(id, "jerk", "a hand or the head accelerates at %.0f m/s^2 (a pop or a snap too hard to read)", R.accel);
        if (R.peak < (talk ? .06f : .12f)) Flag(id, "readable", "the biggest hand or head displacement is only %.2f m: the emote will not read at a distance", R.peak);
        if (bothArms && !c->symmetric && twinMax < 6) Flag(id, "twinning", "both arms mirror each other exactly (largest difference %.1f deg): offset one in time or amount, or mark the clip symmetric", twinMax);
        for (size_t i = 0; i < c->touches.size(); ++i) if (R.touch[i] > c->touches[i].within) Flag(id, "contact", "%s never comes within %.1f cm of %s (closest %.1f cm)", kGestureBones[c->touches[i].a], c->touches[i].within * 100, kGestureBones[c->touches[i].b], R.touch[i] * 100);
        if (c->eyeDrop[0] >= 0 && (R.eyeDrop < c->eyeDrop[0] || R.eyeDrop > c->eyeDrop[1])) Flag(id, "eye-drop", "the eyes drop %.3f m, wanted %.2f..%.2f", R.eyeDrop, c->eyeDrop[0], c->eyeDrop[1]);
        if (loops) {      // the loop seam: the raw clip is continuous across the wrap
            float a[kClipChannels], b[kClipChannels]; SampleClip(*c, c->loopB - 1e-3f, a); SampleClip(*c, c->loopB + 1e-3f, b);
            for (int ch = 0; ch < kClipChannels; ++ch) if (fabsf(a[ch] - b[ch]) > .5f) { Flag(id, "loop-seam", "channel %d jumps %.1f across the loop", ch, b[ch] - a[ch]); break; }
        }
        reports.push_back(R); features.push_back(feat);
    }
    // ---- distinct: no two emotes look alike over their first seconds
    for (size_t a = 0; a < reports.size(); ++a) for (size_t b = a + 1; b < reports.size(); ++b) {
        size_t n = std::min(features[a].size(), features[b].size()); if (!n) continue; float d = 0;
        for (size_t i = 0; i < n; ++i) d += Dist(features[a][i], features[b][i]);
        d /= n;
        if (d < .02f) Flag(reports[b].id, "distinct", "looks like %s (mean difference %.3f m)", reports[a].name.c_str(), d);
    }
    // ---- the report
    for (auto& R : reports) {
        int nf = 0; for (auto& fl : g_fails) nf += fl.id == R.id;
        printf("%-3d %-13s %s peak %.2f m  accel %4.0f  slide %.1f cm  pen %4.1f cm  balance %.1f cm  twin %4.1f%s\n", R.id, R.name.c_str(), nf ? "FAIL" : "ok  ", R.peak, R.accel, R.slide * 100, std::max(0.f, R.pen) * 100, R.balance * 100, R.twin, R.eyeDrop > 0 ? (" eye-drop " + std::to_string(R.eyeDrop)).c_str() : "");
    }
    for (auto& fl : g_fails) printf("FAIL %d %s: %s\n", fl.id, fl.check.c_str(), fl.detail.c_str());
    if (!jsonPath.empty()) {
        FILE* j = fopen(jsonPath.c_str(), "w");
        if (j) {
            fprintf(j, "{\"clips\":[");
            for (size_t i = 0; i < reports.size(); ++i) {
                auto& R = reports[i]; fprintf(j, "%s{\"id\":%d,\"name\":\"%s\",\"peak\":%.3f,\"accel\":%.1f,\"slide_cm\":%.2f,\"penetration_cm\":%.2f,\"balance_cm\":%.2f,\"twin_deg\":%.1f,\"fails\":[", i ? "," : "", R.id, R.name.c_str(), R.peak, R.accel, R.slide * 100, std::max(0.f, R.pen) * 100, R.balance * 100, R.twin);
                bool first = true; for (auto& fl : g_fails) if (fl.id == R.id) { fprintf(j, "%s{\"check\":\"%s\",\"detail\":\"%s\"}", first ? "" : ",", fl.check.c_str(), fl.detail.c_str()); first = false; }
                fprintf(j, "]}");
            }
            fprintf(j, "]}\n"); fclose(j);
        }
    }
    if (!expect.empty()) {
        for (auto& fl : g_fails) if (fl.check == expect) { printf("expected failure '%s' found\n", expect.c_str()); return 0; }
        printf("EXPECTED a '%s' failure, none found\n", expect.c_str()); return 1;
    }
    if (g_fails.empty()) printf("emote quality: %zu clips passed every check\n", reports.size());
    return g_fails.empty() ? 0 : 1;
}
