#!/bin/bash
# Play the game through with N bots in one co-op world (tools/playthrough/run.py has the details). Leases N slots without waiting
# (exit 3 = busy or not enough RAM), provisions them, runs, and always releases them.
# usage: tools/playthrough.sh [N] [--goal credits|all] [--minutes M] [--fps F] [--drop PCT]
set -euo pipefail
cd "$(dirname "$0")/.."
[ ! -f local/env.sh ] || . local/env.sh
WITNESS_ALLOCATING=1 . tools/config.sh || exit $?
: "${WITNESS_RESOURCES:?configure an external resource provider in local/env.sh}"
n="${1:-2}"; [[ "$n" =~ ^[0-9]+$ ]] && shift || n=2
[ "$n" -ge 1 ] && [ "$n" -le 16 ] || { echo 'usage: tools/playthrough.sh [1..16] [options]' >&2; exit 2; }
status=$("$WITNESS_RESOURCES" status)
# The game's video options (run.py VIDEO). RAM per game, working set after a mid-game save loaded (measured 2026-10-30, one bot drawing):
#   high (the game's automatic profile without the file: 4x MSAA, all qualities high)  3.6-3.8 GB
#   lowmem (profile low, no MSAA, low textures and reflections)                         3.3 GB (peak 3.5)
#   lowest (lowmem with low shading and geometry detail too)                            1.9 GB (peak 1.9): the bots walk and draw as before
# WITNESS_GAME_GB overrides the number.
export WITNESS_VIDEO="${WITNESS_VIDEO-lowest}"
case "$WITNESS_VIDEO" in lowest) per=2.2 ;; lowmem) per=3.5 ;; *) per=3.8 ;; esac
per="${WITNESS_GAME_GB:-$per}"
python3 -c 'import re,sys; free=float(re.search(r"RAM free ([0-9.]+)",sys.argv[1])[1]); need=3+float(sys.argv[3])*int(sys.argv[2]); print("RAM preflight: %.1f GB free, %.1f GB needed"%(free,need)); sys.exit(0 if free>=need else 3)' "$status" "$n" "$per"
"$WITNESS_RESOURCES" acquire --slots "$n" --ttl 14400 --wait 0
trap '"$WITNESS_RESOURCES" release' EXIT
"$WITNESS_RESOURCES" provision witness >/dev/null
eval "$("$WITNESS_RESOURCES" env witness)"
make -C proxy >/dev/null
python3 tools/playthrough/run.py "$n" "$@"
