#!/bin/bash
# Full scenario suite (baseline, lost first send, two mutants). Takes about 15 minutes; the games take the keyboard and mouse.
. "$(dirname "$0")/config.sh"
exec python3 "$TOOLS/suite.py" "$@"
