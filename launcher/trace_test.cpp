// Game-free test of trace.h (the launcher's moving puzzle line): the solution paths, the drawn part, a button's ring and the timing. Mutants: make trace-test.
#include "trace.h"
#include <cstdio>
#include <set>
#include <string>

static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { ++g_fail; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static void Paths() {
    const int grids[][2] = {{1, 1}, {2, 2}, {4, 3}, {26, 3}, {30, 2}, {12, 4}};
    for (auto& g : grids) {
        int cols = g[0], rows = g[1], maxLen = cols * 2 + rows * 4; std::set<std::string> distinct; int longer = 0;
        for (uint32_t seed = 1; seed <= 300; ++seed) {
            auto p = trace::SolvePath(cols, rows, seed, maxLen);
            CHECK(!p.empty(), "%dx%d seed %u: no path", cols, rows, seed); if (p.empty()) continue;
            CHECK(p.front().i == 0 && p.front().j == rows, "%dx%d seed %u: starts at %d,%d", cols, rows, seed, p.front().i, p.front().j);
            CHECK(p.back().i == cols && p.back().j == 0, "%dx%d seed %u: ends at %d,%d", cols, rows, seed, p.back().i, p.back().j);
            CHECK((int)p.size() - 1 <= maxLen, "%dx%d seed %u: %d steps > %d", cols, rows, seed, (int)p.size() - 1, maxLen);
            std::set<int> seen; std::string key;
            for (size_t k = 0; k < p.size(); ++k) {
                CHECK(p[k].i >= 0 && p[k].i <= cols && p[k].j >= 0 && p[k].j <= rows, "%dx%d seed %u: node %d,%d off the grid", cols, rows, seed, p[k].i, p[k].j);
                CHECK(seen.insert(p[k].j * 1000 + p[k].i).second, "%dx%d seed %u: node %d,%d twice", cols, rows, seed, p[k].i, p[k].j);
                if (k) { int d = std::abs(p[k].i - p[k - 1].i) + std::abs(p[k].j - p[k - 1].j); CHECK(d == 1, "%dx%d seed %u: step %zu is not one grid line (%d)", cols, rows, seed, k, d); }
                key += std::to_string(p[k].i) + "," + std::to_string(p[k].j) + ";";
            }
            distinct.insert(key); if ((int)p.size() - 1 > cols + rows) ++longer;
        }
        if (cols * rows >= 8) {
            CHECK(distinct.size() >= 40, "%dx%d: only %zu different lines in 300 seeds (every seed should give its own)", cols, rows, distinct.size());
            CHECK(longer >= 100, "%dx%d: only %d of 300 lines wind (longer than the staircase)", cols, rows, longer);
        }
    }
}

static void Prefixes() {
    std::vector<trace::Pt> p = {{0, 0}, {10, 0}, {10, 10}, {30, 10}};      // 40 long
    CHECK(trace::Length(p) > 39.99f && trace::Length(p) < 40.01f, "length %f", trace::Length(p));
    auto z = trace::Prefix(p, 0); CHECK(z.size() == 1 && z[0].x == 0 && z[0].y == 0, "nothing drawn at 0 (%zu points)", z.size());
    auto all = trace::Prefix(p, 1); CHECK(all.size() == p.size() && fabsf(trace::Length(all) - 40) < .01f, "all drawn at 1 (%zu points, %f)", all.size(), trace::Length(all));
    float last = -1;
    for (int k = 1; k <= 100; ++k) {
        float f = k / 100.f; auto q = trace::Prefix(p, f); float l = trace::Length(q);
        CHECK(fabsf(l - 40 * f) < .05f, "at %.2f drawn %f, want %f (the head moves smoothly, not corner to corner)", f, l, 40 * f);
        CHECK(l >= last, "at %.2f the line got shorter", f); last = l;
    }
    auto h = trace::Prefix(p, .375f); CHECK(fabsf(h.back().x - 10) < .01f && fabsf(h.back().y - 5) < .01f, "head at 3/8: %f,%f want 10,5", h.back().x, h.back().y);
}

static void Rings() {
    auto r = trace::Ring(0, 0, 100, 30, 3);
    CHECK(r.size() >= 6, "ring has %zu points", r.size());
    CHECK(fabsf(r.front().x) < .01f && fabsf(r.front().y - 15) < .01f, "ring starts at %f,%f, want the middle of the left side 0,15", r.front().x, r.front().y);
    CHECK(fabsf(r.back().x - r.front().x) < .01f && fabsf(r.back().y - r.front().y) < .01f, "ring is not closed");
    CHECK(r[1].y < r[0].y - .5f && fabsf(r[1].x) < 1, "ring goes up the left side first (clockwise): second point %f,%f", r[1].x, r[1].y);
    float want = 2 * (100 - 6) + 2 * (30 - 6) + 2 * 3.14159f * 3; float got = trace::Length(r);
    CHECK(fabsf(got - want) < 1.0f, "ring length %f, want about %f", got, want);
    for (auto& q : r) CHECK(q.x >= -.01f && q.x <= 100.01f && q.y >= -.01f && q.y <= 30.01f, "ring point %f,%f outside the button", q.x, q.y);
    CHECK(trace::EaseOut(0) == 0 && trace::EaseOut(1) == 1 && trace::EaseOut(.5f) > .5f, "ease");
}

static void Timing() {
    const double T = 3000;
    auto a = trace::At(0, T); CHECK(a.phase == trace::kTrace && a.drawn == 0 && a.alpha == 1, "start");
    auto b = trace::At(1500, T); CHECK(b.phase == trace::kTrace && fabsf(b.drawn - .5f) < .001f, "half way drawn %f", b.drawn);
    auto c = trace::At(T + 1, T); CHECK(c.phase == trace::kSolved && c.drawn == 1 && c.flash > .9f, "solved flashes (%f)", c.flash);
    auto d = trace::At(T + trace::kFlashMs + 10, T); CHECK(d.phase == trace::kSolved && d.flash == 0 && d.alpha == 1, "then holds");
    double f0 = T + trace::kSolvedMs; auto e = trace::At(f0 + trace::kFadeMs / 2, T), e2 = trace::At(f0 + trace::kFadeMs * .9, T);
    CHECK(e.phase == trace::kFade && e.alpha < .9f && e.alpha > 0 && e2.alpha < e.alpha, "fades out (%f then %f)", e.alpha, e2.alpha);
    auto g = trace::At(trace::CycleMs(T) - 1, T); CHECK(g.phase == trace::kRest && g.alpha == 0, "rest: nothing drawn");
}

int main() {
    Paths(); Prefixes(); Rings(); Timing();
    if (g_fail) { std::printf("trace test: %d failure(s)\n", g_fail); return 1; }
    std::printf("trace test: all passed\n"); return 0;
}
