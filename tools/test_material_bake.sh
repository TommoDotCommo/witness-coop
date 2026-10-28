#!/bin/bash
# Exercise the authored texture bake and a flat-material mutant at low resolution.
# The colour-only coat samples isolate material variation from baked AO.
set -euo pipefail
cd "$(dirname "$0")/.."
[ ! -f local/env.sh ] || . local/env.sh
blender="${BLENDER:-blender.exe}"
tmp=$(mktemp -d "${WITNESS_TMPDIR:-${TMPDIR:-/tmp}}/material-test.XXXXXXXX")
trap 'rm -rf "$tmp"' EXIT
run() {
  local name=$1; shift
  "$blender" -b --python-exit-code 1 -P "$(wslpath -w models/src/adventurer.py)" -- "$(wslpath -w "$tmp/$name.glb")" --texsize 512 "$@" > "$tmp/$name.log" 2>&1 || { cat "$tmp/$name.log" >&2; exit 1; }
}
run detailed
run flat --flat-materials
python3 - "$tmp/detailed.log" "$tmp/flat.log" <<'PY'
import re, sys
def measure(path, label):
    text = open(path, encoding='utf-8', errors='replace').read()
    m = re.search(r'REPORT coat ' + label + r' spread ([0-9.]+)', text)
    if not m: raise SystemExit('missing ' + label + ' measurement in ' + path)
    return float(m[1])
detail, flat = [measure(p, 'diffuse') for p in sys.argv[1:]]
print('coat colour-only spread: detail %.6f, flat mutant %.6f' % (detail, flat))
if detail < 0.015 or flat > 0.003: raise SystemExit('coat material variation check failed')
jpeg_detail, jpeg_flat = [measure(p, 'JPEG') for p in sys.argv[1:]]
print('coat encoded JPEG spread: detail %.6f, flat mutant %.6f' % (jpeg_detail, jpeg_flat))
PY
