"""Game-free checks of the walking map and planner (tools/win/wnav.py) on made-up scenes, with a mutant for each protection.
Usage: python3 tools/test_wnav.py      (prints one line per check and per mutant; exit 0 when all checks pass and every mutant is rejected)

Scenes are built from points as a depth picture would give them (0.25 m apart):
 - wall:     flat ground, a 3 m wall across it with a 2 m gap: the way goes through the gap (scenes reach past the search margin, else
             the plan goes round the end of a wall through cells never seen, as it should in the game);
 - cliff:    a 3 m step up with stairs at one end: the way climbs the stairs, never the cliff face;
 - cliff2:   the same step with no stairs: never climbed;
 - walkway:  a deck 2 m above the ground: walking along it stays on the deck (two levels in one cell); the ground under its far end
             is not reached from it (no way down);
 - tunnel:   under cover (something above the head where the player stands), cells known only far overhead are not seen yet, not
             blocked (the ground over the tutorial tunnel, seen from the air); in the open the same thing is a plateau;
 - door:     a closed door blocks a corridor; a later look sees the floor there with the band above it empty: it is open;
 - bump:     cells bumped into on this walk are not entered again;
 - foliage:  the cell the player stands in is walkable whatever was seen in it;
 - depth:    a depth picture of flat ground decodes to points on the ground."""
