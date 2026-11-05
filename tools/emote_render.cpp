// Review renders of the emotes through the production pose code and CPU skinning (no game, no GPU).
// For every emote: a sprite sheet (WebP when libwebp is installed, else PNG; frames in rows of up to 12, each frame its front, side and rear views) and a JSON
// file with the frame count, timing and the wrists' and head's screen positions (the review page
// draws spacing trails from them). Also a contact sheet per emote for quick looks.
//   emote_render <glb> <out dir> [--clips <file>] [--ids 1,2,...] [--fps 15] [--size 200x260] [--format png] [--quality 80]
//   emote_render <glb> - --probe "arm.R=10,90,100 fore.R=80"     prints where that pose puts the hands and the head
//   emote_render <glb> - --probe "<seed pose>" --reach tip.R=x,y,z     solves the arm that puts the hand there
#include "../proxy/model/emote_sim.h"
#include <zlib.h>
#include <dlfcn.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
using namespace wm;

static bool WritePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb) {
    std::vector<uint8_t> raw; raw.reserve(size_t(h) * (w * 3 + 1));
    for (int y = 0; y < h; ++y) { raw.push_back(0); raw.insert(raw.end(), rgb.begin() + size_t(y) * w * 3, rgb.begin() + size_t(y + 1) * w * 3); }
    uLongf zn = compressBound(raw.size()); std::vector<uint8_t> z(zn);
    if (compress2(z.data(), &zn, raw.data(), raw.size(), 9) != Z_OK) return false; z.resize(zn);
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

// WebP through the system's libwebp when it is installed (no headers needed: two C functions); PNG otherwise.
struct WebP {
    size_t (*encode)(const uint8_t*, int, int, int, float, uint8_t**) = nullptr; uint8_t* (*decode)(const uint8_t*, size_t, int*, int*) = nullptr; void (*release)(void*) = nullptr;
    WebP() {
        for (const char* n : {"libwebp.so.7", "libwebp.so", "libwebp.so.6"}) {
            void* h = dlopen(n, RTLD_NOW); if (!h) continue;
            encode = (decltype(encode))dlsym(h, "WebPEncodeRGB"); decode = (decltype(decode))dlsym(h, "WebPDecodeRGB"); release = (decltype(release))dlsym(h, "WebPFree");
            if (encode && decode && release) return;
            encode = nullptr;
        }
    }
    bool Write(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb, float q) const {
        uint8_t* out = nullptr; size_t n = encode(rgb.data(), w, h, w * 3, q, &out); if (!n) return false;
        int dw = 0, dh = 0; uint8_t* back = decode(out, n, &dw, &dh);      // round trip: the file must decode to the picture
        bool ok = back && dw == w && dh == h; double err = 0;
        if (ok) { for (size_t i = 0; i < rgb.size(); i += 97) err += fabs(double(back[i]) - rgb[i]); err /= rgb.size() / 97.0; ok = err < 12; }
        if (back) release(back);
        FILE* f = ok ? fopen(path.c_str(), "wb") : nullptr; if (f) { fwrite(out, 1, n, f); fclose(f); }
        release(out); return f != nullptr;
    }
};

struct View { float yaw, pitch; const char* name; };
static const View kViews[] = {{-25, 8, "front"}, {90, 6, "side"}, {200, 8, "rear"}};
constexpr int kViewCount = 3;

struct Raster {
    int W, H, SS; float scale, groundY; std::vector<float> rgb, depth;
    Raster(int w, int h, int ss) : W(w * ss), H(h * ss), SS(ss), rgb(size_t(W) * H * 3), depth(size_t(W) * H) {
        scale = H / 2.45f; groundY = H - 0.12f * scale;
    }
    void Clear() {
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
            float g = y < groundY ? 0.93f - 0.08f * y / H : 0.80f; size_t i = (size_t(y) * W + x) * 3;
            rgb[i] = g; rgb[i + 1] = g; rgb[i + 2] = g + 0.012f;
        }
        std::fill(depth.begin(), depth.end(), -1e30f);
    }
    void Project(const float p[3], const View& v, float out[3]) const {
        float cy = cosf(v.yaw * .0174533f), sy = sinf(v.yaw * .0174533f), cp = cosf(v.pitch * .0174533f), sp = sinf(v.pitch * .0174533f);
        float x = p[0] * cy - p[2] * sy, z = p[0] * sy + p[2] * cy, y = p[1];
        float y2 = y * cp - z * sp, z2 = y * sp + z * cp;
        out[0] = W * .5f + x * scale; out[1] = groundY - y2 * scale; out[2] = z2;
    }
    void Shadow(const float feet[2][3], const View& v) {      // a soft contact shadow on the ground under each foot
        for (int f = 0; f < 2; ++f) {
            float c[3]; float p[3] = {feet[f][0], 0, feet[f][2]}; Project(p, v, c);
            float rx = .13f * scale, ry = .035f * scale;
            for (int y = int(c[1] - ry * 2); y <= int(c[1] + ry * 2); ++y) for (int x = int(c[0] - rx * 1.5f); x <= int(c[0] + rx * 1.5f); ++x) {
                if (x < 0 || y < 0 || x >= W || y >= H) continue;
                float dx = (x - c[0]) / rx, dy = (y - c[1]) / ry, d = dx * dx + dy * dy; if (d > 1) continue;
                float k = 0.22f * (1 - d); size_t i = (size_t(y) * W + x) * 3; for (int ch = 0; ch < 3; ++ch) rgb[i + ch] *= 1 - k;
            }
        }
    }
    void Mesh(const Model& m, const std::vector<std::array<float, 3>>& P, const View& v) {
        std::vector<std::array<float, 3>> S(P.size());
        for (size_t i = 0; i < P.size(); ++i) Project(P[i].data(), v, S[i].data());
        const float L[3] = {-.45f, .75f, .5f}; float ll = sqrtf(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
        float cy = cosf(v.yaw * .0174533f), sy = sinf(v.yaw * .0174533f);
        for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
            uint32_t ia = m.idx[t], ib = m.idx[t + 1], ic = m.idx[t + 2];
            const auto &a = S[ia], &b = S[ib], &c = S[ic];
            float area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]); if (fabsf(area) < 1e-3f) continue;
            // the face normal in model space, turned into view space for the light (the light follows the viewer)
            float e1[3], e2[3], n[3]; for (int k = 0; k < 3; ++k) { e1[k] = P[ib][k] - P[ia][k]; e2[k] = P[ic][k] - P[ia][k]; }
            n[0] = e1[1] * e2[2] - e1[2] * e2[1]; n[1] = e1[2] * e2[0] - e1[0] * e2[2]; n[2] = e1[0] * e2[1] - e1[1] * e2[0];
            float nl = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]); if (nl < 1e-12f) continue;
            float vn[3] = {(n[0] * cy - n[2] * sy) / nl, n[1] / nl, (n[0] * sy + n[2] * cy) / nl};
            float lam = fabsf(vn[0] * L[0] + vn[1] * L[1] + vn[2] * L[2]) / ll, rim = powf(1 - fabsf(vn[2]), 3) * .25f;
            float light = .42f + .62f * lam + rim;
            int x0 = std::max(0, int(floorf(std::min({a[0], b[0], c[0]})))), x1 = std::min(W - 1, int(ceilf(std::max({a[0], b[0], c[0]}))));
            int y0 = std::max(0, int(floorf(std::min({a[1], b[1], c[1]})))), y1 = std::min(H - 1, int(ceilf(std::max({a[1], b[1], c[1]}))));
            const Vertex& va = m.verts[ia]; const Vertex& vb = m.verts[ib]; const Vertex& vc = m.verts[ic];
            for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
                float px = x + .5f, py = y + .5f;
                float u = ((b[0] - px) * (c[1] - py) - (b[1] - py) * (c[0] - px)) / area;
                float w1 = ((c[0] - px) * (a[1] - py) - (c[1] - py) * (a[0] - px)) / area, w2 = 1 - u - w1;
                if (u < 0 || w1 < 0 || w2 < 0) continue;
                float z = u * a[2] + w1 * b[2] + w2 * c[2]; size_t pi = size_t(y) * W + x; if (z <= depth[pi]) continue; depth[pi] = z;
                float col[3] = {va.col[0] / 255.f, va.col[1] / 255.f, va.col[2] / 255.f};
                if (va.col[3] && m.texW > 0 && !m.rgba.empty()) {
                    float tu = u * va.uv[0] + w1 * vb.uv[0] + w2 * vc.uv[0], tv = u * va.uv[1] + w1 * vb.uv[1] + w2 * vc.uv[1];
                    int tx = std::max(0, std::min(m.texW - 1, int(tu * m.texW))), ty = std::max(0, std::min(m.texH - 1, int(tv * m.texH)));
                    for (int k = 0; k < 3; ++k) col[k] = m.rgba[(size_t(ty) * m.texW + tx) * 4 + k] / 255.f;
                }
                for (int k = 0; k < 3; ++k) rgb[pi * 3 + k] = std::min(1.f, col[k] * light);
            }
        }
    }
    void Down(std::vector<uint8_t>& out, int ow, int ox, int oy) const {      // box filter into a tile of a bigger sheet
        int w = W / SS, h = H / SS;
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            float s[3] = {0, 0, 0};
            for (int j = 0; j < SS; ++j) for (int i = 0; i < SS; ++i) for (int k = 0; k < 3; ++k) s[k] += rgb[((size_t(y * SS + j)) * W + x * SS + i) * 3 + k];
            for (int k = 0; k < 3; ++k) { float v = powf(s[k] / (SS * SS), 1 / 1.1f); out[((size_t(oy + y)) * ow + ox + x) * 3 + k] = uint8_t(std::min(255.f, v * 255 + .5f)); }
        }
    }
};

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: emote_render <glb> <out dir> [--clips file] [--ids 1,2] [--fps 15] [--size WxH]\n"); return 2; }
    std::string glb = argv[1], dir = argv[2], clips, probe, reach; bool noWebp = false; float quality = 80; std::vector<int> ids; int fps = 15, TW = 200, TH = 260;
    for (int i = 3; i + 1 < argc; i += 2) {
        std::string a = argv[i];
        if (a == "--clips") clips = argv[i + 1];
        else if (a == "--probe") probe = argv[i + 1];
        else if (a == "--reach") reach = argv[i + 1];
        else if (a == "--format") noWebp = std::string(argv[i + 1]) == "png";
        else if (a == "--quality") quality = float(atof(argv[i + 1]));
        else if (a == "--fps") fps = atoi(argv[i + 1]);
        else if (a == "--size") sscanf(argv[i + 1], "%dx%d", &TW, &TH);
        else if (a == "--ids") { std::stringstream ss(argv[i + 1]); std::string t; while (std::getline(ss, t, ',')) ids.push_back(atoi(t.c_str())); }
    }
    std::ifstream f(glb, std::ios::binary); std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
    Model m; std::string err; if (!LoadGlb(bytes.data(), bytes.size(), &m, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 3; }
    if (!clips.empty()) {
        std::ifstream c(clips); std::string text((std::istreambuf_iterator<char>(c)), {});
        if (!ReloadEmoteClips(text, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 4; }
    }
    if (!reach.empty()) {      // solve the arm (upper arm fwd/twist/side, elbow) that puts the wrist (hand.X) or the hand's tip (tip.X) at a point
        char side = 0; float target[3]; bool tip = reach.rfind("tip.", 0) == 0;
        if (sscanf(reach.c_str() + (tip ? 4 : 5), "%c=%f,%f,%f", &side, &target[0], &target[1], &target[2]) != 4 || (side != 'L' && side != 'R')) { fprintf(stderr, "--reach hand.R=x,y,z or tip.R=x,y,z\n"); return 4; }
        int arm = side == 'R' ? ArmR : ArmL, fore = arm + 1; float mir = side == 'L' ? -1.f : 1.f;
        int jw = JointOf(m, side == 'R' ? "hand.R" : "hand.L"), je = JointOf(m, side == 'R' ? "forearm.R" : "forearm.L"); int idle = FindClip(m, "idle");
        EmoteLibrary lib; std::string text = "emote 1 Seed\nduration 3\nkey 0 " + probe + "\nkey 3 " + probe + "\n";
        if (!lib.Parse(text, &err, "seed")) { fprintf(stderr, "%s\n", err.c_str()); return 4; }
        EmoteClips().clips.push_back(lib.clips[0]); EmoteClips().clips.back().id = 999;
        GestureTarget g = GestureSampleFull(999, 1, 1); GestureState st; st.pose = g.pose; for (int k = 0; k < 3; ++k) st.hips[k] = g.hips[k]; st.hipsWeight = g.hipsWeight;
        float x[4] = {st.pose[arm].a[0], st.pose[arm].a[1] * mir, st.pose[arm].a[2] * mir, st.pose[fore].a[0]}, seed[4]; memcpy(seed, x, sizeof x);
        std::vector<float> mats(kMaxJoints * 16);
        auto cost = [&](const float* q) {
            GestureState s2 = st; s2.pose[arm] = {{q[0], q[1] * mir, q[2] * mir}, 1}; s2.pose[fore] = {{q[3], s2.pose[fore].a[1], s2.pose[fore].a[2]}, 1};
            PoseLayers layers; layers.gesture = &s2; JointMatricesBlendEx(m, -1, 0, idle, 0, 1, mats.data(), layers);
            auto w = JointPos(m, mats.data(), jw);
            if (tip) { auto e = JointPos(m, mats.data(), je); float d[3] = {w[0] - e[0], w[1] - e[1], w[2] - e[2]}, l = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]); for (int k = 0; k < 3; ++k) w[k] += d[k] / l * .085f; }
            float c = 0; for (int k = 0; k < 3; ++k) c += (w[k] - target[k]) * (w[k] - target[k]);
            for (int k = 0; k < 4; ++k) c += 2e-7f * (q[k] - seed[k]) * (q[k] - seed[k]);
            if (q[3] < 0 || q[3] > 145) c += 1;
            return c;
        };
        float best = cost(x);
        for (float step = 20; step > .05f; step *= .6f) for (int it = 0; it < 40; ++it) { bool moved = false;
            for (int k = 0; k < 4; ++k) for (float d : {step, -step}) { float y[4]; memcpy(y, x, sizeof y); y[k] += d; float cy = cost(y); if (cy < best) { best = cy; memcpy(x, y, sizeof y); moved = true; } }
            if (!moved) break; }
        printf("arm.%c=%.0f,%.0f,%.0f fore.%c=%.0f    # wrist error %.1f cm\n", side, x[0], x[1], x[2], side, x[3], sqrtf(std::max(0.f, best)) * 100);
        return 0;
    }
    if (!probe.empty()) {      // where a pose puts the hands and the head (model space, metres: x left, y up, z front)
        EmoteLibrary lib; std::string text = "emote 1 Probe\nduration 3\nfeet " + std::string(probe.find("thigh") != std::string::npos || probe.find("knee") != std::string::npos ? "free" : "planted") + "\nkey 0 " + probe + "\nkey 3 " + probe + "\n";
        if (!lib.Parse(text, &err, "probe")) { fprintf(stderr, "%s\n", err.c_str()); return 4; }
        EmoteClips().clips.push_back(lib.clips[0]); EmoteClips().clips.back().id = 999;
        GestureTarget g = GestureSampleFull(999, 1, 1); GestureState st; st.pose = g.pose; for (int k = 0; k < 3; ++k) st.hips[k] = g.hips[k]; st.hipsWeight = g.hipsWeight; st.plant = g.plant; st.turn = g.turn;
        std::vector<float> mats(kMaxJoints * 16); PoseLayers layers; layers.gesture = &st; JointMatricesBlendEx(m, -1, 0, FindClip(m, "idle"), 0, 1, mats.data(), layers);
        for (const char* n : {"hand.R", "hand.L", "forearm.R", "forearm.L", "head", "chest", "pelvis", "foot.L", "foot.R"}) { auto p = JointPos(m, mats.data(), JointOf(m, n)); printf("%-10s %6.3f %6.3f %6.3f\n", n, p[0], p[1], p[2]); }
        return 0;
    }
    if (ids.empty()) { for (int id = 1; id <= witness::kEmoteCount; ++id) ids.push_back(id); ids.push_back(kTalkClip); }
    const int jHR = JointOf(m, "hand.R"), jHL = JointOf(m, "hand.L"), jH = JointOf(m, "head"), jFL = JointOf(m, "foot.L"), jFR = JointOf(m, "foot.R");
    Raster r(TW, TH, 2); WebP webp; if (noWebp) webp.encode = nullptr;
    for (int id : ids) {
        const EmoteAnim* c = EmoteClips().ById(id); if (!c) { fprintf(stderr, "no clip %d\n", id); return 5; }
        bool talk = id == kTalkClip;
        float length = c->Loops() ? c->loopB + (c->loopB - c->loopA) : c->duration + .7f;
        float lead = .25f;      // a moment of idle before the emote starts
        int frames = int((length + lead) * fps);
        EmoteSim sim(m, true, 7 + id);
        const int cols = std::min(frames, 12), rowsN = (frames + cols - 1) / cols, SW = TW * kViewCount * cols, SH = TH * rowsN;
        std::vector<uint8_t> sheet(size_t(SW) * SH * 3, 235);
        std::vector<float> mats(kMaxJoints * 16);
        std::vector<std::array<float, 3>> P(m.verts.size());
        std::string json = "{\"id\":" + std::to_string(id) + ",\"name\":\"" + c->name + "\",\"fps\":" + std::to_string(fps) + ",\"frames\":" + std::to_string(frames) +
                           ",\"lead\":" + std::to_string(lead) + ",\"duration\":" + std::to_string(c->duration) + ",\"loop\":" + (c->Loops() ? "[" + std::to_string(c->loopA) + "," + std::to_string(c->loopB) + "]" : "null") +
                           ",\"mobile\":" + std::string(id <= witness::kEmoteCount && witness::kEmotes[id].mobile ? "true" : "false") + ",\"tile\":[" + std::to_string(TW) + "," + std::to_string(TH) + "],\"views\":[\"front\",\"side\",\"rear\"],\"track\":[";
        const int sub = 4; float dt = 1.f / (fps * sub), clock = 0;
        for (int fr = 0; fr < frames; ++fr) {
            for (int s = 0; s < sub; ++s) {
                clock += dt; float t = clock - lead;
                if (talk) sim.Frame(0, 0, clock, dt, mats.data(), t >= 0);
                else sim.Frame(t >= 0 ? id : 0, std::max(0.f, t), clock, dt, mats.data());
            }
            for (size_t v = 0; v < m.verts.size(); ++v) SkinPosition(m, mats.data(), m.verts[v], P[v].data());
            auto hr = JointPos(m, mats.data(), jHR), hl = JointPos(m, mats.data(), jHL), hd = JointPos(m, mats.data(), jH);
            auto fl = JointPos(m, mats.data(), jFL), frt = JointPos(m, mats.data(), jFR);
            float feet[2][3] = {{fl[0], fl[1], fl[2]}, {frt[0], frt[1], frt[2]}};
            json += fr ? ",[" : "[";
            for (int v = 0; v < kViewCount; ++v) {
                r.Clear(); r.Shadow(feet, kViews[v]); r.Mesh(m, P, kViews[v]); r.Down(sheet, SW, (fr % cols * kViewCount + v) * TW, fr / cols * TH);
                float a[3], b[3], h[3]; r.Project(hr.data(), kViews[v], a); r.Project(hl.data(), kViews[v], b); r.Project(hd.data(), kViews[v], h);
                char buf[160]; snprintf(buf, sizeof buf, "%s[%.1f,%.1f,%.1f,%.1f,%.1f,%.1f]", v ? "," : "", a[0] / 2, a[1] / 2, b[0] / 2, b[1] / 2, h[0] / 2, h[1] / 2); json += buf;
            }
            json += "]";
        }
        json += "]}\n";
        char name[64]; snprintf(name, sizeof name, "emote_%03d", id);
        std::string file = std::string(name) + (webp.encode ? ".webp" : ".png");
        if (!(webp.encode ? webp.Write(dir + "/" + file, SW, SH, sheet, quality) : WritePng(dir + "/" + file, SW, SH, sheet))) return 6;
        json.insert(json.size() - 2, ",\"sheet\":\"" + file + "\",\"cols\":" + std::to_string(cols));
        FILE* jf = fopen((dir + "/" + name + ".json").c_str(), "w"); if (!jf) return 7; fputs(json.c_str(), jf); fclose(jf);
        // contact sheet: 8 frames across the clip, front and side
        const int N = 8; std::vector<uint8_t> cs(size_t(TW) * N * TH * 2 * 3);
        for (int k = 0; k < N; ++k) {
            int fr = std::min(frames - 1, int((lead + (length - .2f) * k / (N - 1)) * fps));
            for (int v = 0; v < 2; ++v) for (int y = 0; y < TH; ++y)
                memcpy(&cs[((size_t(v * TH + y)) * TW * N + k * TW) * 3], &sheet[((size_t(fr / cols * TH + y)) * SW + (fr % cols * kViewCount + v) * TW) * 3], size_t(TW) * 3);
        }
        WritePng(dir + "/" + name + "_contact.png", TW * N, TH * 2, cs);
        printf("%s %s: %d frames\n", name, c->name.c_str(), frames); fflush(stdout);
    }
}
