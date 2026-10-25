#!/usr/bin/env python3
"""T2 scenario for the map picture (proxy/mapgen.cpp, tools/win/wmap.py): one game flies over the island looking down and stitches its own frames into the underlay of the dev
menu's map. Checks come from the log's numbers and from the files themselves: size, coverage, picture content, how well neighbouring tiles agree in their overlap (a wrong projection
or a mirrored one breaks that), nothing of the mod drawn into the frames, the player back where it was, and the map page drawing the picture.
Mutants: the projection has a wrong field of view (mapgen_bad_scale), the picture is mirrored (mapgen_mirror), the mod keeps drawing (mapgen_show_overlay), the map shows no picture (no_underlay).
usage: qa_map.py (called by tools/qa.sh map)"""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, PYW, pid, win
import qa_sweep

def run(cfg=''):
    qa_sweep.restart(('name=Alpha\nmapgen_settle_ms=1200\n' + cfg).strip(), '', which='A')
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wmap.py')), str(pid('A')), win(DIR1)], capture_output=True, text=True, timeout=1800)
    print(r.stdout, end=''); return r.returncode == 0 and 'ALL OK' in r.stdout

def main():
    results = []
    print('=== map: clean build', flush=True); ok = run(); results.append(('clean', ok)); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    for name, c in (('mapgen_bad_scale (wrong field of view in the projection)', 'mapgen_bad_scale=1'), ('mapgen_mirror (mirrored picture)', 'mapgen_mirror=1'), ('mapgen_show_overlay (the mod draws into the frames)', 'mapgen_show_overlay=1'), ('no_underlay (no picture under the map)', 'no_underlay=1')):
        print('=== map: MUTANT %s' % name, flush=True); caught = not run(c); results.append((name, caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
