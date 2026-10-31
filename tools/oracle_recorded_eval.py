#!/usr/bin/env python3
"""How often does a walking map refuse a place a person really stood on? Every position of the recorded walks (local/recordings/*/session.jsonl, feet
heights) is looked up in each map: `absent` (no cell), `no ground` (the cell has no surface at all), `refused` (surfaces, but none within a step of the
person's feet height, as the planner reads it), `ok`.
usage: oracle_recorded_eval.py RECORDINGS_GLOB MAP.bin [MAP.bin ...]"""
import collections, glob, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'win'))
import wnav
EYE = 1.65

def classify(m, p):
    c = wnav.cell_of(p[0], p[1]); r = m.cells.get(c)
    if r is None: return 'absent'
    if r[0] < -50 and not (r[2] & wnav.WALKED): return 'no ground'
    return 'ok' if any(abs(z - (p[2] - EYE)) <= 0.6 for z in (m.surfaces(c) or []) if z is not None) else 'other level'

def score(maps, pts):
    out = {}
    for path in maps:
        m = wnav.Map(); m.load(path); out[path] = collections.Counter(classify(m, p) for p in pts)
    return out

if __name__ == '__main__':
    pts = [p for q in glob.glob(sys.argv[1]) for w in wnav.walks(wnav.load_recording(q)) for p in w]
    for path, c in score(sys.argv[2:], pts).items():
        print('%-40s %d positions: %s  (%.0f%% refused or missing)' % (os.path.basename(path), len(pts), dict(c), 100.0 * (len(pts) - c['ok']) / len(pts)))
