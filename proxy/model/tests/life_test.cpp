// T0 test of the life layer (life.h) and the fidget scheduler on the bundled adventurer: no game, no GPU.
// usage: life_test <adventurer.glb>   (reads <adventurer.glb>.txt for lid_close=). Exit 0 = all checks passed. Built with -DWM_MUT_xxx it must fail.
#include "../model.h"
#include "../life.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

static int g_fail, g_n;
#define CHECK(c, ...) do { g_n++; if (!(c)) { g_fail++; printf("FAIL line %d: %s | ", __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int JointOf(const wm::Model& m, int node) { for (size_t j = 0; j < m.joints.size(); j++) if (m.joints[j] == node) return (int)j; return -1; }
// where a joint's own origin is in model space for a pose (the skin matrix applied to the bind origin)
static void JointPos(const wm::Model& m, const float* mats, int node, float out[3]) {
    int j = JointOf(m, node); const float* ib = m.ibm.data() + j * 16; float rest[3];
    for (int r = 0; r < 3; r++) rest[r] = -(ib[r * 4] * ib[12] + ib[r * 4 + 1] * ib[13] + ib[r * 4 + 2] * ib[14]);
    const float* sk = mats + j * 16; for (int r = 0; r < 3; r++) out[r] = sk[r] * rest[0] + sk[4 + r] * rest[1] + sk[8 + r] * rest[2] + sk[12 + r];
}
static float Dist(const float* a, const float* b) { return sqrtf((a[0]-b[0])*(a[0]-b[0]) + (a[1]-b[1])*(a[1]-b[1]) + (a[2]-b[2])*(a[2]-b[2])); }

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: life_test <adventurer.glb>\n"); return 2; }
    std::ifstream f(argv[1], std::ios::binary); std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), {});
    wm::Model m; std::string err;
    if (!wm::LoadGlb(file.data(), file.size(), &m, &err)) { printf("load: %s\n", err.c_str()); return 1; }
    float lidClose = 0; { std::ifstream sc(std::string(argv[1]) + ".txt"); std::string line; while (std::getline(sc, line)) sscanf(line.c_str(), "lid_close=%f", &lidClose); }
    CHECK(lidClose > 1.2f && lidClose < 2.5f, "lid_close=%.3f in the sidecar", lidClose);
    wm::LifeRig rig; rig.Bind(m, lidClose);
    CHECK(rig.ok && rig.neck >= 0 && rig.chest >= 0 && rig.eye[0] >= 0 && rig.eye[1] >= 0 && rig.lid[0] >= 0 && rig.lid[1] >= 0, "rig: head %d neck %d chest %d eyes %d %d lids %d %d", rig.head, rig.neck, rig.chest, rig.eye[0], rig.eye[1], rig.lid[0], rig.lid[1]);
    CHECK(rig.scarfA.count == 3 && rig.scarfB.count == 2 && rig.canteen.count == 1 && rig.satchel.count == 1, "chains: scarf %d %d canteen %d satchel %d", rig.scarfA.count, rig.scarfB.count, rig.canteen.count, rig.satchel.count);
    CHECK(rig.headYaw.axis >= 0 && rig.headPitch.axis >= 0 && rig.headYaw.axis != rig.headPitch.axis && rig.eyeYaw[0].axis >= 0 && rig.eyePitch[0].axis >= 0, "axes: head yaw %d pitch %d, eye yaw %d pitch %d", rig.headYaw.axis, rig.headPitch.axis, rig.eyeYaw[0].axis, rig.eyePitch[0].axis);
    CHECK(rig.scarfA.swing[0].axis >= 0 && rig.scarfA.sway[0].axis >= 0 && rig.scarfA.swing[0].axis != rig.scarfA.sway[0].axis, "scarf axes %d %d", rig.scarfA.swing[0].axis, rig.scarfA.sway[0].axis);
    CHECK(rig.scarfA.length[0] > 0.05f && rig.scarfA.length[0] < 0.2f, "scarf segment %.3f m", rig.scarfA.length[0]);
    const int idle = wm::FindClip(m, "idle");
    std::vector<float> bind(wm::kMaxJoints * 16), posed(wm::kMaxJoints * 16);
    wm::JointMatrices(m, idle, 0.0f, bind.data());
    auto pose = [&](const wm::LifeState& L, float t = 0.0f) { wm::PoseLayers pl; pl.life = &L; pl.rig = &rig; wm::JointMatricesBlendEx(m, -1, 0, idle, t, 1, posed.data(), pl); };

    // ---- the lids: closing stretches the lid's edge down over the eye by about the opening; its top row and the eye corners stay
    {
        int lidNode = rig.lid[0], lidJ = JointOf(m, lidNode); std::vector<size_t> edge, top;
        for (size_t i = 0; i < m.verts.size(); i++) for (int k = 0; k < 4; k++) if (m.verts[i].joints[k] == lidJ) { if (m.verts[i].weights[k] > 0.8f) edge.push_back(i); else if (m.verts[i].weights[k] < 0.05f) top.push_back(i); }
        CHECK(edge.size() > 10 && top.size() > 10, "lid vertices: %zu edge, %zu top", edge.size(), top.size());
        wm::LifeState L; L.blink = 1; pose(L);
        float maxDrop = 0, minDrop = 1e9f, topMove = 0, edgeFront = 0;
        for (size_t i : edge) { float a[3], b[3]; wm::SkinPosition(m, bind.data(), m.verts[i], a); wm::SkinPosition(m, posed.data(), m.verts[i], b); float drop = a[1] - b[1]; maxDrop = fmaxf(maxDrop, drop); minDrop = fminf(minDrop, drop); edgeFront = fmaxf(edgeFront, b[2] - a[2]); }
        for (size_t i : top) { float a[3], b[3]; wm::SkinPosition(m, bind.data(), m.verts[i], a); wm::SkinPosition(m, posed.data(), m.verts[i], b); topMove = fmaxf(topMove, Dist(a, b)); }
        CHECK(maxDrop > 0.007f && maxDrop < 0.013f, "a closed lid's edge drops %.1f mm at most (the opening is 9.5 mm)", maxDrop * 1000);
        CHECK(topMove < 0.0006f, "the lid's top row moved %.2f mm", topMove * 1000);
        CHECK(edgeFront < 0.004f, "the closed lid stands %.1f mm further forward", edgeFront * 1000);
        L.blink = 0; pose(L); float still = 0; for (size_t i : edge) { float a[3], b[3]; wm::SkinPosition(m, bind.data(), m.verts[i], a); wm::SkinPosition(m, posed.data(), m.verts[i], b); still = fmaxf(still, Dist(a, b)); }
        CHECK(still < 1e-4f, "an open eye's lid moved %.2f mm", still * 1000);
    }
    // ---- blinking happens by itself: a few times in 20 s, each short, eyes open in between
    {
        wm::LifeState L(7); wm::LifeInput in; in.dt = 1.0f / 60; int blinks = 0, closedFrames = 0; bool wasClosed = false; float maxBlink = 0;
        for (int fr = 0; fr < 20 * 60; fr++) { L.Step(in); bool closed = L.blink > 0.9f; if (closed && !wasClosed) blinks++; wasClosed = closed; if (L.blink > 0.5f) closedFrames++; maxBlink = fmaxf(maxBlink, L.blink); }
        CHECK(blinks >= 3 && blinks <= 12, "%d blinks in 20 s", blinks);
        CHECK(maxBlink >= 0.95f && closedFrames < 20 * 60 / 5, "blink peak %.2f, %d of %d frames half closed", maxBlink, closedFrames, 20 * 60);
    }
    // ---- the glance: a viewer 3 m away to the front-left gets looked at; the head turns the face that way (the eyes' midpoint moves left of the head), then back when the viewer leaves
    {
        wm::LifeState L(3); wm::LifeInput in; in.dt = 1.0f / 60; in.viewer = true; in.viewerDist = 3; float d[3] = {0.7f, -0.05f, 0.7f}; memcpy(in.viewerDir, d, 12);
        for (int fr = 0; fr < 90; fr++) L.Step(in);
        CHECK(L.glancing && L.headYaw > 0.35f && L.headYaw < 1.0f, "glancing %d head yaw %.2f after 1.5 s", (int)L.glancing, L.headYaw);
        CHECK(L.eyeYaw > -0.05f, "eye yaw %.2f (the eyes lead toward the viewer or are centred)", L.eyeYaw);
        pose(L); float h[3], el[3], er[3], hb[3], elb[3], erb[3]; JointPos(m, posed.data(), rig.head, h); JointPos(m, posed.data(), rig.eye[0], el); JointPos(m, posed.data(), rig.eye[1], er);
        JointPos(m, bind.data(), rig.head, hb); JointPos(m, bind.data(), rig.eye[0], elb); JointPos(m, bind.data(), rig.eye[1], erb);
        float faceX = (el[0] + er[0]) * 0.5f - h[0], faceX0 = (elb[0] + erb[0]) * 0.5f - hb[0];
        CHECK(faceX - faceX0 > 0.025f, "the face turned %.1f mm to the left (model +x)", (faceX - faceX0) * 1000);
        in.viewer = false; for (int fr = 0; fr < 3 * 60; fr++) L.Step(in);
        CHECK(!L.glancing && fabsf(L.headYaw) < 0.05f, "after the viewer left: glancing %d head yaw %.2f", (int)L.glancing, L.headYaw);
        // a viewer behind is not looked at; one far to the side is looked at only as far as the neck turns
        wm::LifeState B(3); float back[3] = {0.2f, 0, -0.98f}; memcpy(in.viewerDir, back, 12); in.viewer = true; for (int fr = 0; fr < 6 * 60; fr++) B.Step(in);
        CHECK(!B.glancing && fabsf(B.headYaw) < 0.05f, "viewer behind: glancing %d yaw %.2f", (int)B.glancing, B.headYaw);
        wm::LifeState S(3); float side[3] = {0.99f, 0, -0.1f}; memcpy(in.viewerDir, side, 12); float maxYaw = 0; for (int fr = 0; fr < 6 * 60; fr++) { S.Step(in); maxYaw = fmaxf(maxYaw, S.headYaw); }
        CHECK(maxYaw > 0.8f && maxYaw <= 1.06f, "viewer at 96 degrees: head yaw peaks at %.2f (clamped to 1.05)", maxYaw);
        // an emote that owns the head: the glance lets go
        wm::LifeState E(3); memcpy(in.viewerDir, d, 12); in.headBusy = true; for (int fr = 0; fr < 4 * 60; fr++) E.Step(in);
        CHECK(E.headWeight < 0.02f && !E.glancing, "emote owns the head: weight %.2f glancing %d", E.headWeight, (int)E.glancing);
    }
    // ---- the remote player's own view: looking down tilts the puppet's head down (the eyes' midpoint drops relative to the head joint)
    {
        wm::LifeState L(5); wm::LifeInput in; in.dt = 1.0f / 60; in.viewPitch = -0.6f; for (int fr = 0; fr < 3 * 60; fr++) L.Step(in);
        CHECK(L.headPitch < -0.25f && L.headPitch > -0.65f, "head pitch %.2f for a view pitch of -0.6", L.headPitch);
        pose(L); float h[3], el[3], er[3], hb[3], elb[3], erb[3]; JointPos(m, posed.data(), rig.head, h); JointPos(m, posed.data(), rig.eye[0], el); JointPos(m, posed.data(), rig.eye[1], er);
        JointPos(m, bind.data(), rig.head, hb); JointPos(m, bind.data(), rig.eye[0], elb); JointPos(m, bind.data(), rig.eye[1], erb);
        float dy = (el[1] + er[1]) * 0.5f - h[1], dy0 = (elb[1] + erb[1]) * 0.5f - hb[1];
        CHECK(dy0 - dy > 0.01f, "the eyes dropped %.1f mm relative to the head joint", (dy0 - dy) * 1000);
    }
    // ---- breathing: the chest moves a little all the time, and faster after a run
    {
        auto crossings = [&](wm::LifeState& L, wm::LifeInput in, float seconds) { int n = 0; float prev = L.breath; for (int fr = 0; fr < (int)(seconds * 60); fr++) { L.Step(in); if ((L.breath > 0) != (prev > 0)) n++; prev = L.breath; } return n; };
        wm::LifeState L(9); wm::LifeInput in; in.dt = 1.0f / 60; int rested = crossings(L, in, 20);
        CHECK(rested >= 6 && rested <= 12, "%d breath half-cycles in 20 s at rest (13 a minute)", rested);
        float lo = 1e9f, hi = -1e9f; for (int fr = 0; fr < 6 * 60; fr++) { L.Step(in); pose(L); float n[3]; JointPos(m, posed.data(), rig.neck, n); lo = fminf(lo, n[2]); hi = fmaxf(hi, n[2]); }
        CHECK(hi - lo > 0.0015f && hi - lo < 0.02f, "the neck's base moves %.1f mm with the breath", (hi - lo) * 1000);
        wm::LifeState R(9); in.speed = 3.0f; crossings(R, in, 12); float afterRun = R.exertion; in.speed = 0; int puffed = crossings(R, in, 20);
        CHECK(puffed >= 18 && afterRun > 0.6f && R.exertion > 0.15f && R.exertion < afterRun, "%d breath half-cycles in the 20 s after a run (exertion %.2f, then %.2f)", puffed, afterRun, R.exertion);
    }
    // ---- cloth: a push forward swings the scarf tails back, the swing is bounded and dies away; nothing moves on a still, windless day except a little wind
    {
        wm::LifeState L(11); wm::LifeInput in; in.dt = 1.0f / 60;
        int tipNode = rig.scarfA.node[2]; std::vector<size_t> tipVerts; int tipJ = JointOf(m, tipNode);
        for (size_t i = 0; i < m.verts.size(); i++) for (int k = 0; k < 4; k++) if (m.verts[i].joints[k] == tipJ && m.verts[i].weights[k] > 0.7f) { tipVerts.push_back(i); break; }
        CHECK(tipVerts.size() > 10, "%zu vertices follow the scarf's tip", tipVerts.size());
        auto tipMove = [&](const wm::LifeState& S) { pose(S); float worst = 0; for (size_t i : tipVerts) { float a[3], b[3]; wm::SkinPosition(m, bind.data(), m.verts[i], a); wm::SkinPosition(m, posed.data(), m.verts[i], b); worst = fmaxf(worst, Dist(a, b)); } return worst; };
        in.accel[2] = 6.0f; float peak = 0, peakAngle = 0;
        for (int fr = 0; fr < 20; fr++) L.Step(in);
        in.accel[2] = 0;
        for (int fr = 0; fr < 2 * 60; fr++) { L.Step(in); peak = fmaxf(peak, tipMove(L)); for (auto& p : L.scarfA) peakAngle = fmaxf(peakAngle, fabsf(p.a)); }
        CHECK(peak > 0.012f, "the scarf's tip moved %.1f mm after a push", peak * 1000);
        CHECK(peakAngle <= 0.56f, "scarf swing peaked at %.2f rad (clamped)", peakAngle);
        for (int fr = 0; fr < 8 * 60; fr++) L.Step(in);
        float settled = 0; for (auto& p : L.scarfA) settled = fmaxf(settled, fabsf(p.va));
        CHECK(settled < 0.6f, "8 s after the push the scarf still swings at %.2f rad/s", settled);
        // the first segment lags behind the push: a forward acceleration leaves the tail behind (negative swing) while it lasts
        wm::LifeState P(11); in.accel[2] = 6.0f; float minA = 0;
        for (int fr = 0; fr < 20; fr++) { P.Step(in); minA = fminf(minA, P.scarfA[0].a); }
        in.accel[2] = 0;
        CHECK(minA < -0.03f, "a forward push should leave the tail behind (swing %.3f)", minA);
    }
    // ---- fidgets: a player standing still for a minute fidgets at least once and comes back to the idle; a walking one never does; walking off cancels one
    {
        auto feed = [&](wm::Avatar& a, uint64_t& t, unsigned& seq, float* p, float mps, float seconds) { int frames = (int)(seconds * 60); for (int fr = 0; fr < frames; fr++, t += 17) { if (fr % 2 == 0) { seq++; p[0] += mps / 30; } a.Update(t, true, seq, p, 0); } };
        wm::Avatar a(wm::GaitCfg{}, 42); uint64_t t = 1000; unsigned seq = 1; float p[3] = {0, 0, 5};
        feed(a, t, seq, p, 0, 1.0f);
        int fidgetFrames = 0, idleAfter = 0, firstFidget = -1; std::string names;
        for (int fr = 0; fr < 60 * 60; fr++, t += 17) {
            a.Update(t, true, seq, p, 0); wm::ClipPick pk = a.Pick(m); const std::string& nm = m.clips[pk.clip].name;
            if (nm.rfind("fidget", 0) == 0) { fidgetFrames++; if (firstFidget < 0) firstFidget = fr; if (names.find(nm) == std::string::npos) names += nm + " "; CHECK(pk.time >= 0 && pk.time <= m.clips[pk.clip].duration, "fidget time %.2f of %.2f", pk.time, m.clips[pk.clip].duration); }
            else if (firstFidget >= 0 && nm == "idle") idleAfter++;
        }
        CHECK(fidgetFrames > 60 && firstFidget >= 0 && firstFidget < 30 * 60, "%d fidget frames in a minute of standing (first at %.1f s): %s", fidgetFrames, firstFidget / 60.0f, names.c_str());
        CHECK(idleAfter > 60 && fidgetFrames < 40 * 60, "%d idle frames after the first fidget, %d fidget frames", idleAfter, fidgetFrames);
        wm::Avatar w(wm::GaitCfg{}, 42); t = 1000; seq = 1; p[0] = 0; int walkingFidgets = 0;
        for (int fr = 0; fr < 60 * 60; fr++, t += 17) { if (fr % 2 == 0) { seq++; p[0] += 3.0f / 30; } w.Update(t, true, seq, p, 0); if (m.clips[w.Pick(m).clip].name.rfind("fidget", 0) == 0) walkingFidgets++; }
        CHECK(walkingFidgets == 0, "%d fidget frames while walking", walkingFidgets);
        // walking off in the middle of a fidget: the walk takes over at once (with a fade from the fidget)
        wm::Avatar c(wm::GaitCfg{}, 42); t = 1000; seq = 1; p[0] = 0; bool started = false; int framesToWalk = -1;
        for (int fr = 0; fr < 120 * 60 && !started; fr++, t += 17) { c.Update(t, true, seq, p, 0); started = m.clips[c.Pick(m).clip].name.rfind("fidget", 0) == 0; }
        CHECK(started, "no fidget started in two minutes");
        if (started) for (int fr = 0; fr < 60; fr++, t += 17) { if (fr % 2 == 0) { seq++; p[0] += 3.0f / 30; } c.Update(t, true, seq, p, 0); if (m.clips[c.Pick(m).clip].name == "walk") { framesToWalk = fr; break; } }
        CHECK(framesToWalk >= 0 && framesToWalk < 45, "walking off during a fidget: the walk clip came after %d frames", framesToWalk);
        // busy (an emote): no fidget starts
        wm::Avatar b(wm::GaitCfg{}, 42); b.SetBusy(true); t = 1000; seq = 1; p[0] = 0; int busyFidgets = 0;
        for (int fr = 0; fr < 90 * 60; fr++, t += 17) { b.Update(t, true, seq, p, 0); if (m.clips[b.Pick(m).clip].name.rfind("fidget", 0) == 0) busyFidgets++; }
        CHECK(busyFidgets == 0, "%d fidget frames during an emote", busyFidgets);
    }
    // ---- the fidget clips themselves: start and end where they began (the idle is cross-faded with them), close to the idle's first frame, move the body but stay in reach
    {
        std::vector<float> a(wm::kMaxJoints * 16), b(wm::kMaxJoints * 16), e(wm::kMaxJoints * 16); wm::JointMatrices(m, idle, 0, a.data()); int nf = 0;
        for (size_t c = 0; c < m.clips.size(); c++) if (m.clips[c].name.rfind("fidget", 0) == 0) {
            nf++; wm::JointMatrices(m, (int)c, 0, b.data()); wm::JointMatrices(m, (int)c, m.clips[c].duration - 1e-4f, e.data()); float d0 = 0, d1 = 0;
            for (int i = 0; i < m.jointCount() * 16; i++) { d0 = fmaxf(d0, fabsf(a[i] - b[i])); d1 = fmaxf(d1, fabsf(e[i] - b[i])); }
            CHECK(d0 < 0.06f && d1 < 2e-3f, "%s: %.3f from the idle's first frame at its start, %.4f between its end and its start", m.clips[c].name.c_str(), d0, d1);
            float moved = 0; for (int step = 1; step < 8; step++) { wm::JointMatrices(m, (int)c, m.clips[c].duration * step / 8, b.data()); for (size_t i = 0; i < m.verts.size(); i += 11) { float p0[3], p1[3]; wm::SkinPosition(m, a.data(), m.verts[i], p0); wm::SkinPosition(m, b.data(), m.verts[i], p1); moved = fmaxf(moved, Dist(p0, p1)); } }
            CHECK(moved > 0.03f && moved < 1.3f, "%s: moves the body by %.2f m at most", m.clips[c].name.c_str(), moved);
        }
        CHECK(nf >= 3, "%d fidget clips", nf);
    }
    // ---- the brows act with an emote: sad lifts the inner ends, laughter raises both brows, a facepalm knits them, and the face returns to rest after
    {
        CHECK(rig.brow[0] >= 0 && rig.brow[1] >= 0 && rig.browTilt[0].axis >= 0, "brow bones %d %d, tilt axis %d", rig.brow[0], rig.brow[1], rig.browTilt[0].axis);
        int jL = JointOf(m, rig.brow[0]), jR = JointOf(m, rig.brow[1]); std::vector<size_t> browL, browR;
        for (size_t i = 0; i < m.verts.size(); i++) for (int k = 0; k < 4; k++) { if (m.verts[i].joints[k] == jL && m.verts[i].weights[k] > 0.9f) browL.push_back(i); if (m.verts[i].joints[k] == jR && m.verts[i].weights[k] > 0.9f) browR.push_back(i); }
        CHECK(browL.size() > 20 && browR.size() > 20, "%zu and %zu brow vertices", browL.size(), browR.size());
        auto measure = [&](const std::vector<size_t>& verts, float* innerDy, float* outerDy, float* meanDy) {      // the inner end is the one nearest the face's middle (x near 0)
            float inX = 1e9f, outX = -1e9f; size_t in = 0, out = 0; double sum = 0;
            for (size_t i : verts) { float ax = fabsf(m.verts[i].pos[0]); if (ax < inX) { inX = ax; in = i; } if (ax > outX) { outX = ax; out = i; } }
            auto dy = [&](size_t i) { float a[3], b[3]; wm::SkinPosition(m, bind.data(), m.verts[i], a); wm::SkinPosition(m, posed.data(), m.verts[i], b); return b[1] - a[1]; };
            for (size_t i : verts) sum += dy(i);
            *innerDy = dy(in); *outerDy = dy(out); *meanDy = (float)(sum / verts.size());
        };
        wm::LifeState L(2); wm::LifeInput in; in.dt = 1.0f / 60; in.emote = 19; for (int fr = 0; fr < 60; fr++) L.Step(in);
        pose(L); float inL, outL, meanL, inR, outR, meanR; measure(browL, &inL, &outL, &meanL); measure(browR, &inR, &outR, &meanR);
        CHECK(inL > 0.0025f && inR > 0.0025f && inL - outL > 0.003f && inR - outR > 0.003f && inL < 0.012f, "sad: inner ends up %.1f / %.1f mm, outer %.1f / %.1f mm", inL * 1000, inR * 1000, outL * 1000, outR * 1000);
        in.emote = 20; for (int fr = 0; fr < 60; fr++) L.Step(in); pose(L); measure(browL, &inL, &outL, &meanL); measure(browR, &inR, &outR, &meanR);
        CHECK(meanL > 0.0035f && meanR > 0.0035f && meanL < 0.012f && fabsf(inL - outL) < 0.002f, "laugh: brows up %.1f / %.1f mm (inner-outer %.1f mm)", meanL * 1000, meanR * 1000, (inL - outL) * 1000);
        in.emote = 11; for (int fr = 0; fr < 60; fr++) L.Step(in); pose(L); measure(browL, &inL, &outL, &meanL); measure(browR, &inR, &outR, &meanR);
        CHECK(meanL < -0.0015f && inL < outL, "facepalm: brows down %.1f mm, inner below outer by %.1f mm", meanL * 1000, (outL - inL) * 1000);
        in.emote = 13; for (int fr = 0; fr < 60; fr++) L.Step(in); pose(L); measure(browL, &inL, &outL, &meanL); measure(browR, &inR, &outR, &meanR);
        CHECK(meanR > meanL + 0.002f, "thinking: the right brow (%.1f mm) above the left (%.1f mm)", meanR * 1000, meanL * 1000);
        in.emote = 0; for (int fr = 0; fr < 90; fr++) L.Step(in); pose(L); measure(browL, &inL, &outL, &meanL);
        CHECK(fabsf(meanL) < 0.0005f && fabsf(inL - outL) < 0.0005f, "at rest again: %.2f mm", meanL * 1000);
    }
    // ---- the body lags behind a turn of the view (the head leads), turns half way toward a sideways walk, and walks backwards in reverse
    {
        auto feedYaw = [&](wm::Avatar& a, uint64_t& t, unsigned& seq, float* p, float vx, float vy, float yaw, int frames) { for (int fr = 0; fr < frames; fr++, t += 17) { if (fr % 2 == 0) { seq++; p[0] += vx / 30; p[1] += vy / 30; } a.Update(t, true, seq, p, yaw); } };
        wm::Avatar a; uint64_t t = 1000; unsigned seq = 1; float p[3] = {0, 0, 5};
        feedYaw(a, t, seq, p, 0, 0, 0, 60);
        a.Update(t += 17, true, seq, p, 1.5707963f); float firstFrame = a.yaw();
        CHECK(firstFrame > 0.05f && firstFrame < 0.6f, "one frame after the view turned 90 degrees the body has turned %.2f rad (it should lag)", firstFrame);
        CHECK(a.yawOffset() > 0.9f, "the head has %.2f rad left to turn toward the view", a.yawOffset());
        feedYaw(a, t, seq, p, 0, 0, 1.5707963f, 60);
        CHECK(fabsf(a.yaw() - 1.5707963f) < 0.05f && fabsf(a.yawOffset()) < 0.05f, "a second later the body faces the view (%.2f, offset %.2f)", a.yaw(), a.yawOffset());
        // the life layer turns the head toward the view while the body catches up
        wm::LifeState L(4); wm::LifeInput in; in.dt = 1.0f / 60; in.viewYawOffset = 0.6f; for (int fr = 0; fr < 60; fr++) L.Step(in);
        CHECK(L.headYaw > 0.35f && L.headYaw < 0.6f, "head yaw %.2f for a view 0.6 rad left of the body", L.headYaw);
        // walking forward (view +x, moving +x): body on the view, the cycle runs forward
        wm::Avatar f; t = 1000; seq = 1; p[0] = p[1] = 0; feedYaw(f, t, seq, p, 2.0f, 0, 0, 120); float tf0 = f.Pick(m).time; feedYaw(f, t, seq, p, 2.0f, 0, 0, 30); float tf1 = f.Pick(m).time;
        CHECK(fabsf(f.yaw()) < 0.1f && !f.backward() && tf1 > tf0, "forward: body yaw %.2f backward %d time %.2f -> %.2f", f.yaw(), (int)f.backward(), tf0, tf1);
        // walking backwards (view +x, moving -x): the body keeps facing the view and the walk runs in reverse
        wm::Avatar b; t = 1000; seq = 1; p[0] = p[1] = 0; feedYaw(b, t, seq, p, -2.0f, 0, 0, 120); float tb0 = b.Pick(m).time; feedYaw(b, t, seq, p, -2.0f, 0, 0, 30); float tb1 = b.Pick(m).time;
        CHECK(fabsf(b.yaw()) < 0.1f && b.backward() && tb1 < tb0 && m.clips[b.Pick(m).clip].name == "walk", "backwards: body yaw %.2f backward %d time %.2f -> %.2f", b.yaw(), (int)b.backward(), tb0, tb1);
        // walking sideways (view +x, moving +y): the body turns half way toward the heading, still walking forward
        wm::Avatar sw; t = 1000; seq = 1; p[0] = p[1] = 0; feedYaw(sw, t, seq, p, 0, 2.0f, 0, 120); float ts0 = sw.Pick(m).time; feedYaw(sw, t, seq, p, 0, 2.0f, 0, 30); float ts1 = sw.Pick(m).time;
        CHECK(sw.yaw() > 0.6f && sw.yaw() < 0.95f && !sw.backward() && ts1 > ts0, "sideways: body yaw %.2f (wanted about 0.79) backward %d", sw.yaw(), (int)sw.backward());
        CHECK(sw.yawOffset() < -0.6f && sw.yawOffset() > -0.95f, "sideways: the head turns %.2f back toward the view", sw.yawOffset());
    }
    printf("%s: %d checks, %d failed\n", g_fail ? "FAILED" : "ok", g_n, g_fail);
    return g_fail ? 1 : 0;
}
