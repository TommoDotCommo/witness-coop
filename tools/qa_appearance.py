#!/usr/bin/env python3
"""Two-client appearance sync, actual-renderer captures and launcher controls."""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import ROOT, TOOLS, DIR1, DIR2, PYW, RUN, pid, win, real_saves
import qa_sweep

NAMES = ('skin', 'hair', 'eyes', 'coat', 'scarf', 'trousers', 'boots', 'gear')

def palette(value):
    return '\n'.join('appearance_%s=%d' % (name, value) for name in NAMES) + '\nsee_through=0'

def main():
    out = os.path.join(ROOT, 'local', 'appearance-captures')
    os.makedirs(out, exist_ok=True)
    before = real_saves(); results = []
    try:
        qa_sweep.restart(palette(1), palette(2))
        r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wappearance.py')),
                            pid('A'), win(DIR1), pid('B'), win(DIR2), win(out)],
                           capture_output=True, text=True, timeout=180)
        print(r.stdout, end='')
        if r.returncode and r.stderr: print(r.stderr, end='')
        results.append(('two-client palettes and captures', r.returncode == 0 and 'ALL OK' in r.stdout))

        subprocess.run(['make', '-C', os.path.join(ROOT, 'launcher'), 'launcher'], check=True, capture_output=True)
        exe = os.path.join(ROOT, 'launcher', 'build', 'witness_launcher.exe')
        r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wlauncher_appearance.py')),
                            win(exe), win(DIR1), win(os.path.join(out, 'launcher_appearance.png'))],
                           capture_output=True, text=True, timeout=90)
        print(r.stdout, end='')
        if r.returncode and r.stderr: print(r.stderr, end='')
        results.append(('launcher appearance controls', r.returncode == 0 and 'ALL OK' in r.stdout))
    finally:
        safe = real_saves() == before
        print('hygiene: real saves byte-identical: %s' % safe, flush=True)
        results.append(('real saves byte-identical', safe))
    bad = [name for name, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad)
    return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
