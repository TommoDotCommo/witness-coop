#!/usr/bin/env python3
"""T2 scenario for the UI glyph atlas: no glyph slot holds ink that belongs to the glyph above it (little bits of pixels over 't' and 'v'). Mutant: the old tight atlas.
Also the held keys (tools/win/wkeyrepeat.py): a held F8 / Z / V acts once, a held Tab opens the player panel once; twin key_repeat_toggle=1 (every autorepeat acts).
usage: qa_ui.py [keys]   (called by tools/qa.sh ui; `keys` runs only the held-key check and its twin)"""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, PYW, pid, win, real_saves
import qa_sweep

def run(cfg_a=''):
    qa_sweep.restart(cfg_a, '')
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'watlas.py')), win(DIR1)], capture_output=True, text=True, timeout=120)
    print(r.stdout, end=''); return r.returncode == 0 and 'ALL OK' in r.stdout

def run_keys(cfg_a=''):
    """Held keys: real keyboard autorepeat messages to the game window, the proxy's keystat counters are the verdict."""
    qa_sweep.restart(cfg_a, '', which='A')
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wkeyrepeat.py')), win(DIR1), pid('A')], capture_output=True, text=True, timeout=180)
    print(r.stdout, end=''); return r.returncode == 0 and 'ALL OK' in r.stdout

def main():
    before = real_saves(); results = []; only_keys = len(sys.argv) > 1 and sys.argv[1] == 'keys'
    if not only_keys:
        print('=== ui: clean build', flush=True); ok = run(); results.append(('clean', ok)); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
        print('=== ui: MUTANT ui_tight_atlas (the old atlas)', flush=True); caught = not run('ui_tight_atlas=1'); results.append(('tight', caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    print('=== ui: held keys act once (F8, Tab, Z, V)', flush=True); ok = run_keys(); results.append(('held keys', ok)); print('held keys: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    print('=== ui: MUTANT key_repeat_toggle (every autorepeat press acts again)', flush=True); caught = not run_keys('key_repeat_toggle=1'); results.append(('key repeat toggle', caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    safe = real_saves() == before
    results.append(('real saves byte-identical', safe))
    print('real saves byte-identical: %s' % safe, flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
