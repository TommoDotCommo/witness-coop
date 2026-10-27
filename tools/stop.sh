#!/bin/bash
# Close this agent's games (only pids it registered or started) and, in resource provider mode, free its slots.
. "$(dirname "$0")/config.sh" || exit $?
if [ -x "$WITNESS_RESOURCES" ]; then "$WITNESS_RESOURCES" release; else for p in A B; do [ -f "$RUN/pid$p.txt" ] && taskkill.exe /PID "$(cat "$RUN/pid$p.txt")" /F >/dev/null 2>&1; done; fi
