#!/usr/bin/env python3
"""T2 scenario for the pause menu: nothing of ours is drawn over it. A looks at the player B (a puppet, drawn); Esc opens the game's pause menu on A (the flag
the proxy reads must be 1) and the puppet must not be drawn; Esc again closes it and the puppet is back. Mutants: no_pause_hide=1 (puppets drawn over the menu) and pause_wrong_flag=1 (the first flag candidate, which flips with the model keys and with entering a puzzle).
usage: qa_pause.py (called by tools/qa.sh pause)"""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, DIR2, PYW, pid, win, real_saves
import qa_sweep

def run(cfg_a=''):
    qa_sweep.restart(('name=Alpha\n' + cfg_a).strip(), 'name=Beta')
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wpause.py')), str(pid('A')), win(DIR1), str(pid('B')), win(DIR2)], capture_output=True, text=True, timeout=300)
    print(r.stdout, end=''); return r.returncode == 0 and 'ALL OK' in r.stdout

def main():
    before = real_saves(); results = []
    print('=== pause: clean build', flush=True); ok = run(); results.append(('clean', ok)); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    for name, cfg in (('no_pause_hide (puppets drawn over the pause menu)', 'no_pause_hide=1'), ('pause_wrong_flag (the first flag candidate, which flips with F6 / F7 and puzzles)', 'pause_wrong_flag=1')):
        print('=== pause: MUTANT %s' % name, flush=True); caught = not run(cfg); results.append((name, caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    safe = real_saves() == before
    results.append(('real saves byte-identical', safe))
    print('real saves byte-identical: %s' % safe, flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
