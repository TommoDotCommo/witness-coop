#!/usr/bin/env python3
"""Probe (not a test): game A draws panel 0x64 with the bot while the trace records of that panel are sampled; prints how the last record changes during the draw."""
import os, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, DIR2, PYW, pid, win
import qa_sweep, sweep, solver
manifest = {q['id']: q for q in sweep.load(sweep.MANIFEST, [])}
q = manifest[0x64]; path = solver.solve(q); print('path', path, flush=True)
qa_sweep.restart('name=Alpha', 'name=Beta')
out = open(os.path.join(os.environ.get('WITNESS_RUN', '/tmp'), 'trace_probe.txt'), 'w')
pr = subprocess.Popen([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wtracewatch.py')), str(pid('A')), '64', '40'], stdout=out, stderr=subprocess.STDOUT, text=True)
time.sleep(1); r = sweep.draw(q, path, 'A'); print('draw:', r, flush=True); time.sleep(14); pr.wait(timeout=90); out.close()
print(open(out.name).read())
