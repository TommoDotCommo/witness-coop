#!/bin/bash
# Test one puzzle by hand or by bot: tools/puzzle.sh list|show|go|solve ... (see tools/puzzle.py). The game folders and pids come from the lab lease.
. "$(dirname "$0")/config.sh"
export WITNESS_DIR1 WITNESS_DIR2 WITNESS_RUN="$RUN"
exec python3 "$TOOLS/puzzle.py" "$@"
