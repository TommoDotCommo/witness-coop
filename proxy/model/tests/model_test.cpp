// T0 test of the character model code (no game, no GPU): loading, skinning, animation, camera maths, placement, walk state.
// usage: model_test <adventurer.glb>.  Exit 0 = all checks passed.  Built with -DWM_MUT_xxx it must fail (see Makefile).
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

// A synthetic skinned model: a chain of `n` joints (each 0.01 m along x from its parent), one triangle at the origin bound to the LAST joint.
// In the bind pose that triangle must sit at n * 0.01 m, so a joint index that is cut off, wrapped or shifted puts it somewhere else.
static std::vector<uint8_t> ChainGlb(int n) {
    std::vector<uint8_t> bin(104, 0); float pos[9] = {0, 0, 0, 0.1f, 0, 0, 0, 0.1f, 0}, wt[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}; uint16_t ix[3] = {0, 1, 2};
    memcpy(&bin[0], pos, 36); for (int v = 0; v < 3; v++) bin[36 + v * 4] = (uint8_t)(n - 1); memcpy(&bin[48], wt, 48); memcpy(&bin[96], ix, 6);
    std::string j = "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"buffers\":[{\"byteLength\":104}],\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":12},{\"buffer\":0,\"byteOffset\":48,\"byteLength\":48},{\"buffer\":0,\"byteOffset\":96,\"byteLength\":6}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[0.1,0.1,0]},{\"bufferView\":1,\"componentType\":5121,\"count\":3,\"type\":\"VEC4\"},{\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"},{\"bufferView\":3,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"JOINTS_0\":1,\"WEIGHTS_0\":2},\"indices\":3}]}],\"nodes\":[";
    for (int i = 0; i < n; i++) { j += "{\"translation\":[0.01,0,0]"; if (i + 1 < n) j += ",\"children\":[" + std::to_string(i + 1) + "]"; j += "},"; }
    j += "{\"mesh\":0,\"skin\":0}],\"skins\":[{\"joints\":["; for (int i = 0; i < n; i++) j += (i ? "," : "") + std::to_string(i);
    j += "]}],\"scenes\":[{\"nodes\":[0," + std::to_string(n) + "]}]}"; while (j.size() % 4) j += ' ';
    std::vector<uint8_t> out(12 + 8 + j.size() + 8 + bin.size()); uint32_t total = (uint32_t)out.size(), jl = (uint32_t)j.size(), bl = (uint32_t)bin.size(), mg = 0x46546C67, ver = 2, jt = 0x4E4F534A, bt = 0x004E4942;
    uint8_t* q = out.data(); memcpy(q, &mg, 4); memcpy(q + 4, &ver, 4); memcpy(q + 8, &total, 4); memcpy(q + 12, &jl, 4); memcpy(q + 16, &jt, 4); memcpy(q + 20, j.data(), jl);
    q += 20 + jl; memcpy(q, &bl, 4); memcpy(q + 4, &bt, 4); memcpy(q + 8, bin.data(), bl); return out;
}

