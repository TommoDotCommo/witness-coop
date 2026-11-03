#!/usr/bin/env python3
"""T2 scenario for the shared co-op keys and the hold-Tab player panel (tools/win/wshared.py). A starts with the OLD default keys in its cfg, B with a custom chat key on V.
Mutants: V still watches (old_v_watch=1: the ping never goes out), Tab does not open the panel (no_player_panel=1), a cfg that is not migrated is covered by the T0 guide test.
usage: qa_shared.py (called by tools/qa.sh shared)"""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, DIR2, PYW, pid, win
import qa_sweep

OLD = 'key_ping=P\nkey_voice=N\nkey_emote=G\nkey_voice_channel=H\nkey_color=B\nvoice_mode=ptt\nvoice_test_tone=120,1000,5500\nvoice_volume=50\n'

def run(cfg_a='', cfg_b=''):
    for d in (DIR1, DIR2):      # the one-time list is written once per user: start from none
        for f in ('moved_keys.txt', 'keys_scheme'):
            try: os.remove(os.path.join(d, f))
            except OSError: pass
    qa_sweep.restart(('name=Alpha\n' + OLD + cfg_a).strip(), ('name=Beta\nkey_chat=V\n' + cfg_b).strip())
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wshared.py')), str(pid('A')), win(DIR1), str(pid('B')), win(DIR2)], capture_output=True, text=True, timeout=500)
    print(r.stdout, end='')
    if r.returncode != 0 and r.stderr.strip(): print('  (the bot said:)', r.stderr.strip()[-600:])
    return r.returncode == 0 and 'ALL OK' in r.stdout

def main():
    results = []
    print('=== shared: clean build', flush=True); ok = run(); results.append(('clean', ok)); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    for name, ca in (('old_v_watch (V watches, so the ping never goes out)', 'old_v_watch=1'), ('no_player_panel (Tab does not open the panel)', 'no_player_panel=1')):
        print('=== shared: MUTANT %s' % name, flush=True); caught = not run(ca); results.append((name, caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
