#!/bin/bash
# One game, no partner (T1): a start without a roster in witness_coop.cfg (a start from Steam, or a cfg the launcher has not written)
# must not crash. Leases one slot, installs the current proxy into it, starts the game with a cfg that has no id/listen/peer,
# and requires the world scan in the proxy log and the process still alive 30 s later.
#   tools/solo.sh            the check (exit 0 = alive)
#   tools/solo.sh --mutant   late_voice_lock=1 (the lock as before 2026-10-31): the game must die, otherwise the check tests nothing
# Voice stays at its default (push to talk): the mic indicator is what took the lock. The owner's saves are never touched (save_dir=instance). Needs the resource provider (local/env.sh), as qa.sh does.
set -u
mutant=0; [ "${1:-}" = --mutant ] && mutant=1
WITNESS_ALLOCATING=1 . "$(dirname "$0")/config.sh" || exit $?
if [ -x "$WITNESS_RESOURCES" ] && [ -z "${WITNESS_SLOTS:-}" ]; then
  "$WITNESS_RESOURCES" acquire --slots 1 --ttl 1200 --wait "${QA_WAIT:-0}" || { echo "no game slot free (resource provider status); not waiting"; exit 3; }
  trap '"$WITNESS_RESOURCES" release' EXIT
  "$WITNESS_RESOURCES" provision witness || exit 1
  unset WITNESS_DIR1 WITNESS_DIR2 WITNESS_PORT1 WITNESS_PORT2 WITNESS_RUN
  . "$(dirname "$0")/config.sh" || exit $?
elif [ -x "$WITNESS_RESOURCES" ]; then
  trap '"$WITNESS_RESOURCES" release' EXIT
  "$WITNESS_RESOURCES" provision witness || exit 1
fi
"$TOOLS/install.sh" A >/dev/null || exit 1
d="$WITNESS_DIR1"
printf 'save_dir=instance\nshare_zip=1\nforce_active=1\ncap_fps=%s\n%s' "${WITNESS_FPS:-30}" "$([ $mutant = 1 ] && echo 'late_voice_lock=1')" > "$d/witness_coop.cfg"
rm -f "$d/witness_proxy.log"
pid=$(timeout 30 "$PYW" -I "$(win "$TOOLS/win/launch.py")" "$(win "$d")" -skip_config_dialog -windowed -new_game | tail -1 | tr -d '\r')
[ -n "$pid" ] || { echo "solo: the game did not start"; exit 1; }
echo "$pid" > "$RUN/pidA.txt"; [ -x "$WITNESS_RESOURCES" ] && "$WITNESS_RESOURCES" register-pid "$pid" >/dev/null 2>&1
alive() { tasklist.exe /FI "PID eq $pid" /NH 2>/dev/null | grep -c witness64; }
scanned=0
for i in $(seq 1 60); do
  grep -q 'devmenu: 594 panels' "$d/witness_proxy.log" 2>/dev/null && { scanned=$i; break; }
  [ "$(alive)" = 0 ] && break
  sleep 3
done
sleep 30
a=$(alive); coop=$(grep -E '^coop:' "$d/witness_proxy.log" 2>/dev/null | head -1)
echo "solo: mutant=$mutant world_scanned_at=$((scanned * 3))s alive_after_30s=$a | $coop"
echo "$coop" | grep -q 'networking off' || { echo "solo: the cfg must have no roster (the game reported networking on)"; exit 1; }
if [ $mutant = 1 ]; then
  [ "$a" = 0 ] && { echo "solo MUTANT: PASS (the game died without the lock fix, as it must)"; exit 0; }
  echo "solo MUTANT: FAIL (the game survived late_voice_lock=1; the check tests nothing)"; exit 1
fi
[ "$scanned" != 0 ] && [ "$a" = 1 ] && { echo "solo: PASS (a start without a roster scans the world and stays alive)"; exit 0; }
echo "solo: FAIL"; grep -vE '^uiglyph' "$d/witness_proxy.log" | tail -5; exit 1
