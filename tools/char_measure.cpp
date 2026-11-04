// Measures a built character (.glb) the way a viewer sees it, for tools/check_character.py, which holds the numbers to
// models/src/character_spec.py. Prints one JSON object. Game-free; uses the proxy's own pose and life code.
//   char_measure <glb>
// What it measures (glTF space: metres, y up, the character faces +z, its left is +x):
//   - proportions: standing height, head height (chin to crown of the skin), eye line, nose length, hand length
//   - the eyes from the front, rendered at 0.1 mm a pixel with labels: the visible white and iris of each eye give the lid opening's
//     width and height, the iris's share of it and the gap between the eyes; again with a full blink from the life layer (it must close)
//   - the head's mirror symmetry, its triangle count, degenerate triangles
#include "../proxy/model/emote_sim.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>
using namespace wm;

static int Dominant(const Vertex& v) { int b = 0; for (int k = 1; k < 4; ++k) if (v.weights[k] > v.weights[b]) b = k; return v.joints[b]; }

// Labels: 0 none, 1 skin/other, 2 eye white, 3 iris or pupil (anything on an eye bone), seen from the front through x in [x0,x1], y in [y0,y1].
struct Label { int W, H; float x0, y0, px; std::vector<uint8_t> id; std::vector<float> z, lum; };
static Label FrontLabels(const Model& m, const std::vector<std::array<float, 3>>& P, float x0, float x1, float y0, float y1, float px, int eyeL, int eyeR, const std::vector<std::array<float, 3>>& centres) {
    Label L; L.W = int((x1 - x0) / px); L.H = int((y1 - y0) / px); L.x0 = x0; L.y0 = y0; L.px = px; L.id.assign(size_t(L.W) * L.H, 0); L.z.assign(size_t(L.W) * L.H, -1e9f); L.lum.assign(size_t(L.W) * L.H, 0);
    for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
        uint32_t i[3] = {m.idx[t], m.idx[t + 1], m.idx[t + 2]}; float sx[3], sy[3], sz[3];
        for (int k = 0; k < 3; ++k) { sx[k] = (P[i[k]][0] - x0) / px; sy[k] = (y1 - P[i[k]][1]) / px; sz[k] = P[i[k]][2]; }
        float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sy[1] - sy[0]) * (sx[2] - sx[0]); if (fabsf(area) < 1e-6f) continue;
        int bx0 = std::max(0, int(std::min({sx[0], sx[1], sx[2]}))), bx1 = std::min(L.W - 1, int(std::max({sx[0], sx[1], sx[2]})) + 1);
        int by0 = std::max(0, int(std::min({sy[0], sy[1], sy[2]}))), by1 = std::min(L.H - 1, int(std::max({sy[0], sy[1], sy[2]})) + 1);
        if (bx0 > bx1 || by0 > by1) continue;
        const Vertex& v = m.verts[i[0]]; int dom = Dominant(v);
        float lum = 0, sat = 0; {
            float c[3] = {v.col[0] / 255.f, v.col[1] / 255.f, v.col[2] / 255.f};
            if (v.col[3] && m.texW > 0) { float u = (v.uv[0] + m.verts[i[1]].uv[0] + m.verts[i[2]].uv[0]) / 3, w = (v.uv[1] + m.verts[i[1]].uv[1] + m.verts[i[2]].uv[1]) / 3;
                int tx = std::max(0, std::min(m.texW - 1, int(u * m.texW))), ty = std::max(0, std::min(m.texH - 1, int(w * m.texH))); for (int k = 0; k < 3; ++k) c[k] = m.rgba[(size_t(ty) * m.texW + tx) * 4 + k] / 255.f; }
            lum = (c[0] + c[1] + c[2]) / 3; sat = std::max({c[0], c[1], c[2]}) - std::min({c[0], c[1], c[2]});
        }
        bool nearEye = false; for (const auto& c : centres) { float d = 0; for (int k = 0; k < 3; ++k) d += (P[i[0]][k] - c[k]) * (P[i[0]][k] - c[k]); nearEye |= d < .02f * .02f; }
        if (getenv("MEASURE_DEBUG") && nearEye && dom != eyeL && dom != eyeR) fprintf(stderr, "near-eye tri: group %.2f lum %.2f sat %.2f textured %d\n", v.appearanceGroup, lum, sat, int(v.col[3]));
        uint8_t id = (dom == eyeL || dom == eyeR) ? 3 : (nearEye && int(v.appearanceGroup + .5f) != 1 && lum > .45f && sat < .25f ? 2 : 1);
        for (int y = by0; y <= by1; ++y) for (int x = bx0; x <= bx1; ++x) {
            float qx = x + .5f, qy = y + .5f;
            float a = ((sx[1] - qx) * (sy[2] - qy) - (sy[1] - qy) * (sx[2] - qx)) / area, b = ((sx[2] - qx) * (sy[0] - qy) - (sy[2] - qy) * (sx[0] - qx)) / area, c = 1 - a - b;
            if (a < 0 || b < 0 || c < 0) continue;
            float z = a * sz[0] + b * sz[1] + c * sz[2]; size_t pi = size_t(y) * L.W + x; if (z <= L.z[pi]) continue; L.z[pi] = z; L.id[pi] = id; L.lum[pi] = lum;
        }
    }
    return L;
}
struct EyeBox { float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f, ix0 = 1e9f, ix1 = -1e9f; int px = 0, iris = 0, white = 0; double whiteLum = 0; };
static EyeBox Measure(const Label& L, float xc) {      // the visible eye (white or iris) within 3 cm of xc
    EyeBox e;
    for (int y = 0; y < L.H; ++y) for (int x = 0; x < L.W; ++x) {
        uint8_t id = L.id[size_t(y) * L.W + x]; if (id < 2) continue;
        float wx = L.x0 + (x + .5f) * L.px, wy = L.y0 + (L.H - y - .5f) * L.px; if (fabsf(wx - xc) > .03f) continue;
        e.px++; e.x0 = std::min(e.x0, wx); e.x1 = std::max(e.x1, wx); e.y0 = std::min(e.y0, wy); e.y1 = std::max(e.y1, wy);
        if (id == 2) { e.white++; e.whiteLum += L.lum[size_t(y) * L.W + x]; }
        if (id == 3) { e.iris++; e.ix0 = std::min(e.ix0, wx); e.ix1 = std::max(e.ix1, wx); }
    }
    return e;
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::ifstream f(argv[1], std::ios::binary); std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
    Model m; std::string err; if (!LoadGlb(bytes.data(), bytes.size(), &m, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 3; }
    float lidRotate = 0, lidClose = 1;
    { std::ifstream t(std::string(argv[1]) + ".txt"); std::string line; while (std::getline(t, line)) { sscanf(line.c_str(), "lid_rotate=%f", &lidRotate); sscanf(line.c_str(), "lid_close=%f", &lidClose); } }
    std::vector<float> bind(kMaxJoints * 16); JointMatrices(m, -1, 0, bind.data());
    std::vector<std::array<float, 3>> P(m.verts.size()); for (size_t i = 0; i < P.size(); ++i) SkinPosition(m, bind.data(), m.verts[i], P[i].data());
    auto jid = [&](const char* n) { return JointOf(m, n); };
    int jHead = jid("head"), jLip = jid("lip_lower"), jEyeL = jid("eye.L"), jEyeR = jid("eye.R"), jHandL = jid("hand.L"), jHandR = jid("hand.R");
    auto jp = [&](int j) { return JointPos(m, bind.data(), j); };
    float height = m.bmax[1] - m.bmin[1];
    // the skin of the head: appearance group 1 (skin) on the head or lip bone
    float chin = 1e9f, crown = -1e9f, noseTipY = 0, noseTipZ = -1e9f; std::vector<size_t> headSkin;
    for (size_t i = 0; i < m.verts.size(); ++i) {
        const Vertex& v = m.verts[i]; int d = Dominant(v); if (int(v.appearanceGroup + .5f) != 1) continue;
        if (d != jHead && d != jLip) continue;      // (vertex joints are joint indices)
        headSkin.push_back(i); crown = std::max(crown, P[i][1]);
        if (P[i][2] > .04f) chin = std::min(chin, P[i][1]);
    }
    std::array<float, 3> eL = jEyeL >= 0 ? jp(jEyeL) : std::array<float, 3>{.036f, 1.677f, .076f}, eR = jEyeR >= 0 ? jp(jEyeR) : std::array<float, 3>{-.036f, 1.677f, .076f};
    float eyeY = (eL[1] + eR[1]) * .5f;
    for (size_t i : headSkin) if (fabsf(P[i][0]) < .006f && P[i][1] < eyeY && P[i][1] > eyeY - .08f && P[i][2] > noseTipZ) { noseTipZ = P[i][2]; noseTipY = P[i][1]; }
    // symmetry: each head-skin vertex mirrored, against the head's skin surface (exact closest point on its triangles, a 4 mm grid)
    auto closest = [](const float* p, const float* a, const float* b, const float* c) {      // squared distance, point to triangle (Ericson)
        float ab[3], ac[3], ap[3]; for (int k = 0; k < 3; ++k) { ab[k] = b[k] - a[k]; ac[k] = c[k] - a[k]; ap[k] = p[k] - a[k]; }
        auto dot = [](const float* u, const float* v) { return u[0] * v[0] + u[1] * v[1] + u[2] * v[2]; };
        auto d2 = [&](const float* q) { float d = 0; for (int k = 0; k < 3; ++k) d += (p[k] - q[k]) * (p[k] - q[k]); return d; };
        float d1 = dot(ab, ap), d2v = dot(ac, ap); if (d1 <= 0 && d2v <= 0) return d2(a);
        float bp[3]; for (int k = 0; k < 3; ++k) bp[k] = p[k] - b[k]; float d3 = dot(ab, bp), d4 = dot(ac, bp); if (d3 >= 0 && d4 <= d3) return d2(b);
        float vc = d1 * d4 - d3 * d2v; float q[3];
        if (vc <= 0 && d1 >= 0 && d3 <= 0) { float v = d1 / (d1 - d3); for (int k = 0; k < 3; ++k) q[k] = a[k] + v * ab[k]; return d2(q); }
        float cp[3]; for (int k = 0; k < 3; ++k) cp[k] = p[k] - c[k]; float d5 = dot(ab, cp), d6 = dot(ac, cp); if (d6 >= 0 && d5 <= d6) return d2(c);
        float vb = d5 * d2v - d1 * d6;
        if (vb <= 0 && d2v >= 0 && d6 <= 0) { float w = d2v / (d2v - d6); for (int k = 0; k < 3; ++k) q[k] = a[k] + w * ac[k]; return d2(q); }
        float va = d3 * d6 - d5 * d4;
        if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) { float w = (d4 - d3) / ((d4 - d3) + (d5 - d6)); for (int k = 0; k < 3; ++k) q[k] = b[k] + w * (c[k] - b[k]); return d2(q); }
        float den = 1 / (va + vb + vc), v = vb * den, w = vc * den; for (int k = 0; k < 3; ++k) q[k] = a[k] + ab[k] * v + ac[k] * w; return d2(q);
    };
    std::vector<char> isHead(m.verts.size(), 0); for (size_t i : headSkin) isHead[i] = 1;
    std::map<long long, std::vector<size_t>> tgrid; auto key = [](float x, float y, float z) { return ((long long)floorf(x / .004f) * 1000003LL + (long long)floorf(y / .004f)) * 1000003LL + (long long)floorf(z / .004f); };
    for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
        if (!isHead[m.idx[t]] || !isHead[m.idx[t + 1]] || !isHead[m.idx[t + 2]]) continue;
        float c[3] = {0, 0, 0}; for (int j = 0; j < 3; ++j) for (int k = 0; k < 3; ++k) c[k] += P[m.idx[t + j]][k] / 3; tgrid[key(c[0], c[1], c[2])].push_back(t);
    }
    std::vector<float> mis;
    for (size_t k = 0; k < headSkin.size(); k += 5) {
        const auto& p = P[headSkin[k]]; float q[3] = {-p[0], p[1], p[2]}, best = 1e9f;
        for (int dx = -2; dx <= 2; ++dx) for (int dy = -2; dy <= 2; ++dy) for (int dz = -2; dz <= 2; ++dz) {
            auto it = tgrid.find(key(q[0] + dx * .004f, q[1] + dy * .004f, q[2] + dz * .004f)); if (it == tgrid.end()) continue;
            for (size_t t : it->second) best = std::min(best, closest(q, P[m.idx[t]].data(), P[m.idx[t + 1]].data(), P[m.idx[t + 2]].data()));
        }
        if (best < 1e8f) mis.push_back(sqrtf(best));
    }
    std::sort(mis.begin(), mis.end()); float sym99 = mis.empty() ? 1 : mis[size_t(mis.size() * .99)];
    // head triangles and degenerate ones
    int headTris = 0, degenerate = 0;
    for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
        const auto &a = P[m.idx[t]], &b = P[m.idx[t + 1]], &c = P[m.idx[t + 2]];
        float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        float cr[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        if (cr[0] * cr[0] + cr[1] * cr[1] + cr[2] * cr[2] < 1e-18f) degenerate++;
        const Vertex& v = m.verts[m.idx[t]]; if (int(v.appearanceGroup + .5f) == 1 && jHead >= 0 && Dominant(v) == jHead) headTris++;
    }
    // hands: wrist to the farthest point of the hand
    auto handLen = [&](int j) { if (j < 0) return 0.f; auto w = jp(j); float best = 0; for (size_t i = 0; i < P.size(); ++i) if (Dominant(m.verts[i]) == j) { float d = 0; for (int c = 0; c < 3; ++c) d += (P[i][c] - w[c]) * (P[i][c] - w[c]); best = std::max(best, d); } return sqrtf(best); };
    int jBrowL = jid("brow.L"); float browY = jBrowL >= 0 ? jp(jBrowL)[1] : eyeY + .03f;
    // the eyes, seen from the front; then blinked by the life layer
    int eyeNodeL = jEyeL >= 0 ? jEyeL : -2, eyeNodeR = jEyeR >= 0 ? jEyeR : -2;
    std::vector<std::array<float, 3>> centres = {eL, eR};
    Label open = FrontLabels(m, P, -.08f, .08f, eyeY - .03f, eyeY + .03f, .0001f, eyeNodeL, eyeNodeR, centres);
    EyeBox oL = Measure(open, eL[0]), oR = Measure(open, eR[0]);
    if (const char* dump = getenv("MEASURE_DUMP")) {      // the label image, as a picture: skin grey, white white, iris dark
        FILE* fp = fopen(dump, "wb"); fprintf(fp, "P5\n%d %d\n255\n", open.W, open.H);
        for (uint8_t id : open.id) fputc(id == 0 ? 40 : id == 1 ? 120 : id == 2 ? 255 : 0, fp); fclose(fp);
    }
    LifeRig rig; rig.Bind(m, lidClose, lidRotate); LifeState life; life.blink = 1;
    std::vector<float> shut(kMaxJoints * 16); PoseLayers layers; layers.life = &life; layers.rig = &rig; JointMatricesBlendEx(m, -1, 0, -1, 0, 1, shut.data(), layers);
    std::vector<std::array<float, 3>> Q(m.verts.size()); for (size_t i = 0; i < Q.size(); ++i) SkinPosition(m, shut.data(), m.verts[i], Q[i].data());
    Label closed = FrontLabels(m, Q, -.08f, .08f, eyeY - .03f, eyeY + .03f, .0001f, eyeNodeL, eyeNodeR, centres);
    EyeBox cL = Measure(closed, eL[0]), cR = Measure(closed, eR[0]);
    auto box = [](const EyeBox& e) { char b[200]; snprintf(b, sizeof b, "{\"w\":%.5f,\"h\":%.5f,\"x0\":%.5f,\"x1\":%.5f,\"iris_w\":%.5f,\"area_mm2\":%.2f,\"white_lum\":%.3f}", e.px ? e.x1 - e.x0 : 0, e.px ? e.y1 - e.y0 : 0, e.x0, e.x1, e.iris ? e.ix1 - e.ix0 : 0, e.px * 0.01f, e.white ? e.whiteLum / e.white : 0.0); return std::string(b); };
    printf("{\"height\":%.4f,\"head_height\":%.4f,\"chin\":%.4f,\"crown\":%.4f,\"eye_y\":%.4f,\"brow_y\":%.4f,\"nose_tip_y\":%.4f,\"hand_len\":[%.4f,%.4f],"
           "\"sym99_mm\":%.3f,\"head_tris\":%d,\"degenerate\":%d,\"lid_rotate\":%.3f,\"open\":[%s,%s],\"closed\":[%s,%s]}\n",
           height, crown - chin, chin, crown, eyeY, browY, noseTipY, handLen(jHandL), handLen(jHandR), sym99 * 1000, headTris, degenerate, lidRotate,
           box(oL).c_str(), box(oR).c_str(), box(cL).c_str(), box(cR).c_str());
}
