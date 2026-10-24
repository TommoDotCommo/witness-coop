#!/bin/bash
# The whole suite split over N parallel shards (default 3), each its own lab owner with its own two slots, ports, folders and run
# folder, so the shards cannot collide. Needs 2N free slots and about 4 GB of RAM per pair; a shard that finds no free slots exits 3
# (then rerun with fewer shards). Extra arguments are scenario name filters, as for qa.sh. Output: one log per shard under the run folder
# and a summary here. Exit 0 only when every shard passed.
. "$(dirname "$0")/config.sh"
N="${QA_SHARDS:-3}"; BASE="${LAB_OWNER:-$(basename "$ROOT")}"; LOGS="$(mktemp -d "${TMPDIR:-/tmp}/qa_par.XXXXXX")"
pids=()
for i in $(seq 1 "$N"); do
  ( LAB_OWNER="$BASE-s$i" "$TOOLS/qa.sh" --shard "$i/$N" "$@" > "$LOGS/shard$i.log" 2>&1; echo $? > "$LOGS/shard$i.rc" ) &
  pids+=($!); sleep 25       # stagger: provisioning and window placement of one shard finish before the next starts
done
wait "${pids[@]}"
rc=0
for i in $(seq 1 "$N"); do
  r=$(cat "$LOGS/shard$i.rc"); echo "shard $i/$N exit $r ($LOGS/shard$i.log)"; grep -E "^(MUTANT: )?.* (PASS|FAIL)$" "$LOGS/shard$i.log" | sed 's/^/   /'
  [ "$r" = 0 ] || rc=1
done
[ $rc = 0 ] && echo "ALL SHARDS OK" || echo "SOME SHARDS FAILED"
exit $rc
