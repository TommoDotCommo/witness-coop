#pragma once
// Keyframed emote clips: the text format in assets/emotes/*.emote, its parser and its evaluator.
// Game-free and header-only: the proxy (embedded copy, emote_data.h), the quality gate
// (tests/emote_quality.cpp) and the review renderer (tools/emote_render.cpp) all use this code,
// so what is reviewed is what is played. The format is documented in docs/ANIMATION.md.
//
// Channels are degrees per bone (forward, twist, side, resolved against the rig's bind axes in
// model.cpp, exactly like the old procedural poses), the hips' offset in centimetres (x to the
// character's left, y up, z forward) and the body's turn about the feet in degrees.
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace wm {

// The order is the runtime pose array's order (gestures.h). Pelvis was added for weight shifts.
enum GestureBone { Spine, Chest, Neck, Head, ArmL, ElbowL, HandL, ArmR, ElbowR, HandR, ThighL, ShinL, FootL, ThighR, ShinR, FootR, Pelvis, GestureBones };
inline constexpr const char* kGestureBones[] = {"spine","chest","neck","head","upper_arm.L","forearm.L","hand.L","upper_arm.R","forearm.R","hand.R","thigh.L","shin.L","foot.L","thigh.R","shin.R","foot.R","pelvis"};
// Short names authors use in clips; the rig names above are accepted too.
inline constexpr const char* kGestureAlias[] = {"spine","chest","neck","head","arm.L","fore.L","hand.L","arm.R","fore.R","hand.R","thigh.L","knee.L","foot.L","thigh.R","knee.R","foot.R","pelvis"};
inline constexpr int kHipsChannel = GestureBones * 3, kTurnChannel = kHipsChannel + 3, kPlantChannel = kTurnChannel + 1, kStepChannel = kPlantChannel + 1, kClipChannels = kStepChannel + 6;
// kStepChannel: where each planted foot goes, as an offset of the IK target in centimetres, model space (left foot xyz, then right foot xyz).
// Mask bits: one per bone, then the hips' offset and the turn.
inline constexpr int kHipsBit = GestureBones, kTurnBit = GestureBones + 1;
// Overlap: how many chain steps each bone sits from the body's core. A bone samples its curve
// `drag * depth` seconds late, so the hand trails the forearm, which trails the shoulder.
inline constexpr float kChainDepth[GestureBones] = {0.5f, 1, 2, 3, 2, 3, 4.5f, 2, 3, 4.5f, 0, 0, 0, 0, 0, 0, 0};
inline bool IsLegBone(int b) { return b >= ThighL && b <= FootR; }
inline bool IsLeftBone(int b) { return b == ArmL || b == ElbowL || b == HandL || b == ThighL || b == ShinL || b == FootL; }
inline int MirrorBone(int b) {
    switch (b) { case ArmL: return ArmR; case ArmR: return ArmL; case ElbowL: return ElbowR; case ElbowR: return ElbowL; case HandL: return HandR; case HandR: return HandL;
    case ThighL: return ThighR; case ThighR: return ThighL; case ShinL: return ShinR; case ShinR: return ShinL; case FootL: return FootR; case FootR: return FootL; default: return b; }
}

struct ClipKey { float t = 0; bool hit = false; float v[kClipChannels] = {}; };
struct ClipTouch { int a = -1, b = -1; float within = 0.05f; float from = 0, to = 1e9f; };   // the gate: bone a's hand/face mesh comes within `within` metres of b during [from, to]
struct EmoteAnim {
    int id = -1; std::string name; float duration = 0;
    float loopA = -1, loopB = -1;      // looped clips repeat [loopA, loopB) after the first pass
    float drag = 0.03f;                // seconds of lag per chain step
    float stiffness = 1;               // scales every joint spring's frequency
    bool planted = true;               // the plant channel's default: foot IK keeps the soles where the gait put them
    bool symmetric = false;            // the gate allows mirrored ("twinned") arms
    float eyeDrop[2] = {-1, -1};       // the gate: the eyes drop within this range (metres) at the clip's held pose (Sit)
    uint32_t mask = 0;                 // bones (and hips, turn) the clip drives; the others keep the gait
    std::vector<ClipKey> keys;
    std::vector<ClipTouch> touches;
    std::vector<float> mIn, mOut;      // tangents, keys.size() * kClipChannels
    bool Drives(int bit) const { return (mask >> bit) & 1u; }
    bool Loops() const { return loopA >= 0 && loopB > loopA; }
};

inline int GestureBoneNamed(const std::string& s) {
    for (int b = 0; b < GestureBones; ++b) if (s == kGestureBones[b] || s == kGestureAlias[b]) return b;
    return -1;
}

namespace clipdetail {
inline void Tangents(EmoteAnim& c) {
    size_t n = c.keys.size(); c.mIn.assign(n * kClipChannels, 0); c.mOut.assign(n * kClipChannels, 0);
    if (n < 2) return;
    // A loop seam whose keys sit exactly on loopA and loopB uses the wrapped neighbours.
    int ka = -1, kb = -1;
    if (c.Loops()) for (size_t k = 0; k < n; ++k) { if (fabsf(c.keys[k].t - c.loopA) < 1e-4f) ka = int(k); if (fabsf(c.keys[k].t - c.loopB) < 1e-4f) kb = int(k); }
    for (int ch = 0; ch < kClipChannels; ++ch) {
        auto slope = [&](size_t a, size_t b) { float h = c.keys[b].t - c.keys[a].t; return h > 1e-6f ? (c.keys[b].v[ch] - c.keys[a].v[ch]) / h : 0.f; };
        auto mono = [&](float d0, float d1, float h0, float h1) {      // Fritsch-Butland: no overshoot, flat at extremes
            if (d0 * d1 <= 0) return 0.f;
            float w1 = 2 * h1 + h0, w2 = h1 + 2 * h0; return (w1 + w2) / (w1 / d0 + w2 / d1);
        };
        for (size_t k = 0; k < n; ++k) {
            float m = 0;
            if (k > 0 && k + 1 < n) m = mono(slope(k - 1, k), slope(k, k + 1), c.keys[k].t - c.keys[k - 1].t, c.keys[k + 1].t - c.keys[k].t);
            if (ka >= 0 && kb > ka + 1 && (int(k) == ka || int(k) == kb)) {
                float h0 = c.keys[kb].t - c.keys[kb - 1].t, h1 = c.keys[ka + 1].t - c.keys[ka].t;
                m = mono(slope(kb - 1, kb), slope(ka, ka + 1), h0, h1);
            }
            c.mIn[k * kClipChannels + ch] = c.mOut[k * kClipChannels + ch] = m;
            if (c.keys[k].hit) {      // an impact: arrive at speed, stop dead, ease out of it
                c.mIn[k * kClipChannels + ch] = k > 0 ? slope(k - 1, k) : 0;
                c.mOut[k * kClipChannels + ch] = 0;
            }
        }
    }
}
inline bool Num(const std::string& s, float* out) {
    if (s.empty()) return false;
    char* e = nullptr; float v = strtof(s.c_str(), &e);
    if (!e || *e || !std::isfinite(v)) return false;
    *out = v; return true;
}
inline std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out; std::string cur; for (char ch : s) { if (ch == sep) { out.push_back(cur); cur.clear(); } else cur += ch; } out.push_back(cur); return out;
}
}

