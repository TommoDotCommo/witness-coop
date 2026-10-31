"""Walk the player somewhere as a person would: look, plan a way, turn with the mouse, hold W. No teleport.

Usage: Windows Python wnav.py <pid> <game folder> x y z [tolerance m]      (or import: Walker(p, c, game_dir).go((x, y, z)))
       python3 tools/win/wnav.py --merge-maps OUT.bin A.bin B.bin ...   (maps added together, game-free)
       python3 tools/win/wnav.py --merge-recordings MAP.bin SESSION.jsonl ...   (a person's recorded walks into a map file, game-free)
Prints one JSON line {"ok", "why", "seconds", "walked", "stuck", "replans", "looks", "backouts", "sidesteps", "blurred", "from", "to"}.
Environment: WNAV_MAPS (more maps to read, ';' separated), WNAV_RECORDINGS (session.jsonl files of tools/win/wrecord.py, ';' separated:
their walks are known-good cells), WNAV_STUCKS (stucks before giving up, 20: at 12 the legs through a maze of unseen walls gave up where the next stuck went on), WNAV_TRACE (a trace in <game folder>/wnav_trace.jsonl),
WNAV_PRESSED (bump all of the wall it is pressed against: 1 at every stuck, 0 never, default from the second stuck within 6 m), WNAV_KEEP (a depth picture kept at each stuck as <game folder>/wnav_<Walker.tag>stuck<N>.bin).

Measured (build 11370412): a mouse count turns the camera 0.0025 rad (positive dx turns right: the yaw at CAM_ANG goes down), dy the same in
pitch; W walks about 1.8 m/s along the yaw (atan2 of the view direction, as `hover` and `tp` take it). A command takes about 11 ms.

The map file (wnav_map.bin): records of REC = '<hhfQB' (23 bytes, little-endian, no header): cell x, cell y (int16, cell = floor(metres /
0.5)), floor z (float32, the lowest surface seen, metres), mask (uint64: bit b set = something seen between floor + 0.25 b and floor +
0.25 (b + 1), so a cell holds 16 m above its floor, WINDOW), flags (uint8: 1 WALKED, someone stood there; 2 BUMPED, a walk was stopped
there). A floor below -1000 marks a cell known only from a bump. Maps merge by adding their points (Map.load); two levels more than
WINDOW apart cannot share a cell, the higher one is kept.

How it finds the way (a robot with a depth camera, as a person uses their eyes):
 - it looks: the proxy's `depthdump` writes the scene depth of the frame as a 192 x 108 picture with its camera (witness_depth.bin); every
   pixel is a point of the world. A picture taken while the camera turned or moved is thrown away (still(): its depth and camera are from
   different frames, so its points land metres off). The points go into the cells. A cell can be walked at a surface when nothing is between
   0.75 m and 1.9 m above it (a wall, a fence, a cliff face fill that band; a tree's crown or a roof above the head does not);
 - it plans: A* over (cell, level) from where it stands to the target. The rules of a step between neighbouring cells:
   * up at most UP 0.6 m, down at most DOWN 1.2 m; a cell may hold several levels (a deck over the ground, stairs beside it): all are kept;
   * stairs: a rise up to STAIR 0.85 m is a step when the step before it climbed too, or the next cell on climbs on (surfaces are bin
     starts, so a 0.45 m step can read 0.7 m); the same rise on flat ground before and after is a ledge, not climbed;
   * a drop of more than UP cannot be walked back up: it costs DROP (8 m of walking), so a plan does not jump down into a pocket to save a
     few metres;
   * a cell whose band is filled at the step's height is soft (bushes, tall grass): walkable at SOFT times the cost; a cliff face never;
   * a cell never seen is walkable at 3 times the cost; under cover (something over the head where it stands) a cell known only far above
     is not seen yet (the ground over a tunnel); a level more than WINDOW above a cell's floor is not seen yet either (junk points far below
     the ground filled a third of the town's cells and hid the street);
   * a WALKED cell costs 0.8 (a person's recorded walk, the bot's own), a BUMPED one 40; no corner cutting past a cell that cannot be entered;
 - a plan that cannot reach the target ends at the cell nearest it across AND in height (LEVEL cells per metre of height: the ground below
   a ledge is not near the ledge), when that is a metre nearer than here;
 - it walks the plan, looking again every half second (the camera faces the way it walks) and planning again when the way ahead is no
   longer free (the cells found blocked stay dear for the rest of the walk: a look from another side finds them open again, and the walker
   turned back at them to and fro); at the start and after being stuck it looks all round (four looks);
 - stuck (W held but under 0.5 m moved in 2 s, or no nearer the target in 25 s): something unseen is in the way (the game has invisible walls
   at the shore and on steep slopes): the cells just ahead are marked bumped (dear, not forbidden: a door may open later), it steps aside
   (A, else D; else back with S) and plans again; from the second stuck near here (wall_rule) it looks once more first: the band-filled points up to 1.2 m ahead and 4 m either side are
   what it is pressed against (a wall the map took for a hedge), and all those cells are bumped, not only the one ahead;
 - a pocket (stuck a third time within 3 m, or no way on the map from here): it walks back out along its own trail (10 m), keeps out of the
   cells round that place for this walk (and makes all within POCKET 5 m of it dear: a U of unseen walls is wider than one bump), unbumps the cells near doors the game says are open, and plans again (3 times at most); only then
   does it give up, and the caller may teleport (the last resort).
The map is kept in <game folder>/wnav_map.bin and read with the maps in WNAV_MAPS (the other bots' and the shared one): it is learnt once
and shared (the orchestrator merges the bots' maps). Doors open and close: a cell whose floor is seen with the band above it empty takes
that newer view."""
import os, sys, math, time, json, heapq, struct
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)

