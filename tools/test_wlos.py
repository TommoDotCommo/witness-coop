"""Game-free test of wlos (standing places with a clear line to a panel). Run: python3 tools/test_wlos.py [--mutant NAME]"""
import os, sys, math
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'win'))
import wlos

MUT = sys.argv[sys.argv.index('--mutant') + 1] if '--mutant' in sys.argv else None
if MUT == 'no_blocking': wlos.blocked_at = lambda cells, x, y, z: False
if MUT == 'ignore_height': _b = wlos.blocked_at; wlos.blocked_at = lambda cells, x, y, z: _b(cells, x, y, 0.0 + 0 * z + cells.get(wlos.cell_of(x, y), [0])[0])
if MUT == 'behind_ok': wlos.standing_places.__defaults__ = (1.4, 6.0, 180.0, 2.5)

def check(ok, what):
    if not ok: print('FAIL', what); sys.exit(1)

# a panel at (0, 0, 1.65) facing +x; floor at z = 0 on x 0.5..7, y -7..7; a 2 m wall right in front of it (cells x 1.5..2.5, y -0.5..0.5); a pit (no floor) at x 4..5
cells = {}
for cx in range(-14, 16):
    for cy in range(-14, 15):
        x, y = (cx + 0.5) * 0.5, (cy + 0.5) * 0.5
        if 4.0 <= x < 5.0: continue
        cells[(cx, cy)] = [0.0, 1, 0]
for cx in (3, 4):
    for cy in (-1, 0): cells[(cx, cy)] = [0.0, 0xFF, 0]
panel = (0.0, 0.0, 1.65); normal = (1.0, 0.0)
check(not wlos.clear_line(cells, (3.25, 0.25, 1.65), panel), 'the wall blocks the line straight out from the panel')
check(wlos.clear_line(cells, (3.25, 2.25, 1.65), panel), 'a place to the side is clear')
check(wlos.clear_line(cells, (1.25, 0.25, 1.65), panel), 'the panel\'s own surroundings are skipped')
pl = wlos.standing_places(cells, panel, normal)
check(pl, 'there are places')
ps = [q for _, q in pl]
check(all(wlos.clear_line(cells, (q[0], q[1], q[2] + wlos.EYE), panel) for q in ps), 'every place has a clear line')
check(not any(3.0 <= q[0] <= 3.5 and abs(q[1]) < 0.3 for q in ps), 'no place behind the wall')
check(not any(4.0 <= q[0] < 5.0 for q in ps), 'no place over the pit')
check(not any(q[0] < 0 for q in ps), 'no place behind the panel')
check(all(math.hypot(q[0], q[1]) >= 1.4 for q in ps), 'not closer than the minimum')
sp = wlos.spread(pl, 4, 1.5)
check(len(sp) == 4 and all(math.hypot(a[0] - b[0], a[1] - b[1]) >= 1.5 for i, a in enumerate(sp) for b in sp[:i]), 'spread: four places at least 1.5 m apart')
check(math.hypot(sp[0][0] - 3.0, sp[0][1]) < 1.6, 'the best place is near 3 m out')
print('ALL OK')
