"""Walk the player somewhere as a person would: look, plan a way, turn with the mouse, hold W. No teleport.

Usage: Windows Python wnav.py <pid> <game folder> x y z [tolerance m]      (or import: Walker(p, c, game_dir).go((x, y, z)))
Prints one JSON line {"ok", "why", "seconds", "walked", "stuck", "replans", "looks", "from", "to"}.

Measured (build 11370412): a mouse count turns the camera 0.0025 rad (positive dx turns right: the yaw at CAM_ANG goes down), dy the same in
pitch; W walks about 1.8 m/s along the yaw (atan2 of the view direction, as `hover` and `tp` take it). A command takes about 11 ms.

How it finds the way (a robot with a depth camera, as a person uses their eyes):
 - it looks: the proxy's `depthdump` writes the scene depth of the frame as a 192 x 108 picture with its camera (witness_depth.bin); every
   pixel is a point of the world. The points go into a map of 0.5 m cells: each cell keeps its lowest surface seen (the floor) and which
   0.25 m height bins above that hold something (a 64-bit mask). A cell can be walked when nothing is between 0.75 m and 1.9 m above its
   floor (a wall, a fence, a cliff face fill that band; a tree's crown or a roof above the head does not);
 - it plans: A* over the cells from where it stands to the target, a step up of at most 0.6 m and down of at most 1.2 m between cells;
   a cell never seen counts as walkable but dearer, so the plan heads into the unknown only where the known way runs out;
 - it walks the plan, looking again every half second (the camera faces the way it walks) and planning again when the way ahead is no
   longer free; at the start and after being stuck it looks all round (four looks);
 - stuck (W held but under 0.5 m moved in 2 s, or no nearer the target in 25 s): something unseen is in the way (the game has invisible walls at the shore and
   on steep slopes): the cells just ahead are marked bumped (dear, not forbidden: a door may open later), it backs off and plans again.
The map is kept in <game folder>/wnav_map.bin and read with the maps in WNAV_MAPS (';' separated, the other bots' and the shared one): it is
learnt once and shared (the orchestrator merges the bots' maps). Doors open and close: a cell whose floor is seen with the band above it
empty takes that newer view."""
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
REC = struct.Struct('<hhfQB')  # cell x, cell y, floor z, height mask, flags


def wrap(a): return (a + math.pi) % (2 * math.pi) - math.pi
def cell_of(x, y): return (int(math.floor(x / CELL)), int(math.floor(y / CELL)))
def centre(c): return ((c[0] + 0.5) * CELL, (c[1] + 0.5) * CELL)
BAND_MASK = sum(1 << b for b in range(int(BAND[0] / BIN), int(math.ceil(BAND[1] / BIN))))
HEAD_MASK = (1 << int(math.ceil(BAND[1] / BIN))) - 1      # the bins above a surface the player's body needs free (shifted to start one bin above it)


