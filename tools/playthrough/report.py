#!/usr/bin/env python3
"""Read playthrough runs and say what they found.

usage: report.py [run folder ...]      (default: every run under <run folders>/*/playthrough/)
       report.py --panels [folders]     the best result per panel across runs (data/panel_status.json is written beside this file)

Per run: where it started, how far along the credits route it got (per area), solve speed, failures grouped by reason, co-op propagation
(percentiles, the slowest), persistent divergences and the divergence check (divergence.py), redraws, and what blocked it."""
import glob, json, os, statistics, sys, collections
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE); sys.path.insert(0, os.path.dirname(HERE))
from logic import Logic
from planner import route
import divergence
from witness import RUN


def runs(args):
    if args: return args
    roots = {os.path.dirname(RUN), os.environ.get('WITNESS_TMPDIR', '')} - {''}      # every owner's run folder (the lab's run area too)
    return sorted({f for r in roots for f in glob.glob(os.path.join(r, '*', 'playthrough', '*'))}, key=os.path.basename)


def load(folder):
    try: return [json.loads(l) for l in open(os.path.join(folder, 'events.jsonl'))]
    except OSError: return []


def reason(why):
    for key in ('stuck before', 'did not start', 'cannot be seen', 'did not accept', 'timed out', 'redrawn', 'overshot', 'no result'):
        if key in why: return key
    return why[:40] or '?'


def one(folder, L, R):
    ev = load(folder)
    if not ev: return
    start = next((e for e in ev if e['kind'] == 'started'), {})
    solved = {int(e['panel'], 16) for e in ev if e['kind'] in ('solved', 'already solved')}
    print('=== %s  (%s, %d bots, %.0f min)' % (os.path.basename(folder), start.get('start', 'new game'), start.get('bots', 0), ev[-1]['t'] / 60))
    by_area = collections.defaultdict(lambda: [0, 0])
    for h in R: by_area[L.entities[h].area][1] += 1; by_area[L.entities[h].area][0] += h in solved
    print('  credits route: %d / %d   ' % (len(solved & R), len(R)) + ', '.join('%s %d/%d' % (a, d, t) for a, (d, t) in sorted(by_area.items()) if d))
    s = [e['seconds'] for e in ev if e['kind'] == 'solved' and e.get('timing')]
    if s: print('  solves: %d, median %.1f s, p90 %.1f s' % (len(s), statistics.median(s), sorted(s)[int(0.9 * (len(s) - 1))]))
    fails = collections.Counter(reason(e.get('why', '')) for e in ev if e['kind'] == 'failed')
    if fails: print('  failures:', dict(fails))
    mv = [m for e in ev if e['kind'] in ('solved', 'failed') for m in e.get('moves', [])]
    if mv:      # how the bots got to their panels: walking, or the teleport as a last resort (with why the walk did not get there)
        walks = [m for m in mv if m['how'] == 'walk']; tps = [m for m in mv if m['how'] == 'teleport']
        print('  movement: %d walks (%d got there, %.0f m), %d teleports' % (len(walks), sum(1 for m in walks if m.get('ok')), sum(m.get('walked', 0) for m in walks), len(tps)))
        why = collections.Counter(reason(m.get('why', '')) for m in walks if not m.get('ok'))
        if why: print('  walks that did not get there:', dict(why.most_common(6)))
    p = sorted(e['exact_s'] if e.get('exact_s') is not None else e['seconds'] for e in ev if e['kind'] == 'propagated')      # exact (event logs) where the games wrote them
    if p: print('  propagation: %d, p50 %.2f s, p95 %.2f s, max %.2f s' % (len(p), p[len(p) // 2], p[int(0.95 * (len(p) - 1))], p[-1]))
    slow = [dict(e, seconds=e['exact_s'] if e.get('exact_s') is not None else e['seconds']) for e in ev if e['kind'] == 'propagated' and (e.get('exact_s') if e.get('exact_s') is not None else e['seconds']) > 5]
    if slow: print('  slow propagation (>5 s):', ', '.join('%s %.1fs' % (e['panel'], e['seconds']) for e in slow[:8]))
    div = [e for e in ev if e['kind'] == 'divergence' and e.get('persistent', True)]
    if div:
        doors = collections.Counter(d for e in div for d in e.get('doors_differ', [])); panels = collections.Counter(x for e in div for x in e.get('panels_only_here', []) + e.get('panels_missing', []))
        print('  persistent divergences: %d (doors %s; panels %s)' % (len(div), dict(doors.most_common(6)), dict(panels.most_common(6))))
    dc = divergence.summary(ev)      # persistent and not explained by the allow-list (data/divergence_allow.json): a co-op bug to look at
    if dc['unknown'] or dc['known']: print('  divergence check: unknown %s, known %s, resolved %s' % (dc['unknown'] or '-', dc['known'] or '-', dc['resolved'] or '-'))
    for k in ('resolve another way', 'redraw refused: restoring', 'crash', 'no solver'):
        n = [e for e in ev if e['kind'] == k]
        if n: print('  %s: %s' % (k, ', '.join(e['panel'] for e in n[:10])))
    for e in ev:
        if e['kind'] == 'blocked': print('  BLOCKED by:', ', '.join('%s %s (%s)' % (x['panel'], x['name'], x['area']) for x in e['panels']))
        if e['kind'] == 'goal': print('  GOAL reached:', e['goal'])


def panels(folders, L):
    best = {}
    for f in folders:
        for e in load(f):
            if e['kind'] not in ('solved', 'failed', 'no solver'): continue
            h = e['panel']; cur = best.get(h)
            rec = dict(result=e['kind'], why=e.get('why', ''), seconds=e.get('seconds'), run=os.path.basename(f))
            if cur is None or (cur['result'] != 'solved' and e['kind'] == 'solved') or (e['kind'] == 'solved' and cur['result'] == 'solved' and (e.get('seconds') or 1e9) < (cur['seconds'] or 1e9)): best[h] = rec
    out = os.path.join(HERE, 'data', 'panel_status.json'); json.dump(best, open(out, 'w'), indent=0, sort_keys=True)
    c = collections.Counter(v['result'] for v in best.values()); print('panels seen: %d %s -> %s' % (len(best), dict(c), out))


if __name__ == '__main__':
    a = sys.argv[1:]; L = Logic(); R = route(L)
    if a and a[0] == '--panels': panels(runs(a[1:]), L)
    else:
        for f in runs(a): one(f, L, R)
