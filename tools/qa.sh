#!/bin/bash
# `tools/qa.sh model [filter]` runs the character-model suite (suite_model.py) instead; `tools/qa.sh sweep` the puzzle-sweep scenario (qa_sweep.py); `tools/qa.sh lines` the shared-lines scenario (qa_lines.py); `tools/qa.sh tags` the name-tags scenario (qa_tags.py); `tools/qa.sh pov` the POV-screen scenario (qa_pov.py); `tools/qa.sh ui` the glyph-atlas check (qa_ui.py).
# Shared-state suite (live sync, packet loss, peer restart and two tutorial doors, with mutants). Accepts scenario name filters.
# Lab mode: leases two slots (without waiting: exit 3 = busy, run game-free tests instead or retry), builds the hard-linked
# instance folders, runs, and always kills this agent's games and frees the slots. QA_WAIT=<seconds> waits for slots.
# (the whole body is one brace group: bash reads it completely before running, so editing this file while a run is in progress cannot break that run)
export WITNESS_FPS="${WITNESS_FPS:-60}"      # injected mouse events are taken one per frame: the bots draw about twice as fast at 60 fps as at 30 (a pair of games costs little)
{
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
if [ "${1:-}" = model ]; then shift; python3 "$TOOLS/suite_model.py" "$@"; elif [ "${1:-}" = sweep ]; then shift; python3 "$TOOLS/qa_sweep.py" "$@"; elif [ "${1:-}" = lines ]; then shift; python3 "$TOOLS/qa_lines.py" "$@"; elif [ "${1:-}" = tags ]; then shift; python3 "$TOOLS/qa_tags.py" "$@"; elif [ "${1:-}" = pov ]; then shift; python3 "$TOOLS/qa_pov.py" "$@"; elif [ "${1:-}" = ui ]; then shift; python3 "$TOOLS/qa_ui.py" "$@"; elif [ "${1:-}" = pause ]; then shift; python3 "$TOOLS/qa_pause.py" "$@"; elif [ "${1:-}" = social ]; then shift; python3 "$TOOLS/qa_social.py" "$@"; elif [ "${1:-}" = emote ]; then shift; python3 "$TOOLS/qa_emote.py" "$@"; elif [ "${1:-}" = ink ]; then shift; python3 "$TOOLS/qa_ink.py" "$@"; elif [ "${1:-}" = progress ]; then shift; python3 "$TOOLS/qa_progress.py" "$@"; elif [ "${1:-}" = probe ]; then shift; python3 "$@"; else python3 "$TOOLS/suite.py" "$@"; fi
exit $?
}