class Map:
    """cell -> [floor z, mask of the height bins above the floor that hold something, flags]"""
    def __init__(self): self.cells = {}; self.reached = False; self.soft = set(); self.covered = False      # reached: the last plan got to the target (else only near it)

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
        b0 = int((feet - rec[0]) / BIN) if feet >= rec[0] - BIN else None
        if b0 is not None and b0 >= 0: rec[1] &= ~(HEAD_MASK << (b0 + 1)) & 0xFFFFFFFFFFFFFFFF      # the player's body is here: nothing in the way above the feet
        self.cells[c] = self.add_point(rec, feet)

    def bump(self, x, y):
        c = cell_of(x, y); old = self.cells.get(c)
        if old is None: self.cells[c] = [-1e4, 0, BUMPED]
        elif not old[2] & WALKED: old[2] |= BUMPED

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

    def steps(self, c, fu):
        """Where a step from height fu into cell c can land: [(floor, cost factor)]. Every surface within a step (cost 1: stairs go up
        while the ground beside them stays, both are kept), else the soft floor (see walkable); [] when it cannot; None: never seen."""
        r = self.cells.get(c)
        if r is None: return None
        if self.covered and r[0] > fu + BAND[1] and not r[2] & BUMPED: return None      # under cover (a tunnel, a roof) all that is known there may be above it (the ground over the tunnel, seen from the air): down here it is not seen yet
        surf = self.surfaces(c)
        if surf == [None]: return [(fu, 1.0)]
        walked = r[2] & WALKED
        fit = [(z, 1.0) for z in surf if (z > SEA or walked) and -DOWN <= z - fu <= UP]
        if fit: return fit
        if -DOWN <= r[0] - fu <= UP and r[0] > SEA: return [(r[0], SOFT)]
        return []

    def plan(self, a, b, limit=120000, margin=40.0, avoid=frozenset()):
        """Cells from a's to b's along walkable ground (A* over cell and level), else to the reachable cell nearest b, or None.
        a, b: (x, y, z of the eye). `avoid`: cells not to enter (bumped on this walk)."""
        sa, sb = cell_of(a[0], a[1]), cell_of(b[0], b[1]); fb = b[2] - EYE
        lo = (min(sa[0], sb[0]) - margin / CELL, min(sa[1], sb[1]) - margin / CELL); hi = (max(sa[0], sb[0]) + margin / CELL, max(sa[1], sb[1]) + margin / CELL)
        def key(c, z): return (c[0], c[1], int(round(z / BIN)))
        memo = {}
        def steps(c, fu):      # (the same cell is asked about from many neighbours: its surfaces are worked out once a plan)
            k = (c, round(fu, 3))
            if k not in memo: memo[k] = self.steps(c, fu)
            return memo[k]
        s0 = key(sa, a[2] - EYE); floor = {s0: a[2] - EYE}
        r0 = self.cells.get(sa)      # the player is under cover: something is known above its head where it stands (out in the open a surface that high is a plateau)
        self.covered = bool(r0) and r0[0] > -1e3 and any(r0[0] + b * BIN > a[2] - EYE + BAND[1] for b in range(64) if r0[1] >> b & 1)
        g = {s0: 0.0}; prev = {}; heap = [(0.0, s0)]; n = 0; best, best_h = s0, math.hypot(sa[0] - sb[0], sa[1] - sb[1])
        def back(u):
            out = [u]
            while out[-1] in prev: out.append(prev[out[-1]])
            out.reverse(); self.soft = set()      # the soft cells this way goes through knowingly (see blocked_along)
            for p_, k_ in zip(out, out[1:]):
                st = self.steps((k_[0], k_[1]), floor[p_]) or []
                if any(abs(z - floor[k_]) < 1e-6 and c_ == SOFT for z, c_ in st if z is not None): self.soft.add((k_[0], k_[1]))
            return [(k[0], k[1]) for k in out]
        while heap and n < limit:
            _, u = heapq.heappop(heap); n += 1
            fu = floor[u]
            if abs(u[0] - sb[0]) <= 1 and abs(u[1] - sb[1]) <= 1 and abs(fu - fb) < 2.0: self.reached = True; return back(u)
            hu = math.hypot(u[0] - sb[0], u[1] - sb[1])
            if hu < best_h: best, best_h = u, hu
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    if not dx and not dy: continue
                    v = (u[0] + dx, u[1] + dy)
                    if not (lo[0] <= v[0] <= hi[0] and lo[1] <= v[1] <= hi[1]) or v in avoid: continue
                    if dx and dy:      # no corner cutting past a cell that cannot be stepped onto
                        if any(st == [] for st in (steps((u[0] + dx, u[1]), fu), steps((u[0], u[1] + dy), fu))): continue
                    st = steps(v, fu); step = 1.4142 if dx and dy else 1.0
                    if st is None: opts = [(fu, 3.0)]      # never seen: walkable, dearer
                    else:
                        r = self.cells[v]
                        opts = [(z, 40.0 if r[2] & BUMPED else (0.8 if r[2] & WALKED else k)) for z, k in st]
                    for fv, cost in opts:
                        kv = key(v, fv); nd = g[u] + step * cost
                        if nd < g.get(kv, 1e18):
                            g[kv] = nd; prev[kv] = u; floor[kv] = fv
                            heapq.heappush(heap, (nd + HW * math.hypot(v[0] - sb[0], v[1] - sb[1]), kv))
        # no way to the target itself (it stands inside something, or the map does not reach it yet): the way to the reachable cell nearest
        # to it, when that is nearer than here by a metre or more (from there the next look may show more)
        self.reached = False
        if best_h < math.hypot(sa[0] - sb[0], sa[1] - sb[1]) - 1.0 / CELL: return back(best)
        return None

    def blocked_along(self, q, cells, dist=8.0):
        """Is a planned way (its cells, from the one the player at eye position q is in, up to `dist` m) no longer walkable on the map?
        The levels it could be on are carried along as in plan (stairs and the ground beside them). A cell that is now soft but was not
        chosen soft by the plan (a door shut, a wall seen) counts as blocked: planning again may still go through it, knowingly."""
        floors = {round(q[2] - EYE, 2)}; run = 0.0
        for p_, c in zip(cells, cells[1:]):
            nxt = set()
            for f in floors:
                st = self.steps(c, f)
                if st and c not in self.soft: st = [q_ for q_ in st if q_[1] != SOFT]
                nxt |= {f} if st is None else {round(z if z is not None else f, 2) for z, _ in st}
            if not nxt: return True
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

    def look(self):
        """One depth picture into the map. False: the proxy did not answer."""
        try: before = os.path.getmtime(self.depth)
        except OSError: before = 0
        self.c.send(['depthdump']); t0 = time.monotonic()
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
        self.map.integrate(pts); self.looks += 1
        q = self.pos(); self.map.stand(q[0], q[1], q[2] - EYE)
        return True

    def look_around(self):
        self.key('W', False)
        for _ in range(4): self.look(); self.turn(math.pi / 2); time.sleep(0.05)

    def note(self, **kv):
        if self.trace: self.trace.write(json.dumps(dict(t=round(time.monotonic(), 2), **kv)) + '\n'); self.trace.flush()

    def plan(self, target):
        q = self.pos(); cells = self.map.plan(q, target, avoid=self.avoid)
        if cells is None: return None
        self.replans += 1; self.cells, self.ci = cells, 0
        way = simplify(cells, target[2]); self.note(plan=[[round(w[0], 1), round(w[1], 1)] for w in way], at=[round(v, 2) for v in q]); return way

    def ahead_blocked(self, dist=8.0):
        """Is the plan's way ahead (its own cells, from the one nearest the player) no longer walkable?"""
        q = self.pos(); cells = self.cells
        if not cells: return False
        near = min(range(self.ci, min(len(cells), self.ci + 16)), key=lambda i: math.dist(centre(cells[i]), q[:2]))
        self.ci = near; return self.map.blocked_along(q, cells[near:], dist)

    def go(self, target, tol=0.8, seconds=240, ztol=2.0):
        """Walk to `target` (x, y, z: the eye there) until within `tol` across and `ztol` in height. Returns a dict like the JSON line."""
        t0 = time.monotonic(); deadline = t0 + seconds
        self.c.send(['hover off']); self.pitch_to(-0.25)
        start = self.pos(); last = noted = start; self.note(go=list(target), at=list(start)); self.look_around()
        stucks = int(os.environ.get('WNAV_STUCKS') or 12)
        ok, why, way, k = False, 'out of time', None, 0
        last_look = 0.0; moved_at, moved_t = start, time.monotonic(); near_d, near_t = math.dist(start[:2], target[:2]), time.monotonic()
        while time.monotonic() < deadline:
            q = self.pos(); self.walked += math.dist(q[:2], last[:2])
            if math.dist(q[:2], noted[:2]) > 1.0: self.note(pos=[round(q[0], 2), round(q[1], 2), round(q[2], 2)]); noted = q
            last = q
            if math.dist(q[:2], target[:2]) < tol and abs(q[2] - target[2]) < ztol: ok, why = True, ''; break
            if way is None or k >= len(way):
                way, k = self.plan(target), 0
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
                y = self.yaw(); again = sum(1 for s in self.stuck_at if math.dist(s, q[:2]) < 3.0); self.stuck_at.append(q[:2])
                for r in (0.45, 0.75, 1.05, 1.35):      # the cells ahead (wider each time it is stuck here again)
                    for side in range(-again, again + 1):
                        x = q[0] + r * math.cos(y) - side * CELL * math.sin(y); yy = q[1] + r * math.sin(y) + side * CELL * math.cos(y)
                        self.map.bump(x, yy); self.avoid.add(cell_of(x, yy))
                self.note(stuck=[round(q[0], 2), round(q[1], 2)], again=again)
                self.log('stuck %d at %.1f %.1f: bumped, planning again' % (self.stuck, q[0], q[1]))
                self.key('W', False); self.c.send(['key S down'], 0.4); self.c.send(['key S up'])
                self.look_around(); way = None
            time.sleep(0.02)
        self.release()
        try: self.map.save(self.map_path)
        except OSError: pass
        return {'ok': ok, 'why': why, 'seconds': round(time.monotonic() - t0, 1), 'walked': round(self.walked, 1), 'stuck': self.stuck,
                'replans': self.replans, 'looks': self.looks, 'from': [round(v, 2) for v in start], 'to': [round(v, 2) for v in self.pos()]}


if __name__ == '__main__':
    from wcontrol import Control
    exec(compile(open(os.path.join(HERE, 'wlib.py')).read(), 'wlib.py', 'exec'))
    pid, gdir = int(sys.argv[1]), sys.argv[2]
    target = tuple(float(v) for v in sys.argv[3:6]); tol = float(sys.argv[6]) if len(sys.argv) > 6 else 0.8
    w = Walker(attach(pid), Control(gdir), gdir, log=lambda m: print(m, file=sys.stderr, flush=True))
    out = w.go(target, tol, float(os.environ.get('WNAV_SECONDS') or 240))
    print(json.dumps(out), flush=True); sys.exit(0 if out['ok'] else 1)
