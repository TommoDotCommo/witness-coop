#!/usr/bin/env python3
"""Walking-map coverage of the credits route: is every panel's approach point connected on the map to the start area?

usage: python3 tools/nav_coverage.py [MAP.bin] [--panels panels.json] [--root x,y,z] [--strict] [--list] [--json OUT] [--recordings SESSION.jsonl ...]
       python3 tools/nav_coverage.py selftest        game-free check on a made-up island, with a mutant that must be reported

For each of the 238 panels of data/credits_route.json: its approach point is the stand pose a person taught (data/taught.json) where there is
one, else 1.4 m out from the panel along its facing (the manifest's x, y, z, rz; the eye is level with the panel). It counts as covered when
a cell of the map within RADIUS m of that point (and within HEIGHT m of its floor) can be reached from the start area by flooding the map
with the walker's own step rules (tools/win/wnav.py: Map.steps, up/down/stairs, no corner cutting). Cells never seen are NOT walkable here
(the planner may cross a few of them at triple cost; coverage asks what the map knows). Soft cells (bushes, a wall one cell thick) are crossed
unless --strict. Per area it prints covered / total, then the panels that are cut off, and the share of the route's panels reached.
The start area is the approach of the first tutorial panel (0x64) unless --root x,y,z (feet) is given. The manifest is the newest
panels.json under the lab's run folders unless --panels is given. Exit 0 always (it measures); selftest exits 1 on failure."""
import glob, json, math, os, sys, collections
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(HERE, 'win')); sys.path.insert(0, os.path.join(HERE, 'playthrough'))
import wnav
from wnav import CELL, EYE, UP, STAIR, SOFT, BIN, Map, cell_of

DATA = os.path.join(HERE, 'playthrough', 'data')
RADIUS, HEIGHT, OUT = 1.0, 1.5, 1.4      # m: how near a reached cell must be to an approach point, how far off in height, how far out in front of a panel
START_PANEL = 0x64


def load_route():
    return [int(h, 16) for h in json.load(open(os.path.join(DATA, 'credits_route.json')))['panels']]


def find_manifest():
    found = sorted(glob.glob('/mnt/c/coop/lab/run/*/panels.json'), key=os.path.getmtime)
    return found[-1] if found else None


def approach_points(route, manifest, taught):
    """{panel: (x, y, feet z, how)}; panels the manifest does not know are left out (returned as missing)."""
    pts, missing = {}, []
    for h in route:
        t = taught.get('%#x' % h)
        if t and t.get('stand'): pts[h] = (t['stand'][0], t['stand'][1], t['stand'][2] - EYE, 'taught'); continue
        q = manifest.get(h)
        if q is None: missing.append(h); continue
        pts[h] = (q['x'] + OUT * math.cos(q['rz']), q['y'] + OUT * math.sin(q['rz']), q['z'] - EYE, 'front')
    return pts, missing


class Reached(collections.defaultdict):
    truncated = False


