#!/usr/bin/env python3
"""T2 scenario for shared lines: a bot solves three panels by drawing with the mouse in game A; game B, standing in front of the maze, must hold A's three lines
(its own proxy counts them) and have pixels of them on screen (a GPU occlusion query: the lines that survive the depth test), game A must hold none (no echo of
its own lines). Mutants: lines never sent (B holds none) and lines not drawn (B has no pixels). usage: qa_lines.py (called by tools/qa.sh lines)"""
import os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, DIR2, RUN, pyw, win
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
    qa_sweep.restart(cfg_a, cfg_b); qa_sweep.run_panels('A', PANELS, manifest)
    pyw('wcmd.py', win(DIR2), 'menusel 293'); pyw('wcmd.py', win(DIR2), 'menudo teleport'); time.sleep(3)
    t0 = time.time(); b = linestat(DIR2)
    while not clean_ok(b, (0, 0, 0)) and time.time() - t0 < 25: time.sleep(0.5); b = linestat(DIR2)      # the line arrives, the ribbon is drawn: a few frames
    a = linestat(DIR1)
    print('  B holds %s (lines, verts, visible px); A holds %s' % (b, a), flush=True)
    return b, a

def clean_ok(b, a):
    return bool(b) and b[0] >= 1 and b[2] > 500 and bool(a) and a[0] == 0

def main():
    results = []
    print('=== lines: clean build', flush=True); b, a = scenario(); ok = clean_ok(b, a); results.append(('clean', ok)); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    for name, ca, cb in (('no_line_send (lines never sent)', 'no_line_send=1', ''), ('no_line_draw (lines not drawn)', '', 'no_line_draw=1')):
        print('=== lines: MUTANT %s' % name, flush=True); b, a = scenario(ca, cb); caught = not clean_ok(b, a); results.append((name, caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
