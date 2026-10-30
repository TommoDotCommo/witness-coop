#!/usr/bin/env python3
"""Game-free test of proplog.py on synthetic event logs: a fast solve, one slow because the receiver was re-gating after a suspected load,
one slow for no logged reason. Mutant: a reason() that never looks at the receiver's log must leave the re-gate unexplained.
usage: python3 test_proplog.py"""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import proplog as P

A = [(1000, 'state: world stable, 594 panels, 327 doors'), (20000, 'state: local panel 0x64 solved -> event'),
     (40000, 'state: local panel 0x182 solved -> event'), (60000, 'state: local panel 0x293 solved -> event')]
B = [(1100, 'state: world stable, 594 panels, 327 doors'), (20090, 'state: remote panel 0x64 applied as solved'),
     (38000, 'state: save reload suspected (known progress vanished), waiting for a stable world'),
     (50300, 'state: world stable, 594 panels, 327 doors'), (50310, 'state: remote panel 0x182 applied as solved'),
     (63000, 'state: remote panel 0x293 applied as solved')]
got = {p: (round(s, 2), why) for s, p, _, _, why in P.arrivals({'A': A, 'B': B})}
fails = 0
def check(ok, what):
    global fails
    print(('PASS  ' if ok else 'FAIL  ') + what); fails += not ok
check(got.get('0x64') == (0.09, ''), 'a fast solve: 0.09 s, nothing to explain')
check(got.get('0x182') == (10.31, 'receiver re-gating'), 'a solve held while the receiver re-gated after a suspected load')
check(got.get('0x293') == (3.0, ''), 'a slow solve with nothing in the logs stays unexplained')
P.reason = lambda lb, t, g: ''
check(P.arrivals({'A': A, 'B': B})[1][4] == '', 'mutant (receiver log ignored): the re-gate is no longer explained, so the check above would fail it')
print('%d failed' % fails); sys.exit(1 if fails else 0)
