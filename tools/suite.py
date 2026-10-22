#!/usr/bin/env python3
"""Shared-state suite. Each scenario restarts both games, a bot solves the first panel on A, and the result is checked against
what that scenario must produce. MUTANT scenarios are deliberately broken builds that must FAIL the check, so that a pass
means the check can fail. usage: suite.py [substring of scenario name ...]   (about 4 minutes per scenario)"""
import json, os, re, subprocess, sys, time
from witness import ROOT, TOOLS, DIR1, DIR2, PYW, RUN, pid, pyw

def sh(cmd, env=None, timeout=600):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=timeout, env={**os.environ, **(env or {})}).stdout

def last_stats(d):
    lines = [l for l in open(os.path.join(d, 'witness_proxy.log'), errors='ignore') if l.startswith('state: emitted')]
    m = re.search(r'emitted panels (\d+) doors (\d+), applied panels (\d+) doors (\d+) \| net sent (\d+) resent (\d+) in-order (\d+) dup (\d+) ooo (\d+) acked (\d+)', lines[-1]) if lines else None
    return dict(zip(['ep', 'ed', 'ap', 'ad', 'sent', 'resent', 'inorder', 'dup', 'ooo', 'acked'], map(int, m.groups()))) if m else {}

def real_saves():
    """Hashes of the owner's real save folder (%APPDATA%\\The Witness), read through Windows Python. Instances must never touch it."""
    code = ("import os,hashlib,json\nd=os.path.join(os.environ['APPDATA'],'The Witness');m={}\n"
            "for r,_,fs in os.walk(d):\n for f in fs:\n  p=os.path.join(r,f);m[os.path.relpath(p,d)]=hashlib.sha256(open(p,'rb').read()).hexdigest()\nprint(json.dumps(m))")
    return json.loads(subprocess.run([PYW, '-I', '-c', code], capture_output=True, text=True).stdout)

def fps(which, secs=4):
    return float(pyw('wfps.py', pid(which), str(secs)).strip() or 0)

def hygiene(saves_before):
    """Checks that do not depend on the scenario: real saves untouched, save folder redirected, frame cap in force."""
    out = {}
    out['real saves untouched'] = real_saves() == saves_before
    log = open(os.path.join(DIR1, 'witness_proxy.log'), errors='ignore').read()
    files = [f for _, _, fs in os.walk(os.path.join(DIR1, 'appdata')) for f in fs]
    out['save folder redirected, instance wrote %d file(s)' % len(files)] = 'SHGetFolderPathW(csidl 0x1a) -> redirected' in log and len(files) > 0
    out['_fps'] = fps('A')     # the bot left game A focused, so it is not throttled as a background window
    return out

SCENARIOS = [   # name, extra cfg for A, extra cfg for B, check(a_stats, b_stats, identical) -> (ok, why)
  ('baseline', '', '', lambda a, b, ok: (ok and b.get('ep', 9) == 0 and b.get('ed', 9) == 0 and a.get('resent', 9) == 0, 'identical, no echo from B, no resend needed')),
  ('lost first send of every event', 'drop_first_event=1', 'drop_first_event=1', lambda a, b, ok: (ok and a.get('resent', 0) >= 2, 'identical after resends (A resent >= 2)')),
  ('MUTANT: no resend + lost first send', 'drop_first_event=1\nno_resend=1', '', lambda a, b, ok: (not ok, 'must DIVERGE, otherwise the loss test tests nothing')),
  ('MUTANT: frame cap off', 'cap_fps=0', '', lambda a, b, ok: (ok, 'state still identical; the hygiene check must see an uncapped game')),
  ('MUTANT: echo guard off', '', 'no_echo_guard=1', lambda a, b, ok: (b.get('ep', 0) + b.get('ed', 0) > 0, 'B must echo events back, otherwise the echo check tests nothing')),
]
only = sys.argv[1:]; results = []
SAVES0 = real_saves()
for name, ca, cb, check in SCENARIOS:
    if only and not any(o in name for o in only): continue
    print('\n=== %s' % name, flush=True)
    sh(os.path.join(TOOLS, 'restart.sh'), {'CFG_A': ca, 'CFG_B': cb})
    time.sleep(30)    # both worlds must pass the stable-world gate (three identical scans 3 s apart)
    # The bot uses SendInput, which is desktop-wide: only one bot may run at a time on this PC (until in-process input exists).
    open(os.path.join(RUN, 'guard.stop'), 'w').write('bot starting')      # the focus guard must not fight the bot
    lab = os.environ.get('LAB', os.path.expanduser('~/source/coop-lab/lab'))
    if os.path.exists(lab): sh('%s input-lock acquire --ttl 400 --wait 1800' % lab)
    try:
        print(sh('python3 %s %s | tail -2' % (os.path.join(TOOLS, 'solve_bot.py'), pid('A'))).strip(), flush=True)
    finally:
        if os.path.exists(lab): sh('%s input-lock release' % lab)
    time.sleep(14)    # the stats line is logged every 10 s
    out = sh('python3 %s' % os.path.join(TOOLS, 'state_diff.py')); print(out.strip(), flush=True)
    a, b = last_stats(DIR1), last_stats(DIR2)
    ok, why = check(a, b, 'IDENTICAL' in out)
    h = hygiene(SAVES0); f = h.pop('_fps'); cap_on = 'cap_fps=0' not in ca
    h['frame cap: game A at %.1f fps (capped game must be 20-40, an uncapped one runs far faster)' % f] = (20 <= f <= 40) if cap_on else (f > 100)
    for k_, v_ in h.items(): print('  hygiene: %-86s %s' % (k_, 'ok' if v_ else 'FAILED'))
    cap_key = [k_ for k_ in h if k_.startswith('frame cap')][0]; others = all(v_ for k_, v_ in h.items() if k_ != cap_key)
    if name.startswith('MUTANT: frame cap'): ok = h[cap_key] and others; why = 'the cap check must see an uncapped game (> 100 fps), otherwise it tests nothing'
    else: ok = ok and h[cap_key] and others
    print('A %s\nB %s' % (a, b)); print('RESULT: %s  (%s)' % ('PASS' if ok else 'FAIL', why), flush=True)
    results.append((name, ok))
print('\n==== SUMMARY')
for n, ok in results: print('%-40s %s' % (n, 'PASS' if ok else 'FAIL'))
print('ALL OK' if results and all(ok for _, ok in results) else 'SOME FAILED')
sys.exit(0 if results and all(ok for _, ok in results) else 1)
