#!/usr/bin/env python3
"""T2 scenario for chat, the typing indicator and location pings (tools/win/wsocial.py): A types with the real key and real characters, B must see the dots, the line and a bubble; pings
land where the cross-hair is and arrive intact; a run of lines under 30% packet loss arrives once each and in order. Mutants: T does nothing (no_chat_ui), typing never announced
(no_typing_dots), presence never sent (no_presence_send), markers not drawn (no_ping_marks), reliable messages never resent (no_social_resend, under the loss).
usage: qa_social.py (called by tools/qa.sh social)"""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, DIR2, PYW, pid, win
import qa_sweep

LOSS = 'drop_pct=30\ndrop_seed=%d\n'

def run(cfg_a='', cfg_b='', loss=True):
    qa_sweep.restart(('name=Alpha\n' + (LOSS % 3 if loss else '') + cfg_a).strip(), ('name=Beta\n' + (LOSS % 5 if loss else '') + cfg_b).strip())
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wsocial.py')), str(pid('A')), win(DIR1), str(pid('B')), win(DIR2), '8'], capture_output=True, text=True, timeout=600)
    print(r.stdout, end=''); return r.returncode == 0 and 'ALL OK' in r.stdout

def main():
    results = []
    print('=== social: clean build (30%% packet loss)', flush=True); ok = run(); results.append(('clean', ok)); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    for name, ca, cb in (('no_chat_ui (T does nothing)', 'no_chat_ui=1', ''), ('no_typing_dots (typing never announced)', 'no_typing_dots=1', ''), ('no_presence_send (presence never sent)', 'no_presence_send=1', ''),
                         ('no_ping_marks (markers not drawn)', '', 'no_ping_marks=1'), ('no_social_resend (never resent, under the loss)', 'no_social_resend=1', '')):
        print('=== social: MUTANT %s' % name, flush=True); caught = not run(ca, cb); results.append((name, caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
