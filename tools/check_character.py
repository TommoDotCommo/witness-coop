#!/usr/bin/env python3
"""The character gate: measures a built adventurer (tools/char_measure.cpp: proportions, the eyes as seen from the front, a
blink through the proxy's life layer, symmetry, budgets) and holds it to models/src/character_spec.py TARGETS.
usage: check_character.py <model.glb> [--json]      exit 0 when every target is met; each miss is printed with its number.
docs/CHARACTER.md explains each target."""
from pathlib import Path
import json, os, subprocess, sys, tempfile
root = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(root / 'models/src'))
from character_spec import TARGETS

def measure(glb):
    with tempfile.TemporaryDirectory(prefix='char-measure-') as tmp:
        exe = Path(tmp) / 'measure'
        flags = os.environ.get('CHAR_MEASURE_FLAGS', '').split()      # a code mutant for tools/test_character.sh, e.g. -DWM_MUT_NO_LID_TURN
        subprocess.run(['g++', '-std=c++17', '-O2', *flags, str(root / 'tools/char_measure.cpp'), str(root / 'proxy/model/model.cpp'), '-o', str(exe)], check=True)
        return json.loads(subprocess.run([str(exe), str(glb)], check=True, capture_output=True, text=True).stdout)

def ratios(d):
    hh = d['head_height']; o = d['open']; w = (o[0]['w'] + o[1]['w']) / 2
    r = dict(
        heads_tall=d['height'] / hh,
        eye_line=(d['eye_y'] - d['chin']) / hh,
        aperture_aspect=(o[0]['h'] / max(o[0]['w'], 1e-6) + o[1]['h'] / max(o[1]['w'], 1e-6)) / 2,
        eye_spacing=(o[0]['x0'] - o[1]['x1']) / max(w, 1e-6),
        iris_fraction=(o[0]['iris_w'] + o[1]['iris_w']) / 2 / max(w, 1e-6),
        nose_length=(d['eye_y'] - d['nose_tip_y']) / hh,
        face_symmetry_mm=d['sym99_mm'],
        hand_to_face=sum(d['hand_len']) / 2 / max(d['brow_y'] - d['chin'], 1e-6),
        head_tris=d['head_tris'],
        blink_left=d['closed'][0]['area_mm2'] / max(o[0]['area_mm2'], 1e-6),
        eye_white=(o[0].get('white_lum', 0) + o[1].get('white_lum', 0)) / 2,
        degenerate=d['degenerate'])
    return r

def check(glb):
    d = measure(glb); r = ratios(d); misses = []
    for name, (lo, hi) in TARGETS.items():
        if name not in r: continue
        v = r[name]; ok = lo <= v <= hi
        print('%-18s %10.3f   target %g..%g %s' % (name, v, lo, hi, '' if ok else '  <-- MISSES'))
        if not ok: misses.append(name)
    return d, r, misses

if __name__ == '__main__':
    glb = sys.argv[1]; d, r, misses = check(glb)
    if '--json' in sys.argv: print(json.dumps({'measured': d, 'ratios': r, 'misses': misses}))
    print('%s: %s' % (glb, 'meets the character spec' if not misses else 'misses ' + ', '.join(misses)))
    sys.exit(1 if misses else 0)
