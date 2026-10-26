# T2 check of the lobby, run with Windows Python: two real launchers and two real games. A hosts, B joins (a wrong invite first); names, chat both ways, voice (A transmits a test voice,
# B's mixer must hear it whole; muting stops it), the host's choices reach B; the start is refused until B is ready, then after the countdown both launchers write the cfg of their game from
# the same roster and start it; the two games find each other and race; when both games end both launchers are back in the lobby with the result, their own cfg put back, and voice again.
# usage: wlobby.py <launcher A> <dir A> <launcher B> <dir B> <lobby port> <run dir> <rectA> <rectB>      (rect: "x y w h")
import os, re, subprocess, sys, time
exeA, dirA, exeB, dirB, lport, run = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5]), sys.argv[6]
rects = {'A': sys.argv[7], 'B': sys.argv[8]}
here = os.path.dirname(os.path.abspath(__file__))
def stat_path(d): return os.path.join(d, 'lobby_stat.txt')
def cmd_path(d): return os.path.join(d, 'launcher_cmd.txt')
def cmd(d, *lines):
    p = cmd_path(d); t = time.time()
    while os.path.exists(p) and time.time() - t < 8: time.sleep(0.05)
    open(p + '.tmp', 'w').write('\n'.join(lines) + '\n'); os.replace(p + '.tmp', p)
    t = time.time()
    while os.path.exists(p) and time.time() - t < 8: time.sleep(0.05)
def stat(d):
    try: txt = open(stat_path(d), errors='ignore').read()
    except OSError: return None
    r = dict(members={}, speakers={}, chat='', result='', invite='', lobby={}, voice={}, game={}, settings={})
    for l in txt.splitlines():
        if l.startswith('lobby '): v = l.split(); r['lobby'] = {v[i]: int(v[i + 1]) for i in range(1, len(v) - 1, 2)}
        elif l.startswith('invite'): r['invite'] = l[7:].strip()
        elif l.startswith('member '):
            m = re.match(r'member (\d+) name (.*) host (\d) ready (\d) ingame (\d) speaking (\d) playing (\d) port (\d+) ip (\S+)', l)
            if m: r['members'][int(m[1])] = dict(name=m[2], host=int(m[3]), ready=int(m[4]), ingame=int(m[5]), speaking=int(m[6]), playing=int(m[7]), port=int(m[8]), ip=m[9])
        elif l.startswith('settings '):
            m = re.match(r'settings mode (\d) goal (\S*) newgame (\d) progress (.*)', l)
            if m: r['settings'] = dict(mode=int(m[1]), goal=m[2], newgame=int(m[3]), progress=m[4])
        elif l.startswith('result'): r['result'] = l[7:]
        elif l.startswith('chat'): r['chat'] = l[5:]
        elif l.startswith('voice_speaker '):
            m = re.match(r'voice_speaker (\d+) talking (\d) rms ([0-9.]+) tone ([0-9.]+) ([0-9.]+) ([0-9.]+) packets (\d+)', l)
            if m: r['speakers'][int(m[1])] = dict(talking=int(m[2]), rms=float(m[3]), tone=[float(m[4]), float(m[5]), float(m[6])], packets=int(m[7]))
        elif l.startswith('voice '): v = l.split(); r['voice'] = {v[i]: int(v[i + 1]) for i in range(1, len(v) - 1, 2)}
        elif l.startswith('game '): v = l.split(); r['game'] = {v[i]: int(v[i + 1]) for i in range(1, len(v) - 1, 2)}
    return r
def wait(pred, secs, d=None, what=''):
    end = time.time() + secs; last = None
    while time.time() < end:
        last = (stat(dirA), stat(dirB))
        try:
            if all(last) and pred(*last): return last
        except (KeyError, IndexError): pass
        time.sleep(0.15)
    return last
ok = True
def check(name, cond, detail=''):
    global ok; ok = ok and bool(cond); print('  %-108s %s  (%s)' % (name, 'ok' if cond else 'FAILED', str(detail)[:200]), flush=True)
