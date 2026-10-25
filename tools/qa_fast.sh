#!/bin/bash
# Every T2 scenario of the co-op features (sweep, pov, lines, tags, ui; QA_SCENARIOS="pov ui" picks some) spread over N parallel shards (QA_FAST_SHARDS, default 3), each its own lab
# owner with its own two slots, ports and folders; scenarios in one shard run one after the other. Needs 2N free slots and about 4 GB of RAM per pair (a shard that finds no
# slots exits 3: rerun with fewer). The game-free tests (make -C proxy/core test) take seconds and are not part of this: run them first. Exit 0 only when every scenario passed.
. "$(dirname "$0")/config.sh"
N="${QA_FAST_SHARDS:-3}"; BASE="${LAB_OWNER:-$(basename "$ROOT")}"; LOGS="$(mktemp -d "${TMPDIR:-/tmp}/qa_fast.XXXXXX")"
read -r -a ALL <<< "${QA_SCENARIOS:-sweep pov lines tags ui}"
declare -a SHARD; for idx in "${!ALL[@]}"; do SHARD[$((idx % N))]+="${ALL[$idx]} "; done
pids=(); start=$(date +%s)
for i in "${!SHARD[@]}"; do
  ( for s in ${SHARD[$i]}; do t=$(date +%s); LAB_OWNER="$BASE-f$i" "$TOOLS/qa.sh" "$s" > "$LOGS/$s.log" 2>&1; echo "$? $(( $(date +%s) - t ))" > "$LOGS/$s.rc"; done ) &
  pids+=($!); sleep 20       # stagger: provisioning and window placement of one shard finish before the next starts
done
wait "${pids[@]}"
rc=0
for s in "${ALL[@]}"; do r=$(cat "$LOGS/$s.rc" 2>/dev/null || echo "? ?"); set -- $r; echo "$s: exit $1 in $2 s ($LOGS/$s.log)"; [ "$1" = 0 ] || rc=1; done
echo "total $(( $(date +%s) - start )) s"; [ $rc = 0 ] && echo "ALL SCENARIOS OK" || echo "SOME SCENARIOS FAILED"
exit $rc
