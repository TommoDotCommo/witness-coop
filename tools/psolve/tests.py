#!/usr/bin/env python3
"""T0 tests for psolve (game-free): small panels with known answers, one per rule, and a mutant check (a wrong rule must change the answer).
Run: make -C tools/psolve test"""
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, os.path.join(HERE, '..'))
import solver

def grid(gx, gy, cells=None, extra_flags=None, start=0, end=None, gaps=()):
    """A gx x gy node grid panel dict as wpanels.py would give it: start stub below node `start`... here simply flags on grid nodes."""
    n = gx * gy; flags = [0x600000] * n; edges = []
    for r in range(gy):
        for c in range(gx):
            i = r * gx + c
            if c + 1 < gx and (i, i + 1) not in gaps: edges.append((i, i + 1))
            if r + 1 < gy and (i, i + gx) not in gaps: edges.append((i, i + gx))
    end = n - 1 if end is None else end
    flags[start] |= 2; flags[end] |= 1
    for k, v in (extra_flags or {}).items(): flags[k] |= v
    cells = cells or [0] * ((gx - 1) * (gy - 1))
    return {'id': 0, 'nodes': n, 'edges': len(edges), 'gx': gx, 'gy': gy, 'ndec': len(cells), 'flags': flags, 'ca': [a for a, b in edges], 'cb': [b for a, b in edges],
            'dec': cells, 'seq': [], 'refl': 0, 'pos': [0.0] * (2 * n), 'powered': True, 'solved': False}

def check_path(p, path):
    assert path and p['flags'][path[0]] & 2 and p['flags'][path[-1]] & 1, path
    es = set(zip(p['ca'], p['cb'])) | set(zip(p['cb'], p['ca']))
    for a, b in zip(path, path[1:]): assert (a, b) in es, 'not an edge %d-%d' % (a, b)
    assert len(set(path)) == len(path), 'revisits a node'

def cells_of(path, gx, gy):
    """Regions of the cells separated by the path, computed independently of psolve (a second implementation of the separation rule)."""
    used = set(zip(path, path[1:])) | set(zip(path[1:], path))
    cw, ch = gx - 1, gy - 1; seen = {}; regs = []
    for s in range(cw * ch):
        if s in seen: continue
        st = [s]; seen[s] = len(regs); reg = []
        while st:
            ci = st.pop(); reg.append(ci); r, c = divmod(ci, cw)
            for (dr, dc, a, b) in ((0, 1, (r * gx + c + 1), ((r + 1) * gx + c + 1)), (0, -1, r * gx + c, (r + 1) * gx + c), (1, 0, (r + 1) * gx + c, (r + 1) * gx + c + 1), (-1, 0, r * gx + c, r * gx + c + 1)):
                nr, nc = r + dr, c + dc
                if 0 <= nr < ch and 0 <= nc < cw and (nr * cw + nc) not in seen and (a, b) not in used: seen[nr * cw + nc] = len(regs); st.append(nr * cw + nc)
        regs.append(reg)
    return regs

SQ_B, SQ_W = 0x101, 0x102

def test_plain_and_dots():
    p = grid(3, 3, extra_flags={6: 0x20}, end=2)           # the end is two steps away, but the dot in the far corner has to be visited first
    path = solver.solve_rules(p); check_path(p, path); assert 6 in path
    p = grid(3, 3, extra_flags={1: 0x20, 3: 0x20})         # dots on two edge midpoints of the corner: the line must visit both
    path = solver.solve_rules(p); check_path(p, path); assert 1 in path and 3 in path

def test_squares():
    # two squares of different colours side by side in a 2 x 2 cell grid: the line has to separate them
    p = grid(3, 3, cells=[SQ_B, SQ_W, 0, 0]); path = solver.solve_rules(p); check_path(p, path)
    regs = cells_of(path, 3, 3); reg = {ci: k for k, r in enumerate(regs) for ci in r}; assert reg[0] != reg[1]
    # mutant: same colours need no separation, but with different colours the answer can never keep them together
    p2 = grid(3, 3, cells=[SQ_B, SQ_B, 0, 0]); path2 = solver.solve_rules(p2); check_path(p2, path2)

