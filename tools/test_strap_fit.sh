#!/bin/bash
# Build the actual Blender geometry without the expensive texture bake. A flat,
# unprojected strap is the mutant; the independent mesh-clearance check must reject it.
set -euo pipefail
cd "$(dirname "$0")/.."
[ ! -f local/env.sh ] || . local/env.sh
blender="${BLENDER:-blender.exe}"
tmp=$(mktemp -d "${WITNESS_TMPDIR:-${TMPDIR:-/tmp}}/strap-test.XXXXXXXX")
trap 'rm -rf "$tmp"' EXIT
cp models/src/adventurer.py "$tmp/clean.py"
cp models/src/appearance.py "$tmp/appearance.py"
python3 - "$tmp/clean.py" "$tmp/mutant.py" <<'PY'
import sys
s = open(sys.argv[1]).read()
old = 'bm.verts.new((xx, strap_surface(xx, zz), zz))'
assert s.count(old) == 1
open(sys.argv[2], 'w').write(s.replace(old, 'bm.verts.new((xx, -0.12, zz))'))
PY
run() { "$blender" -b --python-exit-code 1 -P "$(wslpath -w "$tmp/$1.py")" -- "$(wslpath -w "$tmp/$1.glb")" --no-texture > "$tmp/$1.log" 2>&1; }
run clean || { cat "$tmp/clean.log" >&2; exit 1; }
if run mutant; then echo 'unprojected strap mutant unexpectedly passed' >&2; exit 1; fi
if ! grep -q 'satchel strap clearance outside tolerance' "$tmp/mutant.log"; then cat "$tmp/mutant.log" >&2; exit 1; fi
echo 'strap fit: clean geometry passed; unprojected mutant rejected'
