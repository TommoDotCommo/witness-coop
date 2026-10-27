#!/bin/bash
# Real-client full-mesh test, isolated in this worktree's resource provider lease. Default: 8 clients.
set -euo pipefail
cd "$(dirname "$0")/.."
[ ! -f local/env.sh ] || . local/env.sh
WITNESS_ALLOCATING=1 . tools/config.sh || exit $?
: "${WITNESS_RESOURCES:?configure an external resource provider in local/env.sh}"
n="${1:-8}"
[[ "$n" =~ ^[2-8]$ ]] || { echo 'usage: tools/qa_group.sh [2..8]' >&2; exit 2; }
"$WITNESS_RESOURCES" status
ps -C python3 -o pid,etime,args | rg 'sweep.py|suite.py' || true
# Keep the shared 3 GB reserve plus the measured ~1.8 GB for each new Witness process.
# Large groups must not consume a transient RAM spike while another sweep is restarting its clients.
# The real-game suite can close and relaunch several games without releasing its leases.
if [ "$n" -ge 3 ] && ps -C python3 -o args= | rg '[[:space:]/](sweep|suite)\.py([[:space:]]|$)' >/dev/null; then
  echo 'large group QA deferred: another live sweep/suite may be restarting game clients' >&2
  exit 3
fi
status=$("$WITNESS_RESOURCES" status)
python3 -c 'import re,sys; free=float(re.search(r"RAM free ([0-9.]+)",sys.argv[1])[1]); need=3+1.8*int(sys.argv[2]); print("RAM preflight: %.1f GB free, %.1f GB needed"%(free,need)); sys.exit(0 if free>=need else 3)' "$status" "$n"
"$WITNESS_RESOURCES" acquire --slots "$n" --ttl 3600 --wait 0
trap '"$WITNESS_RESOURCES" release' EXIT
"$WITNESS_RESOURCES" provision witness
resource_exports=$("$WITNESS_RESOURCES" env witness)
eval "$resource_exports"
make -C proxy
python3 tools/group_suite.py "$n"
