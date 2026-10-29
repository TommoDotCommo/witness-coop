#!/bin/bash
# The whole shared-state suite (tools/suite.py) run by parallel shards, each its own resource provider owner with its own two slots,
# ports, folders and run folder, so the shards cannot collide. This is what `tools/qa.sh` and `tools/qa.sh --all` run.
# The selected scenarios go into one queue, longest first (typical seconds in tools/qa_durations.json); every shard takes the next one
# when it is free, so the shards finish close together. A new shard starts (QA_STAGGER seconds apart, default 90: the previous
# shard's games have loaded and show in the free RAM by then) while scenarios are left, fewer than
# QA_SHARDS_MAX (default 4) run, and the resource provider status has room for one more: 2 free slots and about 4 GB of free RAM
# above the provider's own floor.
# With no room it waits, up to QA_WAIT seconds (default 1800) while no shard runs, instead of giving up; without a resource provider it
# runs one shard. QA_SHARDS=N starts exactly N shards instead, each waiting up to QA_WAIT for its lease.
# Extra arguments are scenario name filters, as for qa.sh. Output: one log per shard and durations.json in a qa_par.* folder of the run
# folder, and a summary table here (scenario, shard, seconds, result, wall time). Exit 0 only when every scenario ran and passed; 3 when
# no shard ever found room.
# (the whole body is one brace group: bash reads it completely before running, so editing this file during a run cannot break that run)
{
WITNESS_ALLOCATING=1 . "$(dirname "$0")/config.sh" || exit $?
t_start=$(date +%s)
WAIT="${QA_WAIT:-1800}"; STAGGER="${QA_STAGGER:-90}"; CAP="${QA_SHARDS:-${QA_SHARDS_MAX:-4}}"
BASE="${WITNESS_RESOURCE_OWNER:-witness-$(python3 -c 'import uuid; print(uuid.uuid4().hex)')}"
LOGS="$(mktemp -d "$RUN/qa_par.XXXXXX")"; QUEUE="$LOGS/queue"
total=$(python3 "$TOOLS/shard.py" queue "$QUEUE" "$@") && cp "$QUEUE" "$LOGS/planned"
[ "${total:-0}" -gt 0 ] || { echo "no scenario matches: $*"; exit 2; }
echo "$total scenarios, at most $CAP shard(s); logs in $LOGS"
room() {      # shards the provider has room for now (it already counts the running shards' games)
  if [ -n "${QA_SHARDS:-}" ]; then echo 1
  elif [ -x "$WITNESS_RESOURCES" ]; then "$WITNESS_RESOURCES" status 2>/dev/null | QA_SHARDS_MAX=99 python3 "$TOOLS/shard.py" count
  elif [ ${#pids[@]} = 0 ]; then echo 1; else echo 0; fi
}
left() { grep -c . "$QUEUE"; }
alive() { local n=0 p; for p in "${pids[@]}"; do kill -0 "$p" 2>/dev/null && n=$((n + 1)); done; echo $n; }
pids=(); n=0; last=0; idle_since=$t_start
killtree() { local c; for c in $(pgrep -P "$1"); do killtree "$c"; done; kill -TERM "$1" 2>/dev/null; }     # the suite and its bots first, then qa.sh
stop_shards() { for p in "${pids[@]}"; do killtree "$p"; done; wait; exit 130; }    # each qa.sh frees its own lease (and its games) in its EXIT trap
trap stop_shards INT TERM
while :; do
  a=$(alive); l=$(left)
  [ "$l" = 0 ] && [ "$a" = 0 ] && break
  [ "$a" = 0 ] || idle_since=$(date +%s)
  if [ "$l" -gt 0 ] && [ "$a" -lt "$CAP" ] && { [ -z "${QA_SHARDS:-}" ] || [ "$n" -lt "$QA_SHARDS" ]; } && [ $(( $(date +%s) - last )) -ge "$STAGGER" ] \
     && { [ "$a" = 0 ] || [ "$l" -ge 2 ]; } && [ "$(room)" -ge 1 ]; then
    n=$((n + 1)); last=$(date +%s)
    echo "$(date +%T) shard $n starts ($l scenarios left, $a shard(s) running)"
    ( unset WITNESS_SLOTS WITNESS_DIR1 WITNESS_DIR2 WITNESS_PORT1 WITNESS_PORT2 WITNESS_RUN
      QA_WAIT="$([ -n "${QA_SHARDS:-}" ] && echo "$WAIT" || echo 120)" WITNESS_RESOURCE_OWNER="$BASE-s$n" "$TOOLS/qa.sh" --queue "$QUEUE" > "$LOGS/shard$n.log" 2>&1
      echo $? > "$LOGS/shard$n.rc" ) &
    pids+=($!)
  elif [ "$a" = 0 ] && [ $(( $(date +%s) - idle_since )) -ge "$WAIT" ]; then
    echo "no room for a shard for ${WAIT}s (resource provider status); $l scenario(s) not run"; break
  elif [ "$a" = 0 ]; then
    [ $(( ($(date +%s) - idle_since) % 120 )) -lt 5 ] && echo "$(date +%T) no room for a shard yet (2 free slots and about 4 GB of free RAM above the provider floor); waiting"
  fi
  sleep 5
done
wait
rc=0
for i in $(seq 1 "$n"); do
  r=$(cat "$LOGS/shard$i.rc" 2>/dev/null || echo '?'); echo "shard $i exit $r ($LOGS/shard$i.log)"
  [ "$r" = 0 ] || [ "$r" = 3 ] || rc=1      # 3: the shard got no lease after all and took no scenario
done
echo; python3 "$TOOLS/shard.py" summary "$LOGS" $(( $(date +%s) - t_start )) | tee "$LOGS/summary.txt"
grep -qE '  (FAIL|NOT RUN)$' "$LOGS/summary.txt" && rc=1
[ "$(grep -cE '  PASS$' "$LOGS/summary.txt")" = "$total" ] || rc=1
[ "$n" = 0 ] && { echo "NO SHARD STARTED (busy)"; exit 3; }
[ $rc = 0 ] && echo "ALL SHARDS OK" || echo "SOME SHARDS FAILED"
exit $rc
}
