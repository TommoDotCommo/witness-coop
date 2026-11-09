# T1 check of "Your puzzles" played alone from the launcher, run with Windows Python: one real launcher and one real game. The library holds a set
# and the pick is that set. New game... (the launcher's own dialog) writes the set's copy and randomizer=custom;set:<hash> and starts the game,
# which applies it. A panel is solved and the game saves (the pause menu). Then the set is changed in the library (it now has another hash) and
# Play solo continues: the game must get the set its save was made with (the copy kept under its hash), the same puzzles (fingerprint), and the
# solved panel back.
# usage: wsolo_custom.py <launcher> <dir> <run dir> <rect> <hash>      (rect: "x y w h")
import os, re, subprocess, sys, time
exe, d, run, rect, H = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5]
here = os.path.dirname(os.path.abspath(__file__))
STAT, CMD = os.path.join(d, 'lobby_stat.txt'), os.path.join(d, 'launcher_cmd.txt')
def cmd(*lines):
    t = time.time()
    while os.path.exists(CMD) and time.time() - t < 8: time.sleep(0.05)
    open(CMD + '.tmp', 'w').write('\n'.join(lines) + '\n'); os.replace(CMD + '.tmp', CMD)
    t = time.time()
    while os.path.exists(CMD) and time.time() - t < 8: time.sleep(0.05)
def game():      # the game started from this folder (a solo launch is not followed by the launcher's state file, a lobby's is)
    exe_ = os.path.join(d, 'witness64_d3d11.exe').lower()
    out = subprocess.run(['powershell', '-NoProfile', '-Command', "Get-CimInstance Win32_Process -Filter \"Name='witness64_d3d11.exe'\" | ForEach-Object { \"$($_.ProcessId)|$($_.ExecutablePath)\" }"],
                         capture_output=True, text=True, timeout=60).stdout
    pids = [int(l.split('|', 1)[0]) for l in out.splitlines() if '|' in l and l.split('|', 1)[1].strip().lower() == exe_]
    return {'pid': pids[0], 'running': 1} if pids else {'pid': 0, 'running': 0}
