// The walking map's cell record built from collision queries (proxy/oracle.cpp feeds it the game's own ray cast and ground query; the T0 test
// proxy/core/tests/oracle_cell.cpp feeds it made-up geometry). The record is the one tools/win/wnav.py reads (REC '<hhfQB'): the lowest surface
// of a 0.5 m cell, and a 64-bit mask with bit b set when something is between floor + 0.25 b and floor + 0.25 (b + 1). The planner stands where
// a bit is set with nothing in the bins 3 to 7 above it (0.75 m to 2 m), so what this has to say about a cell is: the surfaces in its column, and
// where something stands in the way of a body on each of them. Levels with something in the way are dropped; a cell with none left is `no ground`.
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace oraclecell {
constexpr float kCell = 0.5f, kBin = 0.25f, kNoGround = -100.0f;     // (a floor below the planner's SEA of -50: never walkable, not "never seen")
constexpr int kMaxLevels = 12, kKeepLevels = 4;
constexpr float kReach[3] = {0.8f, 1.3f, 1.8f};                       // body heights above a surface that a ray tests (the planner's band is 0.75 to 1.9)
constexpr float kKnee = 0.55f;
constexpr float kReach4 = 0.26f;                                      // how far from the cell centre a body-height obstacle counts as in the cell (the player's radius measured at walls it pressed against: 0.22 m)                                        // a kerb or low wall: a step, not a wall
constexpr uint8_t kWater = 16;      // Rec.flags: the surface in this column that the ground query skips is a flat plane above the ground (a lake): the player is kept out (see below)
struct Rec { float z; uint64_t mask; uint8_t flags; };

// World: bool ground(float x, float y, float zfrom, float maxd, float* z)      the first surface at or below zfrom, within maxd
//        bool ray(const float o[3], const float d[3], float maxd, float* dist, float* nz = 0)  d is a unit vector; nz: the hit surface normal's z
template <class World> struct Scanner {
    World& w; float up_clear = 2.0f; int mutant = 0;     // mutant: 1 no sideways rays, 2 no up ray, 3 a single level (the first found), 4 no knee ray, 5 no water test, 6 keep blocked levels
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
        uint8_t flags = 0;
        // Water. The player cannot walk into a lake (an invisible wall at the shore: no ray finds it), though the lake bed has ground and is walkable if the
        // player is put there. What marks a lake is a flat plane above the bed that the ground query skips (it is not a floor) but a ray down meets.
        if (mutant != 5) { float o[3] = {x, y, ztop}, d[3] = {0, 0, -1}, dist, nz = 0; if (w.ray(o, d, ztop - zbot, &dist, &nz) && nz > 0.9995f && ztop - dist > lv.back() + 0.03f && ztop - dist < lv.back() + 20.0f) flags |= kWater; }
        if ((int)lv.size() > kKeepLevels) lv.resize(kKeepLevels);      // the lowest levels: the window of a cell is 16 m
        static const float dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {0.7071f, 0.7071f}, {-0.7071f, 0.7071f}, {0.7071f, -0.7071f}, {-0.7071f, -0.7071f}};
        // Each level is a place a body could stand if nothing is in the way at body height. Something in the way is a collision, not foliage (rays do not
        // meet foliage), so a level with a hit in the planner's band (0.75 to 2 m up) is dropped: the planner treats a band-filled cell as soft, walkable at
        // 25 times the cost, and the walkers got stuck at exactly these places. A cell with no level left has no ground (never walkable).
        std::vector<std::pair<float, uint64_t>> keep;      // (level, bits above it relative to the level: bit b = 0.25 b)
        for (float z : lv) {
            uint64_t bits = 0; bool blocked = false;
            auto hit = [&](float h, bool body) { int b = (int)std::floor(h / kBin + 1e-4f); if (b >= 0 && b < 64) bits |= 1ull << b; if (body && h >= 0.75f && h < 2.0f) blocked = true; };
            if (mutant != 2) { float o[3] = {x, y, z + 0.05f}, d[3] = {0, 0, 1}, dist; if (w.ray(o, d, up_clear, &dist)) hit(0.05f + dist, true); }      // a ceiling or an overhang
            if (mutant != 1) for (int k = 0; k < 8; k++) {      // something standing in the cell's footprint at body height: from the centre to the edge (to the corner)
                float reach = k < 4 ? kReach4 : kReach4 * 1.4142f;
                for (int h = 0; h < 4; h++) {
                    if (h == 3 && mutant == 4) continue;
                    float hz = h < 3 ? kReach[h] : kKnee; float o[3] = {x, y, z + hz}, d[3] = {dirs[k][0], dirs[k][1], 0}, dist;
                    if (w.ray(o, d, reach, &dist)) hit(hz, h < 3);
                }
            }
            if (!blocked || mutant == 6) keep.push_back({z, bits});
        }
        if (keep.empty()) return {kNoGround, 1, flags};
        Rec r{keep[0].first, 0, flags};
        for (auto& k : keep) { setbit(r, k.first); for (int b = 0; b < 64; b++) if (k.second >> b & 1) setbit(r, k.first + b * kBin + 1e-3f); }
        return r;
    }
};
}
