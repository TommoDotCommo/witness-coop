#!/usr/bin/env python3
"""Game-free (T0) test of report.py's blocker list (the owner's recording list): which panels it names, why, and from which save.
Mutants, each a one-line change of report.py, must fail.
usage: python3 test_report.py"""
import json, os, shutil, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))

CHECK = r'''
import json, os, sys, tempfile
sys.path.insert(0, TOOLS); sys.path.insert(0, D)
import report
from logic import Logic
L = Logic()
root = tempfile.mkdtemp()
def run(name, events, checkpoint=False):
    f = os.path.join(root, name); os.makedirs(f)
    with open(os.path.join(f, 'events.jsonl'), 'w') as o:
        for e in events: o.write(json.dumps(e) + '\n')
    if checkpoint: open(os.path.join(f, 'checkpoint.witness_campaign'), 'w').write('save')
    return f
F = lambda h, why: dict(kind='failed', panel=h, why=why)
a = run('a', [dict(kind='started', start='resumed from /saves/one.witness_campaign'),
              F('0x9d9b', 'the line did not start with any panel size or offset tried'), F('0x9d9b', 'stuck before node 1 (path index 1)'),
              F('0x9d9b', 'the line did not start with any panel size or offset tried'),
              F('0x3678', 'the panel cannot be seen from any of the places tried'), F('0x3678', 'the panel cannot be seen from any of the places tried'),
              F('0x28998', 'finished but the game did not accept it (state 2)'),
              F('0x65', 'teleport refused and the player could not be held there: camera at 1 2 3'),
              dict(kind='no solver', panel='0x9fd7', why='no path found')])
b = run('b', [dict(kind='started', start='new game'), F('0x28998', 'finished but the game did not accept it (state 2)'),
              F('0x65', 'teleport refused and the player could not be held there: camera at 1 2 3'), dict(kind='solved', panel='0x65')], checkpoint=True)
got = {x['panel']: x for x in report.blockers([a, b], L)}
want = {'0x9d9b': 'did not start', '0x3678': 'cannot be seen', '0x28998': 'wrong line', '0x9fd7': 'psolve no path'}
bad = []
if set(got) != set(want): bad.append('panels %s, want %s' % (sorted(got), sorted(want)))      # 0x65 was solved in run b: not a blocker
for h, k in want.items():
    if h in got and got[h]['kind'] != k: bad.append('%s is %s, want %s' % (h, got[h]['kind'], k))
if '0x9d9b' in got and got['0x9d9b']['checkpoint'] != '/saves/one.witness_campaign': bad.append('0x9d9b save %r' % got['0x9d9b']['checkpoint'])
if '0x28998' in got and got['0x28998']['checkpoint'] != os.path.join(b, 'checkpoint.witness_campaign'): bad.append('0x28998 save %r' % got['0x28998']['checkpoint'])
if report.kind('steering: overshot node 17') != 'wrong line' or report.kind('teleport refused and the player could not be held there') != 'movement': bad.append('kind()')
print('; '.join(bad) or 'blockers as expected: %d' % len(got)); sys.exit(1 if bad else 0)
'''

MUTANTS = [
    ("        if h in solved or len(whys) < 2: continue", "        if len(whys) < 2: continue"),                                # a panel solved in another run is still listed
    ("    if 'cannot be seen' in why: return 'cannot be seen'", "    pass"),                                                           # not-seen lumped in
    ("        save = own if os.path.exists(own) else (start[len('resumed from '):] if start.startswith('resumed from ') else '')", "        save = start"),      # wrong save
    ("                n = 2 if e['kind'] != 'failed' else 1      # (no solver, unpowered: logged once, they block at once)", "                n = 1"),   # a no-solver panel is dropped
]


def run_with(folder):
    r = subprocess.run([sys.executable, '-c', 'D=%r\nTOOLS=%r\n' % (folder, os.path.dirname(HERE)) + CHECK], capture_output=True, text=True, timeout=120)
    return r.returncode == 0, ((r.stdout + r.stderr).strip().splitlines() or [''])[-1]


def main():
    ok, out = run_with(HERE); print('clean:', 'passed' if ok else 'FAILED', '-', out, flush=True); bad = not ok
    for old, new in MUTANTS:
        with tempfile.TemporaryDirectory(prefix='report-mut-') as d:
            for f in os.listdir(HERE):
                if f.endswith('.py'): shutil.copy(os.path.join(HERE, f), d)
            shutil.copytree(os.path.join(HERE, 'data'), os.path.join(d, 'data'))
            s = open(os.path.join(d, 'report.py')).read(); assert s.count(old) == 1, old; open(os.path.join(d, 'report.py'), 'w').write(s.replace(old, new))
            caught, out = run_with(d); caught = not caught
            print('mutant %s: %s (%s)' % (old.strip()[:60], 'rejected' if caught else 'SURVIVED', out), flush=True); bad |= not caught
    print('ALL OK' if not bad else 'FAILED'); return 1 if bad else 0


if __name__ == '__main__': sys.exit(main())
