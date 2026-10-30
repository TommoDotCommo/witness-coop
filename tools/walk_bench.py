#!/usr/bin/env python3
"""The walking benchmark's panel list, its table and its regression check (tools/test_walk_bench.sh runs the games).

usage: python3 tools/walk_bench.py panels                 the fixed list, one tour over the island (comma separated, for wbench.py)
       python3 tools/walk_bench.py table RESULTS.jsonl    per leg: walked / teleported / failed, metres, seconds; totals
       python3 tools/walk_bench.py compare BEFORE AFTER   exit 0 when no leg got worse (walked > teleported > failed) and the
                                                          teleports did not rise; prints both side by side
       python3 tools/walk_bench.py selftest               game-free check of the compare rule, with a mutant that must fail
The 15 panels are on the credits route (tools/playthrough/data/credits_route.json), spread over the island in a tour: town, monastery,
keep hedges, shadows, quarry (intro, front row up the stairs, stoneworks, the lower ramp control that the dev menu's teleport cannot
reach, the laser on its ledge), desert, symmetry island, the glass factory (bunker approach) and the tutorial gate."""
import json, sys

PANELS = [      # (panel, why it is in the list)
    (0x79df, 'Town Triple Exit: town square, owner-recorded blocker'),
    (0x28998, 'Town RGB House Entry: stuck 12 times in a run'),
    (0x28a79, 'Town Maze Panel: up on the maze'),
    (0xb10, 'Monastery Entry Left: across the monastery bridge'),
    (0x139, 'Keep Hedge Maze 1: out of time in a run'),
    (0x334db, 'Shadows Door Timer Outside: no way on the map in runs'),
    (0x34d4, 'Quarry Intro Left: by the boathouse pocket'),
    (0x21b3, 'Quarry Front Row 1: up the stairs'),
    (0x1e5a, 'Quarry Stoneworks Entry Left'),
    (0x3678, 'Quarry Lower Ramp Control: the teleport sends the player to the spawn'),
    (0x3612, 'Quarry Laser Panel: on a ledge'),
    (0x698, 'Desert Surface 1: no way on the map in runs'),
    (0xb0, 'Symmetry Island Lower Panel: across the water'),
    (0x1a54, 'Glass Factory Entry Panel: stuck 12 times in a run'),
    (0x3629, 'Tutorial Gate Open: no way on the map in a run'),
]
RANK = {'walked': 2, 'teleported': 1, 'failed': 0}


def load(path):
    legs = {}
    for line in open(path):
        try: r = json.loads(line)
        except ValueError: continue
        if 'panel' in r: legs[r['panel']] = r
    return legs


def totals(legs):
    v = list(legs.values())
    return dict(walked=sum(r['result'] == 'walked' for r in v), teleported=sum(r['result'] == 'teleported' for r in v),
                failed=sum(r['result'] == 'failed' for r in v), metres=round(sum(r.get('metres') or 0 for r in v)), seconds=round(sum(r.get('seconds') or 0 for r in v)))


def table(legs):
    rows = ['%-9s %-11s %7s %7s %6s  %s' % ('panel', 'result', 'metres', 'seconds', 'stuck', 'why')]
    for h, _ in PANELS:
        r = legs.get('%#x' % h)
        rows.append('%-9s %-11s %7s %7s %6s  %s' % (('%#x' % h,) + (('missing', '', '', '', '') if r is None else (r['result'], r.get('metres'), r.get('seconds'), r.get('stuck'), (r.get('why') or '')[:60]))))
    t = totals(legs); rows.append('total: %(walked)d walked, %(teleported)d teleported, %(failed)d failed, %(metres)d m, %(seconds)d s' % t)
    return '\n'.join(rows)


def compare(before, after):
    """(ok, lines): no leg's result ranks lower after, the teleports did not rise."""
    lines = ['%-9s %-11s %-11s %8s %8s' % ('panel', 'before', 'after', 's before', 's after')]; worse = []
    for h, _ in PANELS:
        k = '%#x' % h; a, b = before.get(k), after.get(k)
        ra, rb = (a or {}).get('result', 'missing'), (b or {}).get('result', 'missing')
        lines.append('%-9s %-11s %-11s %8s %8s' % (k, ra, rb, (a or {}).get('seconds', ''), (b or {}).get('seconds', '')))
        if RANK.get(rb, -1) < RANK.get(ra, -1): worse.append(k)
    ta, tb = totals(before), totals(after)
    lines.append('before: %s' % ta); lines.append('after:  %s' % tb)
    ok = not worse and tb['teleported'] + tb['failed'] <= ta['teleported'] + ta['failed']
    lines.append('worse: %s' % (', '.join(worse) or 'none')); lines.append('PASS' if ok else 'FAIL')
    return ok, lines


def selftest():
    leg = lambda h, r: {'panel': '%#x' % h, 'result': r, 'seconds': 1, 'metres': 1}
    base = {('%#x' % h): leg(h, 'teleported') for h, _ in PANELS}
    better = dict(base, **{'%#x' % PANELS[0][0]: leg(PANELS[0][0], 'walked')})
    worse = dict(better, **{'%#x' % PANELS[1][0]: leg(PANELS[1][0], 'failed')})
    checks = [('better passes', compare(base, better)[0]), ('a worse leg fails', not compare(base, worse)[0]),
              ('a missing leg fails', not compare(base, {k: v for k, v in better.items() if k != '%#x' % PANELS[2][0]})[0])]
    for name, ok in checks: print('%-22s %s' % (name, 'ok' if ok else 'FAILED'))
    return 0 if all(ok for _, ok in checks) else 1


if __name__ == '__main__':
    a = sys.argv[1:]
    if not a or a[0] == 'panels': print(','.join('%x' % h for h, _ in PANELS))
    elif a[0] == 'table': print(table(load(a[1])))
    elif a[0] == 'compare':
        ok, lines = compare(load(a[1]), load(a[2])); print('\n'.join(lines)); sys.exit(0 if ok else 1)
    elif a[0] == 'selftest': sys.exit(selftest())
