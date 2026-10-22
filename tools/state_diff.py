#!/usr/bin/env python3
"""Shared-state assertion: solved panels and open doors must be identical in both games. Exit 1 on any difference."""
import json, sys
from witness import pyw, pid
def dump(which): return json.loads(pyw('wstatedump.py', pid(which)).strip().splitlines()[-1])
a, b = dump('A'), dump('B'); bad = 0
for kind in ('panels', 'doors'):
    sa, sb = set(a[kind]), set(b[kind])
    print('%-6s A solved/open: %3d  B: %3d  only-A: %s  only-B: %s' % (kind, len(sa), len(sb), sorted(sa - sb), sorted(sb - sa)))
    bad += len(sa ^ sb)
print('entities: A %d panels %d doors | B %d panels %d doors' % (a['n_panels'], a['n_doors'], b['n_panels'], b['n_doors']))
print('STATE DIFF:', 'IDENTICAL' if bad == 0 else '%d DIFFERENCES' % bad); sys.exit(1 if bad else 0)
