#!/usr/bin/env python3
"""T2 Treehouse bridges: a solve on A grows the bridge in B too (the panel's Bridge is told, as the game's own solve handler does), with the twin.
All assertions in wbridge.py read both games' memory."""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, DIR2, PYW, pid, win
import qa_sweep

def run(cfg_a='', cfg_b=''):
    qa_sweep.restart(('name=Alpha\n' + cfg_a).strip(), ('name=Beta\n' + cfg_b).strip())
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wbridge.py')), str(pid('A')), win(DIR1), str(pid('B')), win(DIR2)], capture_output=True, text=True, timeout=300)
    print(r.stdout, end='')
    if r.returncode != 0 and r.stderr.strip(): print('  (the bot said:)', r.stderr.strip()[-600:])
    return r.returncode == 0 and 'ALL OK' in r.stdout

def main():
    results = []
    print('=== bridge: clean build', flush=True); ok = run(); results.append(('clean', ok)); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    print('=== bridge: MUTANT no_bridge_advance (B marks the panel solved but does not tell its Bridge, as before 2026-11-07)', flush=True)
    caught = not run('', 'no_bridge_advance=1'); results.append(('no_bridge_advance', caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
