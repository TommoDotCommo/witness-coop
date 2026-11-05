// Look-dev sheets of the character, shaded the way a player sees it: smooth skinned normals, the
// tangent-space normal map, the material map (occlusion, roughness), linear-light key, fill and rim
// lights, a contact shadow. No game and no GPU. One PNG per sheet:
//   char_lookdev <glb> <out.png> [--sheet body|face|hands|all] [--size 1] [--time 0.6]
//   body:  a turntable, eight views 45 degrees apart
//   face:  portraits at -35, 0, 35, 90 and 160 degrees, and a profile silhouette
//   hands: both hands, back and palm, from the front and the side
// The idle clip at --time seconds is the pose (the bind pose with --time -1); the life layer is off,
// so the eyes are open and look ahead. The review page (tools/char_review.py) shows these side by side.
#include "../proxy/model/emote_sim.h"
#include <zlib.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
using namespace wm;

static bool WritePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb) {
    std::vector<uint8_t> raw; raw.reserve(size_t(h) * (w * 3 + 1));
    for (int y = 0; y < h; ++y) { raw.push_back(0); raw.insert(raw.end(), rgb.begin() + size_t(y) * w * 3, rgb.begin() + size_t(y + 1) * w * 3); }
    uLongf zn = compressBound(raw.size()); std::vector<uint8_t> z(zn);
    if (compress2(z.data(), &zn, raw.data(), raw.size(), 6) != Z_OK) return false; z.resize(zn);
    FILE* f = fopen(path.c_str(), "wb"); if (!f) return false;
    auto be = [](uint32_t v, uint8_t* o) { o[0] = v >> 24; o[1] = v >> 16; o[2] = v >> 8; o[3] = v; };
    auto chunk = [&](const char* tag, const uint8_t* d, uint32_t n) {
        uint8_t hd[8]; be(n, hd); memcpy(hd + 4, tag, 4); fwrite(hd, 1, 8, f); if (n) fwrite(d, 1, n, f);
        uLong c = crc32(0, (const Bytef*)tag, 4); if (n) c = crc32(c, d, n); uint8_t cb[4]; be(uint32_t(c), cb); fwrite(cb, 1, 4, f);
    };
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ih[13]; be(w, ih); be(h, ih + 4); ih[8] = 8; ih[9] = 2; ih[10] = ih[11] = ih[12] = 0; chunk("IHDR", ih, 13);
    chunk("IDAT", z.data(), uint32_t(z.size())); chunk("IEND", nullptr, 0); fclose(f); return true;
}

static float Lin(float s) { return s <= .04045f ? s / 12.92f : powf((s + .055f) / 1.055f, 2.4f); }
static float Srgb(float l) { l = std::max(0.f, std::min(1.f, l)); return l <= .0031308f ? l * 12.92f : 1.055f * powf(l, 1 / 2.4f) - .055f; }
static void Norm(float* v) { float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l > 1e-12f) for (int k = 0; k < 3; ++k) v[k] /= l; }

