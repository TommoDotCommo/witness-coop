#!/usr/bin/env python3
"""T2 production emotes and proximity gestures, with config mutants.
All assertions in wemote.py read process-state bridges or camera memory.
"""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, DIR2, PYW, pid, win
import qa_sweep

def run(cfg_a='', cfg_b=''):
    qa_sweep.restart(('name=Alpha\nvoice_test_tone=1000\nvoice_mode=ptt\nvoice_mute_output=1\n' + cfg_a).strip(), ('name=Beta\n' + cfg_b).strip())
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wemote.py')), str(pid('A')), win(DIR1), str(pid('B')), win(DIR2)], capture_output=True, text=True, timeout=600)
    print(r.stdout, end=''); return r.returncode == 0 and 'ALL OK' in r.stdout

def main():
    results = []
    print('=== emote: clean build', flush=True); ok = run(); results.append(('clean', ok)); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    for name, ca, cb in (('no_emote_send (emotes never sent)', 'no_emote_send=1', ''), ('no_emote_anim (shown, not animated)', '', 'no_emote_anim=1'), ('no_key_swallow (Esc reaches the game too)', 'no_key_swallow=1', ''), ('no_speaking_flag', 'no_speaking_flag=1', ''), ('no_voice_channel', 'no_voice_channel=1', '')):
        print('=== emote: MUTANT %s' % name, flush=True); caught = not run(ca, cb); results.append((name, caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
