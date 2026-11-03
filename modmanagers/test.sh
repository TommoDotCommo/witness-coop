#!/usr/bin/env bash
# T0 for the mod manager integrations: the Vortex extension (node, stub vortex-api) and the MO2 plugin (python, stub mobase), each with mutants that must fail.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
bad=0
node "$here/vortex/test_extension.js" || bad=1
for m in modpath exe steam tool installer; do
    node "$here/vortex/test_extension.js" --mutant "$m" >/dev/null 2>&1 && { echo "vortex mutant $m SURVIVED"; bad=1; }
done
python3 -I "$here/mo2/test_game_thewitness.py" || bad=1
for m in root exe steam launcher checker; do
    python3 -I "$here/mo2/test_game_thewitness.py" --mutant "$m" >/dev/null 2>&1 && { echo "mo2 mutant $m SURVIVED"; bad=1; }
done
[ "$bad" = 0 ] && echo "modmanagers: all passed (10 mutants rejected)"
exit "$bad"
