#!/bin/bash
# Close this agent's games (only pids it registered or started) and, in lab mode, free its slots.
. "$(dirname "$0")/config.sh"
if [ -x "$LAB" ]; then "$LAB" release; else for p in A B; do [ -f "$RUN/pid$p.txt" ] && taskkill.exe /PID "$(cat "$RUN/pid$p.txt")" /F >/dev/null 2>&1; done; fi