def flood(m, root, strict=False, limit=6_000_000):
    """Every (cell, floor) the walker could reach from root=(x, y, feet z), by the planner's step rules (wnav.Map.plan) without the unseen
    cells. Returns {cell: [floors]}."""
    m.covered = False
    steps = m.steps      # (no memo: on the full map the states run into the millions and a memo of them took 12 GB)
    s0 = cell_of(root[0], root[1]); key = lambda c, z: (c[0], c[1], int(round(z / BIN)))
    floor = {key(s0, root[2]): root[2]}; prev = {}; seen = {key(s0, root[2])}; q = collections.deque([key(s0, root[2])]); reached = Reached(list)
    reached[s0].append(root[2])
    while q and len(seen) < limit:
        u = q.popleft(); fu = floor[u]; cu = (u[0], u[1])
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                if not dx and not dy: continue
                v = (cu[0] + dx, cu[1] + dy)
                if dx and dy and any(st == [] for st in (steps((cu[0] + dx, cu[1]), fu), steps((cu[0], cu[1] + dy), fu))): continue
                st = steps(v, fu, STAIR)
                if not st: continue      # None: never seen, []: cannot
                rv = m.cells[v]
                if rv[0] < -1e3: continue      # known only from a bump
                for z, k in st:
                    if z is None or (strict and k == SOFT): continue
                    if z - fu > UP:      # the stair rule: the step before climbed too, or the next cell on climbs on
                        p_ = prev.get(u)
                        if not ((p_ is not None and fu - floor[p_] >= 0.1) or any(0.1 <= z2 - z <= STAIR for z2, k2 in (steps((v[0] + dx, v[1] + dy), z, STAIR) or []) if z2 is not None and k2 == 1.0)): continue
                    kv = key(v, z)
                    if kv in seen: continue
                    seen.add(kv); floor[kv] = z; prev[kv] = u; reached[v].append(z); q.append(kv)
    reached.truncated = len(seen) >= limit
    return reached


def covered(reached, p, radius=RADIUS, height=HEIGHT):
    r = int(math.ceil(radius / CELL)); cx, cy = cell_of(p[0], p[1])
    for x in range(cx - r, cx + r + 1):
        for y in range(cy - r, cy + r + 1):
            if math.dist(wnav.centre((x, y)), p[:2]) > radius: continue
            if any(abs(z - p[2]) <= height for z in reached.get((x, y), ())): return True
    return False


def start_root(m, near, within=20.0):
    """The start area: the cell someone stood on (WALKED) nearest the first tutorial panel's approach, when within `within` m of it, as feet
    (the approach itself sits in a roofed hallway the map has hardly seen); else the approach."""
    best = min(((math.dist(wnav.centre(c), near[:2]), c, r) for c, r in m.cells.items() if r[2] & wnav.WALKED and abs(r[0] - near[2]) < 4.0), default=None)
    if best is None or best[0] > within: return near
    return wnav.centre(best[1]) + (best[2][0],)


def measure(m, pts, root, areas, strict=False):
    """({area: [reached, total]}, [cut off panels], reached set size)"""
    reached = flood(m, root, strict)
    per = collections.OrderedDict(); cut = []
    for h, p in pts.items():
        a = areas.get(h, '?'); per.setdefault(a, [0, 0]); per[a][1] += 1
        if covered(reached, p): per[a][0] += 1
        else: cut.append(h)
    return per, cut, sum(map(len, reached.values()))


def report(per, cut, pts, names, nmiss):
    lines = ['%-34s %8s %8s' % ('area', 'covered', 'of')]
    for a, (c, t) in sorted(per.items()): lines.append('%-34s %8d %8d' % (a, c, t))
    tc, tt = sum(c for c, _ in per.values()), sum(t for _, t in per.values())
    lines.append('%-34s %8d %8d   (%.0f%%)' % ('TOTAL', tc, tt, 100.0 * tc / max(1, tt)))
    if nmiss: lines.append('%d route panels are not in the manifest (not counted)' % nmiss)
    return lines, tc, tt


