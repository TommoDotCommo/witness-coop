#!/usr/bin/env python3
"""T2 scenario for the POV screens: see tools/win/wpov.py for the checks. Mutants: a game that never sends pictures, a viewer that never asks, name tags off (so the
crosshair target and the V prompt do not exist). usage: qa_pov.py (called by tools/qa.sh pov)"""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, DIR2, PYW, pid, win
import qa_sweep

def run(cfg_a='', cfg_b='', group=False):
    p1, p2 = int(os.environ.get('WITNESS_PORT1', '47001')), int(os.environ.get('WITNESS_PORT2', '47002'))
    if group: cfg_a += '\npeer=2,127.0.0.1,%d' % p2; cfg_b += '\npeer=1,127.0.0.1,%d' % p1      # the way the lobby starts games: a roster of numbered players with addresses
    qa_sweep.restart(('name=Alpha\n' + cfg_a).strip(), ('name=Beta\n' + cfg_b).strip())
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wpov.py')), str(pid('A')), win(DIR1), str(pid('B')), win(DIR2), '2'], capture_output=True, text=True, timeout=400)
    print(r.stdout, end=''); return r.returncode == 0 and 'ALL OK' in r.stdout

def main():
    results = []
    print('=== pov: clean build', flush=True); ok = run(); results.append(('clean', ok)); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    print('=== pov: clean build, started the way the lobby does (a roster of numbered players)', flush=True); ok = run(group=True); results.append(('clean group', ok)); print('clean group: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    for name, ca, cb in (('no_partner_video (B never sends pictures)', '', 'no_partner_video=1'), ('no_pov_request (A never asks)', 'no_pov_request=1', ''), ('no_name_tags (no aim target)', 'no_name_tags=1', ''), ('pov_needs_aim (V does nothing unless aimed: the old behaviour)', 'pov_needs_aim=1', ''), ('pov_mode_zero (the screen is only drawn after F9: the old default)', 'pov_mode_zero=1', '')):
        print('=== pov: MUTANT %s' % name, flush=True); caught = not run(ca, cb); results.append((name, caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
