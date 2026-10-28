#!/usr/bin/env python3
"""T2 scenario for body-overlap transparency (proxy/ghost/ghost.h, tools/win/wghost.py): A stays solid between B and a puzzle,
at shoulder-to-shoulder distance and on another level, fading only when A's body intersects B's viewer capsule.
Mutant: see-through switched off on B (see_through=0): A stays solid during actual body overlap.
usage: qa_ghost.py (called by tools/qa.sh ghost)"""
import os, subprocess, sys, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import ROOT, TOOLS, DIR1, DIR2, PYW, pid, win, real_saves
import qa_sweep

def run(cfg_b='', on='1'):
    qa_sweep.restart('name=Alpha', ('name=Beta\n' + cfg_b).strip())
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wghost.py')), str(pid('A')), win(DIR1), str(pid('B')), win(DIR2), on] + ([] if cfg_b else [win(os.path.join(ROOT, 'local', 'ghost-captures'))]), capture_output=True, text=True, timeout=600)
    print(r.stdout, end=''); print(r.stderr, end='')
    if cfg_b:
        assert re.search(r"3\. A intersecting B's body fades\s+FAILED", r.stdout), 'mutant must fail on overlap opacity itself'
    return r.returncode == 0 and 'ALL OK' in r.stdout

def main():
    before = real_saves(); results = []
    print('=== ghost: clean build', flush=True); ok = run(); results.append(('clean', ok)); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    print('=== ghost: MUTANT see_through=0 (B keeps A solid)', flush=True); caught = not run('see_through=0', '1'); results.append(('mutant see_through=0', caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    safe = real_saves() == before
    results.append(('real saves byte-identical', safe))
    print('real saves byte-identical: %s' % safe, flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
