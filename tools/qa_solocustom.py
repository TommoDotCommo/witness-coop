#!/usr/bin/env python3
"""T1 scenario for "Your puzzles" played alone from the launcher and continued (tools/win/wsolo_custom.py): one real launcher and one real game.
New game... with a set picked must copy the set under its hash and start the game with it; after a solve and a save, the set is changed in the
library, and Play solo must continue with the set the save was made with (the same puzzles, the solve kept).
Mutant: the launcher built with WL_MUT_CONTINUE_REPICKS (a continued game is given the pick: its puzzles change under the player) must fail.
usage: qa_solocustom.py [clean | mutant]   (tools/qa.sh solocustom; one game slot)"""
import os, shutil, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, ROOT, DIR1, PYW, RUN, win
import qa_custom

LDIR = os.path.join(ROOT, 'launcher', 'build')
WITNESS_RESOURCES = os.environ['WITNESS_RESOURCES']

def run(launcher_exe='witness_launcher.exe'):
    subprocess.run(['make', '-C', os.path.join(ROOT, 'launcher'), 'launcher', os.path.join('build', 'witness_launcher_CONTINUE_REPICKS.exe')], check=True, capture_output=True)
    subprocess.run([os.path.join(TOOLS, 'install.sh'), 'A'], check=True, capture_output=True)
    exe = qa_custom.cli(); raw = os.path.join(RUN, 'custom_set.txt'); open(raw, 'w').write(qa_custom.SET)
    text = subprocess.run([exe, 'print', raw], capture_output=True, text=True, check=True).stdout
    open(raw, 'w', newline='\n').write(text)
    h = subprocess.run([exe, 'set', raw], capture_output=True, text=True, check=True).stdout.split()[1]
    d = DIR1
    open(os.path.join(d, 'witness_coop.cfg'), 'w').write('save_dir=instance\nshare_zip=1\nforce_active=1\ncap_fps=%s\ndev_menu=1\nname=Alpha\nupnp=0\nfind_tailscale=0\n'
        'randomizer_pick=custom\nrandomizer_set=In-game test\nrandomizer_new_seed=0\nlauncher_choice=solo\n' % os.environ.get('WITNESS_FPS', '30'))
    for sub in ('puzzles', os.path.join('appdata', 'The Witness')): shutil.rmtree(os.path.join(d, sub), ignore_errors=True)      # no set copy, no save: a first game
    for f in ('witness_proxy.log', 'witness_cmd.txt', 'lobby_stat.txt', 'launcher_cmd.txt'):
        try: os.remove(os.path.join(d, f))
        except OSError: pass
    os.makedirs(os.path.join(d, 'puzzles', 'sets')); open(os.path.join(d, 'puzzles', 'sets', 'In-game test.txt'), 'w', newline='\n').write(text)
    subprocess.run(['cp', os.path.join(LDIR, launcher_exe), os.path.join(d, 'witness_launcher_under_test.exe')], check=True)
    slot = os.environ.get('WITNESS_SLOTS', '0').split(',')[0]
    rect = subprocess.run([WITNESS_RESOURCES, 'rect', slot], capture_output=True, text=True).stdout.strip() or '0 0 1280 760'
    out = os.path.join(RUN, 'solocustom_out.txt')
    with open(out, 'w') as f:
        proc = subprocess.Popen([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wsolo_custom.py')), win(os.path.join(d, 'witness_launcher_under_test.exe')), win(d), win(RUN), rect, h], stdout=f, stderr=subprocess.STDOUT, text=True)
        reg = set(); t0 = time.time()
        while proc.poll() is None and time.time() - t0 < 900:
            fp = os.path.join(RUN, 'pidA.txt')      # the games the launcher starts are registered with the resource provider, so a failed run cannot leave them behind
            if os.path.exists(fp):
                v = open(fp).read().strip()
                if v and v != '0' and v not in reg: subprocess.run([WITNESS_RESOURCES, 'register-pid', v], capture_output=True); reg.add(v)
            time.sleep(1)
        if proc.poll() is None: proc.kill()
    text = open(out, errors='ignore').read(); print(text, end='')
    return 'ALL OK' in text

def main():
    results = []
    if 'mutant' not in sys.argv[1:]:
        print('=== solocustom: clean build', flush=True); ok = run(); results.append(('clean', ok)); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    if 'clean' not in sys.argv[1:]:
        print('=== solocustom: MUTANT a continued game is given the pick', flush=True); caught = not run('witness_launcher_CONTINUE_REPICKS.exe')
        results.append(('mutant continue re-picks', caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
