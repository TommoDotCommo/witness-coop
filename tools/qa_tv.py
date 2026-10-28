#!/usr/bin/env python3
"""T2 TV scenario plus missing-feed integration mutant; tools/qa.sh owns cleanup."""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, DIR2, PYW, pid, win, real_saves

def run(extra=''):
    subprocess.run([os.path.join(TOOLS, 'restart.sh')], env={**os.environ, 'CFG_A': 'name=Alpha\n'+extra, 'CFG_B': 'name=Beta'}, check=True)
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win/wtv.py')), str(pid('A')), win(DIR1), str(pid('B')), win(DIR2)], text=True, capture_output=True, timeout=400)
    print(r.stdout, end=''); print(r.stderr, end='', file=sys.stderr)
    if not extra:
        return r.returncode == 0 and 'ALL OK' in r.stdout
    # A launch failure must not count as detecting the deliberately missing feed.
    return (r.returncode != 0
            and 'PASS: no consent: no source pictures, viewer shows static' in r.stdout
            and 'TV_ASSERTION: consenting stream reaches viewer' in r.stderr)

def main():
    before = real_saves()
    try:
        assert run(), 'clean TV scenario failed'
        assert run('no_partner_video=1'), 'missing-feed mutant did not fail its live-feed assertion'
    finally:
        assert real_saves() == before, 'real saves changed'
    print('ALL OK: TV clean + missing-feed mutant rejected')
if __name__ == '__main__': main()
