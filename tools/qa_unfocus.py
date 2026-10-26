#!/usr/bin/env python3
"""T2 scenario for a game window that is not active (and the real mouse) (tools/win/wunfocus.py): two games the way a person runs them (force_active=0: the real mouse, the real focus), A's window has the focus,
B's has not. With run_unfocused=1 B must still run at full speed (40 frames a second or more); without it the game sleeps and B runs at about 10 (the mutant). Holds the lab's input lock.
usage: qa_unfocus.py (called by tools/qa.sh unfocus)"""
import os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, PYW, DIR2, pid, win
import qa_sweep
LAB = os.path.expanduser('~/.local/bin/lab')

def run(cfg_b):
    subprocess.run([LAB, 'input-lock', 'acquire', '--wait', '180', '--ttl', '300'], capture_output=True)
    try:
        qa_sweep.restart('name=Alpha\nforce_active=0\ncap_fps=60\nrun_unfocused=1', ('name=Beta\nforce_active=0\ncap_fps=60\n' + cfg_b).strip())
        r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wunfocus.py')), str(pid('A')), str(pid('B')), '5', win(DIR2)], capture_output=True, text=True, timeout=120)
    finally: subprocess.run([LAB, 'input-lock', 'release'], capture_output=True)
    print(r.stdout.strip(), r.stderr.strip()[-200:])
    m = re.search(r'FPS focusedA ([0-9.]+) unfocusedB ([0-9.]+)', r.stdout); c = re.search(r'CURSOR unfocused_clip (-?\d+) unfocused_setpos (-?\d+) caged (\d)', r.stdout)
    return (float(m[1]), float(m[2]), int(c[1]), int(c[2]), int(c[3])) if m and c else None

def main():
    results = []
    print('=== unfocus: clean build (run_unfocused=1)', flush=True); f = run('run_unfocused=1'); ok = bool(f) and f[0] >= 40 and f[1] >= 40 and f[2] == 0 and f[3] == 0 and f[4] == 0;      # full speed, and the unfocused game made no attempt on the real mouse, and nothing holds it in a cage results.append(('clean', ok)); print('clean: %s %s' % ('PASS' if ok else 'FAIL', f), flush=True)
    print('=== unfocus: MUTANT run_unfocused=0 (the game sleeps in the background)', flush=True); f = run('run_unfocused=0'); caught = bool(f) and f[1] < 25; results.append(('mutant sleeps', caught)); print('mutant: %s %s' % ('caught' if caught else 'NOT CAUGHT', f), flush=True)
    print('=== unfocus: MUTANT the first way (the game\'s own \'active\' byte set: every window grabs the mouse)', flush=True); f = run('run_unfocused=1\nrun_unfocused_old_byte=1'); caught = bool(f) and (f[2] > 0 or f[3] > 0 or f[4] > 0); results.append(('mutant grabs', caught)); print('mutant: %s %s' % ('caught' if caught else 'NOT CAUGHT', f), flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
