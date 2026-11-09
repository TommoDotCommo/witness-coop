#!/usr/bin/env python3
"""Game-free sweep of the built-in randomizers: every mode over many seeds, each run in a process of its own under a time limit, over the games
recorded by tools/randomizer_solo.sh --record (local/randomizer/*.world). A run that crashes (a divide by zero in a generator kills the game) or
never ends is a finding; one that reads what the recording does not have says nothing (counted apart). Upstream bugs this found: Despair's symmetry
list popped empty (a divide by zero), Ascension retrying a Mountain panel for ever (third_party/README.md).
usage: tools/randomizer_sweep.py [--seeds N] [--timeout S] [--modes m1,m2] [--jobs J]"""
import argparse, concurrent.futures as cf, glob, os, subprocess, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ap = argparse.ArgumentParser(); ap.add_argument('--seeds', type=int, default=30); ap.add_argument('--timeout', type=float, default=60)
ap.add_argument('--modes', default='normal,expert,variety,ascension,despair-symbols,despair,shuffler,emptiness,puzzle-pack,star,tutorializer'); ap.add_argument('--jobs', type=int, default=os.cpu_count())
a = ap.parse_args()
subprocess.run(['make', '-s', '-C', os.path.join(ROOT, 'proxy', 'randomizer'), 'build/one-run'], check=True)
exe = os.path.join(ROOT, 'proxy', 'randomizer', 'build', 'one-run')
worlds = sorted(glob.glob(os.path.join(ROOT, 'local', 'randomizer', '*.world')), key=os.path.getsize, reverse=True)
if not worlds: sys.exit('no recorded game in local/randomizer/ (tools/randomizer_solo.sh --record)')
def run(mode, seed):
    spec = '%s;seed:%d' % (mode, seed)
    for w in worlds:
        try: p = subprocess.run([exe, w, spec], capture_output=True, text=True, timeout=a.timeout)
        except subprocess.TimeoutExpired: return mode, seed, 'HANG', 'still running after %.0f s' % a.timeout
        if p.returncode < 0: return mode, seed, 'CRASH', 'signal %d' % -p.returncode
        out = p.stdout.strip()
        if out.startswith('ok'): return mode, seed, 'ok', out
        if out.startswith('failed'): return mode, seed, 'FAILED', out
    return mode, seed, 'missed', out
jobs = [(m, s) for m in a.modes.split(',') for s in range(1, a.seeds + 1)]
res = {}
with cf.ThreadPoolExecutor(a.jobs) as ex:
    for mode, seed, verdict, detail in ex.map(lambda j: run(*j), jobs):
        res.setdefault(mode, []).append((seed, verdict, detail))
bad = 0
for mode in a.modes.split(','):
    r = res.get(mode, []); count = lambda v: sum(1 for x in r if x[1] == v)
    worst = [x for x in r if x[1] in ('CRASH', 'HANG', 'FAILED')]; bad += len(worst)
    print('%-16s ok %3d  missed %3d  CRASH %d  HANG %d  FAILED %d  %s' % (mode, count('ok'), count('missed'), count('CRASH'), count('HANG'), count('FAILED'),
          ' '.join('seed %d %s' % (x[0], x[1]) for x in worst[:6])))
sys.exit(1 if bad else 0)
