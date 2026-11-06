#pragma once
// The launcher's moving puzzle line, as a Witness panel draws it: a random solution traced along a grid from the start circle to the end cap, a flash when it is
// solved, a fade, and the next one; and the line that runs around a button from its start dot. Pure (no Windows calls): tested by trace_test.cpp with mutants.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace trace {
struct Pt { float x, y; };
struct Node { int i, j; };      // a grid node: column i (0..cols), row j (0..rows, 0 at the top)

static inline uint32_t Next(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
static inline float Uniform(uint32_t& s) { return (Next(s) >> 8) * (1.0f / 16777216.0f) + 1e-7f; }

// A random solution on a grid of cols x rows cells from (0, rows) (bottom left) to (cols, 0) (top right): unit steps along the grid lines, no node twice, at most
// maxLen steps. It winds (moves to the right are likelier than up or down, back to the left rarer), and every seed gives its own line. A depth-first search with
// the moves in a weighted random order; if it runs out of tries the plain staircase is the answer.
inline std::vector<Node> SolvePath(int cols, int rows, uint32_t seed, int maxLen) {
    std::vector<Node> path; if (cols < 1 || rows < 1) return path;
    const int need = cols + rows; if (maxLen < need) maxLen = need;
    uint32_t s = seed * 2654435761u + 0x9E3779B9u; if (!s) s = 1;
#ifdef TR_MUT_NO_SEED
    s = 12345;
#endif
    for (int k = 0; k < 4; ++k) Next(s);
    std::vector<char> seen((size_t)(cols + 1) * (rows + 1), 0); auto at = [&](int i, int j) -> char& { return seen[(size_t)j * (cols + 1) + i]; };
#ifdef TR_MUT_REVISIT
    char never = 0; auto taken = [&](int, int) -> char& { never = 0; return never; };      // (mutant: a node already on the line can be stepped on again)
#else
    auto taken = at;
#endif
    struct Frame { Node n; Node moves[4]; int count, next; };
    std::vector<Frame> st; long budget = 40000;
    auto expand = [&](Node n) {
        Frame f{n, {}, 0, 0}; const int di[] = {1, 0, 0, -1}, dj[] = {0, -1, 1, 0}; const float w[] = {3.0f, 1.6f, 1.6f, 0.35f}; float key[4];
#ifdef TR_MUT_DIAGONAL
        const int ddi[] = {1, 1, 0, -1}, ddj[] = {-1, 0, -1, 1};      // (mutant: a diagonal move)
        for (int d = 0; d < 4; ++d) { int i = n.i + ddi[d], j = n.j + ddj[d]; if (i < 0 || j < 0 || i > cols || j > rows || taken(i, j)) continue; key[f.count] = powf(Uniform(s), 1.0f / w[d]); f.moves[f.count++] = {i, j}; }
        (void)di; (void)dj;
#else
        for (int d = 0; d < 4; ++d) { int i = n.i + di[d], j = n.j + dj[d]; if (i < 0 || j < 0 || i > cols || j > rows || taken(i, j)) continue; key[f.count] = powf(Uniform(s), 1.0f / w[d]); f.moves[f.count++] = {i, j}; }
#endif
        for (int a = 1; a < f.count; ++a) for (int b = a; b > 0 && key[b] > key[b - 1]; --b) { std::swap(key[b], key[b - 1]); std::swap(f.moves[b], f.moves[b - 1]); }      // the weighted random order
        return f;
    };
    Node start{0, rows}, end{cols, 0}; at(start.i, start.j) = 1; st.push_back(expand(start));
    while (!st.empty() && budget-- > 0) {
        Frame& f = st.back();
        if (f.n.i == end.i && f.n.j == end.j) break;
        if (f.next >= f.count) {
at(f.n.i, f.n.j) = 0; st.pop_back(); continue; }
        Node m = f.moves[f.next++]; int steps = (int)st.size();      // the steps so far, with this one
        if (steps + std::abs(m.i - end.i) + std::abs(m.j - end.j) > maxLen || taken(m.i, m.j)) continue;      // it could no longer reach the end in time
        at(m.i, m.j) = 1; st.push_back(expand(m));
    }
    if (!st.empty() && st.back().n.i == end.i && st.back().n.j == end.j) { for (auto& f : st) path.push_back(f.n); return path; }
    for (int j = rows; j >= 0; --j) path.push_back({0, j});      // (the fallback: up the left side, along the top)
    for (int i = 1; i <= cols; ++i) path.push_back({i, 0});
    return path;
}

inline float Length(const std::vector<Pt>& p) { float l = 0; for (size_t i = 1; i < p.size(); ++i) l += hypotf(p[i].x - p[i - 1].x, p[i].y - p[i - 1].y); return l; }
// The line drawn so far: the first `frac` (0..1) of the polyline's length, ending at its head between two corners.
inline std::vector<Pt> Prefix(const std::vector<Pt>& p, float frac) {
    std::vector<Pt> out; if (p.empty()) return out; frac = std::max(0.f, std::min(1.f, frac)); float want = Length(p) * frac; out.push_back(p[0]);
    for (size_t i = 1; i < p.size(); ++i) {
        float seg = hypotf(p[i].x - p[i - 1].x, p[i].y - p[i - 1].y);
        if (want >= seg) { out.push_back(p[i]); want -= seg; continue; }
#ifdef TR_MUT_PREFIX_NODES
        break;      // (mutant: the head jumps from corner to corner)
#else
        if (want > 0 && seg > 0) { float t = want / seg; out.push_back({p[i - 1].x + (p[i].x - p[i - 1].x) * t, p[i - 1].y + (p[i].y - p[i - 1].y) * t}); }
        break;
#endif
    }
    return out;
}
// A rounded rectangle's outline as a closed polyline from the middle of its left side, going up first (clockwise on screen), back to where it began.
inline std::vector<Pt> Ring(float l, float t, float r, float b, float rad, int arcSteps = 4) {
    std::vector<Pt> p; rad = std::max(0.f, std::min(rad, std::min(r - l, b - t) / 2)); float my = (t + b) / 2; const float kPi = 3.14159265f;
    auto arc = [&](float cx, float cy, float a0) { for (int k = 0; k <= arcSteps; ++k) { float a = a0 + (kPi / 2) * k / arcSteps; p.push_back({cx + rad * cosf(a), cy + rad * sinf(a)}); } };
    p.push_back({l, my});
    arc(l + rad, t + rad, kPi);            // top left: from the left side round to the top
    arc(r - rad, t + rad, 1.5f * kPi);     // top right
    arc(r - rad, b - rad, 0);              // bottom right
    arc(l + rad, b - rad, 0.5f * kPi);     // bottom left
    p.push_back({l, my});
#ifdef TR_MUT_RING_CCW
    std::reverse(p.begin(), p.end());
#endif
    return p;
}
inline float EaseOut(float t) { t = std::max(0.f, std::min(1.f, t)); float u = 1 - t; return 1 - u * u * u; }

// One puzzle's life on the banner: the line is traced (traceMs), solved (a flash in its first kFlashMs, then it holds), fades out, a rest with the grid empty, then a new puzzle.
enum Phase { kTrace, kSolved, kFade, kRest };
constexpr double kSolvedMs = 1700, kFlashMs = 550, kFadeMs = 800, kRestMs = 900;
struct State { Phase phase; float drawn, flash, alpha; };      // drawn: 0..1 of the line; flash: 0..1 (the solve's light); alpha: the whole line's opacity
inline double CycleMs(double traceMs) { return traceMs + kSolvedMs + kFadeMs + kRestMs; }
inline State At(double ms, double traceMs) {
    if (ms < traceMs) return {kTrace, float(ms / traceMs), 0, 1};
    ms -= traceMs; if (ms < kSolvedMs) { float f = ms < kFlashMs ? float(1 - ms / kFlashMs) : 0; return {kSolved, 1, f * f, 1}; }
    ms -= kSolvedMs;
#ifdef TR_MUT_NO_FADE
    if (ms < kFadeMs) return {kFade, 1, 0, 1};
#else
    if (ms < kFadeMs) { float a = float(1 - ms / kFadeMs); return {kFade, 1, 0, a * a}; }
#endif
    return {kRest, 0, 0, 0};
}
}  // namespace trace
