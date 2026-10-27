#!/bin/bash
# `tools/sweep.sh [args]` runs the panel sweep (win/wsweep.py driver in sweep.py): lease two slots, start both games, teleport to every
# puzzle and solve it. resource provider mode as in qa.sh (exit 3 = busy).
WITNESS_ALLOCATING=1 . "$(dirname "$0")/config.sh" || exit $?
if [ -x "$WITNESS_RESOURCES" ] && [ -z "${WITNESS_SLOTS:-}" ]; then
  "$WITNESS_RESOURCES" acquire --slots 2 --ttl 3600 --wait "${QA_WAIT:-0}" || { echo "no game slots free (resource provider status); not waiting"; exit 3; }
  trap '"$WITNESS_RESOURCES" release' EXIT
  "$WITNESS_RESOURCES" provision witness || exit 1
  unset WITNESS_DIR1 WITNESS_DIR2 WITNESS_PORT1 WITNESS_PORT2 WITNESS_RUN
  . "$(dirname "$0")/config.sh" || exit $?
fi
"$TOOLS/restart.sh" >/dev/null || exit 1
sleep 30; echo bot > "$RUN/guard.stop"
python3 "$TOOLS/sweep.py" "$@"
