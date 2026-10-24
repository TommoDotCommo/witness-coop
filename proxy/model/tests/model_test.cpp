// T0 test of the character model code (no game, no GPU): loading, skinning, animation, camera maths, placement, walk state.
// usage: model_test <cesium-man.glb> <fox.glb> <rigged-figure.glb>.  Exit 0 = all checks passed.  Built with -DWM_MUT_xxx it must fail (see Makefile).
#include "../model.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

static int g_fail, g_n;
#define CHECK(c, ...) do { g_n++; if (!(c)) { g_fail++; printf("FAIL line %d: %s | ", __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)
static bool Near(float a, float b, float e) { return fabsf(a - b) <= e; }

int main(int argc, char** argv) {
    std::ifstream f(argv[1], std::ios::binary); std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), {});
    wm::Model m; std::string err;
    CHECK(wm::LoadGlb(file.data(), file.size(), &m, &err), "load: %s", err.c_str());
    if (g_fail) return 1;
    CHECK(m.verts.size() > 1000 && m.idx.size() % 3 == 0, "verts %zu idx %zu", m.verts.size(), m.idx.size());
    CHECK(m.joints.size() == 19 && m.jointCount() == 20, "joints %zu", m.joints.size());
    CHECK(m.clips.size() == 1 && m.clips[0].duration > 0.5f && m.clips[0].duration < 10, "clips %zu", m.clips.size());
    CHECK(m.texW > 0 && m.rgba.size() == (size_t)m.texW * m.texH * 4, "texture %dx%d", m.texW, m.texH);
    float h = m.bmax[1] - m.bmin[1];
    CHECK(h > 1.3f && h < 1.8f, "bind-pose height %.3f (the bundled figure is 1.5 high)", h);
    CHECK(fabsf(m.bmin[1]) < 0.05f && fabsf((m.bmin[0] + m.bmax[0]) * 0.5f) < 0.2f && m.bmax[2] - m.bmin[2] < 0.6f, "bind pose is not standing at the origin: (%.2f %.2f %.2f)..(%.2f %.2f %.2f)", m.bmin[0], m.bmin[1], m.bmin[2], m.bmax[0], m.bmax[1], m.bmax[2]);
    for (uint32_t i : m.idx) if (i >= m.verts.size()) { CHECK(false, "index out of range"); break; }
    float wsum = 0; for (auto& v : m.verts) { float s = v.weights[0] + v.weights[1] + v.weights[2] + v.weights[3]; wsum = fmaxf(wsum, fabsf(s - 1)); }
    CHECK(wsum < 1e-3f, "weights not normalised (%g)", wsum);

    // garbage and truncated input must be refused, not crash
    wm::Model bad; std::string e2; const char junk[] = "this is not a glb file at all";
    CHECK(!wm::LoadGlb((const uint8_t*)junk, sizeof junk, &bad, &e2), "garbage accepted");
    CHECK(!wm::LoadGlb(file.data(), file.size() / 3, &bad, &e2), "truncated file accepted");

    // skinning: in the bind pose the joint matrices must give back the mesh; walking must keep the body together and move it
    std::vector<float> jm0(wm::kMaxJoints * 16), jmt(wm::kMaxJoints * 16);
    wm::JointMatrices(m, -1, 0, jm0.data());
    float dur = m.clips[0].duration, maxMove = 0, lowest = 1e9f, highest = -1e9f; bool inside = true;
    for (int step = 0; step < 16; step++) {
        wm::JointMatrices(m, 0, dur * step / 16.0f, jmt.data());
        for (size_t i = 0; i < m.verts.size(); i += 7) {
            float a[3], b[3]; wm::SkinPosition(m, jm0.data(), m.verts[i], a); wm::SkinPosition(m, jmt.data(), m.verts[i], b);
            for (int k = 0; k < 3; k++) { maxMove = fmaxf(maxMove, fabsf(a[k] - b[k])); if (b[k] < m.bmin[k] - h * 0.6f || b[k] > m.bmax[k] + h * 0.6f) inside = false; }
            lowest = fminf(lowest, b[1]); highest = fmaxf(highest, b[1]);
        }
    }
    CHECK(maxMove > 0.1f, "the walk cycle moved nothing (max %.3f)", maxMove);
    CHECK(inside, "skinned vertices left the body's neighbourhood: the joint matrices are wrong");
    CHECK(lowest > m.bmin[1] - 0.25f * h && highest < m.bmax[1] + 0.25f * h, "height range while walking %.2f..%.2f vs %.2f..%.2f", lowest, highest, m.bmin[1], m.bmax[1]);
    // the clip loops
    std::vector<float> ja(wm::kMaxJoints * 16), jb(wm::kMaxJoints * 16);
    wm::JointMatrices(m, 0, dur * 0.5f, ja.data()); wm::JointMatrices(m, 0, dur * 1.5f, jb.data());
    float dl = 0; for (int i = 0; i < m.jointCount() * 16; i++) dl = fmaxf(dl, fabsf(ja[i] - jb[i]));
    CHECK(dl < 1e-3f, "clip does not loop (diff %g)", dl);

    // quaternion interpolation stays a rotation: half way between identity and 90 degrees about z is 45 degrees, unit length
    { float a[4] = {0, 0, 0, 1}, b[4] = {0, 0, sinf(3.14159265f / 4), cosf(3.14159265f / 4)}, q[4]; wm::Slerp(a, b, 0.5f, q);
      float l = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
      CHECK(Near(l, 1, 1e-4f) && Near(q[2], sinf(3.14159265f / 8), 1e-3f), "slerp gives (%.3f %.3f %.3f %.3f) length %.4f", q[0], q[1], q[2], q[3], l); }

    // camera: the matrix and the existing puppet projection must agree, depth must run 0 (near) to 1 (far)
    wm::Cam c = {{-200, -115, 3.7f}, -1.257f, 0.1f, 60, 16.0f / 9, 0.1f, 2000};
    float vp[16]; wm::ViewProj(c, vp);
    unsigned seed = 7; auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return ((seed >> 8) & 0xFFFF) / 65535.0f; };
    int projChecked = 0; float prevDepth = -1;
    for (int i = 0; i < 200; i++) {
        float w[3] = {c.pos[0] + (rnd() - 0.5f) * 40, c.pos[1] + (rnd() - 0.5f) * 40, c.pos[2] + (rnd() - 0.5f) * 10}, nx, ny;
        if (!wm::ProjectPoint(c, w, &nx, &ny)) continue;
        float cl[4]; for (int r = 0; r < 4; r++) cl[r] = vp[r] * w[0] + vp[4 + r] * w[1] + vp[8 + r] * w[2] + vp[12 + r];
        CHECK(Near(cl[0] / cl[3], nx, 1e-3f) && Near(cl[1] / cl[3], ny, 1e-3f), "matrix (%.4f %.4f) vs puppet projection (%.4f %.4f)", cl[0] / cl[3], cl[1] / cl[3], nx, ny);
        CHECK(cl[2] / cl[3] > 0 && cl[2] / cl[3] < 1, "depth %.4f outside 0..1", cl[2] / cl[3]); projChecked++;
    }
    CHECK(projChecked > 30, "only %d points projected", projChecked);
    for (float dd : {1.0f, 5.0f, 20.0f, 100.0f}) {
        float fwd[3] = {cosf(c.phi) * cosf(c.theta), cosf(c.phi) * sinf(c.theta), sinf(c.phi)}, w[3] = {c.pos[0] + fwd[0] * dd, c.pos[1] + fwd[1] * dd, c.pos[2] + fwd[2] * dd};
        float z = vp[2] * w[0] + vp[6] * w[1] + vp[10] * w[2] + vp[14], ww = vp[3] * w[0] + vp[7] * w[1] + vp[11] * w[2] + vp[15];
        CHECK(z / ww > prevDepth && Near(ww, dd, 1e-2f), "depth not increasing with distance: %.5f after %.5f (w %.3f for %.1f)", z / ww, prevDepth, ww, dd); prevDepth = z / ww;
    }

    // placement: model +Z (front) faces the heading, +Y is up, +X is the model's left, the feet land on the given point, the height is as asked
    wm::Fit fit = wm::FitToHeight(m, 1.8f);
    for (float yaw : {0.0f, 0.7f, -2.0f}) {
        float feet[3] = {10, 20, 5}, M[16]; wm::ModelMatrix(feet, yaw, fit, 0, M);
        auto xf = [&](float x, float y, float z, float* o) { for (int r = 0; r < 3; r++) o[r] = M[r] * x + M[4 + r] * y + M[8 + r] * z + M[12 + r]; };
        float o[3], fr[3], up[3], lf[3]; xf(fit.offset[0], fit.offset[1], fit.offset[2], o);
        CHECK(Near(o[0], 10, 1e-4f) && Near(o[1], 20, 1e-4f) && Near(o[2], 5, 1e-4f), "feet at (%.3f %.3f %.3f)", o[0], o[1], o[2]);
        xf(fit.offset[0], fit.offset[1], fit.offset[2] + 1 / fit.scale, fr); xf(fit.offset[0], fit.offset[1] + 1 / fit.scale, fit.offset[2], up); xf(fit.offset[0] + 1 / fit.scale, fit.offset[1], fit.offset[2], lf);
        CHECK(Near(fr[0] - 10, cosf(yaw), 1e-4f) && Near(fr[1] - 20, sinf(yaw), 1e-4f), "front (%.3f %.3f) for yaw %.2f", fr[0] - 10, fr[1] - 20, yaw);
        CHECK(Near(up[2] - 5, 1, 1e-4f) && Near(up[0], 10, 1e-4f), "up (%.3f %.3f %.3f)", up[0], up[1], up[2]);
        CHECK(Near(lf[0] - 10, -sinf(yaw), 1e-4f) && Near(lf[1] - 20, cosf(yaw), 1e-4f), "left (%.3f %.3f) for yaw %.2f: the model is mirrored", lf[0] - 10, lf[1] - 20, yaw);
        float top[3]; xf(fit.offset[0], m.bmax[1], fit.offset[2], top); CHECK(Near(top[2] - 5, 1.8f, 1e-3f), "height %.3f", top[2] - 5);
    }

    // walk state: a player walking at 3 m/s (30 datagrams a second, drawn at 60 fps) is walking; stopping makes it idle; a teleport is not running
    {
        wm::Avatar a; uint64_t t = 1000; unsigned seq = 1; float p[3] = {0, 0, 5};
        for (int fr = 0; fr < 120; fr++, t += 17) { if (fr % 2 == 0) { seq++; p[0] += 3.0f / 30; } a.Update(t, true, seq, p, 0.3f); }
        wm::ClipPick pk = a.Pick(m);
        CHECK(Near(a.speed(), 3.0f, 0.5f), "walking speed measured %.2f", a.speed());
        CHECK(pk.clip == 0 && pk.time > 1.0f, "walking pick clip %d time %.2f", pk.clip, pk.time);
        CHECK(Near(a.pos()[0], p[0], 0.4f), "displayed x %.2f vs %.2f", a.pos()[0], p[0]);
        for (int fr = 0; fr < 90; fr++, t += 17) a.Update(t, true, seq, p, 0.3f);
        CHECK(a.speed() < 0.25f, "still walking after the packets stopped (%.2f)", a.speed());
        CHECK(a.Pick(m).clip == 0 && a.Pick(m).time == 0, "idle must hold the first frame (clip %d time %.2f)", a.Pick(m).clip, a.Pick(m).time);
        CHECK(Near(a.pos()[0], p[0], 0.05f), "settled at x %.2f vs %.2f", a.pos()[0], p[0]);
        float far[3] = {500, 300, 5}; seq++; a.Update(t, true, seq, far, 0); t += 17; seq++; a.Update(t + 33, true, seq, far, 0);
        CHECK(a.speed() < 1.0f && Near(a.pos()[0], 500, 1.0f), "teleport: speed %.1f pos %.1f", a.speed(), a.pos()[0]);
    }
    // the other bundled models: the clip is chosen by name, an unnamed single clip is the walk, a model without a texture still loads
    auto load = [&](const char* path, wm::Model* out) { std::ifstream g(path, std::ios::binary); std::vector<uint8_t> b((std::istreambuf_iterator<char>(g)), {}); std::string e; bool ok = wm::LoadGlb(b.data(), b.size(), out, &e); CHECK(ok, "%s: %s", path, e.c_str()); return ok; };
    auto walkAt = [&](const wm::Model& mm, float mps, int frames, wm::Avatar& a) { uint64_t t = 5000; unsigned seq = 1; float p[3] = {0, 0, 5};
        for (int fr = 0; fr < frames; fr++, t += 17) { if (fr % 2 == 0) { seq++; p[0] += mps / 30; } a.Update(t, true, seq, p, 0); } (void)mm; };
    wm::Model fox, fig;
    if (argc > 2 && load(argv[2], &fox)) {
        CHECK(fox.clips.size() == 3 && fox.joints.size() == 24, "fox: %zu clips %zu joints", fox.clips.size(), fox.joints.size());
        auto nameOf = [&](wm::ClipPick pk) { return pk.clip >= 0 ? fox.clips[pk.clip].name : std::string("none"); };
        wm::Avatar still, walk, run; walkAt(fox, 0, 120, still); walkAt(fox, 3.0f, 120, walk); walkAt(fox, 6.5f, 120, run);
        CHECK(nameOf(still.Pick(fox)) == "Survey" && still.Pick(fox).time > 1.0f, "standing fox plays '%s' at %.2f", nameOf(still.Pick(fox)).c_str(), still.Pick(fox).time);
        CHECK(nameOf(walk.Pick(fox)) == "Walk", "walking fox plays '%s'", nameOf(walk.Pick(fox)).c_str());
        CHECK(nameOf(run.Pick(fox)) == "Run", "running fox plays '%s'", nameOf(run.Pick(fox)).c_str());
        wm::Fit ff = wm::FitToHeight(fox, 0.7f); CHECK(fox.bmax[1] - fox.bmin[1] > 0 && Near((fox.bmax[1] - fox.bmin[1]) * ff.scale, 0.7f, 1e-3f), "fox fit scale %g", ff.scale);
        std::vector<float> j(wm::kMaxJoints * 16); wm::JointMatrices(fox, 2, 0.3f, j.data()); float a[3], bb[3]; wm::JointMatrices(fox, -1, 0, jm0.data()); wm::SkinPosition(fox, j.data(), fox.verts[100], a); wm::SkinPosition(fox, jm0.data(), fox.verts[100], bb);
        CHECK(fabsf(a[0] - bb[0]) + fabsf(a[1] - bb[1]) + fabsf(a[2] - bb[2]) > 1e-3f, "fox run clip does not move the mesh");
    }
    if (argc > 3 && load(argv[3], &fig)) {
        CHECK(fig.texW == 0 && fig.rgba.empty() && fig.joints.size() == 19 && fig.clips.size() == 1, "rigged figure: texture %d, %zu joints, %zu clips", fig.texW, fig.joints.size(), fig.clips.size());
        wm::Avatar w, s0; walkAt(fig, 3.0f, 120, w); walkAt(fig, 0, 120, s0);
        CHECK(w.Pick(fig).clip == 0 && w.Pick(fig).time > 1.0f, "unnamed clip is the walk (clip %d time %.2f)", w.Pick(fig).clip, w.Pick(fig).time);
        CHECK(s0.Pick(fig).clip == 0 && s0.Pick(fig).time == 0, "unnamed clip, standing: first frame (time %.2f)", s0.Pick(fig).time);
    }
    printf("%s: %d checks, %d failed\n", g_fail ? "FAILED" : "ok", g_n, g_fail);
    return g_fail ? 1 : 0;
}
