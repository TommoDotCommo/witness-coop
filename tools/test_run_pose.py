#!/usr/bin/env python3
"""Check exported run articulation against bind pose, without Blender or the game."""
import math
import sys
from glbtools import read_glb
from check_model import read_acc

j, data = read_glb(sys.argv[1])
run = next(a for a in j['animations'] if a['name'] == 'run')
limits = {'thigh': (15, 55), 'shin': (15, 100), 'upper_arm': (15, 55)}
seen = set()
failed = False
for ch in run['channels']:
    node = j['nodes'][ch['target']['node']]
    name = node.get('name', '')
    part = name.split('.')[0]
    if part not in limits or ch['target']['path'] != 'rotation':
        continue
    rest = node.get('rotation', [0, 0, 0, 1])
    keys = read_acc(j, data, run['samplers'][ch['sampler']]['output'])
    def angle(q):
        dot = abs(sum(x*y for x, y in zip(q, rest))) / math.sqrt(sum(x*x for x in q) * sum(x*x for x in rest))
        return math.degrees(2 * math.acos(min(1, dot)))
    maximum = max(map(angle, keys)); lo, hi = limits[part]
    good = lo <= maximum <= hi
    print('%s: %.1f degrees (%s)' % (name, maximum, 'ok' if good else 'FAIL'))
    failed |= not good
    seen.add(name)
expected = {p + '.' + side for p in limits for side in ('L', 'R')}
if seen != expected:
    print('missing run channels:', sorted(expected - seen)); failed = True
sys.exit(failed)
