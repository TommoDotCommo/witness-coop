# T1 check of the Steam path with one game (Windows Python; tools/qa_steam_lobby.py runs it). One Steam account on one PC cannot be two lobby members,
# so this proves the parts one player can: A) a game started without Steam's environment (as the launcher started games before Steam lobbies) has no
# Steam connection, and the mod says so instead of falling back to UDP; B) the launcher hosts a friends-only Steam lobby whose id, mod key, protocol key,
# code and member limit read back through the Steam API, starts its game from that lobby with a Steam cfg (transport=steam), and the mod in the game
# reports the game's own Steam connection up (steamstat: app 210970).
# usage: wsteamlobby.py <launcher exe> <game dir> <run dir> <rect "x y w h">
import os, re, subprocess, sys, time
exe, gdir, run, rect = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
here = os.path.dirname(os.path.abspath(__file__))
LOG = os.path.join(gdir, 'witness_proxy.log')
ok = True
def check(name, cond, detail=''):
    global ok; ok = ok and bool(cond); print('  %-100s %s  (%s)' % (name, 'ok' if cond else 'FAILED', str(detail)[:240]), flush=True)
def log_lines():
    try: return open(LOG, errors='ignore').read().splitlines()
    except OSError: return []
def wait_log(pattern, secs):
    end = time.time() + secs
    while time.time() < end:
        for l in log_lines():
            if re.search(pattern, l): return l
        time.sleep(0.5)
    return None
