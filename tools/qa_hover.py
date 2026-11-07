#!/usr/bin/env python3
"""T2 the teleport hold yields to the game's emergency reset. A player held inside the world's geometry is reset to the island's start by the
game; a hold that pulled it back made the game reset it again every frame (thousands of times; twice it crashed, 2026-11-07). Here A is held
under the Monastery floor for 8 s: the hold must let go after the game's reset: the player ends away from that spot and the resets stop.
Twin hover_fights_reset=1: the player stays pinned at the bad spot and the game keeps resetting it."""
import os, sys, time, glob, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import DIR1
import qa_sweep

def game_log():
    logs = sorted(glob.glob(os.path.join(DIR1, 'logs', 'log*.txt')), key=os.path.getmtime)
    return logs[-1] if logs else ''
def resets(path):
    try: return open(path, errors='replace').read().count('emergency reset')
    except OSError: return 0
def send(lines):
    c = os.path.join(DIR1, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(c) and time.time() - t < 8: time.sleep(0.05)
    open(c + '.tmp', 'w').write('\n'.join(lines) + '\n'); os.replace(c + '.tmp', c)

def run(cfg_a=''):
    qa_sweep.restart(('name=Alpha\nstart_progress=preset:tutorial\nhover_diag=1\n' + cfg_a).strip(), 'name=Beta\nstart_progress=preset:tutorial')
    path = game_log(); before = resets(path)
    proxy = os.path.join(DIR1, 'witness_proxy.log'); pos = os.path.getsize(proxy)
    send(['hover 16.400 -29.455 11.930 -0.2225 0.0'])      # feet at 10.24: inside the structure under the Monastery floor (15.14)
    time.sleep(8.0)
    n = resets(path) - before
    feet = where()
    off = math.hypot(feet[0] - 16.4, feet[1] + 29.455) if feet else -1
    time.sleep(3.0); later = resets(path) - before - n
    tail = open(proxy, 'rb').read()[pos:].decode('latin1')      # (a byte offset: the log is read as bytes)
    print('  resets in 8 s: %d, in the 3 s after: %d; the player is %.1f m from the held spot' % (n, later, off), flush=True)
    for l in [l for l in tail.splitlines() if 'hoverdiag' in l or 'hover off' in l][-2:]: print('   ', l, flush=True)
    send(['hover off'])
    return off > 5 and later == 0      # the game moved the player off the bad spot and it stays there; a hold that fights pins it there

def where():
    out = os.path.join(DIR1, 'witness_oracle_out.txt'); before = open(out).read() if os.path.exists(out) else ''
    send(['where']); t = time.time()
    while time.time() - t < 5:
        cur = open(out).read() if os.path.exists(out) else ''
        if cur != before and 'feet=' in cur:
            x, y, z = cur.split('feet=')[1].split()[0].split(','); return float(x), float(y), float(z)
        time.sleep(0.05)
    return None

def main():
    print('=== hover: clean', flush=True); ok = run(); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    print('=== hover: MUTANT hover_fights_reset (the hold pulls the player back after every reset)', flush=True)
    caught = not run('hover_fights_reset=1'); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    print('ALL OK' if ok and caught else 'FAILED'); return 0 if ok and caught else 1

if __name__ == '__main__': sys.exit(main())
