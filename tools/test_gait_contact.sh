#!/bin/bash
# Test the exported glTF via the renderer's CPU skinning path, plus a mismatched-stride mutant.
# Heel/toe joint markers are a proxy for boot contact, not a full mesh or visual assessment.
set -euo pipefail
cd "$(dirname "$0")/.."
[ ! -f local/env.sh ] || . local/env.sh
blender="${BLENDER:-blender.exe}"
tmp=$(mktemp -d "${WITNESS_TMPDIR:-${TMPDIR:-/tmp}}/gait-check.XXXXXXXX")
trap 'rm -rf "$tmp"' EXIT
g++ -std=c++17 -O2 -o "$tmp/probe" tools/gait_probe.cpp proxy/model/model.cpp
build() {
  local name=$1; shift
  "$blender" -b --python-exit-code 1 -P "$(wslpath -w models/src/adventurer.py)" -- "$(wslpath -w "$tmp/$name.glb")" --no-texture "$@" > "$tmp/$name.log" 2>&1 || { cat "$tmp/$name.log" >&2; exit 1; }
  "$tmp/probe" "$tmp/$name.glb" 1.2 2.8 > "$tmp/$name.metrics"
}
build clean
build mutant --gait-mutant
python3 - "$tmp/clean.metrics" "$tmp/mutant.metrics" <<'PY'
import re, sys
def check(path):
    text = open(path).read()
    vals = {}
    for name, median, p90, count in re.findall(r'(walk|run): ground marker slip median ([0-9.]+) p90 ([0-9.]+) m/s \((\d+) samples\)', text):
        vals[name] = (float(p90), int(count))
    lows = {name: float(low) for name, low in re.findall(r'(walk|run): lowest marker ([-0-9.]+) m', text)}
    good = set(vals) == {'walk', 'run'} and set(lows) == {'walk', 'run'} and vals['walk'][1] >= 30 and vals['run'][1] >= 30 and vals['walk'][0] < 0.10 and vals['run'][0] < 1.0 and all(x >= -0.01 for x in lows.values())
    print(text.strip())
    return good
if not check(sys.argv[1]): raise SystemExit('clean gait contact check failed')
if check(sys.argv[2]): raise SystemExit('mismatched-stride mutant survived')
print('gait contact: clean passed; mismatched-stride mutant rejected')
PY
