#!/usr/bin/env python3
"""Compare generator and runtime palettes and material regions, without Blender."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('appearance', ROOT / 'models/src/appearance.py')
a = importlib.util.module_from_spec(spec); spec.loader.exec_module(a)
assert len(a.NAMES) == 8 and len(a.GROUPS) == 8 and len(a.TINTS) == 8
assert len({n for names in a.GROUPS for n in names}) == sum(map(len, a.GROUPS))
assert a.material_group('white') == a.material_group('eye') == a.material_group('brass') == 0
for i, name in enumerate(a.NAMES):
    for value in range(4):
        parsed = a.parse_appearance(f'{name}={value}')
        assert parsed[i] == value and sum(parsed) == value
    for value in ('-1','4','256','1junk','01','1 2'):
        try: a.parse_appearance(f'{name}={value}')
        except ValueError: pass
        else: raise AssertionError(value)
for bad in ('unknown=1', 'skin=1,skin=2', 'skin'):
    try: a.parse_appearance(bad)
    except ValueError: pass
    else: raise AssertionError(bad)
# This compiles the exact runtime function; duplicate data cannot silently drift.
cpp = '''#include "appearance.h"
#include <cstdio>
int main() { for (unsigned g = 0; g <= 9; ++g) for (unsigned c = 0; c < 4; ++c) {
    witness::Appearance a; a.choice.fill(c); float t[3]; witness::AppearanceTint(a,g,t);
    std::printf("%u %u %.8f %.8f %.8f\\n", g,c,t[0],t[1],t[2]);
} }'''
with tempfile.TemporaryDirectory(prefix='appearance-palette-') as tmp:
    p = Path(tmp); (p/'test.cpp').write_text(cpp)
    subprocess.run(['g++','-std=c++17','-I',str(ROOT/'proxy/core'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    for line in subprocess.check_output([str(p/'test')],text=True).splitlines():
        group, choice, *tint = line.split(); group, choice = int(group), int(choice)
        expected = a.TINTS[group-1][choice-1] if 1 <= group <= 8 and choice else (1,1,1)
        assert all(abs(float(x)-y) < 1e-6 for x,y in zip(tint,expected)), line
for group, materials in enumerate(a.GROUPS, 1):
    for material in materials:
        assert a.material_group(material) == group
        rgb = [.2,.3,.4]
        assert a.tint_colour(rgb, material, [0]*8) == rgb
        for c in range(1,4):
            choices = [0]*8; choices[group-1] = c
            got = a.tint_colour(rgb,material,choices)
            assert got != rgb and all(0 < x <= 1.04 for x in got)
            for other in range(1,9):
                if other != group:
                    for m in a.GROUPS[other-1]: assert a.tint_colour(rgb,m,choices) == rgb
print('appearance generator/runtime palettes: all 32 choices and independent material groups passed')
