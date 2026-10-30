#!/usr/bin/env python3
"""Walking-map coverage of the credits route: is every panel's approach point connected on the map to the start area?

usage: python3 tools/nav_coverage.py [MAP.bin] [--panels panels.json] [--root x,y,z] [--strict] [--list] [--json OUT] [--legs OUT.json] [--recordings SESSION.jsonl ...]
       python3 tools/nav_coverage.py validate BASE.bin NEW.bin     (is the grown map as clean as the base: junk floors, walked cells)
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


def nearest_reached(reached, p, rmax=80.0, height=8.0):
    """The reached (cell centre x, y, floor) nearest to point p (horizontally, within `height` m in z), searching rings outward, else None."""
    cx, cy = cell_of(p[0], p[1]); best = None
    for r in range(0, int(rmax / CELL) + 1):
        if best and r * CELL > best[0] + CELL: break      # (best[0] is the weighted distance, at least the horizontal one)
        ring = [(x, y) for x in range(cx - r, cx + r + 1) for y in (cy - r, cy + r)] + [(x, y) for y in range(cy - r + 1, cy + r) for x in (cx - r, cx + r)]
        for c in ring:
            for z in reached.get(c, ()):
                if abs(z - p[2]) > height: continue
                d = math.dist(wnav.centre(c), p[:2]) + 2.0 * abs(z - p[2])      # (a level 3 m off is 6 m away: there is a wall or a cliff between)
                if best is None or d < best[0]: best = (d, wnav.centre(c)[0], wnav.centre(c)[1], z)
    return best and best[1:]


def plan_legs(reached, cut, pts, cluster=8.0):
    """Survey legs for the cut-off panels: one per cluster of them (the panels within `cluster` m of one already chosen come with it), each from
    the reached ground nearest its approach to the approach itself. [{'panel', 'near': [panels in the cluster], 'from': [x, y, feet], 'to': [x, y, feet], 'gap'}],
    the longest gaps last (the near ones grow the map the far ones lean on)."""
    legs = []
    for h in cut:
        p = pts[h]
        if any(math.dist(p[:2], pts[l['panel']][:2]) < cluster and abs(p[2] - pts[l['panel']][2]) < 4.0 for l in legs):
            next(l for l in legs if math.dist(p[:2], pts[l['panel']][:2]) < cluster)['near'].append(h); continue
        f = nearest_reached(reached, p)
        legs.append({'panel': h, 'near': [h], 'from': list(f) if f else None, 'to': list(p[:3]), 'gap': round(math.dist(f[:2], p[:2]) + 2.0 * abs(f[2] - p[2]), 1) if f else None})
    legs.sort(key=lambda l: (l['gap'] is None, l['gap'] or 0)); return legs


PIT = 3.0      # m: a floor this far under every neighbour's lowest surface is a junk floor (depth points taken while the camera turned land metres off)


def junk_stats(m, only=None):
    """(cells looked at, pits, walked cells that are not walkable). `only`: the cells to look at (default all)."""
    n = pits = bad = 0
    for c in (only if only is not None else m.cells):
        r = m.cells.get(c)
        if r is None or r[0] < -1e3: continue
        n += 1
        if r[2] & wnav.WALKED and not m.walkable(c)[0]: bad += 1
        nb = [m.cells[v][0] for v in ((c[0] + dx, c[1] + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1) if dx or dy) if v in m.cells and m.cells[v][0] > -1e3]
        if len(nb) >= 5 and r[0] < min(nb) - PIT and not r[2] & wnav.WALKED: pits += 1
    return n, pits, bad


def validate(base, new, slack=2.0, floor=0.01):
    """Is a grown map as clean as the one it grew from? Looks at the cells that are new or changed: pits (junk floors) must not be more than
    `slack` times as common as in the base map (and at least `floor` as a share is always allowed), and no walked cell may be unwalkable.
    Returns (ok, lines)."""
    changed = [c for c, r in new.cells.items() if c not in base.cells or base.cells[c][:2] != r[:2]]
    nb, pb, bb = junk_stats(base); nn, pn, bn = junk_stats(new, changed)
    rb, rn = pb / max(1, nb), pn / max(1, nn)
    ok = rn <= max(floor, slack * rb) and bn == 0
    return ok, ['base: %d cells, %d pits (%.3f%%), %d walked-but-unwalkable' % (nb, pb, 100 * rb, bb),
                'new or changed: %d cells, %d pits (%.3f%%), %d walked-but-unwalkable' % (nn, pn, 100 * rn, bn), 'VALID' if ok else 'INVALID: junk floors or unwalkable walked cells in the new cells']


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
    if a and a[0] == 'validate':      # validate BASE.bin NEW.bin
        b, n = Map(), Map(); b.load(a[1]); n.load(a[2]); ok, lines = validate(b, n); print('\n'.join(lines)); return 0 if ok else 1
    opt = lambda f, d=None: a[a.index(f) + 1] if f in a else d
    pos = [x for i, x in enumerate(a) if not x.startswith('--') and (i == 0 or a[i - 1] not in ('--panels', '--root', '--json', '--legs'))]
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
    if '--legs' in a:
        legs = plan_legs(flood(m, root, '--strict' in a), cut, pts); json.dump(legs, open(opt('--legs'), 'w'), indent=1)
        print('%d survey legs for %d cut-off panels -> %s' % (len(legs), len(cut), opt('--legs')))
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
    base = island(wall=False); good = island(wall=False); ground(good, 40, 44, 0, 4, 0.0)
    junk = island(wall=False); ground(junk, 40, 44, 0, 4, 0.0)
    for x in (41.0, 42.0, 43.0): junk.integrate([(x, 2.0, -9.0)])
    junk2 = island(wall=False); ground(junk2, 40, 44, 0, 4, 0.0); junk2.cells[(50, 50)] = [0.0, 1, 0]; junk2.cells[(50, 50)][2] |= wnav.WALKED; junk2.cells[(50, 50)][1] = 0
    check('validate: clean new cells pass', validate(base, good)[0]); check('validate mutant: pits in the new cells fail', not validate(base, junk)[0])
    check('validate mutant: a walked cell with nothing to stand on fails', not validate(base, junk2)[0])
    reached = flood(island(cut_path=True, wall=False), (3.0, 3.0, 0.0))
    legs = plan_legs(reached, [2, 4], {2: (33.0, 3.0, 0.0, 'x'), 4: (34.0, 5.0, 0.0, 'x')})
    check('legs: one per cluster, from the end of the reached path', len(legs) == 1 and legs[0]['near'] == [2, 4] and 12.0 <= legs[0]['from'][0] <= 14.5, ) if legs else check('legs: planned', False)
    return 0 if ok else 1


if __name__ == '__main__': sys.exit(main(sys.argv[1:]))