def test_stars():
    star = lambda c: 0x200 | c
    p = grid(3, 3, cells=[star(1), SQ_B, 0, 0])            # a black star needs exactly one black partner in its region
    path = solver.solve_rules(p); check_path(p, path)
    regs = cells_of(path, 3, 3); reg = {ci: k for k, r in enumerate(regs) for ci in r}; assert reg[0] == reg[1]
    p = grid(3, 3, cells=[star(1), SQ_B, SQ_B, 0], end=2)  # a star with two black partners in one region is wrong: the line must split them (the short line to the end does not)
    path = solver.solve_rules(p); check_path(p, path)
    regs = cells_of(path, 3, 3); reg = {ci: k for k, r in enumerate(regs) for ci in r}
    assert [reg[1] == reg[0], reg[2] == reg[0]].count(True) == 1

def test_triangles():
    tri = lambda n: (n << 16) | 0x600 | 0xA
    p = grid(3, 3, cells=[tri(3), 0, 0, 0]); path = solver.solve_rules(p); check_path(p, path)
    es = set(zip(path, path[1:])) | set(zip(path[1:], path))
    sides = [(0, 1), (3, 4), (0, 3), (1, 4)]; assert sum(1 for a, b in sides if (a, b) in es) == 3

def test_poly():
    poly = lambda mask: (mask << 16) | 0x400
    p = grid(3, 3, cells=[poly(0x1), 0, 0, 0])              # a single-cell polyomino: its region must be exactly one cell
    path = solver.solve_rules(p); check_path(p, path)
    regs = cells_of(path, 3, 3); reg = {ci: k for k, r in enumerate(regs) for ci in r}; assert len(regs[reg[0]]) == 1

def test_gap_blocks():
    p = grid(3, 3, gaps={(0, 1), (0, 3)})                   # both edges out of the start are gone: no line exists
    assert solver.solve_rules(p) is None

def test_sound_dots():
    # sound dots (0x2000 / 0x4000 = pitch 1 / 2) have to be met in the order of the panel's own sequence, whichever order is the shorter way
    for seq in ([1, 2], [2, 1]):
        p = grid(3, 3, extra_flags={1: 0x20 | 0x2000, 3: 0x20 | 0x4000}); p['dseq'] = seq
        path = solver.solve_rules(p); check_path(p, path)
        assert path.index(1) < path.index(3) if seq == [1, 2] else path.index(3) < path.index(1), (seq, path)

def test_hole_cells():
    # a cell of type 0xa is not there: no region, no area. A one-cell polyomino next to a hole whose separating edge is missing can still be satisfied
    p = grid(3, 3, cells=[0xa02, 0x10400, 0, 0], gaps={(1, 4)}); path = solver.solve_rules(p); assert path is not None and p['flags'][path[0]] & 2; check_path(p, path)
    assert 4 in path and 5 in path, path

def run():
    n = 0
    for name, fn in sorted(globals().items()):
        if name.startswith('test_'): fn(); n += 1; print('ok', name)
    print('%d psolve tests passed' % n)

def run_mutants():
    """Every rule switched off in turn (PSOLVE_MUTANT=<rule>) must make at least one test fail."""
    bad = []
    for rule in ('dots', 'squares', 'stars', 'triangles', 'poly', 'pitch', 'hole'):
        r = subprocess.run([sys.executable, os.path.abspath(__file__)], env={**os.environ, 'PSOLVE_MUTANT': rule}, capture_output=True, text=True)
        print('mutant %-9s %s' % (rule, 'caught' if r.returncode else 'NOT CAUGHT'))
        if r.returncode == 0: bad.append(rule)
    assert not bad, 'mutants not caught: %s' % bad

if __name__ == '__main__':
    if '--mutants' in sys.argv: run_mutants()
    else: run()