static bool g_island = false;      // --light island: the game's daylight (a warm sun, a blue sky above, sandstone bounce below)
struct Posed { std::vector<std::array<float, 3>> P, N, T; };
// Skin positions, normals and tangents (the bind tangents come from the UVs, per vertex).
static Posed Pose(const Model& m, const float* mats, const std::vector<std::array<float, 3>>& bindT) {
    Posed o; size_t n = m.verts.size(); o.P.resize(n); o.N.resize(n); o.T.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const Vertex& v = m.verts[i]; SkinPosition(m, mats, v, o.P[i].data());
        float R[9] = {}; for (int k = 0; k < 4; ++k) { const float* s = mats + v.joints[k] * 16; for (int c = 0; c < 3; ++c) for (int r = 0; r < 3; ++r) R[c * 3 + r] += s[c * 4 + r] * v.weights[k]; }
        for (int r = 0; r < 3; ++r) { o.N[i][r] = R[r] * v.nrm[0] + R[3 + r] * v.nrm[1] + R[6 + r] * v.nrm[2]; o.T[i][r] = R[r] * bindT[i][0] + R[3 + r] * bindT[i][1] + R[6 + r] * bindT[i][2]; }
        Norm(o.N[i].data()); Norm(o.T[i].data());
    }
    return o;
}
static std::vector<std::array<float, 3>> BindTangents(const Model& m) {
    std::vector<std::array<float, 3>> t(m.verts.size(), {0, 0, 0});
    for (size_t i = 0; i + 2 < m.idx.size(); i += 3) {
        const Vertex &a = m.verts[m.idx[i]], &b = m.verts[m.idx[i + 1]], &c = m.verts[m.idx[i + 2]];
        float e1[3], e2[3]; for (int k = 0; k < 3; ++k) { e1[k] = b.pos[k] - a.pos[k]; e2[k] = c.pos[k] - a.pos[k]; }
        float du1 = b.uv[0] - a.uv[0], dv1 = b.uv[1] - a.uv[1], du2 = c.uv[0] - a.uv[0], dv2 = c.uv[1] - a.uv[1], d = du1 * dv2 - du2 * dv1;
        if (fabsf(d) < 1e-12f) continue; float r = 1 / d;
        for (int k = 0; k < 3; ++k) { float tk = (e1[k] * dv2 - e2[k] * dv1) * r; for (int j = 0; j < 3; ++j) t[m.idx[i + j]][k] += tk; }
    }
    for (size_t i = 0; i < t.size(); ++i) {      // Gram-Schmidt against the normal
        const float* n = m.verts[i].nrm; float d = t[i][0] * n[0] + t[i][1] * n[1] + t[i][2] * n[2];
        for (int k = 0; k < 3; ++k) t[i][k] -= n[k] * d;
        if (t[i][0] * t[i][0] + t[i][1] * t[i][1] + t[i][2] * t[i][2] < 1e-12f) t[i] = {1, 0, 0};
        Norm(t[i].data());
    }
    return t;
}

