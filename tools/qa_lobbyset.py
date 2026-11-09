#!/usr/bin/env python3
"""T2 scenario for the lobby sending the host's own puzzles (launcher/lobby.cpp SET_ASK/SET_PART, tools/win/wlobby_set.py): two real launchers and two
real games. A's library holds a set (the one of tools/qa_custom.py) and A's pick is that set; B has no puzzles. B must get the set from A over the
network, both games must start with it and apply it with the same fingerprint.
Mutant: the launcher built with WL_MUT_LOBBY_NO_SET_SEND (the host never answers a request for its set) must fail.
usage: qa_lobbyset.py [clean | mutant]   (tools/qa.sh lobbyset; only the clean run, or only the mutant)"""
import os, shutil, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, ROOT, DIR1, DIR2, PYW, RUN, win
import qa_custom, qa_lobby

LDIR = os.path.join(ROOT, 'launcher', 'build')
WITNESS_RESOURCES = os.environ['WITNESS_RESOURCES']

def run(launcher_exe='witness_launcher.exe'):
    subprocess.run(['make', '-C', os.path.join(ROOT, 'launcher'), 'launcher', os.path.join('build', 'witness_launcher_LOBBY_NO_SET_SEND.exe')], check=True, capture_output=True)
    subprocess.run([os.path.join(TOOLS, 'install.sh'), 'both'], check=True, capture_output=True)
    exe = qa_custom.cli(); raw = os.path.join(RUN, 'custom_set.txt'); open(raw, 'w').write(qa_custom.SET)
    text = subprocess.run([exe, 'print', raw], capture_output=True, text=True, check=True).stdout
    open(raw, 'w', newline='\n').write(text)
    h = subprocess.run([exe, 'set', raw], capture_output=True, text=True, check=True).stdout.split()[1]
    p1, p2 = int(os.environ.get('WITNESS_PORT1', '47001')), int(os.environ.get('WITNESS_PORT2', '47002'))
    for d, name, port, extra in ((DIR1, 'Alpha', p1, 'voice_mode=off\nrandomizer_pick=custom\nrandomizer_set=In-game test\nrandomizer_new_seed=0\n'), (DIR2, 'Beta', p2, 'voice_mode=off\n')):
        open(os.path.join(d, 'witness_coop.cfg'), 'w').write(qa_lobby.base_cfg(name, port, extra))
        shutil.rmtree(os.path.join(d, 'puzzles'), ignore_errors=True)
        for f in ('witness_proxy.log', 'witness_cmd.txt'):
            try: os.remove(os.path.join(d, f))
            except OSError: pass
        subprocess.run(['cp', os.path.join(LDIR, launcher_exe), os.path.join(d, 'witness_launcher_under_test.exe')], check=True)
    os.makedirs(os.path.join(DIR1, 'puzzles', 'sets')); open(os.path.join(DIR1, 'puzzles', 'sets', 'In-game test.txt'), 'w', newline='\n').write(text)      # A's library only
    rects = [subprocess.run([WITNESS_RESOURCES, 'rect', s], capture_output=True, text=True).stdout.strip() or '0 0 1280 760' for s in (os.environ.get('WITNESS_SLOTS', '0,1').split(',') + ['0', '1'])[:2]]
    out = os.path.join(RUN, 'lobbyset_out.txt'); lport = p1 + 100
    with open(out, 'w') as f:
        proc = subprocess.Popen([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wlobby_set.py')), win(os.path.join(DIR1, 'witness_launcher_under_test.exe')), win(DIR1), win(os.path.join(DIR2, 'witness_launcher_under_test.exe')), win(DIR2), str(lport), win(RUN), rects[0], rects[1], h], stdout=f, stderr=subprocess.STDOUT, text=True)
        reg = set(); t0 = time.time()
        while proc.poll() is None and time.time() - t0 < 600:
            for w in 'AB':      # the games the launchers start are registered with the resource provider, so a failed run cannot leave them behind
                fp = os.path.join(RUN, 'pid%s.txt' % w)
                if os.path.exists(fp):
                    v = open(fp).read().strip()
                    if v and (w, v) not in reg: subprocess.run([WITNESS_RESOURCES, 'register-pid', v], capture_output=True); reg.add((w, v))
            time.sleep(1)
        if proc.poll() is None: proc.kill()
    for w in 'AB':
        try: os.remove(os.path.join(RUN, 'pid%s.txt' % w))
        except OSError: pass
    text = open(out, errors='ignore').read(); print(text, end='')
    return 'ALL OK' in text

def main():
    results = []
    if 'mutant' not in sys.argv[1:]:
        print('=== lobbyset: clean build', flush=True); ok = run(); results.append(('clean', ok)); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    if 'clean' not in sys.argv[1:]:
        print('=== lobbyset: MUTANT the host never sends its set', flush=True); caught = not run('witness_launcher_LOBBY_NO_SET_SEND.exe')
        results.append(('mutant no set sent', caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
