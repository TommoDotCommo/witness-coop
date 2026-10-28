#!/bin/bash
# The eye aperture must follow the sculpted socket and leave readable whites.
# Mutants cover protruding, back-facing and buried geometry and an oversized iris.
set -euo pipefail
cd "$(dirname "$0")/.."
[ ! -f local/env.sh ] || . local/env.sh
blender="${BLENDER:-blender.exe}"
tmp=$(mktemp -d "${WITNESS_TMPDIR:-${TMPDIR:-/tmp}}/face-test.XXXXXXXX")
trap 'rm -rf "$tmp"' EXIT
run() { "$blender" -b --python-exit-code 1 -P "$(wslpath -w "models/src/adventurer.py")" -- "$(wslpath -w "$tmp/$1.glb")" --no-texture "$2" > "$tmp/$1.log" 2>&1; }
run clean '' || { cat "$tmp/clean.log" >&2; exit 1; }
if run mutant --eye-mutant; then echo 'protruding eye mutant unexpectedly passed' >&2; exit 1; fi
if ! grep -q 'eye white does not fit its socket' "$tmp/mutant.log"; then cat "$tmp/mutant.log" >&2; exit 1; fi
if run winding --eye-normal-mutant; then echo 'back-facing eye mutant unexpectedly passed' >&2; exit 1; fi
if ! grep -q 'eye white faces away from the camera' "$tmp/winding.log"; then cat "$tmp/winding.log" >&2; exit 1; fi
if run recessed --eye-recess-mutant; then echo 'buried eye mutant unexpectedly passed' >&2; exit 1; fi
if ! grep -q 'eye white is buried in its socket' "$tmp/recessed.log"; then cat "$tmp/recessed.log" >&2; exit 1; fi
if run covered --iris-mutant; then echo 'eye-covering iris mutant unexpectedly passed' >&2; exit 1; fi
if ! grep -q 'iris covers the eye white' "$tmp/covered.log"; then cat "$tmp/covered.log" >&2; exit 1; fi
echo 'face fit: clean geometry passed; protruding, back-facing, buried and covering eye mutants rejected'
