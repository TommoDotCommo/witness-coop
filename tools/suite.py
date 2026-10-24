#!/usr/bin/env python3
"""Shared-state suite. Each scenario starts both games, a bot solves the first panel on A, and the result is checked against
what that scenario must produce. MUTANT scenarios are deliberately broken builds that must FAIL the check, so that a pass
means the check can fail. usage: suite.py [substring of scenario name ...]   (about 4 minutes per scenario)"""
import json, os, re, shlex, shutil, subprocess, sys, time
from witness import ROOT, TOOLS, DIR1, DIR2, PYW, RUN, pid, pyw, win

LASER_ON = 0xbf800000      # laser +0x12c after the dispatcher switched it on

def sh(cmd, env=None, timeout=600):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=timeout, env={**os.environ, **(env or {})}).stdout

def last_stats(d):
    lines = [l for l in open(os.path.join(d, 'witness_proxy.log'), errors='ignore') if l.startswith('state: emitted')]
    m = re.search(r'emitted panels (\d+) doors (\d+), applied panels (\d+) doors (\d+) \| net sent (\d+) resent (\d+) in-order (\d+) dup (\d+) ooo (\d+) acked (\d+)', lines[-1]) if lines else None
    out = dict(zip(['ep', 'ed', 'ap', 'ad', 'sent', 'resent', 'inorder', 'dup', 'ooo', 'acked'], map(int, m.groups()))) if m else {}
    power = re.search(r'\| power emitted (\d+) applied (\d+)', lines[-1]) if lines else None
    if power: out.update(zip(['epow', 'apow'], map(int, power.groups())))
    cable = re.search(r'\| cable emitted (\d+) applied (\d+)', lines[-1]) if lines else None
    if cable: out.update(zip(['ecab', 'acab'], map(int, cable.groups())))
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

def restart_catchup(ca, cb, mutant, include_power=False, expected=None):
    """Discard B's world after the live solve has already been delivered and ACKed.

    Assert against process memory before and after; a log saying 'resync' is not evidence of restored progress.
    """
    before_a, before_b = state('A'), state('B')
    expected = dict(expected or {'panels': '0x64', 'doors': '0x34eda'})
    if include_power: expected['powered_panels'] = '0x1b'
    for kind, entity in expected.items():
        if entity not in before_a[kind] or before_a[kind] != before_b[kind]:
            return False, 'precondition failed: both worlds must have the solved panel and its effects before B restarts'
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
        if after_a[kind] != before_a[kind]:
            delta = {k_: (before_a[kind].get(k_), after_a[kind].get(k_)) for k_ in set(before_a[kind]) | set(after_a[kind]) if before_a[kind].get(k_) != after_a[kind].get(k_)}
            return False, 'surviving A lost or changed its progress (%s: %s)' % (kind, dict(list(delta.items())[:6]))
    if after_b['n_panels'] != before_b['n_panels'] or after_b['n_doors'] != before_b['n_doors']:
        return False, 'B entity census is incomplete after restart'
    if mutant:
        ok = all(expected[kind] not in after_b[kind] for kind in expected)
        return ok, 'catch-up disabled: fresh B must lack every effect of the solve (B has: %s; lit cables A %s B %s)' % ({k_: expected[k_] in after_b[k_] for k_ in expected}, sorted(after_a['lit_cables']), sorted(after_b['lit_cables']))
    ok = all(after_a[kind] == after_b[kind] for kind in expected)
    return ok, 'fresh B restored all progress from surviving A after original events were ACKed'

def late_join(ca, cb, mutant):
    """Play on A alone, then start B for the first time. A never saw B, so no ACK exists; no_resend=1 on A means the original
    events were sent once into the void, so only the full-state snapshot sent when A first sees B can deliver them."""
    before_a = state('A')
    if '0x64' not in before_a['panels'] or '0x34eda' not in before_a['doors']:
        return False, 'precondition failed: A must have the solved panel and its open door before B exists'
    if os.path.exists(os.path.join(RUN, 'pidB.txt')): return False, 'precondition failed: B must never have been started'
    result = subprocess.run([os.path.join(TOOLS, 'restart.sh'), 'B'],
                            env={**os.environ, 'CFG_A': ca, 'CFG_B': cb}, capture_output=True, text=True, timeout=240)
    if result.returncode: return False, 'B start failed: ' + result.stdout + result.stderr
    time.sleep(30)
    open(os.path.join(RUN, 'guard.stop'), 'w').write('late join complete')
    after_a, after_b = state('A'), state('B')
    if after_a['panels'] != before_a['panels'] or after_a['doors'] != before_a['doors']: return False, 'A lost or changed its progress'
    if after_b['n_panels'] != after_a['n_panels'] or after_b['n_doors'] != after_a['n_doors']: return False, 'B entity census is incomplete'
    if mutant: return ('0x64' not in after_b['panels'] and '0x34eda' not in after_b['doors']), 'no snapshot: B must lack panel 0x64 and door 0x34eda'
    return (after_a['panels'] == after_b['panels'] and after_a['doors'] == after_b['doors']), 'B restored all progress from A on first contact'

