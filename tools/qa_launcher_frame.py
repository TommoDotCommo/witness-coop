#!/usr/bin/env python3
"""Game-free check of the launcher's own window frame (no Windows title bar; the banner is the caption with its own minimize, maximize and close), the race goal
shown only for a race, and "You left the lobby." clearing itself (tools/win/wlauncher_frame.py). No game slot: it only starts the launcher on a temporary folder.
Mutants (builds of the launcher) that must fail it: the Windows title bar kept (WL_MUT_WINDOWS_CAPTION), the goal always shown (WL_MUT_GOAL_ALWAYS), the status
staying (WL_MUT_STATUS_STAYS).   usage: qa_launcher_frame.py"""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, ROOT, PYW, win
LDIR = os.path.join(ROOT, 'launcher', 'build')
MUTANTS = ['WINDOWS_CAPTION', 'GOAL_ALWAYS', 'STATUS_STAYS']

def run(exe):
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wlauncher_frame.py')), win(os.path.join(LDIR, exe))], capture_output=True, text=True, timeout=180)
    print(r.stdout + r.stderr, end=''); return r.returncode == 0 and 'ALL OK' in r.stdout

def main():
    subprocess.run(['make', '-C', os.path.join(ROOT, 'launcher'), 'launcher'] + ['build/witness_launcher_%s.exe' % m for m in MUTANTS], check=True, capture_output=True)
    print('=== launcher frame: clean build', flush=True); ok = run('witness_launcher.exe'); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    caught = True
    for m in MUTANTS:
        print('=== launcher frame: MUTANT %s' % m, flush=True); c = not run('witness_launcher_%s.exe' % m); caught = caught and c; print('mutant %s: %s' % (m, 'caught' if c else 'NOT CAUGHT'), flush=True)
    print('ALL OK' if ok and caught else 'FAILED'); return 0 if ok and caught else 1

if __name__ == '__main__': sys.exit(main())
