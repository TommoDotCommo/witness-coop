#!/bin/bash
# Every T2 scenario of the co-op features (sweep, pov, lines, tags, ui, pause, social, emote, ink, progress, launcher, voice, map; QA_SCENARIOS="pov ui" picks some) spread over N parallel shards (QA_FAST_SHARDS, default 3), each its own resource provider
# owner with its own two slots, ports and folders; scenarios in one shard run one after the other. Needs 2N free slots and about 4 GB of RAM per pair (a shard that finds no
# slots exits 3: rerun with fewer). The game-free tests (make -C proxy/core test) take seconds and are not part of this: run them first. Exit 0 only when every scenario passed.
WITNESS_ALLOCATING=1 . "$(dirname "$0")/config.sh" || exit $?
N="${QA_FAST_SHARDS:-3}"; BASE="${WITNESS_RESOURCE_OWNER:-witness-$(python3 -c 'import uuid; print(uuid.uuid4().hex)')}"; LOGS="$(mktemp -d "${TMPDIR:-/tmp}/qa_fast.XXXXXX")"
read -r -a ALL <<< "${QA_SCENARIOS:-sweep pov lines tags ui pause social emote ink progress launcher voice map}"
declare -a SHARD; for idx in "${!ALL[@]}"; do SHARD[$((idx % N))]+="${ALL[$idx]} "; done
pids=(); start=$(date +%s)
for i in "${!SHARD[@]}"; do
  ( unset WITNESS_SLOTS WITNESS_DIR1 WITNESS_DIR2 WITNESS_PORT1 WITNESS_PORT2 WITNESS_RUN; for s in ${SHARD[$i]}; do t=$(date +%s); WITNESS_RESOURCE_OWNER="$BASE-f$i" "$TOOLS/qa.sh" "$s" > "$LOGS/$s.log" 2>&1; echo "$? $(( $(date +%s) - t ))" > "$LOGS/$s.rc"; done ) &
  pids+=($!); sleep 20       # stagger: provisioning and window placement of one shard finish before the next starts
done
wait "${pids[@]}"
rc=0
for s in "${ALL[@]}"; do r=$(cat "$LOGS/$s.rc" 2>/dev/null || echo "? ?"); set -- $r; echo "$s: exit $1 in $2 s ($LOGS/$s.log)"; [ "$1" = 0 ] || rc=1; done
echo "total $(( $(date +%s) - start )) s"; [ $rc = 0 ] && echo "ALL SCENARIOS OK" || echo "SOME SCENARIOS FAILED"
exit $rc
