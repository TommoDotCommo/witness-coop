#!/usr/bin/env python3
"""Builds the Vortex extension zip (files at the zip root, as Vortex and Nexus expect) from modmanagers/vortex/game-thewitness.
  build_extension.py <output folder>      writes <folder>/game-thewitness-<info.json version>.zip and prints its path
The folder is also the upstream layout for Nexus-Mods/vortex-games (game-thewitness/index.js, info.json, gameart.jpg)."""
import json
from pathlib import Path
import sys
import zipfile

src = Path(__file__).resolve().parent / 'game-thewitness'
if len(sys.argv) != 2:
    sys.exit(__doc__)
version = json.loads((src / 'info.json').read_text())['version']
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
target = out / ('game-thewitness-%s.zip' % version)
with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED) as z:
    for name in ['index.js', 'info.json', 'gameart.jpg']:
        info = zipfile.ZipInfo(name, (2026, 11, 3, 0, 0, 0))     # fixed time: the same zip every build
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, (src / name).read_bytes())
print(target)