// A synthetic model with two meshes, each with its own skin on the same two joints (listed in the other order), and two flat-colour materials (red, blue).
static std::vector<uint8_t> TwoSkinGlb() {
    std::vector<uint8_t> bin(208, 0); float pos[9] = {0, 0, 0, 0.1f, 0, 0, 0, 0.1f, 0}, wt[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}; uint16_t ix[3] = {0, 1, 2};
    for (int m = 0; m < 2; m++) { uint8_t* b = &bin[m * 104]; memcpy(b, pos, 36); for (int v = 0; v < 3; v++) b[36 + v * 4] = m == 0 ? 1 : 0; memcpy(b + 48, wt, 48); memcpy(b + 96, ix, 6); }
    std::string j = "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"buffers\":[{\"byteLength\":208}],\"bufferViews\":[";
    for (int m = 0; m < 2; m++) { int o = m * 104; char s[400]; snprintf(s, sizeof s, "%s{\"buffer\":0,\"byteOffset\":%d,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":%d,\"byteLength\":12},{\"buffer\":0,\"byteOffset\":%d,\"byteLength\":48},{\"buffer\":0,\"byteOffset\":%d,\"byteLength\":6}", m ? "," : "", o, o + 36, o + 48, o + 96); j += s; }
    j += "],\"accessors\":[";
    for (int m = 0; m < 2; m++) { int b = m * 4; char s[600]; snprintf(s, sizeof s, "%s{\"bufferView\":%d,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[0.1,0.1,0]},{\"bufferView\":%d,\"componentType\":5121,\"count\":3,\"type\":\"VEC4\"},{\"bufferView\":%d,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"},{\"bufferView\":%d,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}", m ? "," : "", b, b + 1, b + 2, b + 3); j += s; }
    j += "],\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,0,0,1]}},{\"pbrMetallicRoughness\":{\"baseColorFactor\":[0,0,1,1]}}],"
         "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"JOINTS_0\":1,\"WEIGHTS_0\":2},\"indices\":3,\"material\":0}]},{\"primitives\":[{\"attributes\":{\"POSITION\":4,\"JOINTS_0\":5,\"WEIGHTS_0\":6},\"indices\":7,\"material\":1}]}],"
         "\"nodes\":[{\"children\":[1]},{\"translation\":[1,0,0],\"children\":[2]},{\"translation\":[1,0,0]},{\"mesh\":0,\"skin\":0},{\"mesh\":1,\"skin\":1}],"
         "\"skins\":[{\"joints\":[1,2]},{\"joints\":[2,1]}],\"scenes\":[{\"nodes\":[0,3,4]}]}"; while (j.size() % 4) j += ' ';
    std::vector<uint8_t> out(12 + 8 + j.size() + 8 + bin.size()); uint32_t total = (uint32_t)out.size(), jl = (uint32_t)j.size(), bl = (uint32_t)bin.size(), mg = 0x46546C67, ver = 2, jt = 0x4E4F534A, bt = 0x004E4942;
    uint8_t* q = out.data(); memcpy(q, &mg, 4); memcpy(q + 4, &ver, 4); memcpy(q + 8, &total, 4); memcpy(q + 12, &jl, 4); memcpy(q + 16, &jt, 4); memcpy(q + 20, j.data(), jl);
    q += 20 + jl; memcpy(q, &bl, 4); memcpy(q + 4, &bt, 4); memcpy(q + 8, bin.data(), bl); return out;
}

