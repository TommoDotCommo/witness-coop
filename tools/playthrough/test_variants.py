#!/usr/bin/env python3
"""Game-free test of solver.variants (a panel with two exits gives one solution per exit) with a mutant. usage: python3 test_variants.py"""
import os, subprocess, sys, tempfile, shutil
TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CHECK = r'''
import sys; sys.path.insert(0, D); import solver
#   0(start) - 1 - 2(exit A)          a line graph with a branch: from node 1 the line can go on to 2 or turn to 3
#              |
#              3(exit B)
p = dict(nodes=4, flags=[0x2, 0, 0x1, 0x1], ca=[0, 1, 1], cb=[1, 2, 3], pos=[0, 0, 1, 0, 2, 0, 1, 1], dec=[], decf=[], deccol=[], seq=[], gx=0, gy=0, ndec=0, mirror=0, style=0)
v = solver.variants(p)
ends = sorted(x[-1] for x in v)
if ends != [2, 3]: print('FAIL two exits must give two solutions, got', v); sys.exit(1)
one = dict(p, flags=[0x2, 0, 0x1, 0])
if len(solver.variants(one)) != 1: print('FAIL one exit, one solution'); sys.exit(1)
print('ok')
'''
def run(d): return subprocess.run([sys.executable, '-c', 'D=%r\n' % d + CHECK], capture_output=True, text=True).returncode == 0
ok = run(TOOLS); print('clean:', 'passed' if ok else 'FAILED')
with tempfile.TemporaryDirectory() as d:
    s = open(os.path.join(TOOLS, 'solver.py')).read(); old = "f & ~END if (f & END and i != e_) else f"
    assert s.count(old) == 1; open(os.path.join(d, 'solver.py'), 'w').write(s.replace(old, "f"))
    caught = not run(d); print('mutant (other exits not masked):', 'rejected' if caught else 'SURVIVED')
sys.exit(0 if ok and caught else 1)
