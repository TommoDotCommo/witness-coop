#!/usr/bin/env python3
"""Panel sweep: teleport to every puzzle on the island and solve it, on game A, checking game B follows. usage: sweep.py [inventory]"""
import json, os, sys
from witness import DIR1, DIR2, RUN, pid, pyw, win

def inventory(which='A', full=False):
    return json.loads(pyw('wpanels.py', pid(which), *(['--full'] if full else []), timeout=180).strip().splitlines()[-1])

if __name__ == '__main__':
    inv = inventory(full=True); json.dump(inv, open(os.path.join(RUN, 'panels.json'), 'w'))
    kinds = {}
    for p in inv: kinds.setdefault((p['starts'] > 0 and p['ends'] > 0, p['powered']), []).append(p['id'])
    print('%d panels' % len(inv), {k: len(v) for k, v in kinds.items()})
