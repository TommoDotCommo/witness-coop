#!/usr/bin/env python3
"""A 2D navigation simulator for the walker (tools/win/wnav.py): the real planner and the real walk loop against made-up worlds, no game.

usage: python3 tools/navsim.py list                 the named levels
       python3 tools/navsim.py run LEVEL [seed]     walk one level, print the WalkResult and a picture of the world and the trail
       python3 tools/navsim.py clutter SEED         the same on a random world
The world is flat ground with axis-aligned blockers typed by the classes a stuck is against (wnav.stuck_class):
  wall     solid, drawn in the depth picture as a flat face 3 m high                      (a stuck against it reads 'wall')
  foliage  drawn as scattered points 2.2 m high; passable at 0.6 of the speed, or solid when `solid` is set: a wall the map takes for a
           hedge, the cause of most real stucks                                           ('foliage')
  open     solid and NOT drawn, and the ground there is not drawn either: an invisible collision, a kerb, an unseen edge   ('open')
  shore    like open, the sea: no ground, no depth, impassable
  pit      impassable, the ground in it is drawn 6 m down (an honest drop: the planner sees it)
  door     a wall that opens when its flag is set (Sim.open_door(name), or at the second given in Level.opens)
The player is a round body (0.3 m) walking 1.8 m/s along the yaw, A/D sideways, S back; the mouse turns 0.0025 rad a count; a command takes
11 ms. Sim is the whole command surface the walker uses (camera(), send(), monotonic(), sleep()), and its clock is simulated: a walk of
240 s takes a second or two. The depth look is a ray march to the first blocker (a grid of rays, the points the same shape depth_points
gives wnav.Map.integrate). `SimWalker` is wnav.Walker with its depth source replaced; everything else is the walker's own code."""
import math, os, random, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, os.environ.get('NAVSIM_WIN') or os.path.join(HERE, 'win'))      # (NAVSIM_WIN: another copy of wnav.py: the mutants of test_navsim.py)
import wnav

SPEED, RADIUS, DT = 1.8, 0.3, 1 / 30
SOLID = ('wall', 'open', 'shore', 'pit', 'door')
WALL_H, FOLIAGE_H, PIT_DEPTH = 3.0, 2.2, -6.0
CLASSES = ('wall', 'foliage', 'open', 'shore', 'pit', 'door')


class Block:
    def __init__(self, x0, y0, x1, y1, cls, solid=None, name=''):
        assert cls in CLASSES, cls
        self.x0, self.y0, self.x1, self.y1 = min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1)
        self.cls, self.name, self.is_open = cls, name, False
        self.solid = (cls in SOLID) if solid is None else solid      # foliage: solid only when asked

    def blocks(self):
        return self.solid and not (self.cls == 'door' and self.is_open)

    def near(self, x, y, r):      # does a body of radius r at (x, y) touch the block?
        dx = max(self.x0 - x, 0.0, x - self.x1); dy = max(self.y0 - y, 0.0, y - self.y1); return dx * dx + dy * dy < r * r

    def inside(self, x, y): return self.x0 <= x <= self.x1 and self.y0 <= y <= self.y1


class Level:
    """name, blocks, start (x, y), goal (x, y), the expected outcome of walking it with the real walker:
    expect = ('arrives', seconds) or ('gives up', {reasons}, seconds): within that many simulated seconds."""
    def __init__(self, name, blocks, start, goal, expect, why='', opens=None, seconds=240.0, locked=False, tol=1.0, locks_at=None, yaw0=None):
        self.name, self.blocks, self.start, self.goal, self.expect, self.why = name, blocks, start, goal, expect, why
        self.opens = opens or {}      # door name -> second it opens
        self.seconds, self.locked, self.tol = seconds, locked, tol      # locked: the view does not turn (a puzzle holds the mouse)
        self.locks_at, self.yaw0 = locks_at, yaw0      # locks_at: the second the view stops turning; yaw0: the start yaw (default: facing the goal)


