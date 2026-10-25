#!/usr/bin/env python3
"""T2 scenario for name tags and ping: B is started with a name, A (the viewer) must list it with a measured ping and an on-screen tag. Mutants: tags off (A lists nobody),
a game that never answers pings (no ping), a game that never sends its name (A shows a placeholder). usage: qa_tags.py (called by tools/qa.sh tags)"""
import os, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, DIR2, PYW, pid, win
import qa_sweep

def run(cfg_a='', cfg_b=''):
    qa_sweep.restart(('name=Alpha\n' + cfg_a).strip(), ('name=Beta\n' + cfg_b).strip())
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wtags.py')), str(pid('A')), win(DIR1), str(pid('B')), win(DIR2), 'Beta'], capture_output=True, text=True, timeout=300)
    print(r.stdout, end=''); return r.returncode == 0 and 'ALL OK' in r.stdout

def main():
    results = []
    print('=== tags: clean build', flush=True); ok = run(); results.append(('clean', ok)); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    for name, ca, cb in (('no_name_tags (tags off)', 'no_name_tags=1', ''), ('no_ping_reply (B never answers pings)', '', 'no_ping_reply=1'), ('no_name_send (B never sends its name)', '', 'no_name_send=1')):
        print('=== tags: MUTANT %s' % name, flush=True); caught = not run(ca, cb); results.append((name, caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
