#!/usr/bin/env python3
"""Merge an oracle map (the engine's collision, tools/oracle_map.py) with a depth map where the oracle's `no ground` is suspected of being wrong.
usage: oracle_merge.py OUT.bin ORACLE.bin DEPTH.bin [--policy yield|walked]
  yield   every oracle cell without ground is replaced by the depth map's cell when it has one (the oracle then only adds exact surfaces; it refuses
          nothing a depth map accepts)
  walked  only where the depth map's cell was walked by a person or a bot (flag WALKED): the oracle's refusals stand everywhere else
Cells the oracle has ground for, and cells only one map knows, are kept as they are."""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'win'))
import wnav

def merge(oracle, depth, policy='yield'):
    out = wnav.Map(); out.cells = {c: list(r) for c, r in oracle.cells.items()}; n = 0
    for c, r in oracle.cells.items():
        if r[0] >= -50 or not r[2] & wnav.ORACLE: continue
        d = depth.cells.get(c)
        if d is None or (policy == 'walked' and not d[2] & wnav.WALKED): continue
        out.cells[c] = list(d); n += 1
    for c, d in depth.cells.items():
        if c not in out.cells: out.cells[c] = list(d)
    return out, n

if __name__ == '__main__':
    a = sys.argv[1:]; policy = 'yield'
    if '--policy' in a: i = a.index('--policy'); policy = a[i + 1]; del a[i:i + 2]
    o = wnav.Map(); o.load(a[1]); d = wnav.Map(); d.load(a[2]); m, n = merge(o, d, policy); m.save(a[0])
    print('%s: %d cells, %d oracle no-ground cells replaced by depth cells (policy %s)' % (a[0], len(m.cells), n, policy))