class Sim:
    """The player and the world, with the command surface of the walker's `p` (camera) and `c` (send) and its clock."""
    VFOV, ASPECT = 60.0, 16 / 9

    def __init__(self, level, seed=0, rays=(56, 32)):
        self.level, self.blocks, self.rng, self.rays = level, level.blocks, random.Random(seed), rays
        self.x, self.y = level.start; self.yaw = math.atan2(level.goal[1] - level.start[1], level.goal[0] - level.start[0]) if level.yaw0 is None else level.yaw0; self.pitch = -0.25
        self.t = 0.0; self.keys = set(); self.walked = 0.0; self.trail = [(self.x, self.y)]; self.bumps = 0; self.commands = 0; self.looks = 0

    # ---- the clock and the commands ---------------------------------------------------------------------------------------------
    def monotonic(self): return self.t
    def sleep(self, s): self.advance(s)
    def camera(self): return ((self.x, self.y, wnav.EYE), (self.yaw, self.pitch))
    def entity_table(self): return []      # (no doors the game says are open)

    def send(self, lines, settle=0.0):
        for line in lines:
            w = line.split(); self.commands += 1
            if w[0] == 'key' and len(w) == 3: (self.keys.add if w[2] == 'down' else self.keys.discard)(w[1])
            elif w[0] == 'mouse' and not self.level.locked and not (self.level.locks_at is not None and self.t >= self.level.locks_at):
                self.yaw = wnav.wrap(self.yaw - int(w[1]) * wnav.RAD_PER_COUNT); self.pitch = max(-0.93, min(1.2, self.pitch - int(w[2]) * wnav.RAD_PER_COUNT))
        self.advance(0.011 + settle)

    def open_door(self, name):
        for b in self.blocks:
            if b.cls == 'door' and b.name == name: b.is_open = True

    # ---- the body ------------------------------------------------------------------------------------------------------------------
    def hits(self, x, y): return any(b.blocks() and b.near(x, y, RADIUS) for b in self.blocks)

    def in_foliage(self): return any(b.cls == 'foliage' and not b.solid and b.inside(self.x, self.y) for b in self.blocks)

    def advance(self, dt):
        end = self.t + dt
        while self.t < end - 1e-9:
            h = min(DT, end - self.t); self.t += h
            for name, at in self.level.opens.items():
                if self.t >= at: self.open_door(name)
            vx = vy = 0.0
            if 'W' in self.keys: vx += math.cos(self.yaw); vy += math.sin(self.yaw)
            if 'S' in self.keys: vx -= math.cos(self.yaw); vy -= math.sin(self.yaw)
            if 'A' in self.keys: vx += math.cos(self.yaw + math.pi / 2); vy += math.sin(self.yaw + math.pi / 2)
            if 'D' in self.keys: vx += math.cos(self.yaw - math.pi / 2); vy += math.sin(self.yaw - math.pi / 2)
            n = math.hypot(vx, vy)
            if n < 1e-9: continue
            v = SPEED * (0.6 if self.in_foliage() else 1.0) * h / n; x0, y0 = self.x, self.y
            if not self.hits(self.x + vx * v, self.y): self.x += vx * v      # (axis by axis: the body slides along a wall)
            if not self.hits(self.x, self.y + vy * v): self.y += vy * v
            if (self.x, self.y) == (x0, y0): self.bumps += 1
            else:
                self.walked += math.dist((x0, y0), (self.x, self.y))
                if math.dist(self.trail[-1], (self.x, self.y)) > 0.5: self.trail.append((self.x, self.y))

    # ---- the depth look --------------------------------------------------------------------------------------------------------
    def depth(self):
        """World points of one picture: a grid of rays from the eye, each ending at the first thing it meets (what depth_points returns)."""
        self.looks += 1; rng = random.Random(self.looks * 7919 + 13)
        W, H = self.rays; ex, ey, ez = self.x, self.y, wnav.EYE; th, ph = self.yaw, self.pitch
        fwd = (math.cos(ph) * math.cos(th), math.cos(ph) * math.sin(th), math.sin(ph)); right = (math.sin(th), -math.cos(th), 0.0)
        up = (-math.sin(ph) * math.cos(th), -math.sin(ph) * math.sin(th), math.cos(ph))
        tv = math.tan(math.radians(self.VFOV) / 2); tu = tv * self.ASPECT; R = wnav.RANGE
        vis = [b for b in self.blocks if b.cls in ('wall', 'foliage', 'door') and (b.cls != 'door' or not b.is_open)
               and math.hypot(max(b.x0 - ex, 0, ex - b.x1), max(b.y0 - ey, 0, ey - b.y1)) < R + 1]
        holes = [b for b in self.blocks if b.cls in ('open', 'shore')]; pits = [b for b in self.blocks if b.cls == 'pit']
        pts = []
        for j in range(H):
            ny = (1 - 2 * (j + 0.5) / H) * tv
            for i in range(W):
                nx = (2 * (i + 0.5) / W - 1) * tu
                dx = fwd[0] + nx * right[0] + ny * up[0]; dy = fwd[1] + nx * right[1] + ny * up[1]; dz = fwd[2] + ny * up[2]
                tg = R + 1; ground = None
                if dz < -1e-6:
                    t_ = -ez / dz
                    if t_ < R:
                        gx, gy = ex + t_ * dx, ey + t_ * dy
                        if any(b.inside(gx, gy) for b in holes): tg = t_; ground = 'none'      # (nothing drawn there: the ray goes on into the dark)
                        else:
                            tg, ground = t_, (gx, gy, 0.0)
                            for b in pits:
                                if b.inside(gx, gy):
                                    t2 = (PIT_DEPTH - ez) / dz; px, py = ex + t2 * dx, ey + t2 * dy
                                    ground = (px, py, PIT_DEPTH) if t2 < R and b.inside(px, py) else 'none'; tg = t2 if ground != 'none' else tg
                tb, bb = R + 1, None
                for b in vis:      # slab test against the box [x0,x1] x [y0,y1] x [0,h]
                    h = WALL_H if b.cls != 'foliage' else FOLIAGE_H; lo, hi = 0.0, min(tg, R)
                    for o, d, a0, a1 in ((ex, dx, b.x0, b.x1), (ey, dy, b.y0, b.y1), (ez, dz, 0.0, h)):
                        if abs(d) < 1e-9:
                            if o < a0 or o > a1: hi = -1; break
                        else:
                            ta, tb_ = (a0 - o) / d, (a1 - o) / d
                            if ta > tb_: ta, tb_ = tb_, ta
                            lo, hi = max(lo, ta), min(hi, tb_)
                            if lo > hi: break
                    if lo <= hi and lo < tb:
                        if b.cls == 'foliage' and rng.random() < 0.3: continue      # (leaves have gaps)
                        tb, bb = lo, b
                if bb is not None and tb < tg:
                    if bb.cls == 'foliage': tb += rng.uniform(-0.35, 0.35)      # scattered in depth: the band is filled, not flat
                    pts.append((ex + tb * dx, ey + tb * dy, ez + tb * dz))
                elif ground not in (None, 'none'): pts.append(ground)
        return pts

    # ---- a picture -------------------------------------------------------------------------------------------------------------
    def render(self, cell=1.0, margin=4.0):
        xs = [v for b in self.blocks for v in (b.x0, b.x1)] + [self.level.start[0], self.level.goal[0]]; ys = [v for b in self.blocks for v in (b.y0, b.y1)] + [self.level.start[1], self.level.goal[1]]
        x0, x1, y0, y1 = min(xs) - margin, max(xs) + margin, min(ys) - margin, max(ys) + margin; ch = {'wall': '#', 'foliage': '"', 'open': '?', 'shore': '~', 'pit': 'v', 'door': 'D'}
        trail = {(int((x - x0) / cell), int((y - y0) / cell)) for x, y in self.trail}; rows = []
        for j in range(int((y1 - y0) / cell), -1, -1):
            row = ''
            for i in range(int((x1 - x0) / cell) + 1):
                x, y = x0 + (i + .5) * cell, y0 + (j + .5) * cell; c = '.'
                for b in self.blocks:
                    if b.inside(x, y): c = ch[b.cls] if not (b.cls == 'door' and b.is_open) else 'd'; break
                if (i, j) in trail and c in '.d': c = '*'
                if math.dist((x, y), self.level.start) < cell: c = 'S'
                if math.dist((x, y), self.level.goal) < cell: c = 'G'
                row += c
            rows.append(row)
        return '\n'.join(rows)