RAD_PER_COUNT = 0.0025
CELL, BIN = 0.5, 0.25          # map cell (m), height bin (m)
EYE = 1.65                     # camera height above the feet when standing (m): measured 1.57..1.68 looking down (the game holds the pitch above -0.93)
BAND = (0.75, 1.9)             # what must be empty above a floor for the player to walk there (m); from 0.75 m: a 0.5 m cell of steep stairs holds 0.5 m of rise
UP, DOWN = 0.6, 1.2            # the most a step between neighbouring cells may climb / drop (m)
RANGE = 30.0                   # depth points further than this are not used (m)
SEA = -50.0                    # (no sea level: the sea has no depth, and the beaches go below 0 by the water)
HW = 1.5                       # A* heuristic weight: a little longer ways, many fewer cells searched (a plan of 100 m took 5 s at 1.0)
SOFT = 25.0                    # cost factor of a cell whose band is filled at a step's height (foliage, or a wall: see walkable); a wall a cell thick costs a 12 m detour
WALKED, BUMPED = 1, 2          # cell flags
WINDOW = 64 * BIN              # a cell describes 16 m above its floor (its 64-bit mask): a level higher than that is not in the cell
STAIR = UP + BIN               # a rise up to this is a stair step when the step before or after it climbs too (surfaces are bin starts: up to 0.25 m low)
POCKET = 5.0                   # after backing out of a pocket, the cells this near it (m) cost as much as bumped ones for the rest of the walk
LEVEL = 1.0                    # (m per m) the height difference counts this much in how near a cell is to a target the plan cannot reach
DROP = 16.0                    # (8 m of walking) extra cost of a step down that cannot be stepped back up (more than UP): a one-way drop leads into pockets
REC = struct.Struct('<hhfQB')  # cell x, cell y, floor z, height mask, flags
DOOR_TYPE = 0x62DE70           # the door entities' type descriptor (module-relative, as TI_DOOR in wlib.py; build 11370412)


def wrap(a): return (a + math.pi) % (2 * math.pi) - math.pi
def cell_of(x, y): return (int(math.floor(x / CELL)), int(math.floor(y / CELL)))
def centre(c): return ((c[0] + 0.5) * CELL, (c[1] + 0.5) * CELL)
BAND_MASK = sum(1 << b for b in range(int(BAND[0] / BIN), int(math.ceil(BAND[1] / BIN))))
HEAD_MASK = (1 << int(math.ceil(BAND[1] / BIN))) - 1      # the bins above a surface the player's body needs free (shifted to start one bin above it)


