#!/usr/bin/env python3
"""Shared-state suite. Each scenario starts both games, a bot solves the first panel on A, and the result is checked against
what that scenario must produce. MUTANT scenarios are deliberately broken builds that must FAIL the check, so that a pass
means the check can fail. usage: suite.py [substring of scenario name ...]   (about 4 minutes per scenario)"""
import json, os, re, shlex, subprocess, sys, time
from witness import ROOT, TOOLS, DIR1, DIR2, PYW, RUN, pid, pyw, win

def sh(cmd, env=None, timeout=600):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=timeout, env={**os.environ, **(env or {})}).stdout

def last_stats(d):
    lines = [l for l in open(os.path.join(d, 'witness_proxy.log'), errors='ignore') if l.startswith('state: emitted')]
    m = re.search(r'emitted panels (\d+) doors (\d+), applied panels (\d+) doors (\d+) \| net sent (\d+) resent (\d+) in-order (\d+) dup (\d+) ooo (\d+) acked (\d+)', lines[-1]) if lines else None
    out = dict(zip(['ep', 'ed', 'ap', 'ad', 'sent', 'resent', 'inorder', 'dup', 'ooo', 'acked'], map(int, m.groups()))) if m else {}
    power = re.search(r'\| power emitted (\d+) applied (\d+)', lines[-1]) if lines else None
    if power: out.update(zip(['epow', 'apow'], map(int, power.groups())))
    return out

def real_saves():
    """Hashes of the owner's real save folder (%APPDATA%\\The Witness), read through Windows Python. Instances must never touch it."""
    code = ("import os,hashlib,json\nd=os.path.join(os.environ['APPDATA'],'The Witness');m={}\n"
            "for r,_,fs in os.walk(d):\n for f in fs:\n  p=os.path.join(r,f);m[os.path.relpath(p,d)]=hashlib.sha256(open(p,'rb').read()).hexdigest()\nprint(json.dumps(m))")
    return json.loads(subprocess.run([PYW, '-I', '-c', code], capture_output=True, text=True).stdout)

def fps(which, secs=4):
    return float(pyw('wfps.py', pid(which), str(secs)).strip() or 0)

def state(which):
    return json.loads(pyw('wstatedump.py', pid(which)))

def restart_catchup(ca, cb, mutant, include_power=False):
    """Discard B's world after the live solve has already been delivered and ACKed.

    Assert against process memory before and after; a log saying 'resync' is not evidence of restored progress.
    """
    before_a, before_b = state('A'), state('B')
    expected = {'panels': '0x64', 'doors': '0x34eda'}
    if include_power: expected['powered_panels'] = '0x1b'
    for kind, entity in expected.items():
        if entity not in before_a[kind] or before_a[kind] != before_b[kind]:
            return False, 'precondition failed: both worlds must have the solved panel and open door before B restarts'
    stats = last_stats(DIR1)
    if not stats or stats['sent'] != stats['acked']:
        return False, 'precondition failed: wait for all original events to be acknowledged'
    old_a, old_b = pid('A'), pid('B')
    result = subprocess.run([os.path.join(TOOLS, 'restart.sh'), 'B'],
                            env={**os.environ, 'CFG_A': ca, 'CFG_B': cb}, capture_output=True, text=True, timeout=240)
    if result.returncode:
        return False, 'B restart failed: ' + result.stdout + result.stderr
    time.sleep(30)
    open(os.path.join(RUN, 'guard.stop'), 'w').write('restart complete')
    after_a, after_b = state('A'), state('B')
    if pid('A') != old_a or pid('B') == old_b:
        return False, 'restart must replace only B, keeping A alive'
    for kind in expected:
        if after_a[kind] != before_a[kind]: return False, 'surviving A lost or changed its progress'
    if after_b['n_panels'] != before_b['n_panels'] or after_b['n_doors'] != before_b['n_doors']:
        return False, 'B entity census is incomplete after restart'
    if mutant:
        ok = all(expected[kind] not in after_b[kind] for kind in expected)
        return ok, 'catch-up disabled: fresh B must lack panel 0x64 and door 0x34eda'
    ok = all(after_a[kind] == after_b[kind] for kind in expected)
    return ok, 'fresh B restored all progress from surviving A after original events were ACKed'

