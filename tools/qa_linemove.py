#!/usr/bin/env python3
"""T2 scenario for shared lines on a panel that moves: B looks at the first tutorial panel (it is part of a door), A draws its line with the bot. Seen in B's own game (`linedump`):
 1. while A holds the line half drawn, B has the tip half way along the edge, not at its end;
 2. once solved the line is the whole edge (0.41 m);
 3. after the door has swung (B's panel has moved more than 0.3 m) B holds exactly the same line in the panel's own frame, so it moved with the panel (the old way left it where the panel had been).
Mutants (A's cfg): the old anchoring (offsets from the panel's position now), the whole edge as the tip.  usage: qa_linemove.py (called by tools/qa.sh linemove)"""
import math, os, re, subprocess, sys, threading, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, DIR2, pyw, win
import qa_sweep, sweep, solver

def dump(gdir):
    log = os.path.join(gdir, 'witness_proxy.log'); n0 = sum(1 for l in open(log, errors='ignore') if l.startswith('linedump: ') and ' lines' in l)
    pyw('wcmd.py', win(gdir), 'linedump'); t = time.time()
    while time.time() - t < 6:
        ls = open(log, errors='ignore').read().splitlines(); ends = [i for i, l in enumerate(ls) if l.startswith('linedump: ') and l.endswith(' lines')]
        if len(ends) > n0:
            k = ends[-1] - 1; pts = []; head = None
            while k >= 0 and ls[k].startswith('linedump:'):
                m = re.match(r'linedump:   point (\d+) node (\d+) d_mm (-?\d+) (-?\d+) (-?\d+)', ls[k])
                if m: pts.insert(0, tuple(int(x) for x in m.groups()[2:]))
                h = re.match(r'linedump: from (\d+) panel (0x[0-9a-f]+) final (\d) points (\d+) panel_at (\S+) (\S+) (\S+)', ls[k])
                if h: head = dict(final=int(h[3]), at=tuple(float(h[i]) for i in (5, 6, 7)), n=int(h[4])); break
                k -= 1
            return head, pts
        time.sleep(0.1)
    return None, []
def length(pts): return math.dist(pts[0], pts[-1]) / 1000.0 if len(pts) >= 2 else 0.0

def scenario(cfg_a=''):
    manifest = {q['id']: q for q in sweep.load(sweep.MANIFEST, [])}
    if not manifest: sys.exit('no manifest: run tools/sweep.py inventory on a fresh game first')
    q = manifest[0x64]; path = solver.solve(q)
    qa_sweep.restart(('name=Alpha\n' + cfg_a).strip(), 'name=Beta')
    pyw('wcmd.py', win(DIR2), 'menusel 64'); pyw('wcmd.py', win(DIR2), 'menudo teleport'); time.sleep(4)
    held = os.path.join(DIR1, 'wdraw_held.txt')
    if os.path.exists(held): os.remove(held)
    os.environ['WDRAW_HOLD_FRACTION'] = '0.5'; os.environ['WDRAW_HOLD_SECS'] = '10'; os.environ['WSLENV'] = os.environ.get('WSLENV', '') + ':WDRAW_HOLD_FRACTION:WDRAW_HOLD_SECS'      # (only what WSLENV names reaches the Windows side)
    res = {}; th = threading.Thread(target=lambda: res.update(sweep.draw(q, path, 'A'))); th.start()
    t0 = time.time()
    while th.is_alive() and not os.path.exists(held) and time.time() - t0 < 200: time.sleep(0.3)
    time.sleep(1.2); h1, p1 = dump(DIR2) if os.path.exists(held) else (None, [])
    th.join(240); del os.environ['WDRAW_HOLD_FRACTION']; del os.environ['WDRAW_HOLD_SECS']
    h2, p2 = dump(DIR2)      # straight after the solve: B's door has not swung yet
    time.sleep(9.0); h3, p3 = dump(DIR2)
    return res, (h1, p1), (h2, p2), (h3, p3)

def evaluate(r):
    res, (h1, p1), (h2, p2), (h3, p3) = r; ok = True
    def check(name, cond, detail=''):
        nonlocal ok; ok = ok and bool(cond); print('  %-104s %s  (%s)' % (name, 'ok' if cond else 'FAILED', str(detail)[:150]), flush=True)
    check('the bot solved the panel in A', res.get('solved'), res.get('why'))
    l1, l2, l3 = length(p1), length(p2), length(p3)
    check('1. A holds the line half drawn: B has it, not finished, its tip about half way (0.12 to 0.29 m of the 0.41 m edge)', h1 and h1['final'] == 0 and 0.12 < l1 < 0.29, (h1 and h1['final'], round(l1, 3)))
    check('2. once solved B has the whole edge (0.37 to 0.45 m), final', h2 and h2['final'] == 1 and 0.37 < l2 < 0.45, (h2 and h2['final'], round(l2, 3)))
    moved = h2 and h3 and math.dist(h2['at'], h3['at'])
    check('3. the door has swung: B\'s panel is more than 0.3 m from where it was', moved and moved > 0.3, moved and round(moved, 3))
    same = p2 and p3 and len(p2) == len(p3) and all(abs(a - b) <= 3 for x, y in zip(p2, p3) for a, b in zip(x, y))
    check('   and B holds the same line in the panel\'s own frame as before (it moves with the panel)', same, (p2[:1], p3[:1]))
    return ok

def main():
    results = []
    print('=== linemove: clean build', flush=True); ok = evaluate(scenario()); results.append(('clean', ok)); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    if 'clean' in sys.argv[1:]: return 0 if results[0][1] else 1
    for name, cfg in (('old anchoring (offsets from the panel now)', 'line_old_anchor=1'), ('the whole edge as the tip', 'line_whole_edge=1')):
        print('=== linemove: MUTANT %s' % name, flush=True); caught = not evaluate(scenario(cfg)); results.append((name, caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