class SimWalker(wnav.Walker):
    """The real walker on the simulated world: only where the depth picture comes from differs."""
    def __init__(self, sim, folder, stall=None, log=lambda m: None):
        super().__init__(sim, sim, folder, log=log, clock=sim, stall=stall)

    def grab_depth(self):
        self.c.send(['depthdump']); return self.p.depth()


def walk(level, seed=0, stall=None, walker_cls=SimWalker, **go_kw):
    """Walk a level once. (WalkResult, Sim, walker); the simulated seconds are in result['seconds']."""
    sim = Sim(level, seed)
    with tempfile.TemporaryDirectory(prefix='navsim-') as d:
        w = walker_cls(sim, d, stall=stall)
        r = w.go((level.goal[0], level.goal[1], wnav.EYE), level.tol, level.seconds, ztol=1.8, **go_kw)
        w.tr.close()
    return r, sim, w


# ---- the levels ------------------------------------------------------------------------------------------------------------------------
def W(*a, **k): return Block(*a, 'wall', **k)


def open_field(): return Level('open_field', [], (0, 0), (30, 6), ('arrives', 40), 'nothing in the way')
def wide_wall(): return Level('wide_wall', [W(14, -14, 15, 14)], (0, 0), (30, 0), ('arrives', 120), 'a wall 28 m wide: the way round is long')
def wall_80(): return Level('wall_80', [W(14, -40, 15, 40)], (0, 0), (30, 0), ('gives up', {'stuck'}, 150), 'KNOWN GAP: a wall 80 m wide is soft for the planner (a wall is a cliff face of soft cells), it pushes through it stuck after stuck; the walk gives up with stuck_class wall', seconds=200)
def dead_end(): return Level('dead_end', [W(8, -7, 9, 7), W(8, 6, 16, 7), W(8, -7, 16, -6)], (0, 0), (13, 0), ('arrives', 100), 'a U of walls open away from the start, the target inside its base: the way is round the arms')
def room_far_door(): return Level('room_far_door', [W(10, -10, 30, -9), W(10, 9, 30, 10), W(10, -10, 11, 10), W(29, -10, 30, -3), W(29, 3, 30, 10)], (15, 0), (36, 0), ('arrives', 120), 'in a room whose only way out is the far side: the target is just outside the near wall', locked=False)
def zigzag():
    return Level('zigzag', [W(8, -12, 9, 6), W(16, -6, 17, 12), W(24, -12, 25, 6)], (0, 0), (32, 0), ('arrives', 150), 'walls with gaps at alternate ends')
