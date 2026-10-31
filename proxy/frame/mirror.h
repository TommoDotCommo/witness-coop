#pragma once
// The second line of a symmetry panel, game-free (T0 test: mirror_test.cpp).
// The game keeps ONE traced path per panel (count at +0x228, records at +0x230). The reflected line is not stored: the panel keeps a table
// that names, for every node, its mirror node (+0x438, one int per node) and the game draws the reflection of the one path. A shared line
// therefore carries one path, and the second line is made here from it: the panel's nodes sit on a grid (u, v in 0..1, +0x3c0); where a node
// is in the panel's frame is an affine map  p = o + u*a + v*b  (measured on a real panel: it fits to 0.4 mm and the surface can be tilted a few
// degrees from the panel's rotation, so the axes are fitted from the nodes of the line itself, not guessed). A point of the line is turned into (u, v),
// reflected the way the table reflects the nodes (u -> 2cu-u, v -> 2cv-v or both) and put back. All in the panel's local frame, so it follows the panel.
#include <cmath>
#include <vector>

namespace wmirror {
struct Sample { float u, v; float p[3]; };               // a node of the line: its grid place and where it is in the panel's frame
struct Fit { bool ok = false; bool exact = false; float o[3] = {0, 0, 0}, a[3] = {0, 0, 0}, b[3] = {0, 0, 0}; };
struct Flip { bool ok = false; bool u = false, v = false; float cu = 0.5f, cv = 0.5f; };

inline void At(const Fit& f, float u, float v, float out[3]) { for (int k = 0; k < 3; ++k) out[k] = f.o[k] + u * f.a[k] + v * f.b[k]; }

// Which reflection does the table describe? nodes: n, uv: 2n floats, mirror: n ints. Fails when no flip of the grid maps every node to its mirror node.
inline Flip DetectFlip(int n, const float* uv, const int* mirror) {
    Flip best; float bestErr = 1e9f;
    for (int kind = 1; kind <= 3; ++kind) {      // 1: u flips, 2: v flips, 3: both
        bool fu = kind & 1, fv = kind & 2; double su = 0, sv = 0; int c = 0;
        for (int k = 0; k < n; ++k) { int m = mirror[k]; if (m < 0 || m >= n || m == k) continue; su += (uv[2 * k] + uv[2 * m]) * .5; sv += (uv[2 * k + 1] + uv[2 * m + 1]) * .5; ++c; }
        if (!c) return best;
        float cu = float(su / c), cv = float(sv / c), err = 0;
        for (int k = 0; k < n; ++k) {
            int m = mirror[k]; if (m < 0 || m >= n) continue;
            float eu = (fu ? 2 * cu - uv[2 * k] : uv[2 * k]) - uv[2 * m], ev = (fv ? 2 * cv - uv[2 * k + 1] : uv[2 * k + 1]) - uv[2 * m + 1];
            err = std::fmax(err, std::fmax(std::fabs(eu), std::fabs(ev)));
        }
        if (err < bestErr) { bestErr = err; best.u = fu; best.v = fv; best.cu = cu; best.cv = cv; }
    }
    best.ok = bestErr < 0.02f; return best;
}

// Least squares  p = o + u*a + v*b  over the samples. Full rank (the nodes are not on one line of the grid) gives an exact fit; with fewer, the prior (a fit
// remembered from an earlier line on this panel) or a flat one (a = local y, b = local z, as long per grid unit as the samples show) keeps the shape and moves to the samples.
inline Fit FitSamples(const std::vector<Sample>& s, const Fit* prior) {
    Fit f; if (s.empty()) return f;
    double n[3][3] = {}; double r[3][3] = {};
    for (const auto& q : s) { double row[3] = {1, q.u, q.v}; for (int i = 0; i < 3; ++i) { for (int j = 0; j < 3; ++j) n[i][j] += row[i] * row[j]; for (int d = 0; d < 3; ++d) r[i][d] += row[i] * q.p[d]; } }
    double det = n[0][0] * (n[1][1] * n[2][2] - n[1][2] * n[2][1]) - n[0][1] * (n[1][0] * n[2][2] - n[1][2] * n[2][0]) + n[0][2] * (n[1][0] * n[2][1] - n[1][1] * n[2][0]);
    double scale = n[0][0] * n[1][1] * n[2][2];
#ifdef WM_MUT_FLAT
    det = 0;      // (mutant: never fit the surface, always assume it is flat)
#endif
    if (s.size() >= 3 && std::fabs(det) > 1e-6 * (scale > 0 ? scale : 1)) {
        double m[3][6]; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) { m[i][j] = n[i][j]; }
        for (int d = 0; d < 3; ++d) {
            for (int i = 0; i < 3; ++i) { for (int j = 0; j < 3; ++j) m[i][j] = n[i][j]; m[i][3] = r[i][d]; }
            for (int i = 0; i < 3; ++i) {
                int p = i; for (int k = i + 1; k < 3; ++k) if (std::fabs(m[k][i]) > std::fabs(m[p][i])) p = k;
                for (int c = 0; c < 4; ++c) { double t = m[i][c]; m[i][c] = m[p][c]; m[p][c] = t; }
                for (int k = i + 1; k < 3; ++k) { double fct = m[k][i] / m[i][i]; for (int c = i; c < 4; ++c) m[k][c] -= fct * m[i][c]; }
            }
            double x[3]; for (int i = 2; i >= 0; --i) { double v = m[i][3]; for (int c = i + 1; c < 3; ++c) v -= m[i][c] * x[c]; x[i] = v / m[i][i]; }
            f.o[d] = float(x[0]); f.a[d] = float(x[1]); f.b[d] = float(x[2]);
        }
        f.ok = true; f.exact = true; return f;
    }
    if (prior && prior->ok) { f = *prior; f.exact = false; }
    else {
        float sc = 0.6f; int cnt = 0; float acc = 0;
        for (size_t i = 1; i < s.size(); ++i) { float du = s[i].u - s[0].u, dv = s[i].v - s[0].v, g = std::sqrt(du * du + dv * dv); if (g < 1e-4f) continue; float dx = s[i].p[0] - s[0].p[0], dy = s[i].p[1] - s[0].p[1], dz = s[i].p[2] - s[0].p[2]; acc += std::sqrt(dx * dx + dy * dy + dz * dz) / g; ++cnt; }
        if (cnt) sc = acc / cnt;
        f.a[1] = sc; f.b[2] = sc; f.ok = true;
    }
    for (int d = 0; d < 3; ++d) { double acc = 0; for (const auto& q : s) acc += q.p[d] - q.u * f.a[d] - q.v * f.b[d]; f.o[d] = float(acc / s.size()); }
    return f;
}