import math, os, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CHECK = r'''
import json, math, os, struct, sys, tempfile
sys.path.insert(0, D); import wnav
E = wnav.EYE
def ground(m, x0, x1, y0, y1, z):
    pts = []; x = x0
    while x <= x1:
        y = y0
        while y <= y1: pts.append((x, y, z)); y += 0.25
        x += 0.25
    return pts
def wall(x, y0, y1, z0, z1):
    pts = []; y = y0
    while y <= y1:
        z = z0
        while z <= z1: pts.append((x, y, z)); z += 0.25
        y += 0.25
    return pts
def cells_x(path, x): return [wnav.centre(c)[1] for c in path if abs(wnav.centre(c)[0] - x) < 0.3]
fails = []
def check(name, ok, why=''):
    print('%-8s %s %s' % (name, 'ok' if ok else 'FAILED', why)); ok or fails.append(name)

# wall with a gap at y 12..14
m = wnav.Map(); m.integrate(ground(m, -5, 25, -15, 30, 0.0) + wall(10.1, -15, 11.9, 0.25, 3.0) + wall(10.1, 14.1, 30, 0.25, 3.0))
p = m.plan((5, 5, E), (15, 5, E), margin=12); ys = cells_x(p or [], 10.25)
check('wall', p is not None and ys and all(12 <= y <= 14 for y in ys), 'crossed at y %s' % ys)

# cliff: ground 0 for x < 10, plateau 3 for x >= 10, stairs 0.25 m a step (0.5 m treads) at y 16..18 from x 4 to 10
pts = ground(m, -5, 9.9, -15, 30, 0.0) + ground(m, 10, 25, -15, 30, 3.0) + wall(10.0, -15, 12.9, 0.25, 3.0) + wall(10.0, 15.1, 30, 0.25, 3.0)
for k in range(12): pts += ground(m, 4 + k * 0.5, 4 + k * 0.5 + 0.49, 13, 15, (k + 1) * 0.25)
m = wnav.Map(); m.integrate(pts)
p = m.plan((5, 5, E), (15, 5, 3 + E), margin=12); xs = [wnav.centre(c) for c in (p or []) if 9.5 <= wnav.centre(c)[0] <= 10.5]
check('cliff', p is not None and xs and all(12.5 <= y <= 15.5 for _, y in xs), 'reached the plateau at %s' % [round(y, 1) for _, y in xs])
check('ahead', p is not None and not m.blocked_along((5, 5, E), p, 99), 'the way up the stairs reads as blocked')
m = wnav.Map(); m.integrate(ground(m, -5, 9.9, -15, 30, 0.0) + ground(m, 10, 25, -15, 30, 3.0) + wall(10.0, -15, 30, 0.25, 3.0))      # no stairs
p = m.plan((5, 5, E), (15, 5, 3 + E), margin=12); top = [c for c in (p or []) if wnav.centre(c)[0] > 10.5]
check('cliff2', not top and not m.reached, 'climbed the cliff: %d cells on the plateau' % len(top))

# walkway: deck at 2 m over ground 0, along x 0..20 at y 9..11
m = wnav.Map(); m.integrate(ground(m, 0, 20, 0, 20, 0.0) + ground(m, 0, 20, 9, 11, 2.0))
p = m.plan((1, 10, 2 + E), (19, 10, 2 + E))
check('walkway', p is not None and wnav.centre(p[-1])[0] > 18, 'no way along the deck' if p is None else 'ended at %.1f' % wnav.centre(p[-1])[0])
p = m.plan((1, 10, 2 + E), (19, 10, E))      # under the deck's far end: no way down, so not there
check('level', not m.reached, 'took the deck above for the ground under it')

# tunnel: from the air only the ground 4 m above a tunnel is known; the player is in the tunnel: it is not seen yet, not blocked
m = wnav.Map(); m.integrate(ground(m, -5, 25, -5, 25, 4.0))
p = m.plan((2, 10, E), (18, 10, E), margin=5)
check('tunnel', p is not None and m.reached, 'the tunnel reads as blocked by the ground above it')
m.integrate(ground(m, -5, 25, -5, 25, 0.0)); m2 = wnav.Map(); m2.integrate(ground(m2, -5, 9.9, -5, 25, 0.0) + ground(m2, 10, 25, -5, 25, 3.0))
p = m2.plan((2, 10, E), (18, 10, E), margin=5)      # out in the open the same surface overhead is a plateau, not a tunnel
check('open', not m2.reached, 'walked under a plateau as if it were a tunnel')

# door: corridor x 0..20, y 9..11 between walls; a door at x 10; later seen open
side = wall(0, 0, 0, 0, 0)
corr = ground(m, 0, 20, 9, 11, 0.0) + [(x / 4, 8.9, z / 4) for x in range(81) for z in range(1, 13)] + [(x / 4, 11.1, z / 4) for x in range(81) for z in range(1, 13)]
door = [(10.1, 9 + y / 4, z / 4) for y in range(9) for z in range(1, 10)]
m = wnav.Map(); m.integrate(corr + door); shut = m.walkable(wnav.cell_of(10.1, 10), 0.0)
m.integrate(ground(m, 9.5, 10.5, 9.5, 10.5, 0.0)); opened = m.walkable(wnav.cell_of(10.1, 10), 0.0)
check('door', shut[2] > 1 and opened[0] and opened[2] == 1, 'shut %s, after the open look %s' % (shut, opened))
p = m.plan((2, 10, E), (18, 10, E)); free = m.blocked_along((2, 10, E), p or [], 99)
m.integrate([(10.1 + dx / 4, 9 + y / 4, z / 4) for dx in (0, 1) for y in range(9) for z in range(1, 13)] + [(10.1 + dx / 4, 9 + y / 4, 3.5) for dx in (0, 1) for y in range(9)])      # it shuts again (a 3 m slab)
check('shut', p is not None and not free and m.blocked_along((2, 10, E), p, 99), 'way through the open door free: %s, after it shut blocked: %s' % (not free, m.blocked_along((2, 10, E), p, 99)))

# bump: the straight way is free, but bumped cells on this walk are avoided
m = wnav.Map(); m.integrate(ground(m, -5, 25, -15, 30, 0.0)); avoid = set()
for y in range(-30, 41): m.bump(10.1, y * 0.5); avoid.add(wnav.cell_of(10.1, y * 0.5))      # (a detour dearer than a bumped cell)
p = m.plan((5, 5, E), (15, 5, E), avoid=avoid, margin=20); ys = cells_x(p or [], 10.25)
check('bump', p is not None and ys and all(y > 20 for y in ys), 'crossed at y %s' % ys)

# foliage: where the player stands is walkable, whatever fills the band there
m = wnav.Map(); m.integrate(ground(m, 0, 4, 0, 4, 0.0) + [(2.1, 2.1, z / 4) for z in range(1, 7)])
before = m.walkable(wnav.cell_of(2.1, 2.1), 0.0); m.stand(2.1, 2.1, 0.0); after = m.walkable(wnav.cell_of(2.1, 2.1), 0.0)
check('stand', before[2] > 1 and after[0] and after[2] == 1, 'before %s after %s' % (before, after))

# depth: a picture of flat ground at z 0 from a camera 1.65 m up, looking 0.3 rad down
W, H, near, cam = 64, 36, 0.06, (3.0, -2.0, 1.65, 0.7, -0.3, 54.0, 16 / 9)
cx, cy, cz, th, ph, vf, asp = cam
fwd = (math.cos(ph) * math.cos(th), math.cos(ph) * math.sin(th), math.sin(ph)); rt = (math.sin(th), -math.cos(th), 0.0)
up = (-math.sin(ph) * math.cos(th), -math.sin(ph) * math.sin(th), math.cos(ph)); tv = math.tan(math.radians(vf) / 2); tu = tv * asp
px = []; want = []
for j in range(H):
    for i in range(W):
        nx = (2 * (i + 0.5) / W - 1) * tu; ny = (1 - 2 * (j + 0.5) / H) * tv
        dz = fwd[2] + ny * up[2]; t = -cz / dz if dz < -1e-6 else 99
        px.append(near / t if t < 25 else 0.0)
        if t < 25: want.append((cx + t * (fwd[0] + nx * rt[0] + ny * up[0]), cy + t * (fwd[1] + nx * rt[1] + ny * up[1])))
data = struct.pack('<3I', 0x31444457, W, H) + struct.pack('<8f', cx, cy, cz, th, ph, vf, asp, near) + struct.pack('<%df' % len(px), *px)
c_, pts = wnav.depth_points(data, 1)
err = max((math.dist(q[:2], w) for q, w in zip(pts, want)), default=9)
check('depth', len(pts) == len(want) > 200 and max(abs(z) for _, _, z in pts) < 0.02 and err < 0.02, '%d points, worst height %.3f, worst place %.3f m' % (len(pts), max((abs(z) for _, _, z in pts), default=9), err))

# deep: junk points 25 m under the ground (a third of the town's cells had a floor at -15 m) must not hide the ground the player walks on
junk = ground(None, -5, 25, -5, 25, -15.0)
m = wnav.Map(); m.integrate(junk)
p = m.plan((2, 10, 10 + E), (18, 10, 10 + E), margin=5)
check('deep', p is not None and m.reached, 'the level 25 m above the junk reads as blocked')
m.integrate(ground(None, -5, 25, -5, 25, 10.0)); top = m.surfaces(wnav.cell_of(5.1, 5.1))
check('rebase', 10.0 in [round(z, 2) for z in top or [] if z is not None], 'the ground seen above the junk was dropped: %s' % top)
m = wnav.Map(); m.integrate(junk); m.stand(5.1, 5.1, 10.0); top = m.surfaces(wnav.cell_of(5.1, 5.1))
check('standhi', any(abs(z - 10.0) < 0.3 for z in top or [] if z is not None), 'standing 25 m above the junk is not kept: %s' % top)
d_ = tempfile.mkdtemp(); a_, b_ = os.path.join(d_, 'a.bin'), os.path.join(d_, 'b.bin')
m = wnav.Map(); m.integrate(junk); m.save(a_); m = wnav.Map(); m.integrate(ground(None, -5, 25, -5, 25, 10.0)); m.save(b_)
m = wnav.Map(); m.load(a_); m.load(b_); top = m.surfaces(wnav.cell_of(5.1, 5.1))
check('loadhi', any(abs(z - 10.0) < 0.3 for z in top or [] if z is not None), 'merging maps lost the ground above the junk: %s' % top)

# stairs: 0.45 m a step on 0.5 m treads, with the surfaces read up to 0.24 m low on every other tread (they are bin starts): climbed
pts = ground(None, -5, 3.9, -15, 30, 0.0) + ground(None, 10, 25, -15, 30, 5.4)
for k in range(12): pts += ground(None, 4 + k * 0.5, 4 + k * 0.5 + 0.49, -15, 30, (k + 1) * 0.45 - (0.24 if k % 2 else 0.0))
m = wnav.Map(); m.integrate(pts)
p = m.plan((2, 5, E), (15, 5, 5.4 + E), margin=8)
check('stairs', p is not None and m.reached, 'the steep stairs read as a wall')
# carry: stairs of 0.5 m beside the ground (the ground stays a level beside the first tread): the way up must not read as blocked
pts = ground(None, -5, 9.9, -15, 30, 0.0) + ground(None, 10, 25, -15, 30, 6.0) + wall(10.0, -15, 12.9, 0.25, 6.0) + wall(10.0, 15.1, 30, 0.25, 6.0)
for k in range(12): pts += ground(None, 4 + k * 0.5, 4 + k * 0.5 + 0.49, 13, 15, (k + 1) * 0.5)
m = wnav.Map(); m.integrate(pts); p = m.plan((5, 5, E), (15, 5, 6.0 + E), margin=12)
check('carry', p is not None and m.reached and not m.blocked_along((5, 5, E), p, 99), 'the way up the 0.5 m stairs reads as blocked')
# ledge: a 0.75 m step on flat ground, flat on both sides, is not a stair
m = wnav.Map(); m.integrate(ground(None, -5, 9.9, -15, 30, 0.0) + ground(None, 10, 25, -15, 30, 0.75))
p = m.plan((5, 5, E), (15, 5, 0.75 + E), margin=8)
check('ledge', not m.reached, 'climbed a 0.75 m ledge')
# drop: a one-way drop (1 m down into a basin, out by stairs at its far side) is dearer than a short walk round it
pts = ground(None, -5, 25, -15, 30, 0.0)
pts = [q for q in pts if not (7 <= q[0] <= 13 and 1 <= q[1] <= 9)] + ground(None, 7, 13, 1, 9, -1.0)
pts = [q for q in pts if not (12 <= q[0] <= 13 and 1 <= q[1] <= 9)] + ground(None, 12, 12.49, 1, 9, -0.6) + ground(None, 12.5, 13, 1, 9, -0.25)
m = wnav.Map(); m.integrate(pts)
p = m.plan((5, 5, E), (15, 5, E), margin=12); inside = [c for c in (p or []) if 7.5 <= wnav.centre(c)[0] <= 11.5 and 1.5 <= wnav.centre(c)[1] <= 8.5]
check('drop', p is not None and m.reached and not inside, 'went down into the basin: %d cells in it' % len(inside))

# recorded walks: a person walked through a hedge (the band filled: soft) at y 5; the cells on their way are known-good, 2 m samples filled in
m = wnav.Map(); m.integrate(ground(None, -5, 25, -15, 30, 0.0) + [(x / 4, y / 4, z / 4) for x in range(38, 44) for y in range(-60, 121) for z in range(1, 8)])
rec_ = os.path.join(d_, 'session.jsonl')
with open(rec_, 'w') as f:
    f.write(json.dumps({'event': 'task', 'pose': [0, 0, 0, 0, 0]}) + '\n')
    for x in range(0, 21, 2): f.write(json.dumps({'event': 'sample', 'pose': [x + 0.1, 5.1, E, 0, 0]}) + '\n')
    f.write(json.dumps({'event': 'skip'}) + '\n'); f.write(json.dumps({'event': 'sample', 'pose': [-40, -40, E, 0, 0]}) + '\n')
ws = list(wnav.walks(wnav.load_recording(rec_)))
for w in ws: m.walk_line(w)
ks = [m.walkable(wnav.cell_of(x, 5.1), 0.0)[2] for x in (9.6, 10.1, 10.6)]
check('recorded', [len(w) for w in ws] == [11] and ks == [1.0, 1.0, 1.0], 'walks of %s points, cost through the hedge %s' % ([len(w) for w in ws], ks))
p = m.plan((2, 0, E), (18, 0, E), margin=10); ys = cells_x(p or [], 10.25)
check('recwalk', p is not None and ys and all(abs(y - 5.25) < 1.1 for y in ys), 'crossed the hedge at y %s, not on the recorded way' % ys)

# still: a depth picture is used only when the camera held still while it was taken
c0 = ((0.0, 0.0, 1.65), (0.5, -0.25))
check('still', wnav.still(c0, ((0.05, 0.0, 1.65), (0.505, -0.25))) and not wnav.still(c0, ((0.0, 0.0, 1.65), (0.6, -0.25))) and not wnav.still(c0, ((0.0, 0.0, 1.65), (0.5, -0.4))), 'a turning camera was taken for still')
# back out of a pocket: the way back along the trail, newest first, 10 m of it
trail = [(x * 1.0, 0.0, E) for x in range(30)]
bk = wnav.trail_back(trail, 10.0)
check('backout', len(bk) == 10 and bk[0][0] == 28.0 and bk[-1][0] == 19.0, 'back along the trail: %s' % [q[0] for q in bk])
check('pocket', wnav.pocket([(5, 5), (5.5, 5)], (5.2, 5.1)) and not wnav.pocket([(5, 5), (20, 5)], (5.2, 5.1)), 'stuck twice in one place is not a pocket')
# doors: the game says a door is open: the cells bumped at it while it was shut are free again
m = wnav.Map(); m.integrate(ground(None, -5, 25, -15, 30, 0.0)); avoid = set()
for y in range(-30, 61): m.bump(10.1, y * 0.5)
n_ = m.unbump_near([(10.1, 5.0)], 2.0)
check('doors', n_ >= 6 and not m.cells[wnav.cell_of(10.1, 5.1)][2] & wnav.BUMPED and m.cells[wnav.cell_of(10.1, 12.1)][2] & wnav.BUMPED, '%d unbumped' % n_)
# lvlfall: the target stands on an island 3 m up that nothing leads to; the low ground reaches within 2.3 m of it, a plateau up stairs
# (3 m up too) within 4.8 m: the way goes to the plateau, the target's level, not to the low cell nearest across
pts = ground(None, -5, 30, -15, 9.9, 0.0) + ground(None, -5, 9.9, 10, 20, 0.0) + ground(None, 17.3, 19.9, 10, 20, 0.0) + ground(None, 25.1, 30, 10, 20, 0.0) + ground(None, -5, 30, 20.1, 30, 0.0) + ground(None, 10, 17.2, 10, 20, 3.0) + ground(None, 20, 25, 10, 20, 3.0)      # (no ground under the island or the plateau)
for k in range(12): pts += ground(None, 4 + k * 0.5, 4 + k * 0.5 + 0.49, 14, 16, (k + 1) * 0.25)
m = wnav.Map(); m.integrate(pts)
p = m.plan((5, 15, E), (22, 15, 3 + E), margin=12); last = wnav.centre(p[-1]) if p else (0, 0)
check('lvlfall', p is not None and not m.reached and 10 < last[0] < 17.6 and abs(last[1] - 15) < 6, 'the plan ends at %s (on the low ground below the island if x > 17.6)' % (last,))
# hit: a way found blocked on a look is dear for the rest of the walk (a look from another side finds it open again, and the walker turned
# back at it again and again): the walker keeps the blocked cell and its neighbours, and the next plan goes round them
m = wnav.Map(); m.integrate(ground(None, -5, 25, -15, 30, 0.0)); w = object.__new__(wnav.Walker)
class P_:
    def camera(self): return ((5.0, 5.0, E), (0.0, 0.0))
w.p = P_(); w.map = m; w.hit = set(); w.dear = set(); w.avoid = set(); w.replans = 0; w.trace = None
first = m.plan((5, 5, E), (15, 5, E), margin=12); w.cells, w.ci = first, 0
m.integrate(wall(10.1, 3.0, 8.0, 0.25, 3.0))      # a look sees a wall across the way
blocked = w.ahead_blocked(99)
mid = [c for c in first if abs(wnav.centre(c)[0] - 10.25) < 0.3][0]
p = w.plan((15, 5, E)); xs = [wnav.centre(c) for c in w.cells if abs(wnav.centre(c)[0] - 10.25) < 0.3]
check('hit', blocked and mid in w.hit and xs and all(y < 3.0 or y > 8.1 for _, y in xs), 'blocked %s, %d cells kept dear, crossed the wall line at y %s' % (blocked, len(w.hit), [round(y, 1) for _, y in xs]))
# pressed: stuck against a long wall: all of it in reach is bumped (not only the cell ahead); the ground and what is behind are not
pts_ = ground(None, 0, 12, 0, 10, 0.0) + wall(10.1, 0, 10, 0.25, 3.0) + wall(8.0, 4.0, 6.0, 0.25, 3.0)
cs = wnav.pressed(pts_, (9.6, 5.0, E), 0.0); ys = sorted(wnav.centre(c)[1] for c in cs); xs_ = {wnav.centre(c)[0] for c in cs}
check('pressed', ys and ys[0] <= 1.3 and ys[-1] >= 8.7 and xs_ == {10.25}, 'bumped %d cells, y %s..%s, x %s' % (len(cs), ys[:1], ys[-1:], sorted(xs_)))
sys.exit(1 if fails else 0)
'''