def wall_gap(): return Level('wall_gap', [W(12, -30, 13, 6), W(12, 8, 13, 30)], (0, 0), (26, 0), ('arrives', 90), 'a wall with one 2 m gap off the line')
def pit(): return Level('pit', [Block(10, -12, 20, 12, 'pit')], (0, 0), (30, 0), ('arrives', 100), 'a deep pit across the way: seen, so the way goes round it')
def pillars():
    return Level('pillars', [W(x, y, x + 1, y + 1) for x in range(6, 40, 5) for y in range(-20, 21, 5)], (0, 0), (40, 3), ('arrives', 100), 'pillars every 5 m')
def narrow_door(): return Level('narrow_door', [W(12, -30, 13, -0.6), W(12, 0.6, 13, 30)], (0, 0), (26, 0), ('arrives', 90), 'a gap 1.2 m wide (the body is 0.6 m)')
def closed_room():
    return Level('closed_room', [W(14, -8, 28, -7), W(14, 7, 28, 8), W(14, -8, 15, 8), W(27, -8, 28, 8)], (0, 0), (21, 0), ('gives up', {'no way', 'stuck'}, 110), 'the target is in a room with no door: the walk should give up, not wander', seconds=150)
def shore_bay():
    """The shore round (0, 200) in the real island: land bends round a bay of water. Straight ahead is sea, which has no depth (unseen cells: the planner takes
    them for walkable at 3 times the cost), with an invisible wall at the shore line; the land way goes round the bay."""
    return Level('shore_bay', [Block(8, -14, 36, 18, 'shore'), Block(36, -14, 40, 22, 'shore')], (0, 0), (46, 6), ('arrives', 150), 'a bay of water across the straight way: invisible at the shore line')
def hedge_pocket():
    """A hedge pocket: a U of solid foliage (drawn scattered, so the map takes it for soft cells it can walk) with the target behind its base and the player
    inside; the base is thin, so walking through looks cheaper than the way out and round. Bumping the cell ahead is not enough: the wall rule bumps the
    hedge on either side."""
    return Level('hedge_pocket', [Block(10, -9, 10.6, 9, 'foliage', solid=True), Block(-6, 8, 10.6, 9, 'foliage', solid=True), Block(-6, -9, 10.6, -8, 'foliage', solid=True)], (2, 0), (18, 0), ('arrives', 150), 'solid hedge on three sides: a pocket')