def send_game(*lines):
    p = os.path.join(d, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(p) and time.time() - t < 8: time.sleep(0.02)
    open(p + '.tmp', 'w').write('\n'.join(lines) + '\n'); os.replace(p + '.tmp', p)
    t = time.time()
    while os.path.exists(p) and time.time() - t < 8: time.sleep(0.02)
def log():
    try: return open(os.path.join(d, 'witness_proxy.log'), errors='ignore').read()
    except OSError: return ''
def randstat(secs=240):
    t0 = time.time(); st = {}
    while time.time() - t0 < secs:
        send_game('randstat'); time.sleep(2)
        ls = [l for l in log().splitlines() if l.startswith('randstat: state ')]
        st = dict(re.findall(r' (state|fingerprint) (\S+)', ls[-1])) if ls else {}
        if st.get('state') in ('applied', 'failed'): break
    return st
def solved():
    out = subprocess.run([sys.executable, os.path.join(here, 'wstatedump.py'), str(game().get('pid', 0))], capture_output=True, text=True, timeout=60).stdout.strip().splitlines()
    import json
    try: return {int(k, 16) for k in json.loads(out[-1])['panels']}
    except Exception: return set()
def cfg(): return dict(l.split('=', 1) for l in open(os.path.join(d, 'witness_coop.cfg'), errors='ignore').read().splitlines() if '=' in l and not l.startswith('#'))
ok = True
def check(name, cond, detail=''):
    global ok; ok = ok and bool(cond); print('  %-104s %s  (%s)' % (name, 'ok' if cond else 'FAILED', str(detail)[:200]), flush=True)
def start_game(how):
    cmd(*how); t0 = time.time(); g = game()
    while time.time() - t0 < 40 and not g.get('running'): time.sleep(0.5); g = game(); open(os.path.join(run, 'pidA.txt'), 'w').write(str(g.get('pid', 0)))
    if g.get('pid'):
        x, y, w, h = rect.split(); subprocess.run([sys.executable, os.path.join(here, 'wplace.py'), str(g['pid']), x, y, w, h], capture_output=True, timeout=30)
    return g
def stop_game():
    p = game().get('pid')
    if p: subprocess.run(['taskkill', '/PID', str(p), '/F'], capture_output=True)
    t0 = time.time()
    while time.time() - t0 < 30 and game().get('running'): time.sleep(0.5)
for f in (STAT, CMD):
    if os.path.exists(f): os.remove(f)
copy = os.path.join(d, 'puzzles', H + '.txt'); library = os.path.join(d, 'puzzles', 'sets', 'In-game test.txt')
saves = os.path.join(d, 'appdata', 'The Witness')
si = subprocess.STARTUPINFO(); si.dwFlags |= subprocess.STARTF_USESHOWWINDOW; si.wShowWindow = 4
proc = None
try:
    proc = subprocess.Popen([exe, '--dir', d, '--no-activate', '--stat-file', STAT, '--cmd-file', CMD], startupinfo=si)
    t0 = time.time()
    while time.time() - t0 < 20 and not os.path.exists(STAT): time.sleep(0.2)
    check('0. the launcher is up and no game is running; no save yet', os.path.exists(STAT) and not game().get('running') and not (os.path.isdir(saves) and any(f.endswith('.witness_campaign') for f in os.listdir(saves))))
    # 1. a new game alone, from the launcher's own New game dialog
    cmd('tab 0', 'choice solo'); g = start_game(['newgamedlg'])
    check('1. New game... starts the game', g.get('running') == 1 and g.get('pid'), g)
    check('   with randomizer=custom;set:%s, the set copied under its hash' % H, cfg().get('randomizer') == 'custom;set:' + H and os.path.exists(copy) and open(copy, encoding='utf-8').read() == open(library, encoding='utf-8').read(), cfg().get('randomizer'))
    st1 = randstat(); check('   the game applies it', st1.get('state') == 'applied' and st1.get('fingerprint', '0').strip('0'), st1)
    # 2. progress, saved by the game
    time.sleep(20); send_game('nativesolve 64'); time.sleep(4)
    send_game('key ESC down'); time.sleep(0.2); send_game('key ESC up'); time.sleep(6); send_game('key ESC down'); time.sleep(0.2); send_game('key ESC up')
    t0 = time.time(); meta = ''
    while time.time() - t0 < 120:
        metas = sorted((os.path.join(saves, f) for f in os.listdir(saves) if f.endswith('.coop.txt')), key=os.path.getmtime) if os.path.isdir(saves) else []
        meta = open(metas[-1], errors='ignore').read() if metas else ''
        if 'custom;set:' + H in meta and 0x64 in solved(): break
        time.sleep(3)
    check('2. the game saved, its sidecar names the set (custom;set:%s), panel 0x64 solved' % H, 'custom;set:' + H in meta and 0x64 in solved(), re.findall(r'randomizer .*', meta))
    stop_game(); time.sleep(3)
    # 3. the set changes in the library: a new game would get another hash
    text = open(library, encoding='utf-8').read(); open(library, 'w', encoding='utf-8', newline='\n').write(text.replace(' Squares, a dot, a gap\n', ' Squares, a dot, a gap (changed)\n', 1))
    check('   (the changed set has another hash: a new game would get it)', text != open(library, encoding='utf-8').read())
    # 4. continue
    g = start_game(['primary'])
    check('3. Play solo continues the game', g.get('running') == 1 and g.get('pid'), g)
    check('   with the set its save was made with (custom;set:%s), not the changed one' % H, cfg().get('randomizer') == 'custom;set:' + H, cfg().get('randomizer'))
    st2 = randstat(); check('   the same puzzles (fingerprint)', st2.get('state') == 'applied' and st2.get('fingerprint') == st1.get('fingerprint'), (st1.get('fingerprint'), st2))
    t0 = time.time(); s2 = set()
    while time.time() - t0 < 60 and 0x64 not in s2: s2 = solved(); time.sleep(2)
    check('   and the solved panel back', 0x64 in s2, sorted(hex(x) for x in s2)[:8])
    stop_game()
finally:
    if proc:
        try: proc.terminate()
        except Exception: pass
    f = os.path.join(run, 'pidA.txt')
    if os.path.exists(f):
        try: subprocess.run(['taskkill', '/PID', open(f).read().strip(), '/F'], capture_output=True)
        except Exception: pass
print('ALL OK' if ok else 'FAILED'); sys.exit(0 if ok else 1)