def hygiene(saves_before):
    """Checks that do not depend on the scenario: real saves untouched, save folder redirected, frame cap in force."""
    out = {}
    out['real saves untouched'] = real_saves() == saves_before
    log = open(os.path.join(DIR1, 'witness_proxy.log'), errors='ignore').read()
    files = [f for _, _, fs in os.walk(os.path.join(DIR1, 'appdata')) for f in fs]
    out['save folder redirected, instance wrote %d file(s)' % len(files)] = 'SHGetFolderPathW(csidl 0x1a) -> redirected' in log and len(files) > 0
    clip = pyw('wmouse.py').split()      # real cursor and system clip rectangle: the game must never grab the owner's mouse
    out['real mouse not grabbed (clip rect is the whole screen)'] = clip[0] == 'free'
    out['_fps'] = fps('A')     # game A is not focused: force_active keeps it at the cap instead of the ~10 fps background throttle
    return out

SCENARIOS = [   # name, extra cfg for A, extra cfg for B, check(a_stats, b_stats, identical) -> (ok, why)
  ('baseline', '', '', lambda a, b, ok: (ok and b.get('ep', 9) == 0 and b.get('ed', 9) == 0 and a.get('resent', 9) == 0, 'identical, no echo from B, no resend needed')),
  ('lost first send of every event', 'drop_first_event=1', 'drop_first_event=1', lambda a, b, ok: (ok and a.get('resent', 0) >= 2, 'identical after resends (A resent >= 2)')),
  ('MUTANT: no resend + lost first send', 'drop_first_event=1\nno_resend=1', '', lambda a, b, ok: (not ok, 'must DIVERGE, otherwise the loss test tests nothing')),
  ('30% random packet loss', 'drop_pct=30\ndrop_seed=7', 'drop_pct=30\ndrop_seed=11', lambda a, b, ok: (ok, 'identical despite ~30% of datagrams dropped in each direction (seeded)')),
  ('MUTANT: no resend + 30% random loss', 'drop_pct=30\ndrop_seed=7\nno_resend=1', 'drop_pct=30\ndrop_seed=11', lambda a, b, ok: (not ok, 'must DIVERGE, otherwise the random-loss test tests nothing')),
  ('MUTANT: frame cap off', 'cap_fps=0', '', lambda a, b, ok: (ok, 'state still identical; the hygiene check must see an uncapped game')),
  ('MUTANT: echo guard off', '', 'no_echo_guard=1', lambda a, b, ok: (b.get('ep', 0) + b.get('ed', 0) > 0, 'B must echo events back, otherwise the echo check tests nothing')),
  ('peer restart catch-up', '', '', lambda a, b, ok: (ok and b.get('ap', 0) >= 1 and b.get('ad', 0) >= 1 and b.get('ep', 9) == 0 and b.get('ed', 9) == 0, 'fresh B catches up without echoing')),
  ('MUTANT: peer restart catch-up disabled', 'no_resync_on_restart=1', '', lambda a, b, ok: (not ok, 'fresh B must diverge without full-state catch-up')),
  ('second tutorial door', '', '', lambda a, b, ok: (ok and b.get('ep', 9) == 0 and b.get('ed', 9) == 0, 'both tutorial panels and doors match without B echo')),
  ('MUTANT: second tutorial door skips entity zero', 'skip_entity_zero=1', '', lambda a, b, ok: (not ok, 'door entity zero must be missing on B')),
  ('panel power chain', '', '', lambda a, b, ok: (ok and b.get('epow', 9) == 0 and b.get('apow', 0) >= 1, 'target panel powers on without being solved or echoed')),
  ('MUTANT: panel power chain disabled', 'no_panel_power=1', '', lambda a, b, ok: (not ok, 'target panel must remain dark on B')),
]
only = sys.argv[1:]; results = []
SAVES0 = real_saves()
for name, ca, cb, check in SCENARIOS:
    if only and not any(o in name for o in only): continue
    print('\n=== %s' % name, flush=True)
    subprocess.run([os.path.join(TOOLS, 'restart.sh')], env={**os.environ, 'CFG_A': ca, 'CFG_B': cb},
                   capture_output=True, text=True, timeout=600, check=True)
    time.sleep(30)    # both worlds must pass the stable-world gate (three identical scans 3 s apart)
    # The bot is in-process (tp, mouse and button commands to the proxy): no focus, no desktop input, no lock; several can run at once.
    open(os.path.join(RUN, 'guard.stop'), 'w').write('bot starting')      # the focus guard (for the game grabbing the foreground while loading) ends
    bot = sh('%s -I %s %s %s 2>&1 | tail -3' % (shlex.quote(PYW), shlex.quote(win(os.path.join(TOOLS, 'win', 'wbot.py'))), pid('A'), shlex.quote(win(DIR1)))).strip()
    print(bot, flush=True)
    bot_ok = 'solved: True' in bot      # a scenario that never solved the panel proves nothing, whatever the state diff says
    second_ok = True
    if 'second tutorial door' in name and bot_ok:
        bot2 = pyw('wbot2.py', pid('A'), win(DIR1))
        print(bot2.strip(), flush=True)
        second_ok = 'panel 0x182 solved: True' in bot2
    power_ok = True
    if 'panel power chain' in name and bot_ok:
        before_a, before_b = state('A'), state('B')
        power_ok = all('0x1b' not in s['powered_panels'] and '0x18af' not in s['panels'] for s in (before_a, before_b))
        if power_ok:
            power_bot = pyw('wbot_power.py', pid('A'), win(DIR1))
            print(power_bot.strip(), flush=True)
            power_ok = 'panel 0x18af solved: True | target 0x1b powered: True | target unsolved: True' in power_bot
    time.sleep(14)    # the stats line is logged every 10 s
    catchup = None
    if 'restart catch-up' in name and bot_ok:
        catchup = restart_catchup(ca, cb, name.startswith('MUTANT:'))
    elif 'panel power chain' in name and bot_ok and power_ok and not name.startswith('MUTANT:'):
        catchup = restart_catchup(ca, cb, False, include_power=True)
    out = sh('python3 %s' % os.path.join(TOOLS, 'state_diff.py')); print(out.strip(), flush=True)
    a, b = last_stats(DIR1), last_stats(DIR2)
    ok, why = check(a, b, 'IDENTICAL' in out)
    if 'panel power chain' in name:
        sa, sb = state('A'), state('B')
        same_solves = sa['panels'] == sb['panels'] and '0x18af' in sa['panels'] and '0x1b' not in sa['panels']
        powered_a, powered_b = set(sa['powered_panels']), set(sb['powered_panels'])
        if name.startswith('MUTANT:'):
            target_ok = powered_a - powered_b == {'0x1b'} and not (powered_b - powered_a)
        else: target_ok = powered_a == powered_b and '0x1b' in powered_b
        ok = ok and power_ok and same_solves and target_ok and sa['doors'] == sb['doors']
        if not power_ok: why = 'power bot or fresh-chain precondition failed'
    if 'second tutorial door' in name:
        sa, sb = state('A'), state('B')
        panels_ok = all(entity in sa['panels'] and entity in sb['panels'] for entity in ('0x64', '0x182'))
        doors_ok = all(entity in sa['doors'] for entity in ('0x34eda', '0x0')) and '0x34eda' in sb['doors']
        zero_ok = ('0x0' not in sb['doors']) if name.startswith('MUTANT:') else ('0x0' in sb['doors'])
        ok = ok and second_ok and panels_ok and doors_ok and zero_ok
        if not second_ok: why = 'the bot did not solve the second panel'
    if catchup:
        ok = ok and catchup[0]; why = catchup[1]
    if not bot_ok: ok, why = False, 'the bot did not solve the panel (a state diff of two untouched games proves nothing)'
    h = hygiene(SAVES0); f = h.pop('_fps'); cap_on = 'cap_fps=0' not in ca
    h['frame cap: game A at %.1f fps (capped 20-40; uncapped far more)' % f] = (20 <= f <= 40) if cap_on else (f > 100)
    for k_, v_ in h.items(): print('  hygiene: %-86s %s' % (k_, 'ok' if v_ else 'FAILED'))
    cap_key = [k_ for k_ in h if k_.startswith('frame cap')][0]; others = all(v_ for k_, v_ in h.items() if k_ != cap_key)
    if name.startswith('MUTANT: frame cap'): ok = h[cap_key] and others and bot_ok; why = 'the cap check must see an uncapped game (> 100 fps), otherwise it tests nothing'
    else: ok = ok and h[cap_key] and others
    if not bot_ok: ok = False
    print('A %s\nB %s' % (a, b)); print('RESULT: %s  (%s)' % ('PASS' if ok else 'FAIL', why), flush=True)
    results.append((name, ok))
print('\n==== SUMMARY')
for n, ok in results: print('%-40s %s' % (n, 'PASS' if ok else 'FAIL'))
print('ALL OK' if results and all(ok for _, ok in results) else 'SOME FAILED')
sys.exit(0 if results and all(ok for _, ok in results) else 1)
