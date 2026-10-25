#!/usr/bin/env python3
"""T2 scenario for the launcher window (launcher/main.cpp, driven with window messages by tools/win/wlauncher.py): the launcher is opened on a game folder with a real save in it;
the test chooses video options, a name, a key, other players and options inside the save, presses Save settings, and compares the files on disk (Launcher.variables in the game's
own format, the cfg with every unknown line still there, the save changed only in the edited values). Then it presses Continue: the game it starts must say in its own log that it
applies those video settings, and the next save it writes must carry the edited mouse speed and field of view (the game read them).
Mutants (each a build of the launcher): the video file is not written, the save is not patched, the cfg loses the lines the launcher does not know.
usage: qa_launcher.py (called by tools/qa.sh launcher)"""
import os, re, shutil, struct, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, ROOT, DIR1, PYW, pid, win
import qa_sweep

LDIR = os.path.join(ROOT, 'launcher', 'build')

def send(d, lines):
    cmd = os.path.join(d, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)
    open(cmd + '.tmp', 'w').write('\n'.join(lines) + '\n'); os.replace(cmd + '.tmp', cmd)
    t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)

def kill_game():
    p = pid('A')
    subprocess.run(['taskkill.exe', '/PID', p, '/F'], capture_output=True)
    for _ in range(40):
        if p not in subprocess.run(['tasklist.exe', '/FI', 'PID eq %s' % p, '/NH'], capture_output=True, text=True).stdout: break
        time.sleep(0.25)

def run_window(exe_name, mode):
    exe = os.path.join(DIR1, exe_name); shutil.copy(os.path.join(LDIR, exe_name), exe)
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wlauncher.py')), win(exe), win(DIR1), win(os.path.join(DIR1, 'launcher_controls.txt')), mode], capture_output=True, text=True, timeout=180)
    print(r.stdout, end=''); gp = re.search(r'GAMEPID (\d+)', r.stdout)
    return ('CHECK FAILED' not in r.stdout and 'DONE' in r.stdout), (gp.group(1) if gp else None)

def main():
    subprocess.run(['make', '-C', os.path.join(ROOT, 'launcher'), 'launcher', 'launcher-mutants'], check=True, capture_output=True)
    saves = os.path.join(DIR1, 'appdata', 'The Witness'); results = []
    print('=== launcher: a game folder with a real save', flush=True)
    qa_sweep.restart('name=Alpha', '', which='A')
    send(DIR1, ['key ESC down']); time.sleep(0.2); send(DIR1, ['key ESC up'])      # the pause menu: the game writes its campaign
    t0 = time.time()
    while time.time() - t0 < 40 and not [f for f in os.listdir(saves) if f.endswith('.witness_campaign')]: time.sleep(1)
    time.sleep(1); kill_game()
    fixture = {f: open(os.path.join(saves, f), 'rb').read() for f in os.listdir(saves) if os.path.isfile(os.path.join(saves, f))}
    cfg_fixture = open(os.path.join(DIR1, 'witness_cmd.txt'), 'w') and None
    cfg_fixture = open(os.path.join(DIR1, 'witness_coop.cfg')).read()
    def reset():
        for f in os.listdir(saves):
            if os.path.isfile(os.path.join(saves, f)): os.remove(os.path.join(saves, f))
        for f, b in fixture.items(): open(os.path.join(saves, f), 'wb').write(b)
        open(os.path.join(DIR1, 'witness_coop.cfg'), 'w').write(cfg_fixture)
    print('=== launcher: clean build', flush=True)
    log = os.path.join(DIR1, 'logs', 'log.txt'); before = os.path.getmtime(log) if os.path.exists(log) else 0
    ok, gp = run_window('witness_launcher.exe', 'launch')
    if gp:
        subprocess.run([os.path.expanduser('~/.local/bin/lab'), 'register-pid', gp], capture_output=True)
        open(os.path.join(os.environ.get('WITNESS_RUN', os.path.join(ROOT, 'local', 'run')), 'pidA.txt'), 'w').write(gp)
        t0 = time.time(); block = ''
        while time.time() - t0 < 120:
            if os.path.exists(log) and os.path.getmtime(log) > before:
                t = open(log, errors='ignore').read(); m = re.search(r'Applying user settings:(.*?)(?:\n\s*WIN:|\Z)', t, re.S)
                if m and 'geometry_detail' in m.group(1): block = m.group(1); break
            time.sleep(1)
        want = {'render_profile': '2', 'fullscreen': 'false', 'vsync': 'true', 'resolution': 'custom (1366 x 768)', 'multisampling': '8x MSAA', 'shading_quality': '0', 'reflection_quality': '1', 'texture_quality': '2', 'texture_detail': '1', 'geometry_detail': '0'}
        got = dict((a.strip(), b.strip()) for a, b in (l.split(' = ', 1) for l in block.splitlines() if ' = ' in l))
        bad = {k: (v, got.get(k)) for k, v in want.items() if got.get(k) != v}
        print('  %-90s %s  %s' % ("the game's own log says it applies the video settings chosen in the launcher", 'ok' if block and not bad else 'FAILED', bad if bad else ''), flush=True); ok = ok and bool(block) and not bad
        # the game read the options of the save: its next save carries them
        t0 = time.time()
        while time.time() - t0 < 120 and 'overlay: resources ready' not in open(os.path.join(DIR1, 'witness_proxy.log'), errors='ignore').read(): time.sleep(1)
        time.sleep(10)
        def newest(): return max((os.path.join(saves, f) for f in os.listdir(saves) if f.endswith('.witness_campaign')), key=os.path.getmtime)
        m0 = os.path.getmtime(newest()); size0 = os.path.getsize(newest())
        send(DIR1, ['key ESC down']); time.sleep(0.2); send(DIR1, ['key ESC up'])      # the pause menu: the game writes its campaign again, from what it holds in memory
        t0 = time.time()
        while time.time() - t0 < 60 and os.path.getmtime(newest()) <= m0: time.sleep(1)
        fs = [os.path.basename(newest())]; n0 = 0
        b = open(os.path.join(saves, fs[-1]), 'rb').read()
        def opt(n, fmt): p = b'\0' + n.encode() + b'\0'; i = b.find(p); return struct.unpack(fmt, b[i + len(p):i + len(p) + 4])[0]
        sp, fov, inv = opt('mouse_speed_3d', '<f'), opt('user_fov_vertical', '<f'), opt('invert_y_axis', '<i')
        good = (os.path.getmtime(newest()) > m0 and sp == 12.5 and fov == 70.0 and inv == 1)
        print('  %-90s %s  %s' % ("the game read the launcher's options: the save it writes from memory has mouse speed 12.5, FOV 70, inverted", 'ok' if good else 'FAILED', (os.path.getmtime(newest()) > m0, sp, fov, inv)), flush=True); ok = ok and good
    else: print('  the launcher did not start a game  FAILED', flush=True); ok = False
    kill_game(); results.append(('clean', ok)); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    for name, exe in (('video file not written', 'witness_launcher_NO_VIDEO_WRITE.exe'), ('save not patched', 'witness_launcher_NO_SAVE_WRITE.exe'), ('cfg loses the lines it does not know', 'witness_launcher_CFG_DROP.exe')):
        print('=== launcher: MUTANT %s' % name, flush=True); reset()
        if os.path.exists(os.path.join(saves, 'Launcher.variables')): os.remove(os.path.join(saves, 'Launcher.variables'))
        caught = not run_window(exe, 'files')[0]; results.append((name, caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
