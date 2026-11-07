#!/usr/bin/env python3
"""Game-free test of tools/oracle_recorded_eval.py: a made-up map with a floor, a ground-less oracle cell and a gap; the eye/feet offset is the part a
mutant breaks."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, os.path.join(HERE, 'win')); sys.path.insert(0, HERE)
import wnav, oracle_recorded_eval as ev

def make():
    m = wnav.Map(); c = wnav.cell_of(10.2, 10.2)
    m.cells[c] = [5.0, 1, 0]                                     # one floor at 5.0 m (mask bit 0 = the floor itself)
    m.cells[wnav.cell_of(20.2, 20.2)] = [-100.0, 1, wnav.ORACLE]      # the engine said no ground
    return m

def run():
    m = make(); bad = 0
    cases = [((10.2, 10.2, 5.0 + ev.EYE), 'ok'), ((10.2, 10.2, 5.0), 'other level'), ((20.2, 20.2, 5.0 + ev.EYE), 'no ground'), ((30.2, 30.2, 5.0), 'absent')]
    for p, want in cases:
        got = ev.classify(m, p)
        if got != want: print('FAILED', p, 'wanted', want, 'got', got); bad += 1
        else: print('ok    ', p, want)
    # mutant: the recorded height taken as the feet (no eye offset) must be caught by the first case
    saved = ev.EYE; ev.EYE = 0.0
    caught = ev.classify(m, (10.2, 10.2, 5.0 + saved)) != 'ok'
    ev.EYE = saved
    print('mutant eye offset 0:', 'caught' if caught else 'NOT CAUGHT'); bad += (not caught)
    return bad

if __name__ == '__main__':
    sys.exit(1 if run() else 0)
