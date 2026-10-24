#!/bin/bash
# Teleport the player in your running game(s) to numbered spots across the island, for manual testing.
# usage: tools/tour.sh list | go N | next | prev | here   [A|B|both]     (default game: A; "both" sends the same spot to A and B)
. "$(dirname "$0")/config.sh"
cmd="$1"; arg=""; [[ "$2" =~ ^[0-9]+$ ]] && { arg="$2"; shift; }; which="${2:-A}"
[ "$cmd" = go ] && [ -z "$arg" ] && { echo "usage: $0 go N [A|B|both]" >&2; exit 2; }
for g in $([ "$which" = both ] && echo A B || echo "$which"); do
  d=$WITNESS_DIR1; [ "$g" = B ] && d=$WITNESS_DIR2
  echo -n "game $g: "; "$PYW" -I "$(win "$TOOLS/win/wtour.py")" "$(cat "$RUN/pid$g.txt")" "$(win "$d")" "$(win "$RUN/tour$g.txt")" "$cmd" $arg
done