// A view: the camera yaw about the character (0: in front), pitch, and the window it frames (centre and height in metres).
struct Frame { float yaw, pitch, cx, cy, height; };
struct Tile {
    int W, H; std::vector<float> rgb, depth;
    Tile(int w, int h) : W(w), H(h), rgb(size_t(w) * h * 3), depth(size_t(w) * h) {}
};
static void Render(const Model& m, const Posed& p, const Frame& f, Tile& t, float groundY) {
    float cy = cosf(f.yaw * .0174533f), sy = sinf(f.yaw * .0174533f), cp = cosf(f.pitch * .0174533f), sp = sinf(f.pitch * .0174533f);
    float scale = t.H / f.height;
    auto view = [&](const float* v, float* o) { float x = v[0] * cy - v[2] * sy, z = v[0] * sy + v[2] * cy, y = v[1]; o[0] = x; o[1] = y * cp - z * sp; o[2] = y * sp + z * cp; };
    auto screen = [&](const float* v, float* o) { float q[3]; view(v, q); o[0] = t.W * .5f + (q[0] - f.cx) * scale; o[1] = t.H * .5f - (q[1] - f.cy) * scale; o[2] = q[2]; };
    // the backdrop: a soft vertical gradient, a floor below the ground line
    float gy[3], g0[3] = {0, groundY, 0}; screen(g0, gy);
    for (int y = 0; y < t.H; ++y) for (int x = 0; x < t.W; ++x) {
        size_t i = (size_t(y) * t.W + x) * 3; float a = float(y) / t.H;
        float c[3] = {.70f - .10f * a, .72f - .10f * a, .74f - .09f * a}; if (y > gy[1]) { c[0] = .52f; c[1] = .53f; c[2] = .54f; }
        if (g_island) { c[0] = .50f + .30f * a; c[1] = .68f + .18f * a; c[2] = .90f + .02f * a; if (y > gy[1]) { c[0] = .80f; c[1] = .72f; c[2] = .58f; } }
        for (int k = 0; k < 3; ++k) t.rgb[i + k] = Lin(c[k]);
    }
    std::fill(t.depth.begin(), t.depth.end(), -1e30f);
    size_t n = p.P.size(); std::vector<std::array<float, 3>> S(n), VN(n), VT(n);
    for (size_t i = 0; i < n; ++i) { screen(p.P[i].data(), S[i].data()); view(p.N[i].data(), VN[i].data()); view(p.T[i].data(), VT[i].data()); }
    // lights in view space (x right, y up, z toward the viewer): a warm key high left, a cool fill low right, a rim from behind
    float key[3] = {-.55f, .62f, .56f}, fill[3] = {.65f, -.05f, .75f}, rim[3] = {.35f, .45f, -.82f}; Norm(key); Norm(fill); Norm(rim);
    float keyC[3] = {1.25f, 1.12f, .98f}, fillC[3] = {.30f, .36f, .44f}, rimC[3] = {.55f, .55f, .60f}, ambC[3] = {.16f, .17f, .19f};
    float bounce[3] = {0, -1, .2}, bounceC[3] = {0, 0, 0}; Norm(bounce);
    if (g_island) {      // the sun high and warm, the sky's blue from above, the sandstone's warmth from below
        float k2[3] = {-.45f, .78f, .45f}, f2[3] = {.1f, 1, .2f}; Norm(k2); Norm(f2); memcpy(key, k2, 12); memcpy(fill, f2, 12);
        float kc[3] = {1.55f, 1.36f, 1.08f}, fc[3] = {.34f, .44f, .62f}, ac[3] = {.10f, .11f, .13f}, bc[3] = {.30f, .24f, .16f};
        memcpy(keyC, kc, 12); memcpy(fillC, fc, 12); memcpy(ambC, ac, 12); memcpy(bounceC, bc, 12);
    }
    auto sample = [](const std::vector<uint8_t>& img, int w, int h, float u, float v, float* o) {
        int x = std::max(0, std::min(w - 1, int(u * w))), y = std::max(0, std::min(h - 1, int(v * h)));
        for (int k = 0; k < 4; ++k) o[k] = img[(size_t(y) * w + x) * 4 + k] / 255.f;
    };
    for (size_t tri = 0; tri + 2 < m.idx.size(); tri += 3) {
        uint32_t ia = m.idx[tri], ib = m.idx[tri + 1], ic = m.idx[tri + 2];
        const auto &a = S[ia], &b = S[ib], &c = S[ic];
        float area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]); if (fabsf(area) < 1e-4f) continue;
        int x0 = std::max(0, int(floorf(std::min({a[0], b[0], c[0]})))), x1 = std::min(t.W - 1, int(ceilf(std::max({a[0], b[0], c[0]}))));
        int y0 = std::max(0, int(floorf(std::min({a[1], b[1], c[1]})))), y1 = std::min(t.H - 1, int(ceilf(std::max({a[1], b[1], c[1]}))));
        if (x0 > x1 || y0 > y1) continue;
        const Vertex &va = m.verts[ia], &vb = m.verts[ib], &vc = m.verts[ic];
        for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
            float px = x + .5f, py = y + .5f;
            float u = ((b[0] - px) * (c[1] - py) - (b[1] - py) * (c[0] - px)) / area;
            float w1 = ((c[0] - px) * (a[1] - py) - (c[1] - py) * (a[0] - px)) / area, w2 = 1 - u - w1;
            if (u < 0 || w1 < 0 || w2 < 0) continue;
            float z = u * a[2] + w1 * b[2] + w2 * c[2]; size_t pi = size_t(y) * t.W + x; if (z <= t.depth[pi]) continue; t.depth[pi] = z;
            float N[3], T[3]; for (int k = 0; k < 3; ++k) { N[k] = u * VN[ia][k] + w1 * VN[ib][k] + w2 * VN[ic][k]; T[k] = u * VT[ia][k] + w1 * VT[ib][k] + w2 * VT[ic][k]; }
            Norm(N); if (N[2] < 0 && area > 0) {}      // (both windings are drawn; the depth test keeps the front)
            float col[3] = {Lin(va.col[0] / 255.f), Lin(va.col[1] / 255.f), Lin(va.col[2] / 255.f)}, ao = 1, rough = .8f, metal = 0;
            if (va.col[3] && m.texW > 0) {
                float tu = u * va.uv[0] + w1 * vb.uv[0] + w2 * vc.uv[0], tv = u * va.uv[1] + w1 * vb.uv[1] + w2 * vc.uv[1], s4[4];
                sample(m.rgba, m.texW, m.texH, tu, tv, s4); for (int k = 0; k < 3; ++k) col[k] = Lin(s4[k]);
                if (!m.nrm.empty()) {      // tangent-space normal map (glTF: +Y up)
                    sample(m.nrm, m.nrmW, m.nrmH, tu, tv, s4); float tn[3] = {s4[0] * 2 - 1, s4[1] * 2 - 1, s4[2] * 2 - 1};
                    float d = T[0] * N[0] + T[1] * N[1] + T[2] * N[2]; for (int k = 0; k < 3; ++k) T[k] -= N[k] * d; Norm(T);
                    float Bt[3] = {N[1] * T[2] - N[2] * T[1], N[2] * T[0] - N[0] * T[2], N[0] * T[1] - N[1] * T[0]};
                    for (int k = 0; k < 3; ++k) N[k] = T[k] * tn[0] + Bt[k] * tn[1] + N[k] * tn[2]; Norm(N);
                }
                if (!m.mr.empty()) { sample(m.mr, m.mrW, m.mrH, tu, tv, s4); ao = s4[0]; rough = std::max(.08f, s4[1]); metal = s4[2]; }
            }
            if (N[2] < 0) for (float& k : N) k = -k;      // a back face seen through a gap: light its other side
            float H[3] = {key[0], key[1], key[2] + 1}; Norm(H);
            float nk = std::max(0.f, N[0] * key[0] + N[1] * key[1] + N[2] * key[2]), nf = std::max(0.f, N[0] * fill[0] + N[1] * fill[1] + N[2] * fill[2]);
            float nr = std::max(0.f, N[0] * rim[0] + N[1] * rim[1] + N[2] * rim[2]), fres = powf(1 - std::max(0.f, N[2]), 4), nb = std::max(0.f, N[0] * bounce[0] + N[1] * bounce[1] + N[2] * bounce[2]);
            float shin = 2 / (rough * rough * rough * rough + 1e-3f) - 2, spec = powf(std::max(0.f, N[0] * H[0] + N[1] * H[1] + N[2] * H[2]), std::max(1.f, shin)) * (shin + 8) / 25.f * nk;
            float f0 = .04f + .6f * metal;
            for (int k = 0; k < 3; ++k) {
                float diff = col[k] * (1 - metal) * (keyC[k] * nk + fillC[k] * nf * ao + bounceC[k] * nb * ao + ambC[k] * ao * 1.6f);
                float sp2 = (f0 + (1 - f0) * fres * .5f) * spec * keyC[k] * (metal > .5f ? col[k] * 2 : 1) * ao;
                t.rgb[pi * 3 + k] = diff + sp2 + rimC[k] * (nr * .25f + fres * .35f) * col[k] * ao;
            }
        }
    }
}
static void Blit(const Tile& t, std::vector<uint8_t>& sheet, int sw, int ox, int oy, int ss) {
    int w = t.W / ss, h = t.H / ss;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        float s[3] = {0, 0, 0}; for (int j = 0; j < ss; ++j) for (int i = 0; i < ss; ++i) for (int k = 0; k < 3; ++k) s[k] += t.rgb[(size_t(y * ss + j) * t.W + x * ss + i) * 3 + k];
        for (int k = 0; k < 3; ++k) sheet[(size_t(oy + y) * sw + ox + x) * 3 + k] = uint8_t(Srgb(s[k] / (ss * ss)) * 255 + .5f);
    }
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: char_lookdev <glb> <out.png> [--sheet body|face|hands|all] [--size k] [--time s]\n"); return 2; }
    std::string sheetKind = "all"; float size = 1, when = .6f;
    for (int i = 3; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--sheet")) sheetKind = argv[i + 1];
        else if (!strcmp(argv[i], "--size")) size = float(atof(argv[i + 1]));
        else if (!strcmp(argv[i], "--time")) when = float(atof(argv[i + 1]));
        else if (!strcmp(argv[i], "--light")) g_island = !strcmp(argv[i + 1], "island");
    }
    std::ifstream f(argv[1], std::ios::binary); std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
    Model m; std::string err; if (!LoadGlb(bytes.data(), bytes.size(), &m, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 3; }
    std::vector<float> mats(kMaxJoints * 16); JointMatrices(m, when < 0 ? -1 : FindClip(m, "idle"), std::max(0.f, when), mats.data());
    auto bindT = BindTangents(m); Posed p = Pose(m, mats.data(), bindT);
    auto jp = [&](const char* n) { int j = JointOf(m, n); return j >= 0 ? JointPos(m, mats.data(), j) : std::array<float, 3>{0, 1.6f, 0}; };
    auto head = jp("head"); auto eyeL = jp("eye.L"), eyeR = jp("eye.R"); float eyeY = JointOf(m, "eye.L") >= 0 ? (eyeL[1] + eyeR[1]) * .5f : 1.6766f;      // a lab head has no joints: the spec's eye line
    auto hl = JointOf(m, "hand.L") >= 0 ? jp("hand.L") : std::array<float, 3>{.265f, .91f, 0}, hr = JointOf(m, "hand.R") >= 0 ? jp("hand.R") : std::array<float, 3>{-.265f, .91f, 0};
    struct Row { std::vector<Frame> frames; int w, h; };
    std::vector<Row> rows;
    int S = 2;      // supersampling
    if (sheetKind == "body" || sheetKind == "all") { Row r{{}, int(260 * size), int(400 * size)}; for (int k = 0; k < 8; ++k) r.frames.push_back({k * 45.f - 20, 4, 0, .95f, 2.05f}); rows.push_back(r); }
    if (sheetKind == "face" || sheetKind == "all") { Row r{{}, int(330 * size), int(400 * size)}; for (float yaw : {-35.f, 0.f, 35.f, 90.f, 160.f}) r.frames.push_back({yaw, 2, 0, eyeY - .035f, .34f}); rows.push_back(r); (void)head; }
    if (sheetKind == "hands" || sheetKind == "all") {
        Row r{{}, int(260 * size), int(260 * size)};
        // centred on each hand: the window centre is the hand's position seen from that yaw (x along the view's right, y up)
        auto at = [&](const std::array<float, 3>& h, float yaw, float pitch) { float c = cosf(yaw * .0174533f), s = sinf(yaw * .0174533f), cp = cosf(pitch * .0174533f), sp = sinf(pitch * .0174533f);
            float x = h[0] * c - h[2] * s, z = h[0] * s + h[2] * c; return Frame{yaw, pitch, x, (h[1] - .09f) * cp - z * sp, .24f}; };
        r.frames = {at(hr, -60, 10), at(hr, -120, 10), at(hl, 60, 10), at(hl, 120, 10)}; rows.push_back(r);
    }
    if (sheetKind == "close") {      // material close-ups: the hat's crown and brim, the scarf, the coat's shoulder and the bag, from two sides
        Row r{{}, int(330 * size), int(400 * size)};
        r.frames = {{-25, 18, 0, 1.83f, .30f}, {150, 14, 0, 1.83f, .30f}, {-30, 6, 0, 1.55f, .34f}, {120, 6, 0, 1.55f, .34f}, {-40, 4, .05f, 1.12f, .40f}}; rows.push_back(r);
    }
    int SW = 0, SH = 0; for (auto& r : rows) { SW = std::max(SW, r.w * int(r.frames.size())); SH += r.h; }
    std::vector<uint8_t> sheet(size_t(SW) * SH * 3, 200);
    int oy = 0;
    for (auto& r : rows) {
        Tile t(r.w * S, r.h * S);
        for (size_t k = 0; k < r.frames.size(); ++k) { Render(m, p, r.frames[k], t, 0); Blit(t, sheet, SW, int(k) * r.w, oy, S); }
        oy += r.h;
    }
    if (!WritePng(argv[2], SW, SH, sheet)) return 4;
    printf("%s: %dx%d\n", argv[2], SW, SH);
}