int main(int argc, char** argv) {
    std::ifstream f(argv[1], std::ios::binary); std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), {});
    wm::Model m; std::string err;
    CHECK(wm::LoadGlb(file.data(), file.size(), &m, &err), "load: %s", err.c_str());
    if (g_fail) return 1;
    CHECK(m.verts.size() > 1000 && m.idx.size() % 3 == 0, "verts %zu idx %zu", m.verts.size(), m.idx.size());
    CHECK(m.joints.size() == 29 && m.jointCount() == 30, "joints %zu (the adventurer has 20 body joints, 2 eyes, 2 lids, 5 scarf and 2 gear)", m.joints.size());
    CHECK(m.clips.size() == 6 && m.clips[0].duration > 0.5f && m.clips[0].duration < 10, "clips %zu (idle, walk, run and three fidgets)", m.clips.size());
    CHECK(m.texW > 0 && m.rgba.size() == (size_t)m.texW * m.texH * 4, "texture %dx%d", m.texW, m.texH);
    CHECK(m.nrmW >= 512 && m.nrm.size() == (size_t)m.nrmW * m.nrmH * 4 && m.mrW >= 512 && m.mr.size() == (size_t)m.mrW * m.mrH * 4, "normal map %dx%d, material map %dx%d", m.nrmW, m.nrmH, m.mrW, m.mrH);
    {   // the normal map is mostly 'up' (flat relief tilted a little), the material map has rough cloth and a little metal
        double nz = 0, rough = 0, metal = 0; size_t n = (size_t)m.nrmW * m.nrmH, nm = (size_t)m.mrW * m.mrH;
        for (size_t i = 0; i < n; i += 97) nz += m.nrm[i * 4 + 2];
        for (size_t i = 0; i < nm; i += 97) { rough += m.mr[i * 4 + 1]; metal += m.mr[i * 4 + 2] > 128; }
        nz /= (n / 97) * 255.0; rough /= (nm / 97) * 255.0; metal /= (nm / 97);
        CHECK(nz > 0.9 && rough > 0.4 && rough < 0.95 && metal > 0.002 && metal < 0.1, "normal z mean %.3f, roughness mean %.2f, metal fraction %.3f", nz, rough, metal);
    }
    float h = m.bmax[1] - m.bmin[1];
    CHECK(h > 1.7f && h < 2.1f, "adventurer bind-pose height %.3f", h);
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
    float dur = m.clips[1].duration, maxMove = 0, lowest = 1e9f, highest = -1e9f; bool inside = true;
    for (int step = 0; step < 16; step++) {
        wm::JointMatrices(m, 1, dur * step / 16.0f, jmt.data());
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
    wm::JointMatrices(m, 1, dur * 0.5f, ja.data()); wm::JointMatrices(m, 1, dur * 1.5f, jb.data());
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
        CHECK(pk.clip == 1 && pk.time > 1.0f, "walking pick clip %d time %.2f", pk.clip, pk.time);
        CHECK(Near(a.pos()[0], p[0], 0.4f), "displayed x %.2f vs %.2f", a.pos()[0], p[0]);
        for (int fr = 0; fr < 90; fr++, t += 17) a.Update(t, true, seq, p, 0.3f);
        CHECK(a.speed() < 0.25f, "still walking after the packets stopped (%.2f)", a.speed());
        CHECK(a.Pick(m).clip == 0 && a.Pick(m).time > 0, "idle must animate (clip %d time %.2f)", a.Pick(m).clip, a.Pick(m).time);
        CHECK(Near(a.pos()[0], p[0], 0.05f), "settled at x %.2f vs %.2f", a.pos()[0], p[0]);
        float far[3] = {500, 300, 5}; seq++; a.Update(t, true, seq, far, 0); t += 17; seq++; a.Update(t + 33, true, seq, far, 0);
        CHECK(a.speed() < 1.0f && Near(a.pos()[0], 500, 1.0f), "teleport: speed %.1f pos %.1f", a.speed(), a.pos()[0]);
    }
    auto walkAt = [&](const wm::Model& mm, float mps, int frames, wm::Avatar& a) { uint64_t t = 5000; unsigned seq = 1; float p[3] = {0, 0, 5};
        for (int fr = 0; fr < frames; fr++, t += 17) { if (fr % 2 == 0) { seq++; p[0] += mps / 30; } a.Update(t, true, seq, p, 0); } (void)mm; };
    {
        wm::Avatar still, walk, run; walkAt(m, 0, 120, still); walkAt(m, 3.0f, 120, walk); walkAt(m, 6.5f, 120, run);
        CHECK(m.clips[still.Pick(m).clip].name == "idle" && still.Pick(m).time > 1.0f, "standing plays idle");
        CHECK(m.clips[walk.Pick(m).clip].name == "walk", "walking plays walk");
        CHECK(m.clips[run.Pick(m).clip].name == "run", "running plays run");
    }

    // Preserve unnamed single-clip coverage without distributing a sample character.
    {
        wm::Model single = m; single.clips = {m.clips[1]}; single.clips[0].name.clear();
        wm::Avatar moving, still; walkAt(single, 3, 120, moving); walkAt(single, 0, 120, still);
        CHECK(moving.Pick(single).clip == 0 && moving.Pick(single).time > 1, "unnamed single clip is the walk");
        CHECK(still.Pick(single).clip == 0 && still.Pick(single).time == 0, "unnamed single clip holds at rest");
    }

    // a character with many joints (synthetic skeleton): up to kMaxJoints - 1 = 255 must load and skin correctly, one more must be refused
    for (int n : {150, 255}) {
        std::vector<uint8_t> g = ChainGlb(n); wm::Model cm; std::string ce;
        bool ok = wm::LoadGlb(g.data(), g.size(), &cm, &ce); CHECK(ok, "%d joints: %s", n, ce.c_str());
        if (!ok) continue;
        std::vector<float> cj(wm::kMaxJoints * 16); wm::JointMatrices(cm, -1, 0, cj.data()); float sp[3] = {0, 0, 0}; wm::SkinPosition(cm, cj.data(), cm.verts[0], sp);
        CHECK((int)cm.joints.size() == n && cm.jointCount() == n + 1 && cm.verts[0].joints[0] == n - 1, "%d joints: loaded %zu joints, vertex bound to joint %d", n, cm.joints.size(), cm.verts[0].joints[0]);
        CHECK(Near(sp[0], n * 0.01f, 1e-3f) && Near(sp[1], 0, 1e-4f), "%d joints: the triangle skins to x %.4f, wanted %.4f", n, sp[0], n * 0.01f);
    }
    { std::vector<uint8_t> g = ChainGlb(256); wm::Model cm; std::string ce; CHECK(!wm::LoadGlb(g.data(), g.size(), &cm, &ce), "256 joints accepted (the limit is 255)"); }

    // cross-fade: after the player stops, the pose fades from the walk to the idle over fadeMs instead of jumping; a new state starts from what was playing
    if (m.clips.size() >= 3) {
        auto clipName = [&](int c) { return c >= 0 ? m.clips[c].name : std::string("none"); };
        wm::Avatar a; walkAt(m, 3.0f, 120, a); wm::ClipPick p0 = a.Pick(m);
        CHECK(clipName(p0.clip) == "walk" && p0.from < 0 && p0.blend == 1, "walking: clip %s from %d blend %.2f", clipName(p0.clip).c_str(), p0.from, p0.blend);
        uint64_t t = 5000 + 120 * 17; unsigned seq = 1000; float p[3] = {0, 0, 5}; p[0] = 3.0f * 60 / 30 * 2;
        float firstBlend = -1, lastBlend = -1; int fadeFrames = 0; std::string fromName;
        for (int fr = 0; fr < 200; fr++, t += 17) {
            a.Update(t, true, seq, p, 0); wm::ClipPick q = a.Pick(m);
            if (clipName(q.clip) == "idle" && q.from >= 0 && q.blend < 1) { if (firstBlend < 0) firstBlend = q.blend; lastBlend = q.blend; fadeFrames++; fromName = clipName(q.from); }
        }
        CHECK(fadeFrames >= 5 && firstBlend >= 0 && firstBlend < 0.3f && lastBlend > firstBlend && fromName == "walk", "stopping: %d frames of fade (blend %.2f .. %.2f) from '%s'", fadeFrames, firstBlend, lastBlend, fromName.c_str());
        wm::ClipPick done = a.Pick(m); CHECK(clipName(done.clip) == "idle" && done.blend == 1, "fade finished: clip %s blend %.2f", clipName(done.clip).c_str(), done.blend);
        // the blended pose: w = 0 is the old clip, w = 1 the new one, in between it is neither
        std::vector<float> ja(wm::kMaxJoints * 16), jb(wm::kMaxJoints * 16), jh(wm::kMaxJoints * 16), jn(wm::kMaxJoints * 16); float pa[3], pb[3], ph[3], pn[3]; const wm::Vertex& vx = m.verts[100];
        wm::JointMatrices(m, 1, 0.3f, ja.data()); wm::JointMatrices(m, 0, 0.9f, jb.data()); wm::SkinPosition(m, ja.data(), vx, pa); wm::SkinPosition(m, jb.data(), vx, pb);
        wm::JointMatricesBlend(m, 1, 0.3f, 0, 0.9f, 0.0f, jn.data()); wm::SkinPosition(m, jn.data(), vx, pn);
        CHECK(Near(pn[0], pa[0], 1e-4f) && Near(pn[1], pa[1], 1e-4f) && Near(pn[2], pa[2], 1e-4f), "blend 0 is not the old clip");
        wm::JointMatricesBlend(m, 1, 0.3f, 0, 0.9f, 1.0f, jn.data()); wm::SkinPosition(m, jn.data(), vx, pn);
        CHECK(Near(pn[0], pb[0], 1e-4f) && Near(pn[1], pb[1], 1e-4f) && Near(pn[2], pb[2], 1e-4f), "blend 1 is not the new clip");
        wm::JointMatricesBlend(m, 1, 0.3f, 0, 0.9f, 0.5f, jh.data()); wm::SkinPosition(m, jh.data(), vx, ph);
        float dA = fabsf(ph[0] - pa[0]) + fabsf(ph[1] - pa[1]) + fabsf(ph[2] - pa[2]), dB = fabsf(ph[0] - pb[0]) + fabsf(ph[1] - pb[1]) + fabsf(ph[2] - pb[2]), dAB = fabsf(pa[0] - pb[0]) + fabsf(pa[1] - pb[1]) + fabsf(pa[2] - pb[2]);
        CHECK(dA > 1e-3f && dB > 1e-3f && dA < dAB * 1.5f && dB < dAB * 1.5f, "half blend: %.3f from old, %.3f from new, clips differ by %.3f", dA, dB, dAB);
        // the run clip is timed by its own reference speed (GaitCfg::runRef), not by the walk's
        wm::GaitCfg g1, g2; g2.runRef = 4.5f; wm::Avatar r1(g1), r2(g2); walkAt(m, 6.5f, 120, r1); walkAt(m, 6.5f, 120, r2);
        float t1 = r1.Pick(m).time, t2 = r2.Pick(m).time;
        CHECK(clipName(r1.Pick(m).clip) == "run" && clipName(r2.Pick(m).clip) == "run" && t1 > 0 && Near(t1 / t2, 4.5f / 1.6f, 0.05f), "run clip time %.2f (walk reference) vs %.2f (run reference 4.5): ratio %.2f, wanted %.2f", t1, t2, t1 / t2, 4.5f / 1.6f);
    }
    // eye height: the eyes are found by joint name, in the space the bounds are measured in. The nodes above the skeleton scale it by 0.01 while the
    // inverse bind matrices are in the unscaled space; the fit must put the eyes at the camera's eye height above the feet all the same
    {
        wm::Model em; em.nodes.resize(4); const char* nm[4] = {"armature", "c_eye.l_050", "c_eye.r_063", "c_eyelid_top.l_053"};
        float px[4] = {0, 5, -5, 0}, py[4] = {0, 150, 154, 300}, pz[4] = {0, -30, -30, 0};
        for (int i = 0; i < 4; i++) { em.nodes[i].name = nm[i]; em.nodes[i].parent = i ? 0 : -1; em.nodes[i].t[0] = px[i]; em.nodes[i].t[1] = py[i]; em.nodes[i].t[2] = pz[i]; }
        for (int k = 0; k < 3; k++) em.nodes[0].s[k] = 0.01f;
        em.joints = {1, 2, 3}; em.ibm.assign(48, 0);
        for (int j = 0; j < 3; j++) { float* M = &em.ibm[j * 16]; M[0] = M[5] = M[10] = M[15] = 1; M[12] = -px[j + 1]; M[13] = -py[j + 1]; M[14] = -pz[j + 1]; }      // inverse of the joint's bind pose without the 0.01
        em.bmin[0] = -0.5f; em.bmax[0] = 0.5f; em.bmin[1] = 0; em.bmax[1] = 2.0f; em.bmin[2] = -0.5f; em.bmax[2] = 0.5f;      // 200 units tall, seen through the 0.01
        float ey = wm::EyeHeight(em, "c_eye."); CHECK(Near(ey, 1.52f, 1e-3f), "eye height %.4f (wanted 1.52: the mean of the two eyes, not the eyelid, in bounds space)", ey);
        CHECK(wm::EyeHeight(em, "nothing") == wm::kNoEye && wm::EyeHeight(em, "") == wm::kNoEye, "no matching joint must give kNoEye");
        wm::Fit fe = wm::FitToEye(em, wm::kCameraEye, "c_eye."); float feet[3] = {10, 20, 5}, M[16]; wm::ModelMatrix(feet, 0.4f, fe, 0, M);
        float ez = M[4 + 2] * ey + M[12 + 2];         // world z of a model-space point on the eye line (x = z = 0)
        CHECK(Near(ez - 5, wm::kCameraEye, 1e-3f), "the eyes are %.3f m above the feet, the camera is %.3f", ez - 5, wm::kCameraEye);
        // a pose moves the eyes: a clip that lifts the left eye joint by 10 local units (0.1 in bounds space) lifts the mean by 0.05
        wm::Clip lift; lift.name = "lift"; lift.duration = 1; wm::Channel ch; ch.node = 1; ch.path = 0; ch.times = {0, 1}; ch.values = {5, 150, -30, 5, 160, -30}; lift.channels.push_back(ch); em.clips.push_back(lift);
        std::vector<float> jp(wm::kMaxJoints * 16); wm::JointMatrices(em, 0, 0.999f, jp.data()); float eyp = wm::EyeHeightPosed(em, jp.data(), "c_eye.");
        CHECK(Near(eyp, 1.57f, 2e-3f), "posed eye height %.4f (wanted 1.57: one eye 0.1 higher)", eyp);
        { // eyes below the origin of the bounds space (a negative height is a height): the same model hung 200 units lower, bounds y = -2 .. 0, eyes at -0.48
          wm::Model nm = em; nm.clips.clear();
          for (int i = 1; i < 4; i++) nm.nodes[i].t[1] -= 200;
          for (int j = 0; j < 3; j++) nm.ibm[j * 16 + 13] += 200;                // the inverse bind translation moves with the joint
          nm.bmin[1] = -2.0f; nm.bmax[1] = 0.0f;
          float en = wm::EyeHeight(nm, "c_eye."); CHECK(Near(en, -0.48f, 2e-3f), "negative eye height %.4f (wanted -0.48)", en);
          wm::Fit fn = wm::FitToEye(nm, wm::kCameraEye, "c_eye."); float f2[3] = {0, 0, 0}, M2[16]; wm::ModelMatrix(f2, 0, fn, 0, M2);
          CHECK(Near(M2[6] * en + M2[14], wm::kCameraEye, 2e-3f), "negative: the eyes are %.3f m above the feet, the camera is %.3f", M2[6] * en + M2[14], wm::kCameraEye);
        }
        wm::Fit nf = wm::FitToEye(em, 1.0f, "nothing"); CHECK(Near((em.bmax[1] - em.bmin[1]) * nf.scale, 1.0f / 0.92f, 1e-3f), "no eye joints: the model is %.3f m high", (em.bmax[1] - em.bmin[1]) * nf.scale);
    }

    // several skins on one skeleton feed one joint list, and every mesh is bound through its own skin's order; flat-colour materials keep their colour per vertex
    {
        std::vector<uint8_t> g = TwoSkinGlb(); wm::Model sm; std::string se; bool ok = wm::LoadGlb(g.data(), g.size(), &sm, &se); CHECK(ok, "two skins: %s", se.c_str());
        if (ok) {
            CHECK(sm.joints.size() == 2 && sm.joints[0] == 1 && sm.joints[1] == 2 && sm.verts.size() == 6, "two skins: %zu joints (nodes %d %d), %zu vertices", sm.joints.size(), sm.joints.size() > 0 ? sm.joints[0] : -1, sm.joints.size() > 1 ? sm.joints[1] : -1, sm.verts.size());
            CHECK(sm.verts[0].joints[0] == 1 && sm.verts[3].joints[0] == 1, "two skins: the first mesh (skin joint 1 = node 2) is bound to joint %d, the second (skin joint 0 = node 2) to joint %d, both wanted 1", sm.verts[0].joints[0], sm.verts[3].joints[0]);
            CHECK(sm.verts[0].col[0] == 255 && sm.verts[0].col[1] == 0 && sm.verts[0].col[2] == 0 && sm.verts[0].col[3] == 0 && sm.verts[3].col[0] == 0 && sm.verts[3].col[2] == 255 && sm.verts[3].col[3] == 0,
                  "flat colours: first mesh (%d %d %d %d), second (%d %d %d %d), wanted red and blue with no texture", sm.verts[0].col[0], sm.verts[0].col[1], sm.verts[0].col[2], sm.verts[0].col[3], sm.verts[3].col[0], sm.verts[3].col[1], sm.verts[3].col[2], sm.verts[3].col[3]);
        }
    }

    // which clip of a model an emote plays: the model's own named clips, "No" only as a whole name
    {
        auto mk = [](std::initializer_list<const char*> names) { wm::Model mm; for (const char* n : names) { wm::Clip c; c.name = n; c.duration = 1; mm.clips.push_back(c); } return mm; };
        auto clipName = [](const wm::Model& model, int emote) { int c = wm::EmoteClip(model, emote); return c >= 0 && size_t(c) < model.clips.size() ? model.clips[c].name : std::string(); };
        wm::Model robot = mk({"Dance", "Death", "Idle", "Jump", "No", "Punch", "Running", "Sitting", "Standing", "ThumbsUp", "Walking", "WalkJump", "Wave", "Yes"});
        CHECK(clipName(robot, 1) == "Wave" && clipName(robot, 2) == "Dance" && clipName(robot, 3) == "ThumbsUp" && clipName(robot, 4) == "Yes" && clipName(robot, 5) == "No", "the robot's own emote clips");
        wm::Model xbot = mk({"agree", "headShake", "idle", "run", "sad_pose", "sneak_pose", "walk"});
        CHECK(clipName(xbot, 4) == "agree" && clipName(xbot, 5) == "headShake" && wm::EmoteClip(xbot, 1) < 0 && wm::EmoteClip(xbot, 2) < 0, "xbot: agree and headShake, nothing for wave and dance");
        wm::Model snow = mk({"Idle", "Snow", "Annotate", "walk"});
        CHECK(wm::EmoteClip(snow, 5) < 0 && wm::EmoteClip(snow, 6) < 0 && wm::EmoteClip(snow, 7) < 0, "a clip with 'no' inside its name is not the No emote");
        wm::Model samba = mk({"SambaDance", "TPose"}); CHECK(clipName(samba, 2) == "SambaDance", "SambaDance is a dance");
    }
    printf("%s: %d checks, %d failed\n", g_fail ? "FAILED" : "ok", g_n, g_fail);
    return g_fail ? 1 : 0;
}
