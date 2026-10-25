#!/usr/bin/env python3
"""T2 scenario for emotes and the character switcher (tools/win/wemote.py): A plays emotes by command and with the real wheel (G held, mouse toward a slot, G released), B must be told
and draw the motion on A's puppet, and stop when it ends; A changes character by command and with the real list (C, Down, Down, Enter) and B draws A with that model.
Mutants: emotes never sent (no_emote_send, A), emotes shown but not animated (no_emote_anim, B), characters not announced (no_character_announce, A), a key the mod took also reaches the game (no_key_swallow, A).
usage: qa_emote.py (called by tools/qa.sh emote)"""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, DIR2, PYW, pid, win
import qa_sweep

def run(cfg_a='', cfg_b=''):
    qa_sweep.restart(('name=Alpha\n' + cfg_a).strip(), ('name=Beta\n' + cfg_b).strip())
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wemote.py')), str(pid('A')), win(DIR1), str(pid('B')), win(DIR2)], capture_output=True, text=True, timeout=600)
    print(r.stdout, end=''); return r.returncode == 0 and 'ALL OK' in r.stdout

def main():
    results = []
    print('=== emote: clean build', flush=True); ok = run(); results.append(('clean', ok)); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    for name, ca, cb in (('no_emote_send (emotes never sent)', 'no_emote_send=1', ''), ('no_emote_anim (shown, not animated)', '', 'no_emote_anim=1'), ('no_character_announce (characters not announced)', 'no_character_announce=1', ''), ('no_key_swallow (Esc reaches the game too)', 'no_key_swallow=1', '')):
        print('=== emote: MUTANT %s' % name, flush=True); caught = not run(ca, cb); results.append((name, caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