def hedge_long():
    """A solid hedge 64 m long and one cell thick, the target straight behind it: crossing it looks cheaper (one soft cell) than the way round, so the
    walk is stuck against it again and again. Only the wall rule (all of the hedge in sight bumped at once) gets round it in time."""
    return Level('hedge_long', [Block(14, -32, 14.5, 32, 'foliage', solid=True)], (0, 0), (30, 0), ('arrives', 120), 'a long thin solid hedge: the wall rule', seconds=240)
def locked_view(): return Level('locked_view', [], (0, 0), (20, 0), ('gives up', {'view does not turn'}, 6), 'a puzzle holds the mouse: the view does not turn, the walk says so at once', locked=True, seconds=60, yaw0=math.pi)
def locked_mid(): return Level('locked_mid', [], (0, 0), (20, 0), ('gives up', {'view does not turn'}, 5), 'the view stops turning after the walk began: within 2 s of the turn that did not happen', locks_at=0.9, seconds=60, yaw0=math.pi)
def ledge_spot():
    """A panel's standing place on a ledge: a strip of ground (the ledge) with pits and an invisible collision along its edges, reached by a 1.5 m causeway."""
    return Level('ledge_spot', [Block(10, -20, 20, 20, 'pit'), Block(20, -20, 40, -2, 'pit'), Block(20, 2, 40, 20, 'pit'), Block(10, -20, 11, -0.8, 'open'), Block(10, 0.8, 11, 20, 'open')], (0, 0), (36, 0), ('arrives', 150), 'a causeway across a pit to the ledge the panel stands on')
LEVELS = [open_field, wide_wall, wall_80, dead_end, room_far_door, zigzag, wall_gap, pit, pillars, narrow_door, closed_room, shore_bay, hedge_pocket, ledge_spot, hedge_long, locked_view, locked_mid]
BY_NAME = {f.__name__: f for f in LEVELS}


def clutter(seed, n=14, size=40.0):
    """A random world: n blockers of random class and size between a start at the west and a goal at the east edge, none on the start or goal.
    `reachable` says whether a body can get from the start to the goal at all (a breadth-first search on a 0.25 m grid)."""
    rng = random.Random(seed); start, goal = (0.0, 0.0), (size, rng.uniform(-6, 6)); blocks = []
    for _ in range(n):
        cls = rng.choice(['wall', 'wall', 'foliage', 'foliage', 'open', 'shore', 'pit']); x = rng.uniform(4, size - 4); y = rng.uniform(-size / 2, size / 2)
        w, h = rng.uniform(1, 12), rng.uniform(1, 12)
        b = Block(x, y, x + w, y + h, cls, solid=(rng.random() < 0.4) if cls == 'foliage' else None)
        if not (b.near(*start, 3.0) or b.near(*goal, 3.0)): blocks.append(b)
    lv = Level('clutter%d' % seed, blocks, start, goal, ('arrives', 200), 'random', seconds=240.0); lv.reachable = reachable(lv); return lv


def reachable(level, step=0.25, lim=60.0):
    seen = {(0, 0)}; todo = [(0, 0)]; gx, gy = round(level.goal[0] / step), round(level.goal[1] / step)
    while todo:
        i, j = todo.pop()
        if math.hypot(i * step - level.goal[0], j * step - level.goal[1]) < level.tol: return True
        for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (i + di, j + dj)
            if n in seen or abs(n[0] * step) > lim or abs(n[1] * step) > lim: continue
            if any(b.blocks() and b.near(n[0] * step, n[1] * step, RADIUS) for b in level.blocks): continue
            seen.add(n); todo.append(n)
    return False


def main(argv):
    if argv[:1] == ['list']:
        for f in LEVELS: lv = f(); print('%-14s %s' % (lv.name, lv.why))
        return 0
    if argv[:1] in (['run'], ['clutter']) and len(argv) >= 2:
        lv = BY_NAME[argv[1]]() if argv[0] == 'run' else clutter(int(argv[1]))
        r, sim, w = walk(lv, int(argv[2]) if len(argv) > 2 else 0)
        print(sim.render()); print({k: r[k] for k in ('ok', 'why', 'detail', 'distance_left', 'stuck_class', 'tried', 'seconds', 'walked', 'stuck', 'replans', 'backouts')}); return 0 if r['ok'] else 1
    print(__doc__); return 2


if __name__ == '__main__': sys.exit(main(sys.argv[1:]))
