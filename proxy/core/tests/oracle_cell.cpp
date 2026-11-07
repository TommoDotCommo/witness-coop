// T0: the walking map's cell record built from collision queries (core/oracle_cell.h) on made-up geometry, with mutants that must fail.
#include "../oracle_cell.h"
#include <cstdio>
#include <vector>
using namespace oraclecell;

struct Box { float x0, x1, y0, y1, z0, z1; bool nowalk = false; };      // nowalk: the ground query skips it (a water plane), a ray meets it
struct FakeWorld {      // a floor at z = 0 (not for x < -20: sea) and boxes; ground = the highest top at or below zfrom, ray = nearest slab hit
    std::vector<Box> boxes;
    static bool floorAt(float x) { return x >= -20 && !(x >= 40 && x <= 60); }      // (no floor over the sea, nor in the lake: there the bed box is the floor)
    bool ground(float x, float y, float zfrom, float maxd, float* z) {
        float best = -1e9f; bool ok = false;
        if (floorAt(x) && 0 <= zfrom + 1e-4f) { best = 0; ok = true; }
        for (auto& b : boxes) if (!b.nowalk && x >= b.x0 && x <= b.x1 && y >= b.y0 && y <= b.y1 && b.z1 <= zfrom + 1e-4f && b.z1 > best) { best = b.z1; ok = true; }
        if (!ok || zfrom - best > maxd) return false;
        *z = best; return true;
    }
    bool ray(const float o[3], const float d[3], float maxd, float* dist, float* nz = nullptr) {
        float t0 = 0, t1 = maxd; bool hit = false; float bestT = 1e9f; float bestNz = 0; if (d[2] < -0.999f && o[2] > 0 && floorAt(o[0])) { bestT = o[2]; hit = true; bestNz = 1; }      // the floor plane is a surface a ray meets, too
        for (auto& b : boxes) {
            float lo[3] = {b.x0, b.y0, b.z0}, hi[3] = {b.x1, b.y1, b.z1}; float a = -1e9f, e = maxd; bool in = true;
            for (int i = 0; i < 3 && in; i++) {
                if (std::fabs(d[i]) < 1e-9f) { if (o[i] < lo[i] || o[i] > hi[i]) in = false; continue; }
                float ta = (lo[i] - o[i]) / d[i], tb = (hi[i] - o[i]) / d[i]; if (ta > tb) std::swap(ta, tb);
                a = std::max(a, ta); e = std::min(e, tb); if (a > e) in = false;
            }
            if (in && a > 0 && a < bestT) { bestT = a; hit = true; bestNz = d[2] < -0.999f ? 1.0f : 0.0f; }      // (a ray that starts inside a box finds nothing, as the real one does for the volume it stands in)
        }
        (void)t0; (void)t1; if (hit) { *dist = bestT; if (nz) *nz = bestNz; } return hit;
    }
};
static bool standable(const Rec& r, int b) { if (!(r.mask >> b & 1)) return false; for (int k = 3; k <= 7; k++) if (b + k < 64 && (r.mask >> (b + k) & 1)) return false; return true; }
static bool walkable(const Rec& r) { return r.z > -50.0f; }
static int nstand(const Rec& r) { int n = 0; for (int b = 0; b < 64; b++) n += standable(r, b); return n; }
static int cellx(float x) { return (int)std::floor(x / kCell); }

static int run(int mutant) {
    FakeWorld w; w.boxes = {{5.0f, 5.1f, -10, 10, 0, 3},            // a thin wall 0.1 m thick at x 5.0..5.1: cell 10 is [5.0,5.5): the wall sits on its west edge, the centre is 0.25 m away
                            {8.0f, 8.1f, -10, 10, 0, 0.7f},          // a knee wall (0.7 m: the ray at 0.55 m hits it)
                            {12, 16, -10, 10, 2.5f, 3.0f},           // a slab on legs: 2.5 m of headroom under it
                            {20, 24, -10, 10, 1.5f, 2.0f},           // a low slab: 1.5 m under it
                            {30.0f, 30.2f, 0, 0.2f, 0, 2.5f},       // a post
                            {40, 60, -10, 10, -1.5f, -1.0f},         // a lake bed 1 m down (a pit in the floor: the bed box top is the ground there)
                            {40, 60, -10, 10, -0.5f, -0.2f, true}};  // the water plane above it: not ground, but a ray meets it
    Scanner<FakeWorld> sc(w); sc.mutant = mutant; int fails = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { fails++; if (!mutant) printf("FAIL: %s\n", what); } };
    Rec open = sc.scan(cellx(2.0f), 0, 200, -60);
    check(open.z == 0 && nstand(open) == 1, "open floor: one surface at 0");
    Rec sea = sc.scan(cellx(-30.0f), 0, 200, -60);
    check(sea.z == kNoGround && sea.z < -50, "no ground: a floor below the sea level, so never walkable");
    Rec wall = sc.scan(cellx(5.1f), 0, 200, -60);      // [5.0, 5.5): the wall is in its footprint, the centre ray column (5.25) misses it
    check(!walkable(wall), "a thin wall in the footprint: the cell is not walkable (no ground, so the planner does not cross it as soft)");
    Rec wallbehind = sc.scan(cellx(4.9f), 0, 200, -60);      // [4.5, 5.0): the wall is on its east edge
    check(!walkable(wallbehind), "a thin wall on the edge of the next cell: not walkable either");
    Rec knee = sc.scan(cellx(8.1f), 0, 200, -60);
    check(walkable(knee) && knee.z == 0 && standable(knee, 0) && (knee.mask >> 2 & 1), "a knee wall: the floor stays walkable and the kerb is seen (bin 2)");
    Rec under = sc.scan(cellx(14.0f), 0, 200, -60);
    check(walkable(under) && standable(under, 0) && standable(under, 12), "under a slab with 2.5 m headroom: floor and slab top are both surfaces");
    Rec low = sc.scan(cellx(22.0f), 0, 200, -60);
    check(walkable(low) && low.z == 2.0f, "under a slab 1.5 m up: the floor is dropped, the top is the only level");
    Rec post = sc.scan(cellx(30.1f), 0, 200, -60);      // cell [30.0, 30.5) x [0, 0.5): the post fills the south-west corner of the footprint
    check(!walkable(post), "a post in the corner of the footprint: not walkable");
    Rec lake = sc.scan(cellx(50.0f), 0, 200, -60);
    check((lake.flags & kWater) != 0, "a lake: a flat plane above the bed that the ground query skips marks the cell as water");
    check(!(open.flags & kWater) && !(wall.flags & kWater) && !(under.flags & kWater), "dry cells are not water");
    return fails;
}

int main() {
    int fails = run(0); printf("clean: %s\n", fails ? "FAILED" : "ok"); int bad = fails ? 1 : 0;
    const char* names[] = {"", "no sideways rays", "no up ray", "single level", "no knee ray", "no water test", "blocked levels kept"};
    for (int m = 1; m <= 6; m++) { int f = run(m); printf("mutant %d (%s): %s\n", m, names[m], f ? "caught" : "NOT CAUGHT"); if (!f) bad = 1; }
    return bad;
}
