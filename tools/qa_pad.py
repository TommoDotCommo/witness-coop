#!/usr/bin/env python3
"""T2 scenario for the co-op pad layer (tools/win/wpad.py): A stands in for a pad through the game's own XInputGetState path; hold View opens the player
panel, View + D-pad up the emote wheel while the player stays put, View + RB quick chat reaches B, a tap pings, View + D-pad down types on the on-screen keyboard.
Mutant: pad_leak=1 (only View is taken: the D-pad and the sticks reach the game while View is held) must fail.
usage: qa_pad.py [clean|mutant] (called by tools/qa.sh pad; `clean`: without the mutant, `mutant`: only the mutant)"""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, DIR2, PYW, pid, win
import qa_sweep

def run(cfg_a=''):
    qa_sweep.restart(('name=Alpha\nvoice_mode=auto\n' + cfg_a).strip(), 'name=Beta\nvoice_mode=off')
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wpad.py')), str(pid('A')), win(DIR1), str(pid('B')), win(DIR2)], capture_output=True, text=True, timeout=400)
    print(r.stdout, end='')
    if r.returncode != 0 and r.stderr.strip(): print('  (the bot said:)', r.stderr.strip()[-600:])
    return r.returncode == 0 and 'ALL OK' in r.stdout

def main():
    results = []
    if sys.argv[1:] != ['mutant']:
        print('=== pad: clean build', flush=True); ok = run(); results.append(('clean', ok)); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    if sys.argv[1:] != ['clean']:
        print('=== pad: MUTANT pad_leak (the D-pad and the sticks reach the game while View is held)', flush=True)
        caught = not run('pad_leak=1'); results.append(('pad_leak', caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