class Map:
    """cell -> [floor z, mask of the height bins above the floor that hold something, flags]"""
    def __init__(self): self.cells = {}; self.reached = False; self.soft = set(); self.covered = False; self.blocked_cell = None      # reached: the last plan got to the target (else only near it)

    @staticmethod
    def add_point(rec, z):
        if rec is None: return [z, 1, 0]
        if z < rec[0] - BIN:      # a lower surface: it becomes the floor, the old bins move up
            k = int((rec[0] - z) / BIN)
            rec[1] = ((rec[1] << k) | 1) & 0xFFFFFFFFFFFFFFFF; rec[0] = rec[0] - k * BIN
        else:
            b = max(0, int((z - rec[0]) / BIN))      # (a point exactly one bin under the floor came out as -1)
            if b < 64: rec[1] |= 1 << b
        return rec

    def integrate(self, points):
        """Points of one look. A cell whose floor this look sees (at the floor already known) with the band above it empty is replaced
        by this view (an opened door); otherwise the views add up."""
        fresh = {}
        for x, y, z in points:
            c = cell_of(x, y); fresh[c] = self.add_point(fresh.get(c), z)
        for c, f in fresh.items():
            old = self.cells.get(c)
            if old is None: self.cells[c] = f; continue
            if abs(f[0] - old[0]) < 2 * BIN and not (f[1] & BAND_MASK): self.cells[c] = [f[0], f[1], old[2]]; continue
            if f[0] - old[0] > WINDOW - BAND[1]: self.cells[c] = [f[0], f[1], old[2]]; continue      # above all the cell can hold (junk far below, see steps)
            rec = [old[0], old[1], old[2]]
            for b in range(64):
                if f[1] >> b & 1: self.add_point(rec, f[0] + b * BIN + BIN / 2)
            self.cells[c] = rec
        return len(fresh)

    def stand(self, x, y, feet):
        """The player stands here: this cell is walkable at this height, whatever was seen."""
        c = cell_of(x, y); old = self.cells.get(c)
        if old is None or old[0] < -1e3: self.cells[c] = [feet, 1, WALKED]; return
        rec = [old[0], old[1], old[2] | WALKED]
        if feet - rec[0] > WINDOW - BAND[1] - 1:      # standing above all the cell can hold: move its window up (4 m below the feet kept)
            k = int((feet - 4.0 - rec[0]) / BIN); rec[0] += k * BIN; rec[1] >>= k
        b0 = int((feet - rec[0]) / BIN) if feet >= rec[0] - BIN else None
        if b0 is not None and b0 >= 0: rec[1] &= ~(HEAD_MASK << (b0 + 1)) & 0xFFFFFFFFFFFFFFFF      # the player's body is here: nothing in the way above the feet
        self.cells[c] = self.add_point(rec, feet)

    def bump(self, x, y):
        c = cell_of(x, y); old = self.cells.get(c)
        if old is None: self.cells[c] = [-1e4, 0, BUMPED]
        elif not old[2] & WALKED: old[2] |= BUMPED

    def walk_line(self, points, gap=3.0):
        """A walk someone made (eye positions in order, a person's recording or the bot's own): every cell on the way between two points
        less than `gap` m apart and less than UP apart in height was stood on (known-good, see stand). Returns the cells marked."""
        n = 0
        for a, b in zip(points, points[1:]):
            d = math.dist(a[:2], b[:2])
            if d > gap or abs(a[2] - b[2]) > max(UP, d): continue
            k = max(1, int(math.ceil(d / (CELL / 2))))
            for i in range(k + (b is points[-1])):
                t = i / k; self.stand(a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1]), a[2] + t * (b[2] - a[2]) - EYE); n += 1
        return n

    def unbump_near(self, centres, r=3.0):
        """Doors the game says are open (their positions): the cells within r m of one were maybe bumped while it was shut: not any more."""
        n = 0
        for x, y in centres:
            for cx in range(int(math.floor((x - r) / CELL)), int(math.floor((x + r) / CELL)) + 1):
                for cy in range(int(math.floor((y - r) / CELL)), int(math.floor((y + r) / CELL)) + 1):
                    rec = self.cells.get((cx, cy))
                    if rec and rec[2] & BUMPED and math.dist(centre((cx, cy)), (x, y)) <= r:
                        rec[2] &= ~BUMPED; n += 1
                        if rec[0] < -1e3: del self.cells[(cx, cy)]      # (known only from the bump)
        return n

    def surfaces(self, c):
        """Heights in cell c the player could stand on: a height bin holding something with nothing in the body band above it (0.75 m to
        1.9 m higher: a wall's face or a fence fills that; bumps under 0.75 m are steps). [] for a cell with nothing to stand on,
        None: never seen, [None]: only bumped into."""
        r = self.cells.get(c)
        if r is None: return None
        if r[0] < -1e3: return [None]
        z0, m = r[0], r[1]
        return [z0 + b * BIN for b in range(64) if m >> b & 1 and not (m >> b) & BAND_MASK]

    def walkable(self, c, fu=None):
        """None: never seen. Else (ok, floor, cost factor): from height fu the step into c landing nearest fu (see steps); without fu the
        lowest surface. A cell whose band is filled at a step's height is soft: walkable at cost SOFT (bushes and tall grass fill the band
        too and are walked through; a wall shows itself as a bump)."""
        r = self.cells.get(c)
        if r is None: return None
        if fu is None:
            surf = [z for z in (self.surfaces(c) or []) if z is None or z > SEA or r[2] & WALKED]
            return bool(surf), (surf[0] if surf else r[0]), 1.0
        st = self.steps(c, fu)
        if not st: return False, r[0], 1.0
        z, k = min(st, key=lambda q: abs(q[0] - fu)); return True, z, k

    def steps(self, c, fu, up=UP):
        """Where a step from height fu into cell c can land: [(floor, cost factor)]. Every surface within a step (cost 1: stairs go up
        while the ground beside them stays, both are kept), else the soft floor (see walkable); [] when it cannot; None: never seen."""
        r = self.cells.get(c)
        if r is None: return None
        if self.covered and r[0] > fu + BAND[1] and not r[2] & BUMPED: return None      # under cover (a tunnel, a roof) all that is known there may be above it (the ground over the tunnel, seen from the air): down here it is not seen yet
        if r[0] > -1e3 and fu - r[0] > WINDOW - BAND[1]: return None      # this level is above all the cell can hold (its floor was seen 16 m and more below:
                                                                          # junk points under the town filled a third of its cells): not seen here yet
        surf = self.surfaces(c)
        if surf == [None]: return [(fu, 1.0)]
        walked = r[2] & WALKED
        fit = [(z, 1.0) for z in surf if (z > SEA or walked) and -DOWN <= z - fu <= up]      # (up: STAIR when the planner checks the stair rule itself)
        if fit: return fit
        if -DOWN <= r[0] - fu <= UP and r[0] > SEA: return [(r[0], SOFT)]
        return []

    def plan(self, a, b, limit=120000, margin=40.0, avoid=frozenset(), dear=frozenset()):
        """Cells from a's to b's along walkable ground (A* over cell and level), else to the reachable cell nearest b, or None.
        a, b: (x, y, z of the eye). `avoid`: cells not to enter (bumped on this walk); `dear`: cells round a pocket, cost 40."""
        sa, sb = cell_of(a[0], a[1]), cell_of(b[0], b[1]); fb = b[2] - EYE
        lo = (min(sa[0], sb[0]) - margin / CELL, min(sa[1], sb[1]) - margin / CELL); hi = (max(sa[0], sb[0]) + margin / CELL, max(sa[1], sb[1]) + margin / CELL)
        def key(c, z): return (c[0], c[1], int(round(z / BIN)))
        memo = {}
        def steps(c, fu, up=UP):      # (the same cell is asked about from many neighbours: its surfaces are worked out once a plan)
            k = (c, round(fu, 3), up)
            if k not in memo: memo[k] = self.steps(c, fu, up)
            return memo[k]
        def stair(u, fu, v, dx, dy, fv):
            """A rise of more than UP (up to STAIR) is a stair step when the step before it climbed too, or the next cell on climbs on (a
            surface is the start of its 0.25 m bin, so a 0.45 m step can read 0.7 m); on flat ground before and after it is a ledge."""
            p_ = prev.get(u)
            if p_ is not None and fu - floor[p_] >= 0.1: return True
            return any(0.1 <= z - fv <= STAIR for z, k in (steps((v[0] + dx, v[1] + dy), fv, STAIR) or []) if z is not None and k == 1.0)
        s0 = key(sa, a[2] - EYE); floor = {s0: a[2] - EYE}
        r0 = self.cells.get(sa)      # the player is under cover: something is known above its head where it stands (out in the open a surface that high is a plateau)
        self.covered = bool(r0) and r0[0] > -1e3 and any(r0[0] + b * BIN > a[2] - EYE + BAND[1] for b in range(64) if r0[1] >> b & 1)
        def near(c, f): return math.hypot(c[0] - sb[0], c[1] - sb[1]) + LEVEL * abs(f - fb) / CELL      # how near a cell is to the target (in cells): across, plus LEVEL per metre of height (the nearest ground below a ledge is not near it)
        g = {s0: 0.0}; prev = {}; heap = [(0.0, s0)]; n = 0; best, best_h = s0, near(sa, a[2] - EYE)
        def back(u):
            out = [u]
            while out[-1] in prev: out.append(prev[out[-1]])
            out.reverse(); self.soft = set()      # the soft cells this way goes through knowingly (see blocked_along)
            for p_, k_ in zip(out, out[1:]):
                st = self.steps((k_[0], k_[1]), floor[p_], STAIR) or []
                if any(abs(z - floor[k_]) < 1e-6 and c_ == SOFT for z, c_ in st if z is not None): self.soft.add((k_[0], k_[1]))
            return [(k[0], k[1]) for k in out]
        while heap and n < limit:
            _, u = heapq.heappop(heap); n += 1
            fu = floor[u]
            if abs(u[0] - sb[0]) <= 1 and abs(u[1] - sb[1]) <= 1 and abs(fu - fb) < 2.0: self.reached = True; return back(u)
            hu = near(u, fu)
            if hu < best_h: best, best_h = u, hu
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    if not dx and not dy: continue
                    v = (u[0] + dx, u[1] + dy)
                    if not (lo[0] <= v[0] <= hi[0] and lo[1] <= v[1] <= hi[1]) or v in avoid: continue
                    if dx and dy:      # no corner cutting past a cell that cannot be stepped onto
                        if any(st == [] for st in (steps((u[0] + dx, u[1]), fu), steps((u[0], u[1] + dy), fu))): continue
                    st = steps(v, fu, STAIR); step = 1.4142 if dx and dy else 1.0
                    if st is None: opts = [(fu, 3.0)]      # never seen: walkable, dearer
                    else:
                        r = self.cells[v]
                        opts = [(z, 40.0 if r[2] & BUMPED else (0.8 if r[2] & WALKED else k)) for z, k in st if z is None or z - fu <= UP or stair(u, fu, v, dx, dy, z)]
                    for fv, cost in opts:
                        if v in dear: cost = max(cost, 40.0)
                        kv = key(v, fv); nd = g[u] + step * cost + (DROP if fu - fv > UP else 0.0)
                        if nd < g.get(kv, 1e18):
                            g[kv] = nd; prev[kv] = u; floor[kv] = fv
                            heapq.heappush(heap, (nd + HW * math.hypot(v[0] - sb[0], v[1] - sb[1]), kv))
        # no way to the target itself (it stands inside something, or the map does not reach it yet): the way to the reachable cell nearest
        # to it, when that is nearer than here by a metre or more (from there the next look may show more)
        self.reached = False
        if best_h < near(sa, a[2] - EYE) - 1.0 / CELL: return back(best)
        return None

    def blocked_along(self, q, cells, dist=8.0):
        """Is a planned way (its cells, from the one the player at eye position q is in, up to `dist` m) no longer walkable on the map?
        The levels it could be on are carried along as in plan (stairs and the ground beside them). A cell that is now soft but was not
        chosen soft by the plan (a door shut, a wall seen) counts as blocked: planning again may still go through it, knowingly."""
        floors = {round(q[2] - EYE, 2)}; run = 0.0
        for p_, c in zip(cells, cells[1:]):
            nxt = set()
            for f in floors:
                st = self.steps(c, f, STAIR)
                if st and c not in self.soft: st = [q_ for q_ in st if q_[1] != SOFT]
                nxt |= {f} if st is None else {round(z if z is not None else f, 2) for z, _ in st}
            if not nxt: self.blocked_cell = c; return True      # (which cell: the walker keeps it dear, see Walker.ahead_blocked)
            floors = set(sorted(nxt)[:6]); run += CELL * math.hypot(c[0] - p_[0], c[1] - p_[1])
            if run > dist: break
        return False

    def save(self, path):
        tmp = path + '.tmp'
        with open(tmp, 'wb') as f:
            for (cx, cy), (z, m, fl) in self.cells.items():
                if -32768 <= cx < 32768 and -32768 <= cy < 32768: f.write(REC.pack(cx, cy, z, m, fl))
        os.replace(tmp, path)

    def load(self, path):
        try: data = open(path, 'rb').read()
        except OSError: return 0
        n = 0
        for cx, cy, z, m, fl in REC.iter_unpack(data[:len(data) - len(data) % REC.size]):
            c = (cx, cy); old = self.cells.get(c); n += 1
            if old is None: self.cells[c] = [z, m, fl]; continue
            rec = [old[0], old[1], old[2] | fl]
            if z > -1e3 and old[0] > -1e3 and abs(z - old[0]) > WINDOW - BAND[1]:      # two levels too far apart for one cell: the higher one is kept (see integrate)
                self.cells[c] = [z, m, old[2] | fl] if z > old[0] else rec; continue
            if z > -1e3:
                for b in range(64):
                    if m >> b & 1: self.add_point(rec, z + b * BIN + BIN / 2)
            self.cells[c] = rec
        return n


