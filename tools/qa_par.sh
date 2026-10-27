#!/bin/bash
# The whole suite split over N parallel shards (default 3), each its own resource provider owner with its own two slots, ports, folders and run
# folder, so the shards cannot collide. Needs 2N free slots and about 4 GB of RAM per pair; a shard that finds no free slots exits 3
# (then rerun with fewer shards). Extra arguments are scenario name filters, as for qa.sh. Output: one log per shard under the run folder
# and a summary here. Exit 0 only when every shard passed.
WITNESS_ALLOCATING=1 . "$(dirname "$0")/config.sh" || exit $?
N="${QA_SHARDS:-3}"; BASE="${WITNESS_RESOURCE_OWNER:-witness-$(python3 -c 'import uuid; print(uuid.uuid4().hex)')}"; LOGS="$(mktemp -d "${TMPDIR:-/tmp}/qa_par.XXXXXX")"
pids=()
for i in $(seq 1 "$N"); do
  ( unset WITNESS_SLOTS WITNESS_DIR1 WITNESS_DIR2 WITNESS_PORT1 WITNESS_PORT2 WITNESS_RUN; WITNESS_RESOURCE_OWNER="$BASE-s$i" "$TOOLS/qa.sh" --shard "$i/$N" "$@" > "$LOGS/shard$i.log" 2>&1; echo $? > "$LOGS/shard$i.rc" ) &
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
