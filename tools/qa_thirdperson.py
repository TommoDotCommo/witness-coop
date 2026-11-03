#!/usr/bin/env python3
"""T2 third-person view while emoting (tools/win/wthird.py), with its mutant: tp_never_return=1 (the view never swings back) must fail.
All assertions read the mod's state bridges and the game's camera memory.
"""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, DIR2, PYW, pid, win
import qa_sweep

def run(cfg_a=''):
    qa_sweep.restart(('name=Alpha\n' + cfg_a).strip(), 'name=Beta')
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wthird.py')), str(pid('A')), win(DIR1), str(pid('B')), win(DIR2)], capture_output=True, text=True, timeout=600)
    print(r.stdout, end='')
    if r.returncode != 0 and r.stderr.strip(): print('  (the bot said:)', r.stderr.strip()[-800:])
    return r.returncode == 0 and 'ALL OK' in r.stdout

def main():
    only = sys.argv[1:]
    results = []
    if not only or 'clean' in only:
        print('=== thirdperson: clean build', flush=True); ok = run(); results.append(('clean', ok)); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    if not only or 'mutant' in only:
        print('=== thirdperson: MUTANT tp_never_return (the view never comes back)', flush=True); caught = not run('tp_never_return=1')
        results.append(('tp_never_return', caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
