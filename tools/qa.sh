#!/bin/bash
# Full scenario suite (baseline, lost first send, two mutants). Takes about 15 minutes.
# Lab mode: leases two slots (without waiting: exit 3 = busy, run game-free tests instead or retry), builds the hard-linked
# instance folders, runs, and always kills this agent's games and frees the slots. QA_WAIT=<seconds> waits for slots.
. "$(dirname "$0")/config.sh"
if [ -x "$LAB" ] && [ -z "${LAB_SLOTS:-}" ]; then
  "$LAB" acquire --slots 2 --ttl 3600 --wait "${QA_WAIT:-0}" || { echo "no game slots free (lab status); not waiting"; exit 3; }
  trap '"$LAB" release' EXIT
  "$LAB" provision witness || exit 1
  unset WITNESS_DIR1 WITNESS_DIR2 WITNESS_PORT1 WITNESS_PORT2 WITNESS_RUN
  . "$(dirname "$0")/config.sh"       # re-read: the lease now exists
elif [ -x "$LAB" ]; then
  trap '"$LAB" release' EXIT
  "$LAB" provision witness || exit 1
fi
python3 "$TOOLS/suite.py" "$@"
