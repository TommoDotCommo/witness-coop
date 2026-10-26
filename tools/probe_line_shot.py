#!/usr/bin/env python3
"""Probe (not a test): B looks at panel 0x64, A solves it with the bot, B takes the game's own pictures before the solve and after the door has opened."""
import os, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, DIR2, PYW, RUN, pid, win, pyw
import qa_sweep, sweep, solver
manifest = {q['id']: q for q in sweep.load(sweep.MANIFEST, [])}
q = manifest[0x64]; path = solver.solve(q)
qa_sweep.restart('name=Alpha', 'name=Beta')
pyw('wcmd.py', win(DIR2), 'menusel 64'); pyw('wcmd.py', win(DIR2), 'menudo teleport'); time.sleep(4)
pyw('wcmd.py', win(DIR2), 'shot before'); time.sleep(1.5)
import threading
r = sweep.draw(q, path, 'A'); print('draw:', r, flush=True); pyw('wcmd.py', win(DIR2), 'linedump')
for name, wait in (('solved', 1.0), ('door', 8.0)):
    time.sleep(wait); pyw('wcmd.py', win(DIR2), 'shot ' + name); pyw('wcmd.py', win(DIR2), 'linedump'); time.sleep(1.5)
for name in ('before', 'solved', 'door'):
    src = os.path.join(DIR2, 'shots', name + '.bmp'); dst = os.path.join(RUN, 'line_' + name + '.png')
    subprocess.run(['python3', os.path.join(TOOLS, 'bmp2png.py'), src, dst, '2'], check=False); print('wrote', dst)

print(''.join(l for l in open(os.path.join(DIR2, 'witness_proxy.log'), errors='ignore') if l.startswith('linedump')))
