#!/bin/bash
# `tools/qa.sh model [filter]` runs the character-model suite (suite_model.py) instead.
# Shared-state suite (live sync, packet loss, peer restart and two tutorial doors, with mutants). Accepts scenario name filters.
# resource provider mode: leases two slots (without waiting: exit 3 = busy, run game-free tests instead or retry), builds the hard-linked
# instance folders, runs, and always kills this agent's games and frees the slots. QA_WAIT=<seconds> waits for slots.
WITNESS_ALLOCATING=1 . "$(dirname "$0")/config.sh" || exit $?
if [ -x "$WITNESS_RESOURCES" ] && [ -z "${WITNESS_SLOTS:-}" ]; then
  "$WITNESS_RESOURCES" acquire --slots 2 --ttl 3600 --wait "${QA_WAIT:-0}" || { echo "no game slots free (resource provider status); not waiting"; exit 3; }
  trap '"$WITNESS_RESOURCES" release' EXIT
  "$WITNESS_RESOURCES" provision witness || exit 1
  unset WITNESS_DIR1 WITNESS_DIR2 WITNESS_PORT1 WITNESS_PORT2 WITNESS_RUN
  . "$(dirname "$0")/config.sh"       || exit $? # re-read: the lease now exists
elif [ -x "$WITNESS_RESOURCES" ]; then
  trap '"$WITNESS_RESOURCES" release' EXIT
  "$WITNESS_RESOURCES" provision witness || exit 1
fi
if [ "${1:-}" = model ]; then shift; python3 "$TOOLS/suite_model.py" "$@"; else python3 "$TOOLS/suite.py" "$@"; fi
