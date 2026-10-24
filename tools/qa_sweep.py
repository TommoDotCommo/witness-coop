#!/usr/bin/env python3
"""T2 scenario for the puzzle sweep: a bot teleports to four different kinds of panel in a fresh game A and solves each by drawing with the mouse; the game's
solved flags are the verdict, the same panels must be solved on game B (sync), and with the teleport hold switched off (twin) the far panel must fail.
usage: qa_sweep.py   (called by tools/qa.sh sweep: games and lease are handled there)"""
import json, os, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import ROOT, TOOLS, DIR1, DIR2, RUN, pid, pyw, win
import solver, sweep

# one panel of each kind, all powered in a new game: tutorial lines (0x64, 0x182), maze (0x293), stored solution far away (0x139), polyominoes on a tilted frame (0x2), polyomino strip (0x469)
PANELS = [0x64, 0x182, 0x293, 0x139, 0x2, 0x469]

def restart(cfg_a='', cfg_b=''):
    subprocess.run([os.path.join(TOOLS, 'restart.sh')], env={**os.environ, 'CFG_A': cfg_a, 'CFG_B': cfg_b}, capture_output=True, text=True, timeout=600, check=True)
    time.sleep(45); open(os.path.join(RUN, 'guard.stop'), 'w').write('bot starting')

def solved_set(which):
    return set(json.loads(pyw('wstatedump.py', pid(which)).strip().splitlines()[-1])['panels'])

def run_panels(which, ids, manifest):
    out = {}
    for i in ids:
        q = manifest[i]; path = solver.solve(q)
        out[i] = sweep.draw(q, path, which) if path else {'solved': False, 'why': 'no solver'}
        print('  panel %#x: %s %s' % (i, 'solved' if out[i].get('solved') else 'FAILED', out[i].get('why', '')), flush=True)
    return out

def main():
    results = []
    manifest = {q['id']: q for q in sweep.load(sweep.MANIFEST, [])}
    if not manifest: sys.exit('no manifest: run tools/sweep.py inventory on a fresh game first')
    print('=== sweep: clean build', flush=True); restart()
    got = run_panels('A', PANELS, manifest)
    time.sleep(12); on_a, on_b = solved_set('A'), solved_set('B')
    ok = all(r.get('solved') for r in got.values()) and set(PANELS) <= on_a and set(PANELS) <= on_b
    print('clean: %s (A has %d solved panels, B has %d)' % ('PASS' if ok else 'FAIL', len(on_a), len(on_b)), flush=True); results.append(('clean', ok))
    print('=== sweep: MUTANT no_teleport_hold (the far panel must not be reachable)', flush=True); restart(cfg_a='no_teleport_hold=1')
    got = run_panels('A', [0x139], manifest)
    caught = not got[0x139].get('solved')
    print('mutant: %s' % ('caught (the far panel could not be solved)' if caught else 'NOT CAUGHT'), flush=True); results.append(('mutant no_teleport_hold', caught))
    bad = [n for n, ok_ in results if not ok_]
    print('\nALL OK' if not bad else '\nFAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
