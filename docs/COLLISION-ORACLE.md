# The collision oracle: the game's own ray cast and ground query

The walking bot (`tools/win/wnav.py`) used to build its map from the game's depth picture (192 x 108 pixels, a point per pixel), which is coarse, taken
while turning, and cannot tell a wall from foliage. The proxy now asks the engine: it calls the same functions the game uses to find the floor under
the player and to cast a ray against the world's collision. Build 11370412 only (`witness64_d3d11.exe`); offsets are module-relative.

## How the functions were found

1. The exe contains the text `Warning: Unable to get ground hit from player at (%.3f, %.3f, %.3f)`. Its only reference (exe+0xA27ED, in the function at
   exe+0xA2710) sits right after a call to **exe+0x241870**, which returns a bool and writes the hit point: that is the player's "find the floor".
2. That function builds a list of candidate entities with **exe+0x1934C0** (a box query on the world object, `[exe+0x62D0A0]`, the same global the
   entity table hangs from) and tests them with **exe+0x254FF0**, a ray cast against a list of entities. A hit result is a 0x70-byte struct made by
   **exe+0x2538A0** (distance `FLT_MAX`, entity null).
3. Other strings that name the same machinery: `player_cylinder_height/_radius/_leg_clearance`, `player_max_step_height`, `collision_log`,
   `ignore_collision_volumes`, `Collision_Volume`, `Touch_Collision`.

| exe offset | signature (x64 Windows ABI) | meaning |
|---|---|---|
| +0x241870 | `bool Ground(const float pos[3], float out[3], Hit*, bool, bool, bool, float maxDist)` | first surface below `pos` within `maxDist` that the player could stand on (the call the player makes passes `true, true, false`); false when there is none |
| +0x1934C0 | `void BoxQuery(World*, const float p0[3], const float p1[3], int mask, List*, float, float)` | entities whose bounds meet the segment; the game uses mask `0xC980001` |
| +0x254FF0 | `void RayCast(const float o[3], const float d[3], float maxDist, int flags, int mask, Hit*, List*)` | nearest hit among the listed entities; the game passes flags `-1`, mask `4` |
| +0x2538A0 | `Hit* HitInit(Hit*)` | resets a hit |
| +0x2E7FA0 / +0x2E7F90 | `Reserve(void** data, int* cap, int n, int elem)` / `Free(void** data)` | the game's own array (`int count; int cap; T* data`): the candidate list |

Hit struct: `+0x00` float distance, `+0x08` entity pointer (null: no hit), `+0x28` float[3] surface normal (a floor reads (0, 0, 1)). The entity's type
descriptor (`[entity+8]`) holds a pointer to its name at `+8`: `Inanimate` (terrain, rocks, buildings), `Collision_Volume`, `Machine_Panel`, `Door`,
`Touch_Collision`.

## Commands (`witness_cmd.txt`; results in `witness_oracle_out.txt`, replaced whole after each batch, first line `# seq N n=COUNT`)

- `ray x y z dx dy dz [maxdist] [raw]` -> `ray hit dist=.. at=x,y,z n=nx,ny,nz type=<name> ent=0x.. cand=N` or `ray miss cand=N`
- `ground x y [zfrom] [maxdist]` -> `ground z=.. type=<name>` or `ground none` (defaults: from z = 400, 800 m down)
- `where` -> `where feet=x,y,z cam=x,y,z yaw=.. pitch=..`
- `oracle` runs the lines of `witness_oracle_in.txt` as one batch (about 20 microseconds a query in a batch)
- `scan ID cx0 cy0 nx ny [ztop zbot]` -> `witness_scan_ID.bin`: the walking map cells (cell indices, 0.5 m) as `wnav.py` REC records (see below)

Everything runs on the render thread between frames. The functions are checked by their first bytes before the first call (a different build disables
the commands). A scan gets 10 ms of each frame.

## What the engine does and does not know

- Collision is **streamed around the player**: terrain and buildings answer out to about 250 m, small things less; a place far from the player
  answers `none` for everything. `tools/oracle_map.py` therefore holds the player (`hover`) in the middle of each 50 m tile while it scans the tile.
  A player put where nothing is loaded falls and is returned to the spawn: that is also what happens to a `tp` there.
- `ground` answers `none` for the sea, for rock faces too steep to stand on and for places whose first surface from above faces down. That is the game's
  own "can stand here", so it is what the map wants.
- `ray` hits everything with collision, including panels (`Machine_Panel`) and doors. Foliage has none.

## The map (`proxy/core/oracle_cell.h`)

Per 0.5 m cell: the surfaces in the column of the cell centre (top-down enumeration with `ground`, lowest 4 kept), a mask bit for each; for each
surface a ray up (2 m: a ceiling), and rays from the centre to the cell's edge and corners (8 directions) at 0.55, 0.8, 1.3 and 1.8 m above it: whatever
is hit sets the bit of that height. `wnav.py` stands on a bit with nothing in the three to seven bins above it, so a thin wall or a post inside the
footprint makes the floor unstandable and a kerb (0.55 m) stays a step. A cell with no ground gets a floor of -100 m (below the planner's sea level: never walkable,
unlike a cell never seen, which the planner crosses at triple cost). `python3 -I`-free T0 test: `make -C proxy/core test` (`oracle-cell`, with four mutants).
