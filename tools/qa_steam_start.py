#!/usr/bin/env python3
"""T1 check of Play in Steam handing over to the launcher (common/handover.h; tools/win/wsteamstart.py does the work) with one game copy. It sets Steam's
environment variables itself, so it does not need the Steam client for the decision; what the Steam client does with the process tree (playtime, the
overlay) is not proven here. The decision's mutants are game-free (make -C launcher shell-test).
usage: qa_steam_start.py   (called by tools/qa.sh steamstart; uses the first game copy of the allocation)"""
import os, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, ROOT, DIR1, PYW, RUN, win

def main():
    subprocess.run(['make', '-C', os.path.join(ROOT, 'launcher'), 'launcher'], check=True, capture_output=True)
    subprocess.run([os.path.join(TOOLS, 'install.sh'), 'A'], check=True, capture_output=True)
    subprocess.run(['cp', os.path.join(ROOT, 'launcher', 'build', 'witness_launcher.exe'), os.path.join(DIR1, 'witness_launcher.exe')], check=True)
    res = os.environ.get('WITNESS_RESOURCES', '')
    slot = (os.environ.get('WITNESS_SLOTS', '0').split(',') + ['0'])[0]
    rect = (subprocess.run([res, 'rect', slot], capture_output=True, text=True).stdout.strip() if res else '') or '0 0 1280 760'
    for f in ('pidA.txt', 'pidL.txt', 'pidB.txt', 'pidC.txt'):
        try: os.remove(os.path.join(RUN, f))
        except OSError: pass
    out = os.path.join(RUN, 'steam_start_out.txt')
    with open(out, 'w') as f:
        proc = subprocess.Popen([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wsteamstart.py')), win(DIR1), win(RUN), rect], stdout=f, stderr=subprocess.STDOUT, text=True)
        reg = set(); t0 = time.time()
        while proc.poll() is None and time.time() - t0 < 400:
            for w in ('A', 'L', 'B', 'C'):      # every game and launcher this check starts is registered, so a failed run cannot leave them behind
                fp = os.path.join(RUN, 'pid%s.txt' % w)
                if res and os.path.exists(fp):
                    v = open(fp).read().strip()
                    if v and v not in reg: subprocess.run([res, 'register-pid', v], capture_output=True); reg.add(v)
            time.sleep(0.5)
        if proc.poll() is None: proc.kill()
    text = open(out, errors='ignore').read(); print(text, end='')
    return 0 if 'ALL OK' in text else 1

if __name__ == '__main__': sys.exit(main())