// The grid place of a point of the panel's surface (least squares in the plane of a and b). False when a and b are parallel.
inline bool Uv(const Fit& f, const float p[3], float* u, float* v) {
    float d[3] = {p[0] - f.o[0], p[1] - f.o[1], p[2] - f.o[2]};
    double aa = 0, ab = 0, bb = 0, ad = 0, bd = 0; for (int k = 0; k < 3; ++k) { aa += f.a[k] * f.a[k]; ab += f.a[k] * f.b[k]; bb += f.b[k] * f.b[k]; ad += f.a[k] * d[k]; bd += f.b[k] * d[k]; }
    double det = aa * bb - ab * ab; if (!(std::fabs(det) > 1e-12)) return false;
    *u = float((ad * bb - bd * ab) / det); *v = float((bd * aa - ad * ab) / det); return true;
}

// The reflection of a point of the line. The mutants are the three ways to get it wrong.
inline bool Reflect(const Fit& f, const Flip& fl, const float p[3], float out[3]) {
    float u, v; if (!f.ok || !fl.ok || !Uv(f, p, &u, &v)) return false;
#ifdef WM_MUT_NO_FLIP
    At(f, u, v, out); return true;
#endif
#ifdef WM_MUT_WRONG_AXIS
    if (fl.u != fl.v) { At(f, fl.v ? 2 * fl.cu - u : u, fl.u ? 2 * fl.cv - v : v, out); return true; }
#endif
    At(f, fl.u ? 2 * fl.cu - u : u, fl.v ? 2 * fl.cv - v : v, out); return true;
}
}  // namespace wmirror