struct EmoteLibrary {
    std::vector<EmoteAnim> clips;
    const EmoteAnim* ById(int id) const { for (auto& c : clips) if (c.id == id) return &c; return nullptr; }
    const EmoteAnim* ByName(const char* n) const { for (auto& c : clips) if (c.name == n) return &c; return nullptr; }

    // Parses one or more clips. On failure `err` names the line; the library is unchanged.
    bool Parse(const std::string& text, std::string* err, const char* source = "clip") {
        using namespace clipdetail;
        std::vector<EmoteAnim> out; EmoteAnim* cur = nullptr;
        struct Pose { std::string name; float v[kClipChannels]; };
        std::vector<Pose> poses;
        int lineNo = 0; size_t pos = 0;
        auto fail = [&](const std::string& why) { if (err) *err = std::string(source) + ":" + std::to_string(lineNo) + ": " + why; return false; };
        auto finish = [&]() -> bool {
            if (!cur) return true;
            if (cur->keys.size() < 2) return fail("clip '" + cur->name + "' needs at least two keys");
            if (cur->duration <= 0) return fail("clip '" + cur->name + "' has no duration");
            if (cur->keys.back().t > cur->duration + 1e-4f) return fail("a key is after the clip's duration");
            if (cur->Loops() && cur->loopB > cur->duration + 1e-4f) return fail("the loop ends after the duration");
            cur->mask = 0;
            for (auto& k : cur->keys) for (int ch = 0; ch < kClipChannels; ++ch) if (ch != kPlantChannel && k.v[ch] != 0) cur->mask |= 1u << (ch < kHipsChannel ? ch / 3 : ch < kTurnChannel || ch >= kStepChannel ? kHipsBit : kTurnBit);
            Tangents(*cur); return true;
        };
        while (pos <= text.size()) {
            size_t e = text.find('\n', pos); if (e == std::string::npos) e = text.size();
            std::string line = text.substr(pos, e - pos); pos = e + 1; ++lineNo;
            size_t hash = line.find('#'); if (hash != std::string::npos) line.resize(hash);
            std::vector<std::string> w; { std::string t; for (char ch : line) { if (ch == ' ' || ch == '\t' || ch == '\r') { if (!t.empty()) w.push_back(t), t.clear(); } else t += ch; } if (!t.empty()) w.push_back(t); }
            if (w.empty()) { if (e >= text.size()) break; continue; }
            const std::string& op = w[0]; float f = 0;
            if (op == "emote") {
                if (!finish()) return false;
                if (w.size() < 3 || !Num(w[1], &f)) return fail("emote <id> <name>");
                out.emplace_back(); cur = &out.back(); cur->id = int(f); poses.clear();
                for (size_t i = 2; i < w.size(); ++i) cur->name += (i > 2 ? " " : "") + w[i];
                continue;
            }
            if (!cur) return fail("'" + op + "' before 'emote'");
            if (op == "duration") { if (w.size() != 2 || !Num(w[1], &cur->duration) || cur->duration <= 0) return fail("duration <seconds>"); }
            else if (op == "loop") { if (w.size() != 3 || !Num(w[1], &cur->loopA) || !Num(w[2], &cur->loopB) || cur->loopB <= cur->loopA + .1f) return fail("loop <from> <to>"); }
            else if (op == "drag") { if (w.size() != 2 || !Num(w[1], &cur->drag) || cur->drag < 0 || cur->drag > .15f) return fail("drag <0..0.15 s>"); }
            else if (op == "stiffness") { if (w.size() != 2 || !Num(w[1], &cur->stiffness) || cur->stiffness < .3f || cur->stiffness > 4) return fail("stiffness <0.3..4>"); }
            else if (op == "feet") { if (w.size() != 2 || (w[1] != "planted" && w[1] != "free")) return fail("feet planted|free"); if (!cur->keys.empty()) return fail("'feet' goes before the keys"); cur->planted = w[1] == "planted"; }
            else if (op == "symmetric") cur->symmetric = true;
            else if (op == "eyedrop") { if (w.size() != 3 || !Num(w[1], &cur->eyeDrop[0]) || !Num(w[2], &cur->eyeDrop[1])) return fail("eyedrop <min> <max>"); }
            else if (op == "touch") {
                ClipTouch t; if (w.size() < 4) return fail("touch <bone> <bone> <metres> [from to]");
                t.a = GestureBoneNamed(w[1]); t.b = GestureBoneNamed(w[2]);
                if (t.a < 0 || t.b < 0 || !Num(w[3], &t.within)) return fail("touch <bone> <bone> <metres> [from to]");
                if (w.size() == 6 && (!Num(w[4], &t.from) || !Num(w[5], &t.to))) return fail("touch window");
                cur->touches.push_back(t);
            }
            else if (op == "pose" || op == "key") {
                Pose p{}; p.v[kPlantChannel] = cur->planted ? 1.f : 0.f; size_t i = 1; ClipKey k;
                if (op == "pose") { if (w.size() < 2) return fail("pose <name> ..."); p.name = w[1]; i = 2; }
                else {
                    if (w.size() < 2 || !Num(w[1], &k.t) || k.t < 0) return fail("key <time> ...");
                    if (!cur->keys.empty() && k.t <= cur->keys.back().t + 1e-4f) return fail("keys must go forward in time");
                    if (!cur->keys.empty()) memcpy(p.v, cur->keys.back().v, sizeof p.v);      // a key starts from the previous one
                    i = 2;
                }
                for (; i < w.size(); ++i) {
                    const std::string& tok = w[i];
                    if (tok == "hit" && op == "key") { k.hit = true; continue; }
                    if (tok == "rest") { memset(p.v, 0, sizeof p.v); p.v[kPlantChannel] = cur->planted ? 1.f : 0.f; continue; }
                    if (tok[0] == '@') { bool found = false; for (auto& q : poses) if (q.name == tok.substr(1)) { memcpy(p.v, q.v, sizeof p.v); found = true; } if (!found) return fail("no pose '" + tok.substr(1) + "'"); continue; }
                    size_t eq = tok.find('='); if (eq == std::string::npos) return fail("expected bone=fwd,twist,side, got '" + tok + "'");
                    std::string name = tok.substr(0, eq); auto comps = Split(tok.substr(eq + 1), ',');
                    auto put = [&](int base, int n, float sign1, float sign2) -> bool {
                        if (comps.size() > size_t(n)) return false;
                        for (size_t c = 0; c < comps.size(); ++c) { if (comps[c].empty()) continue; float v; if (!Num(comps[c], &v)) return false; float s = c == 0 ? 1 : c == 1 ? sign1 : sign2; p.v[base + c] = v * s; }
                        return true;
                    };
                    if (name == "hips") { if (!put(kHipsChannel, 3, 1, 1)) return fail("hips=x,y,z (cm)"); continue; }
                    if (name == "turn") { if (!put(kTurnChannel, 1, 1, 1)) return fail("turn=degrees"); continue; }
                    // step.L / step.R / step.LR: move the planted foot's target; x is outward (away from the other foot), y up, z forward
                    if (name == "step.L" || name == "step.LR") { if (!put(kStepChannel, 3, 1, 1)) return fail("step.L=out,up,forward (cm)"); if (name == "step.L") continue; }
                    if (name == "step.R" || name == "step.LR") { float keep = p.v[kStepChannel + 3]; if (!put(kStepChannel + 3, 3, 1, 1)) return fail("step.R=out,up,forward (cm)"); if (!comps.empty() && !comps[0].empty()) p.v[kStepChannel + 3] = -p.v[kStepChannel + 3]; else p.v[kStepChannel + 3] = keep; continue; }
                    if (name == "plant") { if (!put(kPlantChannel, 1, 1, 1) || p.v[kPlantChannel] < 0 || p.v[kPlantChannel] > 1) return fail("plant=0..1"); continue; }
                    bool pair = name.size() > 3 && name.compare(name.size() - 3, 3, ".LR") == 0;
                    // Left-side bones take the same anatomical numbers as the right: their twist and side are mirrored here.
                    if (pair) {      // .LR: both sides the same
                        std::string stem = name.substr(0, name.size() - 3);
                        int r = GestureBoneNamed(stem + ".R"), l = GestureBoneNamed(stem + ".L");
                        if (r < 0 || l < 0) return fail("no bone pair '" + name + "'");
                        if (!put(r * 3, 3, 1, 1) || !put(l * 3, 3, -1, -1)) return fail("bad numbers in '" + tok + "'");
                        continue;
                    }
                    int b = GestureBoneNamed(name); if (b < 0) return fail("no bone '" + name + "'");
                    float mir = IsLeftBone(b) ? -1.f : 1.f;
                    if (!put(b * 3, 3, mir, mir)) return fail("bad numbers in '" + tok + "'");
                }
                if (op == "pose") poses.push_back(p);
                else { memcpy(k.v, p.v, sizeof k.v); cur->keys.push_back(k); }
            }
            else return fail("unknown directive '" + op + "'");
            if (e >= text.size()) break;
        }
        if (!finish()) return false;
        for (size_t a = 0; a < out.size(); ++a) for (size_t b = a + 1; b < out.size(); ++b) if (out[a].id == out[b].id) { if (err) *err = std::string(source) + ": two clips with id " + std::to_string(out[a].id); return false; }
        clips = std::move(out); return true;
    }
};