def depth_points(data, step=2):
    """World points of a witness_depth.bin (see the proxy's RunDepthDump): (camera position, [(x, y, z), ...])."""
    mg, W, H = struct.unpack_from('<3I', data); cx, cy, cz, th, ph, vfov, aspect, near = struct.unpack_from('<8f', data, 12)
    if mg != 0x31444457: raise ValueError('not a depth dump')
    px = struct.unpack_from('<%df' % (W * H), data, 44)
    fwd = (math.cos(ph) * math.cos(th), math.cos(ph) * math.sin(th), math.sin(ph)); right = (math.sin(th), -math.cos(th), 0.0)
    up = (-math.sin(ph) * math.cos(th), -math.sin(ph) * math.sin(th), math.cos(ph))
    tv = math.tan(math.radians(vfov) / 2); tu = tv * aspect; pts = []
    for j in range(0, H, step):
        ny = (1 - 2 * (j + 0.5) / H) * tv
        for i in range(0, W, step):
            d = px[j * W + i]
            if d <= 0: continue
            dist = near / d      # along the view direction
            if dist > RANGE: continue
            nx = (2 * (i + 0.5) / W - 1) * tu
            pts.append((cx + dist * (fwd[0] + nx * right[0] + ny * up[0]), cy + dist * (fwd[1] + nx * right[1] + ny * up[1]), cz + dist * (fwd[2] + ny * up[2])))
    return (cx, cy, cz), pts


