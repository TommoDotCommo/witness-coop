#!/bin/bash
# One real game + seven tiny protocol clients; clean and shared-avatar mutant.
set -euo pipefail
cd "$(dirname "$0")/.."
LAB="${LAB:-$HOME/source/coop-lab/lab}"
n=1
"$LAB" status
ps -C claude -o pid,etime,comm || true
ps -C python3 -o pid,etime,args | rg 'sweep.py|suite.py' || true
# Keep the shared 3 GB reserve plus the measured ~1.8 GB for each new Witness process.
status=$("$LAB" status)
python3 -c 'import re,sys; free=float(re.search(r"RAM free ([0-9.]+)",sys.argv[1])[1]); need=3+1.8*int(sys.argv[2]); print("RAM preflight: %.1f GB free, %.1f GB needed"%(free,need)); sys.exit(0 if free>=need else 3)' "$status" "$n"
"$LAB" acquire --slots "$n" --ttl 3600 --wait 0
trap '"$LAB" release' EXIT
"$LAB" provision witness
eval "$("$LAB" env witness)"
make -C proxy
make -C proxy/core build/group-transport.exe
python3 tools/group_render_suite.py
