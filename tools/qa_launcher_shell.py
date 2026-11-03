#!/usr/bin/env python3
"""Game-free check of the launcher's shape on its real window (tools/win/wlauncher_shell.py): tabs, settings saved on change, press-a-key capture, Reset section,
the remembered Play choice, the lobby view and the pad (injected states). No game slot: it only starts the launcher on a temporary folder.
Mutant (a build of the launcher): a change is not saved (WL_MUT_NO_SAVE_ON_CHANGE) must fail it.   usage: qa_launcher_shell.py"""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, ROOT, PYW, win
LDIR = os.path.join(ROOT, 'launcher', 'build')

def run(exe):
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wlauncher_shell.py')), win(os.path.join(LDIR, exe))], capture_output=True, text=True, timeout=180)
    print(r.stdout + r.stderr, end=''); return r.returncode == 0 and 'ALL OK' in r.stdout

def main():
    subprocess.run(['make', '-C', os.path.join(ROOT, 'launcher'), 'launcher', 'build/witness_launcher_NO_SAVE_ON_CHANGE.exe'], check=True, capture_output=True)
    print('=== launcher shell: clean build', flush=True); ok = run('witness_launcher.exe'); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    print('=== launcher shell: MUTANT a change is not saved', flush=True); caught = not run('witness_launcher_NO_SAVE_ON_CHANGE.exe'); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    print('ALL OK' if ok and caught else 'FAILED'); return 0 if ok and caught else 1

if __name__ == '__main__': sys.exit(main())
