#!/bin/bash
# The hat must sit on the head: the rim of its opening hugs the hair all the way round (models/src/adventurer.py measures it and refuses to build otherwise).
# Mutant: the earlier round opening at the earlier height, which floated above the head with a gap of several centimetres.
set -euo pipefail
cd "$(dirname "$0")/.."
[ ! -f local/env.sh ] || . local/env.sh
blender="${BLENDER:-blender.exe}"
tmp=$(mktemp -d "${WITNESS_TMPDIR:-${TMPDIR:-/tmp}}/hat-test.XXXXXXXX")
trap 'rm -rf "$tmp"' EXIT
run() { "$blender" -b --python-exit-code 1 -P "$(wslpath -w "models/src/adventurer.py")" -- "$(wslpath -w "$tmp/$1.glb")" --no-texture "$2" > "$tmp/$1.log" 2>&1; }
run clean '' || { cat "$tmp/clean.log" >&2; exit 1; }
grep 'REPORT hat fit' "$tmp/clean.log"
if run floating --hat-mutant; then echo 'floating hat mutant unexpectedly passed' >&2; exit 1; fi
if ! grep -q 'the hat does not fit the head' "$tmp/floating.log"; then cat "$tmp/floating.log" >&2; exit 1; fi
grep 'REPORT hat fit' "$tmp/floating.log"
echo 'hat fit: clean hat passed; floating hat mutant rejected'