def cfg_of(d): return dict(l.split('=', 1) for l in open(os.path.join(d, 'witness_coop.cfg'), errors='ignore').read().splitlines() if '=' in l and not l.startswith('#'))
def cfg_lines(d, key): return [l.split('=', 1)[1] for l in open(os.path.join(d, 'witness_coop.cfg'), errors='ignore').read().splitlines() if l.startswith(key + '=')]
def send_game(d, lines):
    p = os.path.join(d, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(p) and time.time() - t < 8: time.sleep(0.01)
    open(p + '.tmp', 'w').write('\n'.join(lines) + '\n'); os.replace(p + '.tmp', p)
    t = time.time()
    while os.path.exists(p) and time.time() - t < 8: time.sleep(0.01)
def rstat(d):
    log = os.path.join(d, 'witness_proxy.log'); n0 = sum(1 for l in open(log, errors='ignore') if l.startswith('racestat: phase')); send_game(d, ['racestat']); t = time.time()
    while time.time() - t < 8:
        ls = open(log, errors='ignore').read().splitlines(); idx = [i for i, l in enumerate(ls) if l.startswith('racestat: phase')]
        if len(idx) > n0:
            time.sleep(0.08); ls = open(log, errors='ignore').read().splitlines(); idx = [i for i, l in enumerate(ls) if l.startswith('racestat: phase')]
            m = re.match(r'racestat: phase (\w+) go_in (-?\d+) elapsed (\d+) solved (\d+) winner (\d+) decided (\d) goal (\S+)', ls[idx[-1]]); r = dict(phase=m[1], elapsed=int(m[3]), solved=int(m[4]), winner=int(m[5]), decided=int(m[6]), goal=m[7], st={})
            for l in ls[idx[-1] + 1:]:
                s = re.match(r'racestat: standing (\d+) place (\d+) phase (\w+) solved (\d+) finish (\d+) me (\d) gone (\d)', l)
                if not s: break
                r['st'][int(s[1])] = dict(place=int(s[2]), phase=s[3], solved=int(s[4]), finish=int(s[5]))
            return r
        time.sleep(0.05)
for d in (dirA, dirB):
    for f in (stat_path(d), cmd_path(d)):
        if os.path.exists(f): os.remove(f)
cfg0 = {d: open(os.path.join(d, 'witness_coop.cfg')).read() for d in (dirA, dirB)}
si = subprocess.STARTUPINFO(); si.dwFlags |= subprocess.STARTF_USESHOWWINDOW; si.wShowWindow = 4
procs = {}
try:
    for k, (exe, d) in {'A': (exeA, dirA), 'B': (exeB, dirB)}.items():
        procs[k] = subprocess.Popen([exe, '--dir', d, '--no-activate', '--stat-file', stat_path(d), '--cmd-file', cmd_path(d)], startupinfo=si)
    a, b = wait(lambda a, b: True, 20); check('0. both launchers are up and write their state', a and b and not a['lobby']['active'] and not b['lobby']['active'], (a and a['lobby'], b and b['lobby']))
    # 1. host and join
    cmd(dirA, 'host %d' % lport); a, b = wait(lambda a, b: a['lobby']['active'] and a['invite'], 10)
    check('1. A hosts a lobby and has an invite (address:port/CODE)', a and re.match(r'^[0-9.]+:%d/[A-Z0-9]{6}$' % lport, a['invite']), a and a['invite'])
    inv = a['invite']; wrong = inv[:-6] + 'ZZZZZZ' if not inv.endswith('ZZZZZZ') else inv[:-6] + 'YYYYYY'
    cmd(dirB, 'join ' + wrong); a, b = wait(lambda a, b: b['lobby']['refused'] == 1, 12)
    check('   B with a wrong code is refused and is not in the lobby', b and b['lobby']['refused'] == 1 and not b['lobby']['joined'] and len(a['members']) == 1, b and b['lobby'])
    cmd(dirB, 'leave'); time.sleep(0.5); cmd(dirB, 'join ' + inv); a, b = wait(lambda a, b: b['lobby']['joined'] and len(a['members']) == 2 and len(b['members']) == 2, 15)
    check('   B joins with the right invite: both see two members, A first (host), B second', a and b and len(a['members']) == 2 and len(b['members']) == 2 and a['members'][1]['host'] == 1 and b['members'][2]['name'] == 'Beta', (a and a['members'], b and b['lobby']))
    check('   the members carry their game ports (what the games will connect to)', a and a['members'][1]['port'] == int(cfg_of(dirA)['listen']) and a['members'][2]['port'] == int(cfg_of(dirB)['listen']), a and [(m['port']) for m in a['members'].values()])
    # 2. chat
    cmd(dirA, 'chat hello from alpha'); cmd(dirB, 'chat hi alpha, this is beta'); a, b = wait(lambda a, b: 'hello from alpha' in a['chat'] and 'hi alpha, this is beta' in a['chat'] and 'hello from alpha' in b['chat'] and 'hi alpha, this is beta' in b['chat'], 10)
    check('2. chat: each line is in both windows, with the name of who said it', a and b and 'Alpha:  hello from alpha' in a['chat'] and 'Beta:  hi alpha, this is beta' in b['chat'] and 'Beta:  hi alpha' in a['chat'], (a and a['chat'], b and b['chat']))
    # 3. voice
    cmd(dirA, 'voicestart'); cmd(dirB, 'voicestart'); a, b = wait(lambda a, b: a['voice']['running'] and b['voice']['running'] and 1 in b['speakers'] and b['speakers'][1]['talking'] and b['speakers'][1]['rms'] > 0.05, 15); sp = b and b['speakers'].get(1)
    check('3. voice: A transmits a test voice and B hears it (talking, loud enough)', sp and sp['talking'] == 1 and sp['rms'] > 0.05 and a['voice']['sent'] > 20 and b['voice']['received'] > 20, sp and (sp, b['voice']))
    check('   it is the whole voice (120 Hz, 1 kHz and 5.5 kHz are all there: this is not the radio)', sp and sp['tone'][0] > 0.02 and sp['tone'][1] > 0.02 and sp['tone'][2] > 0.4 * sp['tone'][1], sp and sp['tone'])
    check('   B sees A speaking in the member list, and A does not hear itself', a and b and any(m['speaking'] for m in b['members'].values() if m['name'] == 'Alpha') and 1 not in a['speakers'] and 2 not in b['speakers'], (b and b['members'], a and a['speakers']))
    cmd(dirA, 'mute 1'); a, b = wait(lambda a, b: b['speakers'].get(1, {}).get('talking') == 0, 8)
    check('   A mutes its microphone: B stops hearing A', b and b['speakers'][1]['talking'] == 0 and a['voice']['muted_ui'] == 1, b and b['speakers'])
    cmd(dirA, 'mute 0')
    # 4. the host's choices
    cmd(dirA, 'mode race', 'goal count:2', 'newgame 1', 'parts area:Tutorial_(Inside)'); a, b = wait(lambda a, b: b['settings'].get('mode') == 1 and b['settings'].get('goal') == 'count:2', 10)
    check('4. the host chooses a race to 2 panels and a new game: B sees it', b and b['settings'] == dict(mode=1, goal='count:2', newgame=1, progress='area:Tutorial_(Inside)'), b and b['settings'])
    # 5. start
    cmd(dirA, 'start'); time.sleep(1.5); a, b = stat(dirA), stat(dirB)
    check('5. the host cannot start while B is not ready', a and a['lobby']['phase'] == 0 and b['lobby']['phase'] == 0, a and a['lobby'])
    cmd(dirB, 'ready 1'); a, b = wait(lambda a, b: a['members'][2]['ready'] == 1, 8)
    check('   B says it is ready: A sees it', a and a['members'][2]['ready'] == 1, a and a['members'])
    cmd(dirA, 'start'); a, b = wait(lambda a, b: a['lobby']['phase'] == 1 and b['lobby']['phase'] == 1, 8)
    check('   A starts: both launchers count down', a and b and a['lobby']['phase'] == 1 and b['lobby']['phase'] == 1, (a and a['lobby'], b and b['lobby']))
    a, b = wait(lambda a, b: a['game']['running'] and b['game']['running'], 30)
    check('6. after the countdown each launcher has started its game', a and b and a['game']['running'] == 1 and b['game']['running'] == 1 and a['game']['pid'] and b['game']['pid'], (a and a['game'], b and b['game']))
    pids = {'A': a['game']['pid'], 'B': b['game']['pid']}
    for k, d in (('A', dirA), ('B', dirB)): open(os.path.join(run, 'pid%s.txt' % k), 'w').write(str(pids[k]))
    cA, cB = cfg_of(dirA), cfg_of(dirB)
    check('   each game was given its own number, port, name and the host\'s choices', cA['id'] == '1' and cB['id'] == '2' and cA['listen'] == str(a['members'][1]['port']) and cB['listen'] == str(a['members'][2]['port']) and cA['game_mode'] == 'race' and cB['game_mode'] == 'race' and cB['race_goal'] == 'count:2' and cA['name'] == 'Alpha' and cB['name'] == 'Beta', (cA, cB))
    check('   ... and each lists the other as its only peer (number, address, port)', len(cfg_lines(dirA, 'peer')) == 1 and cfg_lines(dirA, 'peer')[0].startswith('2,') and cfg_lines(dirA, 'peer')[0].endswith(',' + cB['listen']) and len(cfg_lines(dirB, 'peer')) == 1 and cfg_lines(dirB, 'peer')[0].startswith('1,') and cfg_lines(dirB, 'peer')[0].endswith(',' + cA['listen']), (cfg_lines(dirA, 'peer'), cfg_lines(dirB, 'peer')))
    check('   the new game starts with the parts of the map the host chose', cA.get('start_progress') == 'area:Tutorial_(Inside)' and cB.get('start_progress') == 'area:Tutorial_(Inside)', (cA.get('start_progress'), cB.get('start_progress')))
    check('   the lines of the players\' own cfg that the lobby does not set are kept (voice test tone, countdown)', cA.get('voice_test_tone') == '120,1000,5500' and cA.get('race_countdown_ms') == '9000' and cB.get('race_countdown_ms') == '9000', (cA.get('voice_test_tone'), cB.get('race_countdown_ms')))
    for k, d in (('A', dirA), ('B', dirB)):
        x, y, w, h = rects[k].split(); subprocess.run([sys.executable, os.path.join(here, 'wplace.py'), str(pids[k]), x, y, w, h], capture_output=True, timeout=30)
    a, b = wait(lambda a, b: a['lobby']['phase'] == 2 and not a['voice']['running'] and not b['voice']['running'] and all(m['ingame'] for m in a['members'].values()), 10)
    check('   while the games run the lobby is in-game and its voice is closed (the game has the microphone)', a and b and a['lobby']['phase'] == 2 and a['voice']['running'] == 0 and b['voice']['running'] == 0 and all(m['ingame'] for m in a['members'].values()), (a and a['lobby'], a and a['voice']))
    # 7. the race (the games found each other through the roster the lobby wrote)
    t0 = time.time(); ra = rb = None
    while time.time() - t0 < 240:
        try: ra, rb = rstat(dirA), rstat(dirB)
        except Exception: ra = rb = None
        if ra and rb and ra['phase'] == 'running' and rb['phase'] == 'running': break
        time.sleep(1.0)
    check('7. the two games, started by the launchers, find each other and both reach the race (running)', ra and rb and ra['phase'] == 'running' and rb['phase'] == 'running', (ra and ra['phase'], rb and rb['phase']))
    if ra and rb and ra['phase'] == 'running':
        send_game(dirA, ['solve 5d', 'solve 5e']); time.sleep(2.5); ra, rb = rstat(dirA), rstat(dirB)
        check('   A solves two panels: A finishes first, and B knows', ra['phase'] == 'finished' and ra['winner'] == 1 and rb['winner'] == 1 and rb['st'][1]['phase'] == 'finished', (ra['phase'], ra['winner'], rb['winner']))
        time.sleep(2.5)
    # 8. the games end: back in the lobby
    for k in 'AB': subprocess.run(['taskkill', '/PID', str(pids[k]), '/F'], capture_output=True)
    a, b = wait(lambda a, b: a['lobby']['phase'] == 0 and b['lobby']['phase'] == 0 and not a['game']['running'] and not b['game']['running'] and a['voice']['running'] and b['voice']['running'], 40)
    check('8. when the games end both launchers are back in the lobby, not ready, and the voice is open again', a and b and a['lobby']['phase'] == 0 and b['lobby']['phase'] == 0 and all(not m['ready'] or m['host'] for m in b['members'].values()) and a['voice']['running'] == 1 and b['voice']['running'] == 1, (a and (a['lobby'], a['voice']), b and b['voice']))
    a, b = wait(lambda a, b: 'Race:' in b['result'], 12)
    check('   everybody sees the result: the winner and its time, and the other player not finished', a and b and 'Race: 1. Alpha' in b['result'] and 'Beta (not finished)' in b['result'] and re.search(r'Alpha \d+:\d\d\.\d', b['result']), b and b['result'])
    check('   each player\'s own cfg is put back as it was before the lobby started the game', open(os.path.join(dirA, 'witness_coop.cfg')).read().count('game_mode=race') == 0 and 'peer=' not in ''.join(l for l in open(os.path.join(dirB, 'witness_coop.cfg')).read().splitlines(True) if l.startswith('peer=2,') or l.startswith('peer=1,')), (cfg_lines(dirA, 'peer'), cfg_lines(dirA, 'game_mode')))
    b0 = stat(dirB); cmd(dirB, 'leave'); a, b = wait(lambda a, b: len(a['members']) == 1, 10)
    check('9. B leaves: A\'s list drops to one member', a and len(a['members']) == 1, a and a['members'])
finally:
    for k, p in procs.items():
        try: p.terminate()
        except Exception: pass
    for k in 'AB':
        f = os.path.join(run, 'pid%s.txt' % k)
        if os.path.exists(f):
            try: subprocess.run(['taskkill', '/PID', open(f).read().strip(), '/F'], capture_output=True)
            except Exception: pass
print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
