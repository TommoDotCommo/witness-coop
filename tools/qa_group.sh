#!/bin/bash
# Real-client full-mesh test, isolated in this worktree's lab lease. Default: 8 clients.
set -euo pipefail
cd "$(dirname "$0")/.."
LAB="${LAB:-$HOME/source/coop-lab/lab}"
n="${1:-8}"
[[ "$n" =~ ^[2-8]$ ]] || { echo 'usage: tools/qa_group.sh [2..8]' >&2; exit 2; }
"$LAB" status
ps -C claude -o pid,etime,comm || true
ps -C python3 -o pid,etime,args | rg 'sweep.py|suite.py' || true
# Keep the shared 3 GB reserve plus the measured ~1.8 GB for each new Witness process.
# Large groups must not consume a transient RAM spike while another sweep is restarting its clients.
# The real-game suite can close and relaunch several games without releasing its leases.
if [ "$n" -ge 3 ] && ps -C python3 -o args= | rg '[[:space:]/](sweep|suite)\.py([[:space:]]|$)' >/dev/null; then
  echo 'large group QA deferred: another live sweep/suite may be restarting game clients' >&2
  exit 3
fi
status=$("$LAB" status)
python3 -c 'import re,sys; free=float(re.search(r"RAM free ([0-9.]+)",sys.argv[1])[1]); need=3+1.8*int(sys.argv[2]); print("RAM preflight: %.1f GB free, %.1f GB needed"%(free,need)); sys.exit(0 if free>=need else 3)' "$status" "$n"
"$LAB" acquire --slots "$n" --ttl 3600 --wait 0
trap '"$LAB" release' EXIT
"$LAB" provision witness
eval "$("$LAB" env witness)"
make -C proxy
python3 tools/group_suite.py "$n"
