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
python3 -c 'import re,sys; free=float(re.search(r"RAM free ([0-9.]+)",sys.argv[1])[1]); need=3+1.8*int(sys.argv[2]); print("RAM preflight: %.1f GB free, %.1f GB needed"%(free,need)); sys.exit(0 if free>=need else 3)' "$status" "$n"
"$WITNESS_RESOURCES" acquire --slots "$n" --ttl 14400 --wait 0
trap '"$WITNESS_RESOURCES" release' EXIT
"$WITNESS_RESOURCES" provision witness >/dev/null
eval "$("$WITNESS_RESOURCES" env witness)"
make -C proxy >/dev/null
python3 tools/playthrough/run.py "$n" "$@"
