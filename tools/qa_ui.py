#!/usr/bin/env python3
"""T2 scenario for the UI glyph atlas: no glyph slot holds ink that belongs to the glyph above it (little bits of pixels over 't' and 'v'). Mutant: the old tight atlas.
usage: qa_ui.py (called by tools/qa.sh ui)"""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, PYW, win, real_saves
import qa_sweep

def run(cfg_a=''):
    qa_sweep.restart(cfg_a, '')
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'watlas.py')), win(DIR1)], capture_output=True, text=True, timeout=120)
    print(r.stdout, end=''); return r.returncode == 0 and 'ALL OK' in r.stdout

def main():
    before = real_saves(); results = []
    print('=== ui: clean build', flush=True); ok = run(); results.append(('clean', ok)); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    print('=== ui: MUTANT ui_tight_atlas (the old atlas)', flush=True); caught = not run('ui_tight_atlas=1'); results.append(('tight', caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    safe = real_saves() == before
    results.append(('real saves byte-identical', safe))
    print('real saves byte-identical: %s' % safe, flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