MUTANTS = [      # (file text, replacement): each removes one protection
    ("and abs(across) <= side and feet + BAND[0]", "and abs(across) <= 0.3 and feet + BAND[0]"),      # only the cell ahead is bumped at a wall
    ("if -0.2 <= ahead <= reach and", "if ahead <= reach and"),      # what is behind counts as pressed against too
    ("self.hit |= {(c[0] + dx, c[1] + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1)}; return True", "return True"),      # a blocked way is forgotten at the next plan
    ("LEVEL = 1.0 ", "LEVEL = 0.0 "),      # the fallback goes to the cell nearest across, whatever its level
    ("return [z0 + b * BIN for b in range(64) if m >> b & 1 and not (m >> b) & BAND_MASK]", "return [z0 + b * BIN for b in range(64) if m >> b & 1]"),      # nothing in the way counts
    ("fit = [(z, 1.0) for z in surf if (z > SEA or walked) and -DOWN <= z - fu <= up]", "fit = [(z, 1.0) for z in surf[:1] if (z > SEA or walked) and -DOWN <= z - fu <= up]"),      # one level a cell (stairs over ground, decks)
    ("if -DOWN <= r[0] - fu <= UP and r[0] > SEA: return [(r[0], SOFT)]", "if r[0] > SEA: return [(r[0], SOFT)]"),      # a cliff face is soft, any height
    ("and abs(fu - fb) < 2.0: self.reached = True", ": self.reached = True"),      # the target's height not asked
    ("if abs(f[0] - old[0]) < 2 * BIN and not (f[1] & BAND_MASK): self.cells[c] = [f[0], f[1], old[2]]; continue", "pass"),      # an opened door stays shut
    ("or v in avoid: continue", ": continue"),
    ("if self.covered and r[0] > fu + BAND[1] and not r[2] & BUMPED: return None", "pass"),      # the ground over a tunnel blocks the tunnel
    ("if self.covered and r[0] > fu + BAND[1]", "if r[0] > fu + BAND[1]"),      # a plateau seen from the open is taken for a tunnel      # the ground over a tunnel blocks the tunnel      # bumped cells entered again
    ("if b0 is not None and b0 >= 0: rec[1] &= ~(HEAD_MASK << (b0 + 1)) & 0xFFFFFFFFFFFFFFFF", "pass"),      # where the player stands stays blocked
    ("nxt |= {f} if st is None else {round(z if z is not None else f, 2) for z, _ in st}", "nxt |= {f} if st is None else {round(z if z is not None else f, 2) for z, _ in sorted(st, key=lambda q_: abs((q_[0] if q_[0] is not None else f) - f))[:1]}"),      # one level carried
    ("if st and c not in self.soft: st = [q_ for q_ in st if q_[1] != SOFT]", "pass"),      # a door shut on the way is not seen
    ("up = (-math.sin(ph) * math.cos(th), -math.sin(ph) * math.sin(th), math.cos(ph))\n    tv", "up = (math.sin(ph) * math.cos(th), math.sin(ph) * math.sin(th), math.cos(ph))\n    tv"),      # the camera's up vector wrong
    ("if r[0] > -1e3 and fu - r[0] > WINDOW - BAND[1]: return None", "pass"),      # a level far above junk points is blocked by them
    ("if f[0] - old[0] > WINDOW - BAND[1]: self.cells[c] = [f[0], f[1], old[2]]; continue", "pass"),      # the ground seen above junk is dropped
    ("k = int((feet - 4.0 - rec[0]) / BIN); rec[0] += k * BIN; rec[1] >>= k", "pass"),      # standing above junk is not kept
    ("self.cells[c] = [z, m, old[2] | fl] if z > old[0] else rec; continue", "pass"),      # merging maps drops the higher level
    ("if z is None or z - fu <= UP or stair(u, fu, v, dx, dy, z)]", "if z is None or z - fu <= UP]"),      # no stair rule: steep stairs are walls
    ("if p_ is not None and fu - floor[p_] >= 0.1: return True\n            return any(", "return True\n            return any("),      # every 0.75 m ledge is a stair
    ("+ (DROP if fu - fv > UP else 0.0)", "+ 0.0"),      # a one-way drop costs nothing
    ("self.stand(a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1]), a[2] + t * (b[2] - a[2]) - EYE); n += 1", "self.stand(a[0], a[1], a[2] - EYE); n += 1"),      # recorded samples not filled in between
    ("elif r.get('event') in ('task', 'skip'): out.append(None)", "pass"),      # a teleport between tasks taken for a walk
    ("run += math.dist(a[:2], b[:2]); out.append(b)\n        if run >= metres: break", "out.append(b)"),      # backs out along all of the trail
    ("rec[2] &= ~BUMPED; n += 1", "n += 1"),      # an open door stays bumped
    ("return abs(wrap(y1 - y0)) <= ang and abs(q1 - q0) <= ang and", "return"),      # a look taken while turning is used
]


