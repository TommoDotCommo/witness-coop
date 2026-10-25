#!/usr/bin/env python3
"""T2 scenario for free drawing (tools/win/wink.py): A draws with the canvas pen (X held, mouse moves the pen, the camera stays), B must hold the same stroke, draw it, and lose it on undo / clear;
the ink must sit on the surface under the pen. Mutants: strokes never sent (no_draw_send, A), never drawn (no_draw_draw, B), put at a fixed distance (no_draw_depth, A).
usage: qa_ink.py (called by tools/qa.sh ink)"""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, DIR2, PYW, pid, win
import qa_sweep

def run(cfg_a='', cfg_b=''):
    qa_sweep.restart(('name=Alpha\n' + cfg_a).strip(), ('name=Beta\n' + cfg_b).strip())
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wink.py')), str(pid('A')), win(DIR1), str(pid('B')), win(DIR2)], capture_output=True, text=True, timeout=600)
    print(r.stdout, end=''); return r.returncode == 0 and 'ALL OK' in r.stdout

def main():
    results = []
    print('=== ink: clean build', flush=True); ok = run(); results.append(('clean', ok)); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    for name, ca, cb in (('no_draw_send (strokes never sent)', 'no_draw_send=1', ''), ('no_draw_draw (not drawn)', '', 'no_draw_draw=1'), ('no_draw_depth (fixed distance)', 'no_draw_depth=1', '')):
        print('=== ink: MUTANT %s' % name, flush=True); caught = not run(ca, cb); results.append((name, caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
