#!/usr/bin/env python3
"""T2 scenario for the launcher's looks under the mouse (tools/win/wlook.py): hovering along a tab must not repaint the row of tabs again and again, and no dropdown box covers a text.
Mutants (builds of the launcher): the tab row repaints on every mouse move (the flicker), the dropdowns are 220 wide again.   usage: qa_looks.py (called by tools/qa.sh looks)"""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, ROOT, RUN, PYW, win
LDIR = os.path.join(ROOT, 'launcher', 'build')

def run(exe):
    subprocess.run([os.environ['WITNESS_RESOURCES'], 'input-lock', 'acquire', '--wait', '120', '--ttl', '120'], capture_output=True, check=True)      # the real pointer is used for about a second
    try: return run_locked(exe)
    finally: subprocess.run([os.environ['WITNESS_RESOURCES'], 'input-lock', 'release'], capture_output=True)

def run_locked(exe):
    p = os.path.join(RUN, 'looks_launcher.exe'); subprocess.run(['cp', os.path.join(LDIR, exe), p], check=True)
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wlook.py')), win(p), win(RUN), win(os.path.join(RUN, 'looks_stat.txt'))], capture_output=True, text=True, timeout=120)
    print(r.stdout, end=''); return r.returncode == 0 and 'ALL OK' in r.stdout

def main():
    subprocess.run(['make', '-C', os.path.join(ROOT, 'launcher'), 'launcher', 'launcher-mutants'], check=True, capture_output=True)
    results = []
    print('=== looks: clean build', flush=True); ok = run('witness_launcher.exe'); results.append(('clean', ok)); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    for name, exe in (('tabs repaint on every move (flicker)', 'witness_launcher_TAB_REPAINT_ALWAYS.exe'), ('dropdowns 220 wide again', 'witness_launcher_WIDE_COMBOS.exe'), ('a label longer than its box', 'witness_launcher_LONG_LABELS.exe')):
        print('=== looks: MUTANT %s' % name, flush=True); caught = not run(exe); results.append((name, caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