def load_recording(path):
    """Eye positions of a person's recorded play (tools/win/wrecord.py's session.jsonl: every 'sample' pose and 'trail' point), in order."""
    out = []
    try: lines = open(path, errors='ignore').read().splitlines()
    except OSError: return out
    for line in lines:
        try: r = json.loads(line)
        except ValueError: continue
        if r.get('event') == 'sample' and r.get('pose'): out.append(tuple(r['pose'][:3]))
        elif r.get('event') == 'trail': out.extend(tuple(q[:3]) for q in r.get('points') or [])
        elif r.get('event') in ('task', 'skip'): out.append(None)      # a teleport to the next task: not a walk
    return out


def walks(points):
    """Split a recording's positions at the teleports (None) into separate walks."""
    run = []
    for q in points:
        if q is None:
            if len(run) > 1: yield run
            run = []
        else: run.append(q)
    if len(run) > 1: yield run


def still(cam0, cam1, ang=0.02, move=0.4):
    """Did the camera hold still while a depth picture was taken ((position, (yaw, pitch)) before the request and after the picture came)?
    A picture taken while turning pairs one frame's depth with another frame's camera: its points land metres off (walls smeared sideways,
    the ground turned under itself); such a look is not used."""
    (p0, (y0, q0)), (p1, (y1, q1)) = cam0, cam1
    return abs(wrap(y1 - y0)) <= ang and abs(q1 - q0) <= ang and math.dist(p0, p1) <= move


def trail_back(trail, metres):
    """The way back along a walked trail (eye positions in order, the newest last): its points from the newest backwards until `metres` m of
    trail are covered (all of it when shorter). The pocket rule: no progress here, so leave the way it came in before planning again."""
    out, run = [], 0.0
    for a, b in zip(trail[::-1], trail[::-1][1:]):
        run += math.dist(a[:2], b[:2]); out.append(b)
        if run >= metres: break
    return out


def pressed(pts, q, yaw, reach=1.2, side=4.0):
    """Cells of what the player is pressed against: points in the body band (0.75 m to 1.9 m above the feet) up to `reach` m ahead of the eye
    position q (x, y, z) facing yaw and `side` m to either side. Stuck at a wall the map took for a hedge (band filled: soft), only the one
    cell ahead was bumped, and the walk went on along the wall to the next soft cell, stuck again 12 times: the whole wall in sight goes at once."""
    fx, fy = math.cos(yaw), math.sin(yaw); feet = q[2] - EYE; out = set()
    for x, y, z in pts:
        ahead = (x - q[0]) * fx + (y - q[1]) * fy; across = -(x - q[0]) * fy + (y - q[1]) * fx
        if -0.2 <= ahead <= reach and abs(across) <= side and feet + BAND[0] <= z <= feet + BAND[1]: out.add(cell_of(x, y))
    return out


def stuck_class(pts, q, yaw, reach=1.5, half=0.6, need=200, flat=0.12):
    """What a stuck is against, from the depth picture kept at it (WNAV_KEEP): the points in the body band within `reach` m ahead and `half` m
    either side of the eye position q facing yaw. 'wall': a plane (the distance ahead, after a straight line fitted along the width, scatters
    by no more than `flat` m: a wall, a fence, a building seen square or slanted); 'foliage': as many points but scattered (a hedge, bushes: the
    band is filled yet not flat); 'open': (almost) nothing in the band, so what stops the walk is below the knee (a kerb, a step, the rim of a pit) or
    not drawn (an invisible collision). Measured on the 88 stuck pictures of the round-3 benchmark: open 43, foliage 24, wall 21."""
    feet = q[2] - EYE; fx, fy = math.cos(yaw), math.sin(yaw); a_, c_ = [], []
    for x, y, z in pts:
        a = (x - q[0]) * fx + (y - q[1]) * fy; c = -(x - q[0]) * fy + (y - q[1]) * fx
        if 0 <= a <= reach and abs(c) <= half and feet + BAND[0] <= z <= feet + BAND[1]: a_.append(a); c_.append(c)
    if len(a_) < need: return 'open'
    n = len(a_); ma, mc = sum(a_) / n, sum(c_) / n; vc = sum((c - mc) ** 2 for c in c_)
    b = sum((c - mc) * (a - ma) for a, c in zip(a_, c_)) / vc if vc > 1e-9 else 0.0
    res = math.sqrt(sum((a - ma - b * (c - mc)) ** 2 for a, c in zip(a_, c_)) / n)
    return 'wall' if res <= flat else 'foliage'


def wall_rule(stuck_at, q, mode=None, r=6.0):
    """Is it stuck against a wall the map took for a hedge? mode '1': at every stuck; '0': never; default: the second stuck within `r` m
    (one stuck is a bump into one cell; a second near it means the walk goes on along the same wall to the next soft cell: the whole wall
    in sight goes then, see pressed). Always on it bumped the way of a leg that walks without it (0xb10)."""
    if mode == '1': return True
    if mode == '0': return False
    return any(math.dist(s[:2], q[:2]) < r for s in stuck_at)


def pocket(stuck_at, q, r=3.0, n=2):
    """Stuck here n times already (within r m): a pocket, back out of it."""
    return sum(1 for s in stuck_at if math.dist(s[:2], q[:2]) < r) >= n