def main(a):
    if a and a[0] == 'selftest': return selftest()
    opt = lambda f, d=None: a[a.index(f) + 1] if f in a else d
    pos = [x for i, x in enumerate(a) if not x.startswith('--') and (i == 0 or a[i - 1] not in ('--panels', '--root', '--json'))]
    mp = pos[0] if pos else os.path.join(ROOT, 'local', 'nav_map.bin')
    from logic import Logic
    L = Logic(); route = load_route(); areas = {h: L.entities[h].area for h in route}; names = {h: L.entities[h].name for h in route}
    mf = opt('--panels') or find_manifest()
    manifest = {q['id']: q for q in json.load(open(mf))}
    taught = json.load(open(os.path.join(DATA, 'taught.json')))
    pts, missing = approach_points(route, manifest, taught)
    m = Map(); n = m.load(mp); print('map %s: %d records' % (mp, n))
    if '--recordings' in a:
        for f in a[a.index('--recordings') + 1:]:
            if f.startswith('--'): break
            for w in wnav.walks(wnav.load_recording(f)): m.walk_line(w)
    root = tuple(float(x) for x in opt('--root').split(',')) if opt('--root') else start_root(m, pts[START_PANEL][:3])
    print('start area (feet): %.1f %.1f %.1f' % root)
    per, cut, nstates = measure(m, pts, root, areas, '--strict' in a)
    lines, tc, tt = report(per, cut, pts, names, len(missing)); print('\n'.join(lines)); print('flood reached %d (cell, level) states%s' % (nstates, ' (STOPPED at the limit: the numbers are lower bounds)' if nstates >= 6_000_000 else ''))
    if '--list' in a or cut:
        for h in cut: print('  cut off: %#07x %-40s %-22s approach %.1f %.1f %.1f (%s)' % ((h, names[h], areas[h]) + pts[h]))
    if opt('--json'): json.dump({'map': mp, 'covered': tc, 'total': tt, 'per_area': per, 'cut': ['%#x' % h for h in cut]}, open(opt('--json'), 'w'), indent=1)
    return 0


def selftest():
    """A made-up island: a start square, a path to a hill, a wall across a second path. A panel behind the wall must be reported; cutting the
    path to the hill (a mutant) must lose the hill panel."""
    def ground(m, x0, x1, y0, y1, z):
        x = x0
        while x <= x1:
            y = y0
            while y <= y1: m.integrate([(x, y, z)]); y += 0.25
            x += 0.25
    def island(cut_path=False, wall=True):
        m = Map(); ground(m, 0, 6, 0, 6, 0.0)                       # start square
        ground(m, 6, 30, 2, 4, 0.0) if not cut_path else (ground(m, 6, 14, 2, 4, 0.0), ground(m, 20, 30, 2, 4, 0.0))   # path east to the hill
        ground(m, 30, 36, 0, 6, 0.0)                                # the hill's flat
        ground(m, 0, 6, 6, 30, 0.0)                                 # a path north
        ground(m, 0, 6, 30, 36, 0.0)                                # the far square behind the wall
        if wall:
            for y in (14, 14.25, 14.5):
                z = 0.0
                while z <= 3.0:
                    for x in [i * 0.25 for i in range(0, 25)]: m.integrate([(x, y, z)])
                    z += 0.25
        return m
    pts = {1: (3.0, 3.0, 0.0, 'front'), 2: (33.0, 3.0, 0.0, 'front'), 3: (3.0, 33.0, 0.0, 'front')}; areas = {1: 'Start', 2: 'Hill', 3: 'Far'}
    ok = True
    def check(name, good):
        nonlocal ok; ok &= good; print('%-44s %s' % (name, 'ok' if good else 'FAILED'))
    per, cut, _ = measure(island(), pts, (3.0, 3.0, 0.0), areas)
    check('wall: the panel behind it is reported', cut == [3]); check('open path: start and hill are covered', per['Start'] == [1, 1] and per['Hill'] == [1, 1])
    per, cut, _ = measure(island(wall=False), pts, (3.0, 3.0, 0.0), areas); check('no wall: all three covered', cut == [])
    per, cut, _ = measure(island(cut_path=True, wall=False), pts, (3.0, 3.0, 0.0), areas); check('mutant: a cut path loses the hill panel', 2 in cut)
    per, cut, _ = measure(island(wall=False), {1: (3.0, 3.0, 0.0, 'x'), 4: (20.0, 40.0, 0.0, 'x')}, (3.0, 3.0, 0.0), {1: 'Start', 4: 'Void'}); check('a point on never-seen ground is cut off', cut == [4])
    return 0 if ok else 1


if __name__ == '__main__': sys.exit(main(sys.argv[1:]))