def send_game(lines):
    p = os.path.join(gdir, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(p) and time.time() - t < 8: time.sleep(0.05)
    open(p + '.tmp', 'w').write('\n'.join(lines) + '\n'); os.replace(p + '.tmp', p)
def place(pid):
    x, y, w, h = rect.split(); subprocess.run([sys.executable, '-I', os.path.join(here, 'wplace.py'), str(pid), x, y, w, h], capture_output=True, timeout=60)
def kill(pid):
    subprocess.run(['taskkill', '/PID', str(pid), '/F'], capture_output=True)
def write_cfg(text):
    open(os.path.join(gdir, 'witness_coop.cfg'), 'w').write(text)
BASE = 'id=1\nlisten=47100\npeer=47101\nsave_dir=instance\nshare_zip=1\nforce_active=1\ncap_fps=30\ndev_menu=1\nvoice_mode=off\nvoice_mute_output=1\nname=Alpha\n'
def clean():
    for f in ('witness_proxy.log', 'witness_cmd.txt', 'lobby_stat.txt', 'launcher_cmd.txt'):
        try: os.remove(os.path.join(gdir, f))
        except OSError: pass

# A) the game without Steam's environment
print('A) a game started without SteamAppId, cfg transport=steam', flush=True)
clean(); write_cfg(BASE + 'transport=steam\n')
env = {k: v for k, v in os.environ.items() if k.lower() not in ('steamappid', 'steamgameid')}
si = subprocess.STARTUPINFO(); si.dwFlags = 1; si.wShowWindow = 4      # SW_SHOWNOACTIVATE
g = subprocess.Popen([os.path.join(gdir, 'witness64_d3d11.exe')], cwd=gdir, env=env, startupinfo=si)
open(os.path.join(run, 'pidA.txt'), 'w').write(str(g.pid)); time.sleep(4); place(g.pid)
line = wait_log(r'coop: steam transport down|steam: the game.s Steam connection is up', 90)
check('A1. the mod reports the Steam transport down (the game did not start Steam by itself), not a UDP fallback', line and 'steam transport down' in line, line)
check('A2. no UDP socket was opened for it', not any('listening' in l for l in log_lines()), [l for l in log_lines() if 'coop:' in l][-3:])
send_game(['netstat']); end = time.time() + 15; ns = None
while time.time() < end and not ns:
    ns = next((l for l in log_lines() if 'netstat:' in l), None); time.sleep(0.5)
check('A3. netstat says the transport is Steam and it is down (0 peers)', ns and 'transport steam up 0 peers 0' in ns, ns)
kill(g.pid); time.sleep(3)

# B) the launcher hosts a Steam lobby and starts the game from it
print('B) the launcher hosts a Steam lobby and starts its game', flush=True)
clean(); write_cfg(BASE)
stat_p, cmd_p = os.path.join(gdir, 'lobby_stat.txt'), os.path.join(gdir, 'launcher_cmd.txt')
def cmd(*lines):
    t = time.time()
    while os.path.exists(cmd_p) and time.time() - t < 8: time.sleep(0.05)
    open(cmd_p + '.tmp', 'w').write('\n'.join(lines) + '\n'); os.replace(cmd_p + '.tmp', cmd_p)
    t = time.time()
    while os.path.exists(cmd_p) and time.time() - t < 8: time.sleep(0.05)
def stat():
    try: txt = open(stat_p, errors='ignore').read()
    except OSError: return {}
    r = {}
    for l in txt.splitlines():
        m = re.match(r'steam mode (\d) lobby (\d+) members (\d+) limit (\d+) mod (\S*) protocol (\S*) code_set (\d) failed (\d) status (.*)', l)
        if m: r['steam'] = dict(mode=int(m[1]), lobby=int(m[2]), members=int(m[3]), limit=int(m[4]), mod=m[5], protocol=m[6], code=int(m[7]), failed=int(m[8]), status=m[9])
        if l.startswith('game '): v = l.split(); r['game'] = {v[i]: int(v[i + 1]) for i in range(1, len(v) - 1, 2)}
        if l.startswith('lobby '): v = l.split(); r['lobby'] = {v[i]: int(v[i + 1]) for i in range(1, len(v) - 1, 2)}
        if l.startswith('invite'): r['invite'] = l[7:].strip()
    return r
def wait(pred, secs):
    end = time.time() + secs; s = {}
    while time.time() < end:
        s = stat()
        try:
            if pred(s): return s
        except (KeyError, TypeError): pass
        time.sleep(0.25)
    return s
lsi = subprocess.STARTUPINFO(); lsi.dwFlags = 1; lsi.wShowWindow = 4
launcher = subprocess.Popen([exe, '--dir', gdir, '--no-activate', '--stat-file', stat_p, '--cmd-file', cmd_p], startupinfo=lsi, env=env)
open(os.path.join(run, 'pidL.txt'), 'w').write(str(launcher.pid))
wait(lambda s: 'steam' in s, 20)
cmd('steamhost')
s = wait(lambda s: s['steam']['lobby'] != 0 and s['lobby']['joined'] == 1 or s['steam']['failed'], 60)
st = s.get('steam', {})
check('B1. the launcher created a Steam lobby (a lobby id came back from Steam)', st.get('lobby', 0) != 0 and not st.get('failed'), st)
lid = st.get('lobby', 0)
check('B2. the id is a Steam lobby id (universe 1, chat type) and the invite shows it', (lid >> 56) == 1 and ((lid >> 52) & 15) == 8 and s.get('invite') == str(lid), (lid, s.get('invite')))
check('B3. the mod key and protocol key read back through the Steam API', st.get('mod') == 'witness-coop' and st.get('protocol') == 'lobby3-net1', (st.get('mod'), st.get('protocol')))
check('B4. the join code is in the lobby data; the lobby has 1 member (the host) and room for 16', st.get('code') == 1 and st.get('members') == 1 and st.get('limit') == 16, st)
check('B5. the launcher is the lobby host in its own lobby logic', s.get('lobby', {}).get('host') == 1 and s.get('lobby', {}).get('joined') == 1, s.get('lobby'))
def overlay_in(pid):      # facts, not checks: is Steam's overlay loaded into this process (the invite dialog and Steam's own notices need it)
    out = subprocess.run(['tasklist', '/M', 'gameoverlayrenderer64.dll', '/FI', 'PID eq %d' % pid], capture_output=True, text=True).stdout
    return str(pid) in out
print('     fact: Steam overlay loaded into the launcher: %s' % overlay_in(launcher.pid), flush=True)
cmd('start')
s = wait(lambda s: s['game']['running'] == 1 and s['game']['pid'] != 0, 30)
gpid = s.get('game', {}).get('pid', 0)
check('B6. the launcher started the game from the Steam lobby', gpid != 0, s.get('game'))
if gpid:
    open(os.path.join(run, 'pidA.txt'), 'w').write(str(gpid)); time.sleep(3); place(gpid)
    cfg = open(os.path.join(gdir, 'witness_coop.cfg'), errors='ignore').read()
    check('B7. the game cfg says transport=steam and keeps no UDP peer line', 'transport=steam' in cfg.splitlines() and not any(l.startswith('peer=') for l in cfg.splitlines()), [l for l in cfg.splitlines() if l.startswith(('peer', 'transport'))])
    line = wait_log(r'coop: steam transport down|steam: the game.s Steam connection is up', 90)
    time.sleep(2); print('     fact: Steam overlay loaded into the game started from the lobby: %s (Steam puts it there when the game\'s Steam API is up and the overlay is on)' % overlay_in(gpid), flush=True)
    check('B8. the mod in the game took the game\'s own Steam connection (it inherited the launcher\'s Steam environment)', line and 'connection is up' in line, line)
    n0 = len(log_lines()); send_game(['steamstat']); end = time.time() + 90; ss = None      # (the game takes the command once its loading lets the command thread run)
    while time.time() < end and not ss:
        for l in log_lines()[n0:]:
            if 'steamstat: transport steam' in l: ss = l
        time.sleep(0.5)
    check('B9. steamstat: transport steam up 1 app 210970, this player\'s SteamID known', ss and 'up 1' in ss and 'app 210970' in ss and 'self_set 1' in ss, ss)
    def alive(pid): return str(pid) in subprocess.run(['tasklist', '/FI', 'PID eq %d' % pid], capture_output=True, text=True).stdout
    t_alive = time.time()
    while time.time() - t_alive < 60 and alive(gpid): time.sleep(1)
    check('B9b. the game started from the Steam lobby (Steam puts its overlay in it) is still running a minute later (its Present hook and the overlay\'s looped into a stack overflow before)', alive(gpid), 'alive %.0f s' % (time.time() - t_alive))
    ov = [l for l in log_lines() if l.startswith('overlay: ') and ('Present' in l or 'vtable in' in l)]
    print('     ' + ' | '.join(ov[:5]), flush=True)
    check('B10. alone in the lobby the game says so and sends nothing over UDP', any('alone in a Steam lobby' in l for l in log_lines()) and not any('listening' in l for l in log_lines()), [l for l in log_lines() if l.startswith('coop:') or 'coop:' in l][-3:])
    kill(gpid)
    s = wait(lambda s: s['game']['running'] == 0, 30)
    s = wait(lambda s: s['lobby']['phase'] == 0, 30)
    check('B11. after the game the launcher is back in the Steam lobby (same lobby id), its own cfg put back', s.get('steam', {}).get('lobby') == lid and 'transport=steam' not in open(os.path.join(gdir, 'witness_coop.cfg'), errors='ignore').read(), (s.get('steam'), s.get('lobby')))
cmd('leave'); s = wait(lambda s: s['steam']['lobby'] == 0, 15)
check('B12. leaving leaves the Steam lobby', s.get('steam', {}).get('lobby') == 0, s.get('steam'))
cmd('quit'); time.sleep(2)
if launcher.poll() is None: kill(launcher.pid)
print('ALL OK' if ok else 'SOME FAILED', flush=True)