// The clip's local time for a play time t: one pass, then the loop region forever.
inline float ClipTime(const EmoteAnim& c, float t) {
    if (t < 0) return 0;
    if (c.Loops() && t >= c.loopB) return c.loopA + fmodf(t - c.loopA, c.loopB - c.loopA);
    return t < c.duration ? t : c.duration;
}
// One channel at clip time u (already lagged and looped).
inline float SampleChannel(const EmoteAnim& c, int ch, float u) {
    const auto& K = c.keys; size_t n = K.size();
    if (u <= K[0].t) return K[0].v[ch];
    if (u >= K[n - 1].t) return K[n - 1].v[ch];
    size_t k = 0; while (k + 1 < n && K[k + 1].t <= u) ++k;
    float h = K[k + 1].t - K[k].t, s = (u - K[k].t) / h, s2 = s * s, s3 = s2 * s;
    float p0 = K[k].v[ch], p1 = K[k + 1].v[ch], m0 = c.mOut[k * kClipChannels + ch] * h, m1 = c.mIn[(k + 1) * kClipChannels + ch] * h;
    return (2 * s3 - 3 * s2 + 1) * p0 + (s3 - 2 * s2 + s) * m0 + (-2 * s3 + 3 * s2) * p1 + (s3 - s2) * m1;
}
// Every channel at play time t, with the overlap lag per bone.
inline void SampleClip(const EmoteAnim& c, float t, float out[kClipChannels]) {
    for (int ch = 0; ch < kClipChannels; ++ch) {
#ifdef WE_MUT_NO_DRAG
        float lag = 0;
#else
        float lag = ch < kHipsChannel ? c.drag * kChainDepth[ch / 3] : 0;
#endif      // the hips, the turn and the plant are the core: no lag
        out[ch] = SampleChannel(c, ch, ClipTime(c, t - lag));
    }
}

}  // namespace wm
