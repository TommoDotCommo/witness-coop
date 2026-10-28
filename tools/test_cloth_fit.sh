#!/bin/bash
# The scarf must remain outside the coat collar around their shared surface.
set -euo pipefail
cd "$(dirname "$0")/.."
[ ! -f local/env.sh ] || . local/env.sh
blender="${BLENDER:-blender.exe}"
tmp=$(mktemp -d "${WITNESS_TMPDIR:-${TMPDIR:-/tmp}}/cloth-test.XXXXXXXX")
trap 'rm -rf "$tmp"' EXIT
run() { "$blender" -b --python-exit-code 1 -P "$(wslpath -w "models/src/adventurer.py")" -- "$(wslpath -w "$tmp/$1.glb")" --no-texture "$2" > "$tmp/$1.log" 2>&1; }
run clean '' || { cat "$tmp/clean.log" >&2; exit 1; }
if run mutant --scarf-mutant; then echo 'intersecting scarf mutant unexpectedly passed' >&2; exit 1; fi
if ! grep -q 'scarf intersects the coat collar' "$tmp/mutant.log"; then cat "$tmp/mutant.log" >&2; exit 1; fi
echo 'cloth fit: clean geometry passed; intersecting scarf mutant rejected'
