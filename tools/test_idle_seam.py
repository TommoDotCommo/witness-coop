#!/usr/bin/env python3
"""Check the idle loop's final frame does not snap a joint at the seam."""
import math
import sys
from glbtools import read_glb
from check_model import read_acc

j, data = read_glb(sys.argv[1])
idle = next(a for a in j['animations'] if a['name'] == 'idle')
failed = False
for channel in idle['channels']:
    if channel['target']['path'] != 'rotation':
        continue
    sampler = idle['samplers'][channel['sampler']]
    values = read_acc(j, data, sampler['output'])
    if len(values) < 2:
        continue
    a, b = values[-2:]
    dot = abs(sum(x * y for x, y in zip(a, b))) / math.sqrt(sum(x*x for x in a) * sum(x*x for x in b))
    degrees = math.degrees(2 * math.acos(min(1.0, dot)))
    if degrees > 0.5:
        print('%s: %.2f degrees at idle seam (limit 0.5)' % (j['nodes'][channel['target']['node']]['name'], degrees))
        failed = True
print('idle seam: ' + ('FAIL' if failed else 'ok'))
sys.exit(failed)
