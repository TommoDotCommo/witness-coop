# T2 check of the lobby sending the host's own puzzles ("Your puzzles"), run with Windows Python: two real launchers and two real games. A's library
# has a set and A's pick is that set; B has no puzzles at all. A hosts and B joins: B sees the host's choice (custom;set:<hash>), asks A for the set and
# keeps it (its file in B's puzzles folder, the same text), and says so; A starts; both launchers write randomizer=custom;set:<hash> and start their
# games, and both games apply the set with the same fingerprint.
# usage: wlobby_set.py <launcher A> <dir A> <launcher B> <dir B> <lobby port> <run dir> <rectA> <rectB> <hash>      (rect: "x y w h")
import os, re, subprocess, sys, time
exeA, dirA, exeB, dirB, lport, run = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5]), sys.argv[6]
rects = {'A': sys.argv[7], 'B': sys.argv[8]}; H = sys.argv[9]
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
    r = dict(members={}, lobby={}, game={}, settings={}, invite='')
    for l in txt.splitlines():
        if l.startswith('lobby '): v = l.split(); r['lobby'] = {v[i]: int(v[i + 1]) for i in range(1, len(v) - 1, 2)}
        elif l.startswith('invite'): r['invite'] = l[7:].strip()
        elif l.startswith('member '):
            m = re.match(r'member (\d+) name (.*) host (\d) ready (\d) ingame (\d) speaking (\d) playing (\d) port (\d+) ip (\S+) puzzles (\d)', l)
            if m: r['members'][int(m[1])] = dict(name=m[2], host=int(m[3]), ready=int(m[4]), ingame=int(m[5]), puzzles=int(m[10]))
        elif l.startswith('settings '):
            m = re.match(r'settings mode (\d) goal (\S*) newgame (\d) progress (.*?) randomizer (\S+)$', l)
            if m: r['settings'] = dict(randomizer=m[5])
        elif l.startswith('game '): v = l.split(); r['game'] = {v[i]: int(v[i + 1]) for i in range(1, len(v) - 1, 2)}
    return r
def wait(pred, secs):
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
def send_game(d, lines):
    p = os.path.join(d, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(p) and time.time() - t < 8: time.sleep(0.01)
    open(p + '.tmp', 'w').write('\n'.join(lines) + '\n'); os.replace(p + '.tmp', p)
def randstat(d):
    log = os.path.join(d, 'witness_proxy.log')
    try: ls = [l for l in open(log, errors='ignore').read().splitlines() if l.startswith('randstat: state ')]
    except OSError: return {}
    return dict(re.findall(r' (state|fingerprint) (\S+)', ls[-1])) if ls else {}
for d in (dirA, dirB):
    for f in (stat_path(d), cmd_path(d)):
        if os.path.exists(f): os.remove(f)
setB = os.path.join(dirB, 'puzzles', H + '.txt'); textA = open(os.path.join(dirA, 'puzzles', 'sets', 'In-game test.txt'), encoding='utf-8').read()
si = subprocess.STARTUPINFO(); si.dwFlags |= subprocess.STARTF_USESHOWWINDOW; si.wShowWindow = 4
procs = {}
try:
    for k, (exe, d) in {'A': (exeA, dirA), 'B': (exeB, dirB)}.items():
        procs[k] = subprocess.Popen([exe, '--dir', d, '--no-activate', '--stat-file', stat_path(d), '--cmd-file', cmd_path(d)], startupinfo=si)
    a, b = wait(lambda a, b: True, 20); check('0. both launchers are up; B has no puzzles', a and b and not os.path.exists(setB), (a and a['lobby'], b and b['lobby']))
    cmd(dirA, 'host %d' % lport); a, b = wait(lambda a, b: a['lobby']['active'] and a['invite'], 10)
    check('1. A hosts a lobby', a and a['invite'], a and a['invite'])
    cmd(dirB, 'join ' + a['invite']); a, b = wait(lambda a, b: b['lobby']['joined'] and len(a['members']) == 2, 15)
    check('   B joins', a and b and len(a['members']) == 2, a and a['members'])
    a, b = wait(lambda a, b: b['settings'].get('randomizer') == 'custom;set:' + H and a['members'][2]['puzzles'] == 1, 20)
    check('2. B sees the host\'s choice: A\'s own puzzles (custom;set:%s)' % H, b and b['settings'].get('randomizer') == 'custom;set:' + H, b and b['settings'])
    check('   B gets the set from A and says so (A sees B with the puzzles)', a and a['members'][2]['puzzles'] == 1, a and a['members'])
    got = open(setB, encoding='utf-8').read() if os.path.exists(setB) else None
    check('   B keeps it in its puzzles folder, the same text as A\'s', got == open(os.path.join(dirA, 'puzzles', H + '.txt'), encoding='utf-8').read() if got is not None and os.path.exists(os.path.join(dirA, 'puzzles', H + '.txt')) else False, 'B has %s bytes' % (len(got) if got is not None else 'no file'))
    cmd(dirB, 'ready 1'); a, b = wait(lambda a, b: a['members'][2]['ready'] == 1, 8)
    cmd(dirA, 'start'); a, b = wait(lambda a, b: a['lobby']['phase'] == 1 and b['lobby']['phase'] == 1, 8)
    check('3. A starts: both launchers count down', a and b and a['lobby']['phase'] == 1 and b['lobby']['phase'] == 1, (a and a['lobby'], b and b['lobby']))
    a, b = wait(lambda a, b: a['game']['running'] and b['game']['running'], 30)
    check('   and start their games', a and b and a['game'].get('running') == 1 and b['game'].get('running') == 1, (a and a['game'], b and b['game']))
    if a and b and a['game'].get('pid') and b['game'].get('pid'):
        pids = {'A': a['game']['pid'], 'B': b['game']['pid']}
        for k in 'AB': open(os.path.join(run, 'pid%s.txt' % k), 'w').write(str(pids[k]))
        for k in 'AB':
            x, y, w, h = rects[k].split(); subprocess.run([sys.executable, os.path.join(here, 'wplace.py'), str(pids[k]), x, y, w, h], capture_output=True, timeout=30)
        cA, cB = cfg_of(dirA), cfg_of(dirB)
        check('   both games are given randomizer=custom;set:%s' % H, cA.get('randomizer') == 'custom;set:' + H and cB.get('randomizer') == 'custom;set:' + H, (cA.get('randomizer'), cB.get('randomizer')))
        t0 = time.time(); ra = rb = {}
        while time.time() - t0 < 300:
            for d in (dirA, dirB): send_game(d, ['randstat'])
            time.sleep(3); ra, rb = randstat(dirA), randstat(dirB)
            if ra.get('state') in ('applied', 'failed') and rb.get('state') in ('applied', 'failed'): break
        check('4. both games apply the set, with the same fingerprint', ra.get('state') == 'applied' and rb.get('state') == 'applied' and ra.get('fingerprint') == rb.get('fingerprint') and ra.get('fingerprint', '0').strip('0'), (ra, rb))
        for k in 'AB': subprocess.run(['taskkill', '/PID', str(pids[k]), '/F'], capture_output=True)
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
