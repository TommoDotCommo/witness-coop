#!/usr/bin/env python3
"""Say in words how to draw a panel by hand: tools/directions.py <panel hex> [...]. Steps are grid squares from the start circle, assuming the panel is the usual way up
(start bottom-left; the real orientation is on the panel). Marker nodes on an edge (dots, gaps) are not counted as steps."""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import solver, puzzle

def moves(q, path):
    P = q['pos']; xy = lambda n: (P[2 * n], P[2 * n + 1]); out = []
    for a, b in zip(path, path[1:]):
        (x0, y0), (x1, y1) = xy(a), xy(b); dx, dy = x1 - x0, y1 - y0
        if max(abs(dx), abs(dy)) < 0.02: continue
        m = ('R' if dx > 0 else 'L') if abs(dx) > abs(dy) else ('U' if dy > 0 else 'D')
        if out and out[-1][0] == m: out[-1][1] += 1
        else: out.append([m, 1])
    return out

if __name__ == '__main__':
    inv = {q['id']: q for q in json.load(open(os.environ.get('WITNESS_MANIFEST', '/mnt/c/coop/lab/run/panel-sweep/panels.json')))}
    for h in sys.argv[1:]:
        q = inv[int(h, 16)]; path = solver.solve(q)
        print(h, puzzle.describe(q)[:50])
        print('  ', ' '.join('%s%d' % (m, n) for m, n in moves(q, path)) if path else 'no solution known')
