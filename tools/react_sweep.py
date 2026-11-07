#!/usr/bin/env python3
"""Solve sweep (probe, not a pass/fail scenario): fresh pair with the tutorial done, then tools/win/wreact_sweep.py solves every panel with a
behaviour name the game's own way on A and reports what B did not do. Run it with slots: `tools/qa.sh probe tools/react_sweep.py [hexid ...]`.
Writes react_sweep.json in the run folder."""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, DIR2, PYW, RUN, pid, win
import qa_sweep
qa_sweep.restart('name=Alpha\nstart_progress=preset:tutorial', ('name=Beta\nstart_progress=preset:tutorial\n' + os.environ.get('SWEEP_CFG_B', '').replace(';', '\n')).strip())      # SWEEP_CFG_B: extra cfg lines for B (';' separated), e.g. a twin
out = os.path.join(RUN, 'react_sweep.json')
r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wreact_sweep.py')), str(pid('A')), win(DIR1), str(pid('B')), win(DIR2), win(out)] + sys.argv[1:], timeout=7200)
print('results:', out); sys.exit(r.returncode)
