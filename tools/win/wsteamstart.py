# T1 check of Play in Steam (common/handover.h, co-op standard K1), run with Windows Python (tools/qa_steam_start.py runs it). The game is started the way Steam
# starts it: with Steam's environment (SteamAppId, SteamGameId) and no launcher session. A) it must hand over: witness_launcher.exe beside it starts, the game
# process ends before it opens a window. B) the launcher's own game (Play solo there) must stay: it carries WITNESS_COOP_LAUNCHER, it inherited Steam's
# environment through the launcher (one Steam launch), and it is still running 25 s later. C) a game started without Steam's environment (the test tools) stays.
# Evidence is process state (alive / ended, read with OpenProcess) and the launcher's state file; the proxy log only supplies pids and the decision's inputs.
# usage: wsteamstart.py <game dir> <run dir> <rect "x y w h">     (writes pidA.txt, pidL.txt, pidB.txt, pidC.txt in <run dir> for registration)
import ctypes, ctypes.wintypes as wt, os, re, subprocess, sys, time
gdir, run, rect = sys.argv[1], sys.argv[2], sys.argv[3]
here = os.path.dirname(os.path.abspath(__file__))
LOG = os.path.join(gdir, 'witness_proxy.log')
k32 = ctypes.WinDLL('kernel32', use_last_error=True); k32.OpenProcess.restype = wt.HANDLE
ok = True
def check(name, cond, detail=''):
    global ok; ok = ok and bool(cond); print('  %-104s %s  (%s)' % (name, 'ok' if cond else 'FAILED', str(detail)[:240]), flush=True)
def alive(pid):
    h = k32.OpenProcess(0x1000, False, int(pid))      # PROCESS_QUERY_LIMITED_INFORMATION
    if not h: return False
    code = wt.DWORD(); k32.GetExitCodeProcess(h, ctypes.byref(code)); k32.CloseHandle(h); return code.value == 259      # STILL_ACTIVE
def log_text():
    try: return open(LOG, errors='ignore').read()
    except OSError: return ''
def wait_re(pattern, secs, after=0):
    end = time.time() + secs
    while time.time() < end:
        m = re.search(pattern, log_text()[after:])
        if m: return m
        time.sleep(0.3)
    return None
def kill(pid):
    if pid and alive(pid): subprocess.run(['taskkill', '/PID', str(pid), '/F'], capture_output=True)
def place(pid):
    x, y, w, h = rect.split(); subprocess.run([sys.executable, '-I', os.path.join(here, 'wplace.py'), str(pid), x, y, w, h], capture_output=True, timeout=60)
statf, cmdf = os.path.join(run, 'steamstart_stat.txt'), os.path.join(run, 'steamstart_cmd.txt')
for f in (LOG, statf, cmdf, os.path.join(gdir, 'witness_cmd.txt')):
    try: os.remove(f)
    except OSError: pass
cfg = 'save_dir=instance\nshare_zip=1\nforce_active=1\ncap_fps=30\nvoice_mode=off\nvoice_mute_output=1\nname=Alpha\nlisten=%s\n' % os.environ.get('WITNESS_PORT1', '47001')
open(os.path.join(gdir, 'witness_coop.cfg'), 'w').write(cfg)
base = {k: v for k, v in os.environ.items() if k.upper() not in ('STEAMAPPID', 'STEAMGAMEID', 'WITNESS_COOP_LAUNCHER')}
si = subprocess.STARTUPINFO(); si.dwFlags = 1; si.wShowWindow = 4      # SW_SHOWNOACTIVATE
exe = os.path.join(gdir, 'witness64_d3d11.exe')
launcher_pid = game_b = game_c = None
try:
    # A) as Steam starts it
    print('A) the game started with Steam\'s environment and no launcher session', flush=True)
    env = dict(base, SteamAppId='210970', SteamGameId='210970', WITNESS_LAUNCHER_STAT_FILE=statf, WITNESS_LAUNCHER_CMD_FILE=cmdf)
    g = subprocess.Popen([exe], cwd=gdir, env=env, startupinfo=si); open(os.path.join(run, 'pidA.txt'), 'w').write(str(g.pid))
    m = wait_re(r'handover: the launcher runs \(pid (\d+)\)', 60)
    if m: launcher_pid = int(m[1]); open(os.path.join(run, 'pidL.txt'), 'w').write(str(launcher_pid))
    dec = wait_re(r'handover: steam (\d) launcher_session (\d).*-> (\S+)', 5)
    check('A1. the decision saw Steam and no launcher session', dec and dec[1] == '1' and dec[2] == '0' and dec[3] == 'launcher', dec and dec[0])
    end = time.time() + 20
    while time.time() < end and g.poll() is None: time.sleep(0.2)
    check('A2. the game Steam started has ended (it closed before its window, handing over)', g.poll() is not None, 'exit %s' % g.poll())
    check('A3. witness_launcher.exe beside it is running', launcher_pid and alive(launcher_pid), launcher_pid)
    st = ''; end = time.time() + 20
    while time.time() < end and 'ui tab' not in st:
        try: st = open(statf, errors='ignore').read()
        except OSError: pass
        time.sleep(0.3)
    check('A4. the launcher shows its Play tab (its state file)', re.search(r'ui tab 0 label Play', st), next((l for l in st.splitlines() if l.startswith('ui ')), ''))
    # B) the launcher's own game
    print('B) Play solo in that launcher: the game it starts stays', flush=True)
    mark = len(log_text())
    open(cmdf + '.tmp', 'w').write('choice solo\nprimary\n'); os.replace(cmdf + '.tmp', cmdf)
    m = wait_re(r'proxy loaded: pid=(\d+)', 60, after=mark)
    if m: game_b = int(m[1]); open(os.path.join(run, 'pidB.txt'), 'w').write(str(game_b)); place(game_b)
    dec = wait_re(r'handover: steam (\d) launcher_session (\d).*-> (\S+)', 20, after=mark)
    check('B1. the launcher\'s game carries the launcher session and inherited Steam\'s environment (one Steam launch)', dec and dec[1] == '1' and dec[2] == '1' and dec[3] == 'stay', dec and dec[0])
    time.sleep(25)
    check('B2. the launcher\'s game is still running 25 s later (it did not hand over again)', game_b and alive(game_b), game_b)
    check('B3. the launcher is still running beside it', launcher_pid and alive(launcher_pid))
    kill(game_b); time.sleep(2)
    # C) the test tools' way: no Steam environment
    print('C) the game started without Steam\'s environment stays', flush=True)
    mark = len(log_text())
    g = subprocess.Popen([exe], cwd=gdir, env=base, startupinfo=si); game_c = g.pid; open(os.path.join(run, 'pidC.txt'), 'w').write(str(game_c))
    dec = wait_re(r'handover: steam (\d) launcher_session (\d).*-> (\S+)', 60, after=mark)
    check('C1. without Steam\'s environment the decision is stay', dec and dec[1] == '0' and dec[3] == 'stay', dec and dec[0])
    time.sleep(10); check('C2. that game is running', alive(game_c))
finally:
    try: open(cmdf, 'w').write('quit\n')
    except OSError: pass
    time.sleep(2)
    for p in (game_b, game_c, launcher_pid): kill(p)
print('ALL OK' if ok else 'FAILED')