def run_with(folder):
    r = subprocess.run([sys.executable, '-c', 'D=%r\n' % folder + CHECK], capture_output=True, text=True, timeout=300)
    return r.returncode == 0, ' | '.join(l for l in (r.stdout + r.stderr).strip().splitlines() if 'FAILED' in l or 'Error' in l)[:300]


def main():
    src = os.path.join(HERE, 'win', 'wnav.py')
    r = subprocess.run([sys.executable, '-c', 'D=%r\n' % os.path.dirname(src) + CHECK], capture_output=True, text=True, timeout=300)
    print(r.stdout.strip()); print(r.stderr.strip()[-500:]) if r.returncode and r.stderr else None
    bad = r.returncode != 0; print('clean:', 'passed' if not bad else 'FAILED')
    text = open(src).read()
    for old, new in MUTANTS:
        if old not in text:
            print('mutant text not found: %s' % old[:70]); bad = True; continue
        with tempfile.TemporaryDirectory(prefix='wnav-mut-') as d:
            open(os.path.join(d, 'wnav.py'), 'w').write(text.replace(old, new, 1))
            ok, out = run_with(d)
            print('mutant %-70s %s (%s)' % (old[:70], 'rejected' if not ok else 'SURVIVED', out), flush=True); bad |= ok
    print('ALL OK' if not bad else 'FAILED'); return 1 if bad else 0


if __name__ == '__main__': sys.exit(main())
