#!/usr/bin/env python3
"""Score a walking map against placed-and-pushed probes (local/oracle/stuck_probe.jsonl: the game's own physics, W held 2 s at a place where a walker
once got stuck; `moved` is the metres gained along the yaw). A probe is blocked when it moved under 0.8 m. The map refuses the way when a cell within
one step ahead (the cell 0.5 m on, or any of its neighbours that is nearer the heading) cannot be stepped into from the probe's floor.
usage: oracle_probe_eval.py MAP.bin PROBES.jsonl [more maps...]"""
import json, math, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'win'))
import wnav

def refused(m, r):
    x, y, z = r['feet']; yaw = r['yaw']; fu = z; c, s = math.cos(yaw), math.sin(yaw)
    for d in (0.5, 1.0):
        st = m.steps(wnav.cell_of(x + d * c, y + d * s), fu, wnav.UP)
        if st == []: return True
    return False

def unknown(m, r):
    x, y, z = r['feet']; return m.cells.get(wnav.cell_of(x + 0.5 * math.cos(r['yaw']), y + 0.5 * math.sin(r['yaw']))) is None

if __name__ == '__main__':
    rows = [json.loads(l) for l in open(sys.argv[2]) if 'moved' in l]
    for path in [sys.argv[1]] + sys.argv[3:]:
        m = wnav.Map(); m.load(path)
        blocked = [r for r in rows if r['moved'] < 0.8]; free = [r for r in rows if r['moved'] >= 0.8]
        print('%s: blocked probes the map refuses %d of %d, free probes it refuses %d of %d (unknown cells ahead: %d)' % (
            os.path.basename(path), sum(refused(m, r) for r in blocked), len(blocked), sum(refused(m, r) for r in free), len(free), sum(unknown(m, r) for r in rows)))