def simplify(cells, z):
    """Waypoints (x, y, z) along a cell path: one every 2 m and at every turn."""
    out = []; last = None
    for k, c in enumerate(cells):
        x, y = centre(c)
        if k == 0: continue
        turn = 0 < k < len(cells) - 1 and (cells[k + 1][0] - c[0], cells[k + 1][1] - c[1]) != (c[0] - cells[k - 1][0], c[1] - cells[k - 1][1])
        if last is None or math.dist((x, y), last) >= 2.0 or turn or k == len(cells) - 1: out.append((x, y, z)); last = (x, y)
    return out


class Walker:
    def __init__(self, p, c, game_dir, log=lambda m: None):
        self.p, self.c, self.log, self.game_dir = p, c, log, game_dir
        self.map = Map(); self.down = set(); self.stuck = 0; self.walked = 0.0; self.replans = 0; self.looks = 0
        self.avoid = set(); self.stuck_at = []      # cells bumped on this walk (not entered again), where the stucks were
        self.cells, self.ci = [], 0                   # the plan's cells, the one the player is at
        self.trace = open(os.path.join(game_dir, 'wnav_trace.jsonl'), 'a') if os.environ.get('WNAV_TRACE') else None
        self.map_path = os.path.join(game_dir, 'wnav_map.bin'); self.depth = os.path.join(game_dir, 'witness_depth.bin')
        for q in [q for q in (os.environ.get('WNAV_MAPS') or '').split(';') if q] + [self.map_path]: self.map.load(q)
        self.recorded = 0      # cells known-good from a person's recorded walks (WNAV_RECORDINGS: ';' separated session.jsonl files)
        for q in [q for q in (os.environ.get('WNAV_RECORDINGS') or '').split(';') if q]:
            for w in walks(load_recording(q)): self.recorded += self.map.walk_line(w)
        self.last_pts = []; self.pressed_n = 0; self.tag = ''      # a prefix for the kept pictures (the benchmark sets one per leg)
        self.trail = []; self.dear = set(); self.hit = set(); self.backouts = 0; self.sidesteps = 0; self.doors = None; self.blurred = 0      # where this walker has been (newest last); open doors, read once

    def pos(self): return self.p.camera()[0]
    def yaw(self): return self.p.camera()[1][0]

    def key(self, k, down):
        if down == (k in self.down): return
        self.c.send(['key %s %s' % (k, 'down' if down else 'up')]); (self.down.add if down else self.down.discard)(k)

    def release(self):
        for k in list(self.down): self.key(k, False)

    def turn(self, err):
        dx = int(round(-err / RAD_PER_COUNT))
        if dx: self.c.send(['mouse %d 0' % max(-1200, min(1200, dx))])

    def pitch_to(self, want):
        err = want - self.p.camera()[1][1]
        if abs(err) > 0.05: self.c.send(['mouse 0 %d' % int(round(-err / RAD_PER_COUNT))], 0.03)

    def look(self, again=1):
        """One depth picture into the map. False: the proxy did not answer, or the camera did not hold still (tried `again` more times)."""
        try: before = os.path.getmtime(self.depth)
        except OSError: before = 0
        cam0 = self.p.camera(); self.c.send(['depthdump']); t0 = time.monotonic()
        while time.monotonic() - t0 < 1.0:
            try:
                if os.path.getmtime(self.depth) != before:
                    data = open(self.depth, 'rb').read()
                    if len(data) >= 44: break
            except OSError: pass
            time.sleep(0.01)
        else: return False
        try: _, pts = depth_points(data)
        except (ValueError, struct.error): return False
        if not still(cam0, self.p.camera()):      # (the camera turned or moved while the picture was taken)
            self.blurred += 1; time.sleep(0.1); return self.look(again - 1) if again > 0 else False
        self.map.integrate(pts); self.looks += 1; self.last_pts = pts
        q = self.pos(); self.map.stand(q[0], q[1], q[2] - EYE)
        return True

    def open_doors(self):
        """Positions of the doors the game holds open (its entities of the door type with +0x1d0 = 1.0, as tools/win/wstatedump.py), read once."""
        if self.doors is None:
            self.doors = []
            try:
                for e in self.p.entity_table():
                    if e and self.p.type_of(e) == DOOR_TYPE and self.p.u32(e + 0x1d0) == 0x3f800000: self.doors.append(struct.unpack('<2f', self.p.rd(e + 0x24, 8)))
            except Exception as e_: self.log('doors not read: %r' % e_)
        return self.doors

    def follow(self, points, seconds=15.0):
        """Walk through these points in turn by steering alone (no plan): the way back out of a pocket. True when the last was reached."""
        t0 = time.monotonic(); last, last_t = self.pos(), t0
        for pt in points:
            while time.monotonic() - t0 < seconds:
                q = self.pos()
                if math.dist(q[:2], pt[:2]) < 0.7: break
                err = wrap(math.atan2(pt[1] - q[1], pt[0] - q[0]) - self.yaw()); self.turn(err); self.key('W', abs(err) < 0.7)
                if math.dist(q[:2], last[:2]) > 0.3: last, last_t = q, time.monotonic()
                elif time.monotonic() - last_t > 2.0: self.key('W', False); return False
                time.sleep(0.03)
            else: self.key('W', False); return False
        self.key('W', False); return True

    def sidestep(self):
        """Stuck against something unseen: a step to the left, else to the right (A, D). True when one moved the player."""
        self.key('W', False)
        for k in ('A', 'D'):
            q = self.pos(); self.c.send(['key %s down' % k], 0.5 if k == 'A' else 0.8); self.c.send(['key %s up' % k], 0.1)
            if math.dist(self.pos()[:2], q[:2]) > 0.3: self.sidesteps += 1; return True
        return False

    def back_out(self, why):
        """The pocket rule: leave along the trail it came in by (10 m), the cells round here not entered again on this walk, the doors the
        game says are open unbumped, then plan again. False when there is no trail to go back on or it has backed out 3 times."""
        q = self.pos(); back = trail_back(self.trail, 10.0)
        if self.backouts >= 3 or not back or math.dist(back[-1][:2], q[:2]) < 2.0: return False
        self.backouts += 1; self.note(backout=[round(q[0], 2), round(q[1], 2)], why=why); self.log('backing out (%s) at %.1f %.1f' % (why, q[0], q[1]))
        for dx in range(-3, 4):
            for dy in range(-3, 4):
                if dx * dx + dy * dy <= 9: c_ = cell_of(q[0] + dx * CELL, q[1] + dy * CELL); self.avoid.add(c_); self.map.bump(*centre(c_))
        r_ = int(POCKET / CELL)      # and the whole pocket round it dear (a U of unseen walls is 6 m wide: one disc of bumps only moved the next try aside)
        self.dear |= {(c0[0] + dx, c0[1] + dy) for c0 in [cell_of(q[0], q[1])] for dx in range(-r_, r_ + 1) for dy in range(-r_, r_ + 1) if dx * dx + dy * dy <= r_ * r_}
        self.map.unbump_near(self.open_doors())
        self.follow(back); self.trail = self.trail[:max(1, len(self.trail) - len(back))]; self.look_around(); return True

    def look_around(self):
        self.key('W', False)
        for _ in range(4): self.look(); self.turn(math.pi / 2); time.sleep(0.15)      # (the turn takes a frame or two: a look during it is blurred)

    def keep_depth(self, name):
        """WNAV_KEEP=1: the last depth picture (and the camera it was taken from) kept as <game folder>/wnav_<name>.bin, to see what a stuck is against."""
        if os.environ.get('WNAV_KEEP'):
            try: self.look(0); open(os.path.join(self.game_dir, 'wnav_%s%s.bin' % (self.tag, name)), 'wb').write(open(self.depth, 'rb').read())
            except OSError: pass

    def note(self, **kv):
        if self.trace: self.trace.write(json.dumps(dict(t=round(time.monotonic(), 2), **kv)) + '\n'); self.trace.flush()

    def plan(self, target):
        q = self.pos(); cells = self.map.plan(q, target, avoid=self.avoid, dear=self.dear | self.hit)
        if cells is None: return None
        self.replans += 1; self.cells, self.ci = cells, 0
        way = simplify(cells, target[2]) or [tuple(target)]      # (a plan of one cell: the target is next to the player)
        self.note(plan=[[round(w[0], 1), round(w[1], 1)] for w in way], at=[round(v, 2) for v in q], reached=self.map.reached); return way

    def ahead_blocked(self, dist=8.0):
        """Is the plan's way ahead (its own cells, from the one nearest the player) no longer walkable?"""
        q = self.pos(); cells = self.cells
        if not cells: return False
        near = min(range(self.ci, min(len(cells), self.ci + 16)), key=lambda i: math.dist(centre(cells[i]), q[:2]))
        self.ci = near
        if not self.map.blocked_along(q, cells[near:], dist): return False
        c = self.map.blocked_cell      # the way is blocked here: the next plan keeps off it (else a look from another side finds it open again, the walker turns back at it and round again: it went to and fro 40 times at a ramp)
        self.hit |= {(c[0] + dx, c[1] + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1)}; return True

    def settle(self, target, tol=0.12, seconds=8.0):
        """Shuffle onto an exact spot after a walk (where a line is drawn from matters to within a few tens of cm): face it, tap W for a
        moment, again, until within `tol`. Returns the distance left."""
        t0 = time.monotonic()
        while time.monotonic() - t0 < seconds:
            q = self.pos(); d = math.dist(q[:2], target[:2])
            if d < tol: break
            self.turn(wrap(math.atan2(target[1] - q[1], target[0] - q[0]) - self.yaw())); time.sleep(0.05)
            self.c.send(['key W down'], min(0.25, max(0.03, d / 1.8 * 0.6))); self.c.send(['key W up'], 0.15)
        return math.dist(self.pos()[:2], target[:2])

    def free_look(self):
        """Out of a puzzle: in one the mouse moves the cursor and W does nothing (a failed line left the game there: the first right click
        only took the line back, the second left the puzzle). True when the mouse turns the camera."""
        for _ in range(4):
            y0 = self.yaw(); self.c.send(['mouse 8 0'], 0.15); moved = abs(wrap(self.yaw() - y0)) > 0.005
            if moved: self.c.send(['mouse -8 0'], 0.1); return True
            self.c.send(['button R down'], 0.12); self.c.send(['button R up'], 0.6)
        return False

    def go(self, target, tol=0.8, seconds=240, ztol=2.0):
        """Walk to `target` (x, y, z: the eye there) until within `tol` across and `ztol` in height. Returns a dict like the JSON line."""
        t0 = time.monotonic(); deadline = t0 + seconds; self.hit = set()
        self.c.send(['hover off'])
        if not self.free_look(): return {'ok': False, 'why': 'stuck in a puzzle: the mouse does not turn the camera', 'seconds': round(time.monotonic() - t0, 1), 'walked': 0.0, 'stuck': 0, 'replans': 0, 'looks': 0, 'from': list(self.pos()), 'to': list(self.pos())}
        self.pitch_to(-0.25)
        start = self.pos(); last = noted = start; self.note(go=list(target), at=list(start)); self.look_around()
        stucks = int(os.environ.get('WNAV_STUCKS') or 20)
        ok, why, way, k = False, 'out of time', None, 0
        last_look = 0.0; moved_at, moved_t = start, time.monotonic(); near_d, near_t = math.dist(start[:2], target[:2]), time.monotonic()
        while time.monotonic() < deadline:
            q = self.pos(); self.walked += math.dist(q[:2], last[:2])
            if math.dist(q[:2], noted[:2]) > 1.0: self.note(pos=[round(q[0], 2), round(q[1], 2), round(q[2], 2)]); noted = q
            if self.trail and math.dist(q[:2], self.trail[-1][:2]) > 3.0: self.trail = []      # a teleport (or a reload): the way here is not a walk
            if not self.trail or math.dist(q[:2], self.trail[-1][:2]) > 0.5: self.trail.append(q)
            last = q
            if math.dist(q[:2], target[:2]) < tol and abs(q[2] - target[2]) < ztol: ok, why = True, ''; break
            if way is None or k >= len(way):
                way, k = self.plan(target), 0
                if way is None and math.dist(q[:2], target[:2]) >= tol and self.back_out('no way on the map'): continue
                if way is None: why = 'no way on the map' if math.dist(q[:2], target[:2]) >= tol else 'here, but %.1f m %s it' % (abs(q[2] - target[2]), 'below' if q[2] < target[2] else 'above'); break
            while k < len(way) - 1 and math.dist(q[:2], way[k][:2]) < 1.0: k += 1
            aim = way[k]; d = math.dist(q[:2], aim[:2])
            bearing = math.atan2(aim[1] - q[1], aim[0] - q[0]); err = wrap(bearing - self.yaw())
            self.turn(err); self.key('W', abs(err) < 0.7)
            if time.monotonic() - last_look > 0.5:
                last_look = time.monotonic(); self.look()
                if self.ahead_blocked(): way = None; continue
            # stuck: W held but not 0.5 m moved in 2 s (a wall unseen), or not 0.5 m nearer the target in 25 s (going round in circles)
            now = time.monotonic()
            if math.dist(q[:2], moved_at[:2]) > 0.5 or 'W' not in self.down: moved_at, moved_t = q, now
            dt = math.dist(q[:2], target[:2])
            if dt < near_d - 0.5: near_d, near_t = dt, now
            if now - moved_t > 2.0 or now - near_t > 25.0:
                moved_at, moved_t, near_d, near_t = q, now, dt, now
                self.stuck += 1
                if self.stuck > stucks: why = 'stuck %d times' % (self.stuck - 1); break
                y = self.yaw(); again = sum(1 for s in self.stuck_at if math.dist(s, q[:2]) < 3.0); pocketed = pocket(self.stuck_at, q); self.stuck_at.append(q[:2])
                for r in (0.45, 0.75, 1.05, 1.35):      # the cells ahead (wider each time it is stuck here again)
                    for side in range(-again, again + 1):
                        x = q[0] + r * math.cos(y) - side * CELL * math.sin(y); yy = q[1] + r * math.sin(y) + side * CELL * math.cos(y)
                        self.map.bump(x, yy); self.avoid.add(cell_of(x, yy))
                if wall_rule(self.stuck_at[:-1], q, os.environ.get('WNAV_PRESSED')) and self.look(0):      # (what it is pressed against: all of it, not only the cell ahead; from the second stuck near here: at the first stuck in the benchmark it bumped the way of a leg that walks without it)
                    wall = pressed(self.last_pts, q, y)
                    for c_ in wall: self.map.bump(*centre(c_))
                    self.pressed_n += len(wall)
                self.note(stuck=[round(q[0], 2), round(q[1], 2)], again=again, yaw=round(y, 2), z=round(q[2], 2)); self.keep_depth('stuck%d' % self.stuck)
                self.log('stuck %d at %.1f %.1f: bumped, planning again' % (self.stuck, q[0], q[1]))
                if pocketed and self.back_out('stuck %d times here' % (again + 1)): way = None; continue
                if not self.sidestep(): self.c.send(['key S down'], 0.4); self.c.send(['key S up'])      # (the sidestep or the step back: off the thing unseen)
                self.look_around(); way = None
            time.sleep(0.02)
        self.release()
        try: self.map.save(self.map_path)
        except OSError: pass
        return {'ok': ok, 'why': why, 'seconds': round(time.monotonic() - t0, 1), 'walked': round(self.walked, 1), 'stuck': self.stuck,
                'replans': self.replans, 'looks': self.looks, 'backouts': self.backouts, 'sidesteps': self.sidesteps, 'pressed': self.pressed_n, 'blurred': self.blurred, 'from': [round(v, 2) for v in start], 'to': [round(v, 2) for v in self.pos()]}


