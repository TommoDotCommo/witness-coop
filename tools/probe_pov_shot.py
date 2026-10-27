#!/usr/bin/env python3
"""Probe (not a test): A looks at B, presses V, and the game's own picture of A's screen is saved."""
import math, os, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, DIR2, RUN, pid, win, pyw
import qa_sweep
qa_sweep.restart('name=Alpha', 'name=Beta')
pyw('wcmd.py', win(DIR1), 'menusel 64'); pyw('wcmd.py', win(DIR1), 'menudo teleport'); time.sleep(4)
out = pyw('wstatedump.py', pid('A'))   # (just to keep A's state read once)
cam = subprocess.run([os.environ.get('PYW', 'python.exe'), '-I', '-c', "import sys,os;exec(compile(open(r'%s').read(),'wlib','exec'));p=attach(%s);print(*p.camera()[0],*p.camera()[1])" % (win(os.path.join(TOOLS, 'win', 'wlib.py')), pid('A'))], capture_output=True, text=True).stdout.split()
cx, cy, cz, th = float(cam[0]), float(cam[1]), float(cam[2]), float(cam[3])
pyw('wcmd.py', win(DIR2), 'tp %.4f %.4f %.4f %.5f 0' % (cx + math.cos(th) * 1.6, cy + math.sin(th) * 1.6, cz - 1.69, th)); time.sleep(3)
pyw('wcmd.py', win(DIR1), 'povstat'); pyw('wcmd.py', win(DIR1), 'key V down'); pyw('wcmd.py', win(DIR1), 'key V up'); time.sleep(6); pyw('wcmd.py', win(DIR1), 'povstat'); print(''.join(l for l in open(os.path.join(DIR1, 'witness_proxy.log'), errors='ignore') if l.startswith('povstat')))
pyw('wcmd.py', win(DIR1), 'shot povview'); time.sleep(2)
subprocess.run(['python3', os.path.join(TOOLS, 'bmp2png.py'), os.path.join(DIR1, 'shots', 'povview.bmp'), os.path.join(RUN, 'povview.png'), '2']); print('wrote', os.path.join(RUN, 'povview.png'))
