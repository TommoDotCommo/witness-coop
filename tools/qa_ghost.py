#!/usr/bin/env python3
"""T2 scenario for see-through players (proxy/ghost/ghost.h, tools/win/wghost.py): B looks at a puzzle panel with A standing between B's camera and the panel; B must draw A as a ghost,
solid again when A steps out of the line, a ghost again when A steps back, and faded when A stands in B's face.
Mutant: see-through switched off on B (see_through=0): A stays solid in the way of the puzzle.
usage: qa_ghost.py (called by tools/qa.sh ghost)"""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, DIR2, PYW, pid, win
import qa_sweep

def run(cfg_b='', on='1'):
    qa_sweep.restart('name=Alpha', ('name=Beta\n' + cfg_b).strip())
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wghost.py')), str(pid('A')), win(DIR1), str(pid('B')), win(DIR2), on], capture_output=True, text=True, timeout=600)
    print(r.stdout, end=''); return r.returncode == 0 and 'ALL OK' in r.stdout

def main():
    results = []
    print('=== ghost: clean build', flush=True); ok = run(); results.append(('clean', ok)); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    print('=== ghost: MUTANT see_through=0 (B keeps A solid)', flush=True); caught = not run('see_through=0', '1'); results.append(('mutant see_through=0', caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
