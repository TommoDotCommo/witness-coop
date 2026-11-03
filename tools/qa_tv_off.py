#!/usr/bin/env python3
"""T2 experimental_tv gating: both games with the default (off), then A on next to B off; tools/qa.sh owns cleanup."""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, DIR2, PYW, pid, win, real_saves

def run(mode, cfg_a):
    subprocess.run([os.path.join(TOOLS, 'restart.sh')], env={**os.environ, 'CFG_A': 'name=Alpha\n' + cfg_a, 'CFG_B': 'name=Beta'}, check=True)
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win/wtvoff.py')), str(pid('A')), win(DIR1), str(pid('B')), win(DIR2), mode], text=True, capture_output=True, timeout=400)
    print(r.stdout, end=''); print(r.stderr, end='', file=sys.stderr)
    return r.returncode == 0 and 'ALL OK' in r.stdout

def main():
    before = real_saves()
    try:
        assert run('off', ''), 'default (experimental_tv off) scenario failed'
        assert run('mixed', 'experimental_tv=1\n'), 'mixed scenario (A on, B off) failed'
    finally:
        assert real_saves() == before, 'real saves changed'
    print('ALL OK: experimental_tv off by default on both; A on next to B off breaks nothing')
if __name__ == '__main__': main()
