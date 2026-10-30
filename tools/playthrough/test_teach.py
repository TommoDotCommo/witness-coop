#!/usr/bin/env python3
"""Game-free test of teach.py: a made-up recording (a person tries once, takes the line back, then draws 3 -> 4 -> 5 and the game accepts
it) must give that line, each node's world position, and where they stood when the accepted line started. Each mutant must be caught."""
import json, os, struct, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import teach


def rec(a, b, wa, wb, t=1.0): return struct.pack('<4I3f3f3f', a, b, 7, 7, t, 1.0, *wa, *wb, 0.0)


def session(path):
    P = {3: (1.0, 2.0, 3.0), 4: (1.0, 2.1, 3.0), 5: (1.0, 2.1, 3.1), 9: (0.0, 0.0, 0.0)}
    ev = [{'event': 'task', 'panel': '0x10', 'name': 'Test Panel'},
          {'event': 'sample', 'panel': '0x10', 'pose': [0, 0, 1.6, 0, 0], 'traced': 0, 'state': 0, 'records': ''},
          {'event': 'sample', 'panel': '0x10', 'pose': [0, 0, 1.6, 0, 0], 'traced': 1, 'state': 0, 'records': rec(9, 3, P[9], P[3], 0.4).hex()},
          {'event': 'sample', 'panel': '0x10', 'pose': [5, 5, 1.6, 1, 0], 'traced': 0, 'state': 0, 'records': ''},      # taken back, moved
          {'event': 'sample', 'panel': '0x10', 'pose': [6, 5, 1.6, 1, 0], 'traced': 1, 'state': 0, 'records': rec(3, 4, P[3], P[4], 0.5).hex()},
          {'event': 'sample', 'panel': '0x10', 'pose': [6, 5, 1.6, 1, 0], 'traced': 2, 'state': 0, 'records': (rec(3, 4, P[3], P[4]) + rec(4, 5, P[4], P[5])).hex()},
          {'event': 'solved', 'panel': '0x10', 'line': [], 'seconds': 9.0}]
    open(path, 'w').write('\n'.join(json.dumps(e) for e in ev) + '\n')
    return P


def check():
    with tempfile.TemporaryDirectory() as d:
        os.makedirs(os.path.join(d, 'rec1')); f = os.path.join(d, 'rec1', 'session.jsonl'); P = session(f)
        got = teach.lessons(f).get('0x10')
    if not got: return 'nothing learned'
    if got['line'] != [3, 4, 5]: return 'line %s' % got['line']
    if any(got['nodes'][str(k)] != list(P[k]) for k in (3, 4, 5)): return 'node positions %s' % got['nodes']
    if got['stand'][:2] != [6, 5]: return 'stood at %s (not where the accepted line started)' % got['stand']
    return ''


MUTANTS = [
    ("nodes[str(r[0])] = [round(v, 4) for v in r[6:9]]", "nodes[str(r[0])] = [round(v, 4) for v in r[7:10]]"),      # positions read one field off
    ("line = [recs[0][0]] + [r[1] for r in recs]", "line = [r[1] for r in recs]"),                                      # start node lost
    ("start = samples[min(k + 1, len(samples) - 1)]", "start = samples[0]"),                                           # first attempt's place
]

if __name__ == '__main__':
    why = check(); print('clean: %s' % ('passed' if not why else 'FAILED ' + why)); ok = not why
    src = open(teach.__file__).read()
    for old, new in MUTANTS:
        assert src.count(old) == 1, old
        ns = {'__name__': 'teach_mutant', '__file__': teach.__file__}; exec(compile(src.replace(old, new), teach.__file__, 'exec'), ns)
        real = teach.lessons; teach.lessons = ns['lessons']
        try: w = check()
        except Exception as e: w = repr(e)
        finally: teach.lessons = real
        print('mutant %-60s %s' % (new[:60], 'caught: ' + w if w else 'NOT CAUGHT')); ok = ok and bool(w)
    print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
