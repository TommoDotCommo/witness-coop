#!/usr/bin/env python3
"""T2 scenario for the lobby (launcher/lobby.h, launcher/main.cpp, tools/win/wlobby.py): two real launchers and two real games. A hosts, B joins; chat, voice, the host's choices, ready and start; each
launcher writes its game's cfg from the same roster and starts it; the two games find each other and race; when they end both launchers are back in the lobby with the result.
Mutants (builds of the launcher): the cfg of the game is not written, no result after the game, the player's own cfg not put back, no voice in the lobby.
usage: qa_lobby.py (called by tools/qa.sh lobby)"""
import os, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, ROOT, DIR1, DIR2, PYW, RUN, win

LDIR = os.path.join(ROOT, 'launcher', 'build')
LAB = os.path.expanduser('~/.local/bin/lab')

def base_cfg(name, port, tone):
    return ('id=1\nlisten=%d\npeer=%d\nsave_dir=instance\nshare_zip=1\nforce_active=1\ncap_fps=30\ndev_menu=1\nvoice_mute_output=1\nname=%s\nrace_countdown_ms=9000\n%s' % (port, port + 1, name, tone))

def run(launcher_exe='witness_launcher.exe'):
    subprocess.run(['make', '-C', os.path.join(ROOT, 'launcher'), 'launcher', 'launcher-mutants'], check=True, capture_output=True)
    subprocess.run([os.path.join(TOOLS, 'install.sh'), 'both'], check=True, capture_output=True)
    p1, p2 = int(os.environ.get('WITNESS_PORT1', '47001')), int(os.environ.get('WITNESS_PORT2', '47002'))
    for d, name, port, tone in ((DIR1, 'Alpha', p1, 'voice_mode=open\nvoice_test_tone=120,1000,5500\n'), (DIR2, 'Beta', p2, 'voice_mode=off\n')):
        open(os.path.join(d, 'witness_coop.cfg'), 'w').write(base_cfg(name, port, tone))
        for f in ('witness_proxy.log', 'witness_cmd.txt', 'witness_race_result.txt'):
            try: os.remove(os.path.join(d, f))
            except OSError: pass
        subprocess.run(['cp', os.path.join(LDIR, launcher_exe), os.path.join(d, 'witness_launcher_under_test.exe')], check=True)
    rects = [subprocess.run([LAB, 'rect', s], capture_output=True, text=True).stdout.strip() or '0 0 1280 760' for s in (os.environ.get('LAB_SLOTS', '0,1').split(',') + ['0', '1'])[:2]]
    out = os.path.join(RUN, 'lobby_out.txt'); lport = p1 + 100
    with open(out, 'w') as f:
        proc = subprocess.Popen([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wlobby.py')), win(os.path.join(DIR1, 'witness_launcher_under_test.exe')), win(DIR1), win(os.path.join(DIR2, 'witness_launcher_under_test.exe')), win(DIR2), str(lport), win(RUN), rects[0], rects[1]], stdout=f, stderr=subprocess.STDOUT, text=True)
        reg = set(); t0 = time.time()
        while proc.poll() is None and time.time() - t0 < 900:
            for w in 'AB':      # the games the launchers start are registered with the lab, so a failed run cannot leave them behind
                fp = os.path.join(RUN, 'pid%s.txt' % w)
                if os.path.exists(fp):
                    v = open(fp).read().strip()
                    if v and (w, v) not in reg: subprocess.run([LAB, 'register-pid', v], capture_output=True); reg.add((w, v))
            time.sleep(1)
        if proc.poll() is None: proc.kill()
    text = open(out, errors='ignore').read(); print(text, end='')
    return 'ALL OK' in text

def main():
    results = []
    print('=== lobby: clean build', flush=True); ok = run(); results.append(('clean', ok)); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    if 'clean' in sys.argv[1:]: return 0 if results[0][1] else 1      # `qa.sh lobby clean`: only the clean run
    for name, exe in (('cfg of the game not written', 'witness_launcher_LOBBY_NO_CFG.exe'), ('no result after the game', 'witness_launcher_LOBBY_NO_RESULT.exe'), ('own cfg not put back', 'witness_launcher_LOBBY_NO_RESTORE.exe'), ('no voice in the lobby', 'witness_launcher_LOBBY_NO_VOICE.exe')):
        print('=== lobby: MUTANT %s' % name, flush=True); caught = not run(exe); results.append(('mutant ' + name, caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
