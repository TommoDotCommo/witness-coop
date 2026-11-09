#!/usr/bin/env python3
"""T2 scenario for shared lines: a bot solves the maze by drawing with the mouse in game A while game B stands in front of it; B must hold A's finished
line (its own proxy counts it) and have pixels of it on screen (a GPU occlusion query: the lines that survive the depth test), game A must hold none (no
echo of its own lines). Then B goes away and comes back: the game's new picture of the panel has the line from its trace, so B's overlay holds none and
B's trace has the line. Mutants: lines never sent (B holds none), lines not drawn (B has no pixels), the finished line left to the game on a panel in
view (line_trust_game=1: B holds none while watching, the bug of 2026-11-07 to 11-10). usage: qa_lines.py (called by tools/qa.sh lines)"""
import json, os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, DIR2, RUN, pid, pyw, win
import qa_sweep, sweep

PANELS = [0x293]      # the maze: powered from the start and the most telling line; the two tutorial panels before it only cost time

def linestat(gdir):
    log = os.path.join(gdir, 'witness_proxy.log')
    n0 = sum(1 for l in open(log, errors='ignore') if l.startswith('linestat:'))
    pyw('wcmd.py', win(gdir), 'linestat'); time.sleep(0.6)
    rows = [l for l in open(log, errors='ignore') if l.startswith('linestat:')]
    if len(rows) <= n0: return None
    m = re.search(r'lines (\d+) verts (\d+) visible_px (\d+)', rows[-1]); return tuple(int(x) for x in m.groups()) if m else None

def scenario(cfg_a='', cfg_b=''):
    manifest = {q['id']: q for q in sweep.load(sweep.MANIFEST, [])}
    if not manifest: sys.exit('no manifest: run tools/sweep.py inventory on a fresh game first')
    # Follow native trace progress: the maze's projected first edge can stall.
    os.environ['WDRAW_STEER'] = '1'; os.environ['WSLENV'] = os.environ.get('WSLENV', '') + ':WDRAW_STEER'
    qa_sweep.restart(cfg_a, cfg_b)
    # B watches the maze while A solves it: the game's picture of B's panel exists before the line is written into its trace, and the game does not
    # build it again for that line, so B's overlay must draw A's finished line (owner's playtest 2026-11-10: B saw the panel solved with no line)
    pyw('wcmd.py', win(DIR2), 'menusel 293'); pyw('wcmd.py', win(DIR2), 'menudo teleport'); time.sleep(3)
    qa_sweep.run_panels('A', PANELS, manifest); time.sleep(2)
    t0 = time.time(); b = linestat(DIR2)
    while not clean_ok(b, (0, 0, 0)) and time.time() - t0 < 25: time.sleep(0.5); b = linestat(DIR2)      # the line arrives, the ribbon is drawn: a few frames
    a = linestat(DIR1)
    print('  B, watching, holds %s (lines, verts, visible px); A holds %s' % (b, a), flush=True)
    # B goes away and comes back: the game builds a new picture of the panel, with the line in its trace, and the overlay leaves the line to it
    pyw('wcmd.py', win(DIR2), 'menusel 64'); pyw('wcmd.py', win(DIR2), 'menudo teleport'); time.sleep(4)
    pyw('wcmd.py', win(DIR2), 'menusel 293'); pyw('wcmd.py', win(DIR2), 'menudo teleport'); time.sleep(4)
    back = linestat(DIR2); st = json.loads(pyw('wlinestate.py', str(pid('B')), '293').strip().splitlines()[-1])
    print('  B, back at the panel, holds %s; its trace has %s edges' % (back, st.get('traced')), flush=True)
    return b, a, back, st.get('traced', 0)

def clean_ok(b, a, back=(0, 0, 0), traced=1):
    return bool(b) and b[0] >= 1 and b[2] > 500 and bool(a) and a[0] == 0 and bool(back) and back[0] == 0 and traced > 0

def main():
    results = []
    print('=== lines: clean build', flush=True); r = scenario(); ok = clean_ok(*r); results.append(('clean', ok)); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    for name, ca, cb in (('no_line_send (lines never sent)', 'no_line_send=1', ''), ('no_line_draw (lines not drawn)', '', 'no_line_draw=1'),
                         ('line_trust_game (a finished line left to the game on a panel in view)', '', 'line_trust_game=1')):
        print('=== lines: MUTANT %s' % name, flush=True); r = scenario(ca, cb); caught = not clean_ok(*r); results.append((name, caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