def b_saves():
    return os.path.join(DIR2, 'appdata', 'The Witness')

def wait_first_b_save(timeout=180):
    """The game writes its first save about a minute after a new game starts. Back it up before any progress exists: it is the
    older save the reload will load (the game rewrites the one save file in place and saves again when the pause menu opens)."""
    end = time.time() + timeout
    while time.time() < end:
        files = [f for f in os.listdir(b_saves()) if f.endswith('.witness_campaign')] if os.path.isdir(b_saves()) else []
        if files:
            time.sleep(5)       # let the write finish
            bk = os.path.join(RUN, 'save_backup'); shutil.rmtree(bk, ignore_errors=True); os.makedirs(bk)
            for f in os.listdir(b_saves()): shutil.copy(os.path.join(b_saves(), f), bk)
            return bk
        time.sleep(3)
    return None

def reload_check(bk, mutant):
    """B loads its older save through the game's own menu while both processes and the link stay up."""
    before_a, before_b = state('A'), state('B')
    expected = {'panels': '0x64', 'doors': '0x34eda'}
    for kind, entity in expected.items():
        if entity not in before_a[kind] or entity not in before_b[kind]:
            return False, 'precondition failed: both worlds must have the solved panel and open door before B reloads'
    stats = last_stats(DIR1)
    if not stats or stats['sent'] != stats['acked']: return False, 'precondition failed: wait for all original events to be acknowledged'
    old = (pid('A'), pid('B'))
    out = pyw('wreload.py', win(DIR2), win(bk))
    print(out.strip(), flush=True)
    if 'reload requested' not in out: return False, 'the reload could not be requested'
    seen_lost = False; deadline = time.time() + 90
    while time.time() < deadline:
        time.sleep(3)
        cur = state('B')
        if all(e not in cur[k] for k, e in expected.items()): seen_lost = True       # the load really reset B's world
        if seen_lost and all(e in cur[k] for k, e in expected.items()) and not mutant: break
        if mutant and seen_lost and time.time() > deadline - 45: break
    after_a, after_b = state('A'), state('B')
    log_b = open(os.path.join(DIR2, 'witness_proxy.log'), errors='ignore').read()
    if (pid('A'), pid('B')) != old: return False, 'no process may restart in a same-process reload'
    if not seen_lost: return False, 'the reload never reset B (a test that cannot see the load proves nothing)'
    if any(after_a[k] != before_a[k] and not set(before_a[k]) <= set(after_a[k]) for k in expected): return False, 'A lost progress'
    if mutant: return all(e not in after_b[k] for k, e in expected.items()), 'reload ignored: B must still lack panel 0x64 and door 0x34eda'
    ok = all(e in after_b[k] for k, e in expected.items()) and 'save reload suspected' in log_b
    return ok, 'B caught up from A after loading its older save, without a restart'

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
  ('late join', 'no_resend=1', '', lambda a, b, ok: (ok and b.get('ap', 0) >= 1 and b.get('ad', 0) >= 1 and b.get('ep', 9) == 0 and b.get('ed', 9) == 0, 'B, started for the first time after the solve, receives the snapshot')),
  ('MUTANT: late join without snapshot', 'no_resend=1\nno_resync_on_restart=1', '', lambda a, b, ok: (not ok, 'a never-seen B must diverge without the full-state snapshot')),
  ('same-process reload', '', '', lambda a, b, ok: (b.get('ep', 9) == 0 and b.get('ed', 9) == 0, 'B reloads an older save in the same process and catches up from A')),
  ('MUTANT: same-process reload ignored', '', 'no_reload_sync=1', lambda a, b, ok: (True, 'B must stay diverged after the reload')),
  ('cable maze', '', '', lambda a, b, ok: (ok and b.get('ecab', 9) == 0 and b.get('acab', 0) >= 1 and b.get('ep', 9) == 0, 'solving a maze lights its cable in both games, once, without echo, and a fresh B recovers it')),
  ('cable chain', '', '', lambda a, b, ok: (ok and b.get('ecab', 9) == 0 and b.get('acab', 0) >= 2 and b.get('ep', 9) == 0, 'solving a maze powers the next one through its cable; both mazes and both cables match on B, once, without echo, and a fresh B recovers them')),
  ('MUTANT: cable chain without cable sync', 'no_cable_sync=1', 'no_cable_sync=1', lambda a, b, ok: (not ok, 'both cables must stay dark on B')),
  ('MUTANT: cable maze without cable sync', 'no_cable_sync=1', 'no_cable_sync=1', lambda a, b, ok: (not ok, 'the cable must stay dark on B')),
  ('second tutorial door', '', '', lambda a, b, ok: (ok and b.get('ep', 9) == 0 and b.get('ed', 9) == 0, 'both tutorial panels and doors match without B echo')),
  ('MUTANT: second tutorial door skips entity zero', 'skip_entity_zero=1', '', lambda a, b, ok: (not ok, 'door entity zero must be missing on B')),
  ('panel power chain', '', '', lambda a, b, ok: (ok and b.get('epow', 9) == 0 and b.get('apow', 0) >= 1, 'target panel powers on without being solved or echoed')),
  ('MUTANT: panel power chain without source link', '', 'no_power_link=1', lambda a, b, ok: (not ok, 'B must power the target but lack its source back-link')),
  ('laser target', '', '', lambda a, b, ok: (ok and b.get('ep', 9) == 0, 'a solved panel that targets a laser leaves the laser in the same state on both, and a fresh B recovers it')),
  ('MUTANT: laser target without dispatch', '', 'no_target_dispatch=1', lambda a, b, ok: (not ok, 'B must keep the laser off')),
  ('cable off', '', '', lambda a, b, ok: (ok and b.get('ecab', 9) == 0, 'a default-lit cable switched off on A goes dark on B too, once, without echo, and a fresh B learns it')),
  ('MUTANT: cable off without power-off sync', '', 'no_cable_off=1', lambda a, b, ok: (not ok, 'B must keep the cable lit')),
  ('MUTANT: cable off without snapshot', 'no_off_snapshot=1\nno_snap_reassert=1', '', lambda a, b, ok: (not ok, 'a fresh B must still show the cable lit')),
  ('MUTANT: panel power chain disabled', 'no_panel_power=1', '', lambda a, b, ok: (not ok, 'target panel must remain dark on B')),
]
only = sys.argv[1:]; results = []
SAVES0 = real_saves()
for name, ca, cb, check in SCENARIOS:
    if only and not any(o in name for o in only): continue
    print('\n=== %s' % name, flush=True)
    late = 'late join' in name
    if late:      # free B from the previous scenario and start A alone
        pb = os.path.join(RUN, 'pidB.txt')
        if os.path.exists(pb):
            subprocess.run(['taskkill.exe', '/PID', open(pb).read().strip(), '/F'], capture_output=True); os.remove(pb)
    subprocess.run([os.path.join(TOOLS, 'restart.sh')] + (['A'] if late else []), env={**os.environ, 'CFG_A': ca, 'CFG_B': cb},
                   capture_output=True, text=True, timeout=600, check=True)
    time.sleep(30)    # both worlds must pass the stable-world gate (three identical scans 3 s apart)
    backup = wait_first_b_save() if 'same-process reload' in name else None
    if 'same-process reload' in name and not backup: print('no first save appeared on B', flush=True)
    # The bot is in-process (tp, mouse and button commands to the proxy): no focus, no desktop input, no lock; several can run at once.
    open(os.path.join(RUN, 'guard.stop'), 'w').write('bot starting')      # the focus guard (for the game grabbing the foreground while loading) ends
    chain = 'cable chain' in name
    laser = 'laser target' in name
    cableoff = 'cable off' in name
    maze = 'cable maze' in name or chain
    if laser:      # the laser panel is out of the bot's reach: make it solved on A as a native solve would (fields and dispatcher)
        bot = pyw('wsolve.py', win(DIR1), '0x32f5').strip(); time.sleep(6)
        bot += '\n' + ('panel solved: True' if state('A')['lasers'].get('0x32f9') == LASER_ON else 'laser did not change on A')
        bot = bot.replace('panel solved: True', 'solved: True')
    elif cableoff:      # cable 0x18458 is lit from the start in every world: switch it off with the game's own power-off call
        before = state('A')['lit_cables'].get('0x18458') and state('B')['lit_cables'].get('0x18458')
        bot = pyw('wcmd.py', win(DIR1), 'cableoff', '18458').strip(); time.sleep(8)
        bot = 'solved: True' if before and '0x18458' not in state('A')['lit_cables'] else 'cable was not lit at the start, or did not go dark on A'
    else: bot = sh('%s -I %s %s %s %s 2>&1 | tail -3' % (shlex.quote(PYW), shlex.quote(win(os.path.join(TOOLS, 'win', 'wbot_maze.py' if maze else 'wbot.py'))), pid('A'), shlex.quote(win(DIR1)), '0x293' if maze else '')).strip()
    if chain and 'solved: True' in bot:       # the first cable powers the second maze: wait for its power, then solve it too
        time.sleep(6)
        bot = sh('%s -I %s %s %s 0x295 2>&1 | tail -3' % (shlex.quote(PYW), shlex.quote(win(os.path.join(TOOLS, 'win', 'wbot_maze.py'))), pid('A'), shlex.quote(win(DIR1)))).strip()
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
    if laser and name.startswith('MUTANT:'): time.sleep(45)      # no restart follows: give the game time to write its first save (a hygiene check)
    catchup = None
    if 'same-process reload' in name and bot_ok and backup: catchup = reload_check(backup, name.startswith('MUTANT:'))
    elif late: pass
    elif 'restart catch-up' in name and bot_ok:
        catchup = restart_catchup(ca, cb, name.startswith('MUTANT:'))
    elif cableoff and bot_ok and name != 'MUTANT: cable off without power-off sync':
        catchup = restart_catchup(ca, cb, name.startswith('MUTANT:'), expected={'dark_cables': '0x18458'})
    elif laser and bot_ok and not name.startswith('MUTANT:'):
        catchup = restart_catchup(ca, cb, False, expected={'panels': '0x32f5', 'lasers': '0x32f9'})
    elif maze and bot_ok and not name.startswith('MUTANT:'):
        catchup = restart_catchup(ca, cb, False, expected={'panels': '0x295' if chain else '0x293', 'lit_cables': '0x45c' if chain else '0x349'})
    elif 'panel power chain' in name and bot_ok and power_ok and not name.startswith('MUTANT:'):
        catchup = restart_catchup(ca, cb, False, include_power=True)
    if late and bot_ok: catchup = late_join(ca, cb, name.startswith('MUTANT:'))
    out = sh('python3 %s' % os.path.join(TOOLS, 'state_diff.py')); print(out.strip(), flush=True)
    a, b = last_stats(DIR1), last_stats(DIR2)
    ok, why = check(a, b, 'IDENTICAL' in out)
    if maze:
        sa, sb = state('A'), state('B')
        first = ['0x293', '0x295'] if chain else ['0x293']; cables = ['0x349', '0x45c'] if chain else ['0x349']
        solved_ok = all(x in sa['panels'] and x in sb['panels'] for x in first) and sa['panels'] == sb['panels']
        if name.startswith('MUTANT:'): cable_ok = all(c in sa['lit_cables'] and c not in sb['lit_cables'] for c in cables) and set(sa['lit_cables']) - set(sb['lit_cables']) == set(cables)
        else: cable_ok = all(c in sa['lit_cables'] for c in cables) and sa['lit_cables'] == sb['lit_cables'] and sa['powered_panels'] == sb['powered_panels']
        ok = ok and solved_ok and cable_ok
    if cableoff:
        sa, sb = state('A'), state('B'); gone = '0x18458' not in sa['lit_cables']
        if name == 'MUTANT: cable off without power-off sync': ok = bot_ok and gone and '0x18458' in sb['lit_cables']
        elif name == 'MUTANT: cable off without snapshot': ok = bot_ok and gone      # the restart check below (B restarted before this point) must see the cable lit again on a fresh B
        else: ok = ok and gone and sa['lit_cables'] == sb['lit_cables']
    if laser:
        sa, sb = state('A'), state('B')
        same = sa['lasers'] == sb['lasers'] and sa['lasers']['0x32f9'] == LASER_ON and sa['panels'] == sb['panels']
        if name.startswith('MUTANT:'): ok = bot_ok and sa['lasers']['0x32f9'] == LASER_ON and sb['lasers']['0x32f9'] != LASER_ON and '0x32f5' in sb['panels']
        else: ok = ok and same
    if 'panel power chain' in name:
        sa, sb = state('A'), state('B')
        same_solves = sa['panels'] == sb['panels'] and '0x18af' in sa['panels'] and '0x1b' not in sa['panels']
        powered_a, powered_b = set(sa['powered_panels']), set(sb['powered_panels'])
        link_a, link_b = sa['power_src'].get('0x1b'), sb['power_src'].get('0x1b')
        print('power link of 0x1b: A %s B %s' % (link_a, link_b), flush=True)
        if name.startswith('MUTANT:') and 'source link' not in name:
            target_ok = powered_a - powered_b == {'0x1b'} and not (powered_b - powered_a)
        else: target_ok = powered_a == powered_b and '0x1b' in powered_b and link_a == '0x18b0' and link_b == link_a
        if 'source link' in name:      # the state diff cannot see the back-link: this mutant is caught by the link check alone
            ok = power_ok and same_solves and powered_a == powered_b and '0x1b' in powered_b and link_a == '0x18b0' and link_b is None and sa['doors'] == sb['doors']
        else: ok = ok and power_ok and same_solves and target_ok and sa['doors'] == sb['doors']
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
    if 'same-process reload' in name and not backup: ok, why = False, 'no first save to reload'
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
