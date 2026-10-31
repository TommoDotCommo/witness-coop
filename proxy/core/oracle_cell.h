// The walking map's cell record built from collision queries (proxy/oracle.cpp feeds it the game's own ray cast and ground query; the T0 test
// proxy/core/tests/oracle_cell.cpp feeds it made-up geometry). The record is the one tools/win/wnav.py reads (REC '<hhfQB'): the lowest surface
// of a 0.5 m cell, and a 64-bit mask with bit b set when something is between floor + 0.25 b and floor + 0.25 (b + 1). The planner stands where
// a bit is set with nothing in the bins 3 to 7 above it (0.75 m to 2 m), so what this has to say about a cell is: the surfaces in its column, and
// where something stands in the way of a body on each of them.
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace oraclecell {
constexpr float kCell = 0.5f, kBin = 0.25f, kNoGround = -100.0f;     // (a floor below the planner's SEA of -50: never walkable, not "never seen")
constexpr int kMaxLevels = 12, kKeepLevels = 4;
constexpr float kReach[3] = {0.8f, 1.3f, 1.8f};                       // body heights above a surface that a ray tests (the planner's band is 0.75 to 1.9)
constexpr float kKnee = 0.55f;                                        // a kerb or low wall: a step, not a wall
struct Rec { float z; uint64_t mask; uint8_t flags; };

// World: bool ground(float x, float y, float zfrom, float maxd, float* z)      the first surface at or below zfrom, within maxd
//        bool ray(const float o[3], const float d[3], float maxd, float* dist)  d is a unit vector
template <class World> struct Scanner {
    World& w; float up_clear = 2.0f; int mutant = 0;     // mutant: 1 no sideways rays, 2 no up ray, 3 a single level (the first found), 4 no knee ray
    explicit Scanner(World& world) : w(world) {}

    static void setbit(Rec& r, float z) { int b = (int)std::floor((z - r.z) / kBin + 1e-4f); if (b >= 0 && b < 64) r.mask |= 1ull << b; }

    std::vector<float> levels(float x, float y, float ztop, float zbot) {      // top down; the next query starts just under the last surface found
        std::vector<float> out; float zf = ztop;
        for (int i = 0; i < kMaxLevels; i++) {
            float z; if (!w.ground(x, y, zf, zf - zbot, &z)) break;
            if (!out.empty() && z > out.back() - 0.02f) break;      // (no progress)
            out.push_back(z); zf = z - 0.03f; if (mutant == 3) break;
        }
        std::reverse(out.begin(), out.end()); return out;      // lowest first
    }

    Rec scan(int cx, int cy, float ztop, float zbot) {
        float x = (cx + 0.5f) * kCell, y = (cy + 0.5f) * kCell;
        std::vector<float> lv = levels(x, y, ztop, zbot);
        if (lv.empty()) return {kNoGround, 1, 0};
        Rec r{lv[0], 0, 0};
        if ((int)lv.size() > kKeepLevels) lv.resize(kKeepLevels);      // the lowest levels: the window of a cell is 16 m
        static const float dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {0.7071f, 0.7071f}, {-0.7071f, 0.7071f}, {0.7071f, -0.7071f}, {-0.7071f, -0.7071f}};
        for (float z : lv) {
            setbit(r, z);
            if (mutant != 2) { float o[3] = {x, y, z + 0.05f}, d[3] = {0, 0, 1}, dist; if (w.ray(o, d, up_clear, &dist)) setbit(r, z + 0.05f + dist); }      // a ceiling or an overhang
            if (mutant == 1) continue;
            for (int k = 0; k < 8; k++) {      // something standing in the cell's footprint at body height: from the centre to the edge (to the corner)
                float reach = k < 4 ? 0.26f : 0.37f;
                for (int h = 0; h < 4; h++) {
                    if (h == 3 && mutant == 4) continue;
                    float hz = h < 3 ? kReach[h] : kKnee; float o[3] = {x, y, z + hz}, d[3] = {dirs[k][0], dirs[k][1], 0}, dist;
                    if (w.ray(o, d, reach, &dist)) setbit(r, z + hz);
                }
            }
        }
        return r;
    }
};
}
