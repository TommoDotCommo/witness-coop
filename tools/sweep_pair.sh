#!/bin/bash
# Run the sweep on a pair of games that are already started under a resource provider owner: tools/sweep_pair.sh <owner> <shard of A> <shard of B> <of N> [log folder]
# (WITNESS_RESOURCE_OWNER picks the lease, hence the game folders and the run folder; the manifest is copied from the first run folder if the owner has none.)
owner="$1"; sa="$2"; sb="$3"; n="$4"; logs="${5:-/tmp}"
export WITNESS_RESOURCE_OWNER="$owner"
unset WITNESS_DIR1 WITNESS_DIR2 WITNESS_PORT1 WITNESS_PORT2 WITNESS_RUN
. "$(dirname "$0")/config.sh" || exit $?
export WITNESS_DIR1 WITNESS_DIR2 WITNESS_RUN="$RUN"
echo bot > "$RUN/guard.stop"
[ -f "$RUN/panels.json" ] || { echo "provide this run's panels.json before sweeping" >&2; exit 2; }
nohup python3 -u "$TOOLS/sweep.py" run --game A --shard "$sa/$n" > "$logs/sweep_${owner}_A.log" 2>&1 &
nohup python3 -u "$TOOLS/sweep.py" run --game B --shard "$sb/$n" > "$logs/sweep_${owner}_B.log" 2>&1 &
echo "sweeps started for $owner: A shard $sa/$n, B shard $sb/$n, logs in $logs"
