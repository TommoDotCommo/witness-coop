#!/usr/bin/env python3
"""Shared-state suite. Each scenario restarts both games, a bot solves the first panel on A, and the result is checked against
what that scenario must produce. MUTANT scenarios are deliberately broken builds that must FAIL the check, so that a pass
means the check can fail. usage: suite.py [substring of scenario name ...]   (about 4 minutes per scenario)"""
import os, re, subprocess, sys, time
from witness import ROOT, TOOLS, DIR1, DIR2, pid

def sh(cmd, env=None, timeout=600):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=timeout, env={**os.environ, **(env or {})}).stdout

def last_stats(d):
    lines = [l for l in open(os.path.join(d, 'witness_proxy.log'), errors='ignore') if l.startswith('state: emitted')]
    m = re.search(r'emitted panels (\d+) doors (\d+), applied panels (\d+) doors (\d+) \| net sent (\d+) resent (\d+) in-order (\d+) dup (\d+) ooo (\d+) acked (\d+)', lines[-1]) if lines else None
    return dict(zip(['ep', 'ed', 'ap', 'ad', 'sent', 'resent', 'inorder', 'dup', 'ooo', 'acked'], map(int, m.groups()))) if m else {}

SCENARIOS = [   # name, extra cfg for A, extra cfg for B, check(a_stats, b_stats, identical) -> (ok, why)
  ('baseline', '', '', lambda a, b, ok: (ok and b.get('ep', 9) == 0 and b.get('ed', 9) == 0 and a.get('resent', 9) == 0, 'identical, no echo from B, no resend needed')),
  ('lost first send of every event', 'drop_first_event=1', 'drop_first_event=1', lambda a, b, ok: (ok and a.get('resent', 0) >= 2, 'identical after resends (A resent >= 2)')),
  ('MUTANT: no resend + lost first send', 'drop_first_event=1\nno_resend=1', '', lambda a, b, ok: (not ok, 'must DIVERGE, otherwise the loss test tests nothing')),
  ('MUTANT: echo guard off', '', 'no_echo_guard=1', lambda a, b, ok: (b.get('ep', 0) + b.get('ed', 0) > 0, 'B must echo events back, otherwise the echo check tests nothing')),
]
only = sys.argv[1:]; results = []
for name, ca, cb, check in SCENARIOS:
    if only and not any(o in name for o in only): continue
    print('\n=== %s' % name, flush=True)
    sh(os.path.join(TOOLS, 'restart.sh'), {'CFG_A': ca, 'CFG_B': cb})
    time.sleep(30)    # both worlds must pass the stable-world gate (three identical scans 3 s apart)
    print(sh('python3 %s %s | tail -2' % (os.path.join(TOOLS, 'solve_bot.py'), pid('A'))).strip(), flush=True)
    time.sleep(14)    # the stats line is logged every 10 s
    out = sh('python3 %s' % os.path.join(TOOLS, 'state_diff.py')); print(out.strip(), flush=True)
    a, b = last_stats(DIR1), last_stats(DIR2)
    ok, why = check(a, b, 'IDENTICAL' in out)
    print('A %s\nB %s' % (a, b)); print('RESULT: %s  (%s)' % ('PASS' if ok else 'FAIL', why), flush=True)
    results.append((name, ok))
print('\n==== SUMMARY')
for n, ok in results: print('%-40s %s' % (n, 'PASS' if ok else 'FAIL'))
print('ALL OK' if results and all(ok for _, ok in results) else 'SOME FAILED')
sys.exit(0 if results and all(ok for _, ok in results) else 1)