if __name__ == '__main__' and sys.argv[1:2] == ['--merge-maps']:      # OUT.bin A.bin B.bin ...: the maps added together (the walkers' learnt walls and walked cells)
    m = Map(); n = sum(m.load(q) for q in sys.argv[3:]); m.save(sys.argv[2]); print(json.dumps({'map': sys.argv[2], 'cells': len(m.cells), 'read': n})); sys.exit(0)
if __name__ == '__main__' and sys.argv[1:2] == ['--merge-recordings']:
    m = Map(); m.load(sys.argv[2]); n = sum(m.walk_line(w) for q in sys.argv[3:] for w in walks(load_recording(q)))
    m.save(sys.argv[2]); print(json.dumps({'map': sys.argv[2], 'cells': len(m.cells), 'stood': n})); sys.exit(0)
if __name__ == '__main__':
    from wcontrol import Control
    exec(compile(open(os.path.join(HERE, 'wlib.py')).read(), 'wlib.py', 'exec'))
    pid, gdir = int(sys.argv[1]), sys.argv[2]
    target = tuple(float(v) for v in sys.argv[3:6]); tol = float(sys.argv[6]) if len(sys.argv) > 6 else 0.8
    w = Walker(attach(pid), Control(gdir), gdir, log=lambda m: print(m, file=sys.stderr, flush=True))
    out = w.go(target, tol, float(os.environ.get('WNAV_SECONDS') or 240))
    print(json.dumps(out), flush=True); sys.exit(0 if out['ok'] else 1)
