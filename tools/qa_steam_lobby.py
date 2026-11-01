#!/usr/bin/env python3
"""T1 check of the Steam lobby and the in-game Steam transport with one game (tools/win/wsteamlobby.py does the work). Needs Steam running and logged in
on an account that owns The Witness; it creates a friends-only Steam lobby for a minute and leaves it. Two Steam members cannot be proven on one PC with one
account: the two-account test is in docs/STEAM.md.
usage: qa_steam_lobby.py   (called by tools/qa.sh steam; uses the first game copy of the allocation)"""
import os, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, ROOT, DIR1, PYW, RUN, win

def run(proxy_dll=None):
    subprocess.run(['make', '-C', os.path.join(ROOT, 'launcher'), 'launcher'], check=True, capture_output=True)
    subprocess.run([os.path.join(TOOLS, 'install.sh'), 'A'], check=True, capture_output=True)
    if proxy_dll: subprocess.run(['cp', proxy_dll, os.path.join(DIR1, 'AnselSDK64.dll')], check=True)      # (a mutant; install.sh made the slot's copy private)
    subprocess.run(['cp', os.path.join(ROOT, 'launcher', 'build', 'witness_launcher.exe'), os.path.join(DIR1, 'witness_launcher.exe')], check=True)
    res = os.environ.get('WITNESS_RESOURCES', '')
    slot = (os.environ.get('WITNESS_SLOTS', '0').split(',') + ['0'])[0]
    rect = (subprocess.run([res, 'rect', slot], capture_output=True, text=True).stdout.strip() if res else '') or '0 0 1280 760'
    out = os.path.join(RUN, 'steam_lobby_out.txt')
    for f in ('pidA.txt', 'pidL.txt'):
        try: os.remove(os.path.join(RUN, f))
        except OSError: pass
    with open(out, 'w') as f:
        proc = subprocess.Popen([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wsteamlobby.py')), win(os.path.join(DIR1, 'witness_launcher.exe')), win(DIR1), win(RUN), rect], stdout=f, stderr=subprocess.STDOUT, text=True)
        reg = set(); t0 = time.time()
        while proc.poll() is None and time.time() - t0 < 600:
            for w in ('A', 'L'):      # every game and launcher this check starts is registered, so a failed run cannot leave them behind
                fp = os.path.join(RUN, 'pid%s.txt' % w)
                if res and os.path.exists(fp):
                    v = open(fp).read().strip()
                    if v and v not in reg: subprocess.run([res, 'register-pid', v], capture_output=True); reg.add(v)
            time.sleep(0.5)
        if proc.poll() is None: proc.kill()
    text = open(out, errors='ignore').read(); print(text, end='')
    return 'ALL OK' in text

if __name__ == '__main__':
    print('=== steam: clean build', flush=True); ok = run()
    if '--no-mutants' not in sys.argv:      # the Present loop with the Steam overlay not broken: the game started from the Steam lobby must be seen to crash
        subprocess.run(['make', '-C', os.path.join(ROOT, 'proxy'), 'build/AnselSDK64_PRESENT_LOOP.dll'], check=True, capture_output=True)
        print('=== steam: MUTANT the Present loop with the Steam overlay is not broken', flush=True); out = run(os.path.join(ROOT, 'proxy', 'build', 'AnselSDK64_PRESENT_LOOP.dll'))
        caught = not out; print('mutant: %s' % ('caught' if caught else 'SURVIVED (the check cannot fail for it)'), flush=True); ok = ok and caught
    sys.exit(0 if ok else 1)
