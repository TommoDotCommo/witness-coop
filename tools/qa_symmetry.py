#!/usr/bin/env python3
"""T2 scenario for the second line of a symmetry panel: a bot solves Symmetry Island Right 1 (panel 0x22: a mirrored pair, the game draws the line and its reflection)
by drawing with the mouse in one game; the other game, standing in front of the panel, must draw BOTH lines. The game keeps one traced path per panel and a table of mirror nodes,
so the proof is made on the watching game's own counters: with the reflection on, its proxy reports the line as mirrored (`linestat` ... mirrored 1), about twice the ribbon
vertices and about twice the visible pixels (GPU occlusion query) as for the same line with the reflection switched off (`linemirror 0`, the one-path way before this change).
Both directions are run (A draws and B watches, then B draws and A watches). Mutant: the watcher drops the reflection (no_line_mirror=1): it must fail the same checks.
usage: qa_symmetry.py [A|B]   (called by tools/qa.sh symmetry; the argument runs only that drawer, no mutant)"""
import os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import DIR1, DIR2, RUN, pyw, win
import qa_sweep, sweep, solver

PANEL = 0x22      # Symmetry Island Right 1: powered from the start, 20 nodes, the nodes flip about the middle row
TRIES = 8         # a mirrored panel is drawn less reliably than a plain one (the bot retries; a try that did not solve leaves nothing behind)

def linestat(gdir):
    log = os.path.join(gdir, 'witness_proxy.log')
    n0 = sum(1 for l in open(log, errors='ignore') if l.startswith('linestat:'))
    pyw('wcmd.py', win(gdir), 'linestat'); time.sleep(0.6)
    rows = [l for l in open(log, errors='ignore') if l.startswith('linestat:')]
    if len(rows) <= n0: return None
    m = re.search(r'lines (\d+) verts (\d+) visible_px (\d+) frames \d+ mirrored (\d+)', rows[-1]); return tuple(int(x) for x in m.groups()) if m else None

def settle(gdir, want_px=500, timeout=25):
    t0 = time.time(); s = linestat(gdir)
    while not (s and s[0] >= 1 and s[2] > want_px) and time.time() - t0 < timeout: time.sleep(0.5); s = linestat(gdir)
    time.sleep(1.5); return linestat(gdir)

def scenario(drawer, cfg_a='', cfg_b=''):
    watcher, wdir = ('B', DIR2) if drawer == 'A' else ('A', DIR1)
    manifest = {q['id']: q for q in sweep.load(sweep.MANIFEST, [])}
    if not manifest: sys.exit('no manifest: run tools/sweep.py inventory on a fresh game first')
    qa_sweep.restart(cfg_a, cfg_b); q = manifest[PANEL]; path = solver.solve(q)
    solved = False; mirror = q['mirror']
    for t in range(TRIES):      # (the click on the start circle sometimes lands on the other start, the reflection's: then the reflected path is the one to draw, it solves the panel as well)
        r = sweep.draw(q, path if t % 2 == 0 else [mirror[k] for k in path], drawer); print('  try %d: %s %s' % (t + 1, 'solved' if r.get('solved') else 'not solved', r.get('why', '')), flush=True)
        if r.get('solved'): solved = True; break
    if not solved: return None
    pyw('wcmd.py', win(wdir), 'menusel %x' % PANEL); pyw('wcmd.py', win(wdir), 'menudo teleport'); time.sleep(3)
    on = settle(wdir)
    pyw('wcmd.py', win(wdir), 'linemirror 0'); time.sleep(1.0); off = settle(wdir, want_px=0, timeout=5)
    pyw('wcmd.py', win(wdir), 'linemirror 1')
    print('  %s holds (lines, verts, visible px, mirrored): with the reflection %s, without it %s' % (watcher, on, off), flush=True)
    return on, off

def both_lines(r):
    """Two lines drawn: mirrored, about twice the vertices, about twice the visible pixels."""
    if not r: return False
    on, off = r
    return bool(on and off) and on[0] >= 1 and on[3] >= 1 and off[3] == 0 and off[1] > 0 and 1.8 <= on[1] / off[1] <= 2.2 and off[2] > 100 and on[2] >= 1.5 * off[2]

def main():
    only = sys.argv[1] if len(sys.argv) > 1 else None
    results = []
    for drawer in ([only] if only else ['A', 'B']):
        print('=== symmetry: clean build, %s draws' % drawer, flush=True); r = scenario(drawer); ok = both_lines(r)
        results.append(('clean, %s draws' % drawer, ok)); print('clean: %s' % ('PASS' if ok else 'FAIL' + (' (the bot could not solve the panel)' if r is None else '')), flush=True)
    if not only:
        print('=== symmetry: MUTANT no_line_mirror (the watcher draws one path only)', flush=True); r = scenario('A', '', 'no_line_mirror=1'); caught = r is not None and not both_lines(r)
        results.append(('mutant no_line_mirror', caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT' + (' (the bot could not solve the panel)' if r is None else '')), flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
