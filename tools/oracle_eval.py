#!/usr/bin/env python3
"""Compare an oracle-built map with what walking has shown: cells somebody stood on (WALKED) must be standable at about the height they were
stood at, cells a walk bumped into (BUMPED, never walked) are mostly not.  usage: oracle_eval.py ORACLE.bin [REFERENCE.bin = local/nav_map.bin]      or      oracle_eval.py MAP.bin --trace TRACE.jsonl ..."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'win'))
import wnav

def load(path):
    m = wnav.Map(); m.load(path); return m

def evaluate(oracle, ref):
    r = dict(walked=0, walked_ok=0, walked_nolevel=0, bumped=0, bumped_blocked=0, bumped_ok=0)
    for c, (z, mask, fl) in ref.cells.items():
        o = oracle.cells.get(c)
        if o is None or z < -1e3: continue
        surf = [s for s in (oracle.surfaces(c) or []) if s is not None and s > wnav.SEA]
        if fl & wnav.WALKED:
            r['walked'] += 1
            if any(abs(s - z) <= 0.6 for s in surf): r['walked_ok'] += 1
            else: r['walked_nolevel'] += 1
        elif fl & wnav.BUMPED:
            r['bumped'] += 1
            if any(abs(s - z) <= 0.6 for s in surf) : r['bumped_ok'] += 1
            else: r['bumped_blocked'] += 1
    return r

def trace_eval(m, trace_path, ahead=(0.5, 1.0)):
    """A walker's trace (WNAV_TRACE, <game folder>/wnav_trace.jsonl) is what the engine let it do: every `pos` is a place it stood (the cell must be
    steppable at that height), every `stuck` a place where W did not move it (some cell just ahead along the yaw must be refused). Returns counts."""
    import json, math
    r = dict(stood=0, stood_ok=0, stood_unknown=0, stuck=0, stuck_blocked=0, stuck_open=0, stuck_unknown=0)
    for line in open(trace_path):
        try: e = json.loads(line)
        except ValueError: continue
        if 'pos' in e and 'plan' not in e:
            x, y, z = e['pos']; c = wnav.cell_of(x, y); st = m.steps(c, z - wnav.EYE, wnav.STAIR); r['stood'] += 1
            if st is None: r['stood_unknown'] += 1
            elif st: r['stood_ok'] += 1
        elif 'stuck' in e and 'yaw' in e and 'z' in e:
            x, y = e['stuck']; fu = e['z'] - wnav.EYE; r['stuck'] += 1; res = []
            for d in ahead:
                st = m.steps(wnav.cell_of(x + d * math.cos(e['yaw']), y + d * math.sin(e['yaw'])), fu, wnav.UP); res.append(st)
            if any(st == [] for st in res): r['stuck_blocked'] += 1
            elif all(st is None for st in res): r['stuck_unknown'] += 1
            else: r['stuck_open'] += 1
    return r


if __name__ == '__main__':
    o = load(sys.argv[1]); ref = load(sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), '..', 'local', 'nav_map.bin'))
    if '--trace' in sys.argv:
        for t in sys.argv[sys.argv.index('--trace') + 1:]:
            r = trace_eval(o, t); print(t, r)
            if r['stood']: print('  places stood on that the map lets a step reach: %.1f%% (unknown %d)' % (100.0 * r['stood_ok'] / r['stood'], r['stood_unknown']))
            if r['stuck']: print('  stucks whose cell just ahead the map refuses: %.1f%% (open %d, unknown %d of %d)' % (100.0 * r['stuck_blocked'] / r['stuck'], r['stuck_open'], r['stuck_unknown'], r['stuck']))
        sys.exit(0)
    r = evaluate(o, ref); print(r)
    if r['walked']: print('walked cells standable at their height: %.1f%%' % (100.0 * r['walked_ok'] / r['walked']))
    if r['bumped']: print('bumped cells not standable: %.1f%%' % (100.0 * r['bumped_blocked'] / r['bumped']))
