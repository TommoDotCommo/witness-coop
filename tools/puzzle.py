#!/usr/bin/env python3
"""Test one puzzle by hand or by bot. usage (games already running, see tools/restart.sh):
  puzzle.py list [text]            panels of the manifest with their size, rules and what the solver says (filter by text, id or 'unsolved')
  puzzle.py show ID                everything known about a panel and the line the solver would draw
  puzzle.py go ID [A|B]            teleport the player of that game in front of the panel with the dev menu (what a tester does before playing it)
  puzzle.py solve ID [A|B] [--start W,dy,AX,SX,SY]   teleport there and draw the solution with the mouse, then say what the game says; --start gives the bot
                                   where the start circle is when its own search cannot find it (panel size in metres, vertical shift in pixels,
                                   width factor, and -1 for a mirrored axis), as read from a screenshot
IDs are the hex entity ids the dev menu shows (F8, or `menusel`)."""
import json, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import DIR1, DIR2, RUN, pid, pyw, win
import solver, sweep

def panels():
    m = sweep.load(sweep.MANIFEST, None)
    if m is None: sys.exit('no manifest yet: start a fresh game and run  tools/sweep.py inventory')
    return {q['id']: q for q in m}

def describe(q):
    kinds = set()
    for v in q['dec']:
        if v: kinds.add({0x100: 'square', 0x200: 'star', 0x400: 'poly', 0x500: 'eraser', 0x600: 'triangle'}.get(v & 0xF00, 'rule'))
    if any(f & 0x20 for f in q['flags']): kinds.add('dots')
    if q.get('refl'): kinds.add('symmetry')
    if solver.stored(q): kinds.add('stored solution')
    if str(q['id']) in sweep.floor_panels(): kinds.add('floor puzzle: walk over the pressure plates')
    return '%dx%d nodes %s%s' % (q['gx'], q['gy'], ','.join(sorted(kinds)) or 'plain', '' if not solver.unsupported(q) else '  [solver: ' + ', '.join(solver.unsupported(q)) + ']')

def prepared(q):
    q['only_edges'] = sweep.floor_edges(q['id']); return q

def main(argv):
    if not argv: print(__doc__); return 0
    cmd = argv[0]; P = panels()
    if cmd == 'list':
        flt = argv[1].lower() if len(argv) > 1 else ''
        for i, q in sorted(P.items()):
            line = '%#7x  z %6.1f  %s' % (i, q['z'], describe(q))
            if not flt or flt in line.lower() or flt == '%x' % i: print(line)
        return 0
    which = argv[2] if len(argv) > 2 and not argv[2].startswith('--') else 'B'
    pid_hex = int(argv[1], 16) if len(argv) > 1 else None
    q = P.get(pid_hex)
    if q is not None: prepared(q)
    if q is None: sys.exit('no such panel %s' % argv[1:2])
    if cmd == 'show':
        print('panel %#x at %.1f %.1f %.1f  %s' % (pid_hex, q['x'], q['y'], q['z'], describe(q)))
        print('line the solver draws:', solver.solve(q)); return 0
    gdir = DIR2 if which == 'B' else DIR1
    if cmd == 'go':
        pyw('wcmd.py', win(gdir), 'menusel %x' % pid_hex); pyw('wcmd.py', win(gdir), 'menudo teleport'); print('teleporting game %s to panel %#x' % (which, pid_hex)); return 0
    if cmd == 'solve':
        if '--start' in argv: os.environ['WDRAW_FORCE'] = argv[argv.index('--start') + 1]; os.environ['WSLENV'] = os.environ.get('WSLENV', '') + ':WDRAW_FORCE'
        path = solver.solve(q)
        if path is None: print('no solution known for this panel (%s)' % describe(q)); return 2
        r = sweep.draw(q, path, which)      # floor puzzles (pressure plates) are walked, everything else drawn: sweep.draw chooses
        print(json.dumps({k: r.get(k) for k in ('solved', 'why')})); return 0 if r.get('solved') else 1
    print(__doc__); return 2

if __name__ == '__main__': sys.exit(main(sys.argv[1:]))
