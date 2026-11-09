#!/bin/bash
# One game with a built-in randomizer (T1): the randomizer must make and apply this world's puzzles and the game must keep running.
# Leases one slot, installs the current proxy, starts a new game with randomizer=<spec> (default normal;seed:1234), waits for
# "randomizer: applied", asks for randstat and a few panels.
# --record: instead, a game with no randomizer (the generator must read the game's own puzzles, as it does in every world it makes puzzles for)
# records its world for the game-free test: local/randomizer/<build>.world (game data, never committed; proxy/randomizer/Makefile's generate-test
# replays it).
#   tools/randomizer_solo.sh [--spec <spec>] [--record] [--mutant]      (RECORD_SPEC=<spec> with --record: that randomizer only, in a file of its own)
#   --mutant: the proxy built with WR_MUT_NO_APPLY (the puzzles are made but never written): the check must fail
# The owner's saves are never touched (save_dir=instance). Needs the resource provider (local/env.sh), as qa.sh does.
set -u
spec="normal;seed:1234"; record=0; mutant=0
while [ $# -gt 0 ]; do case "$1" in --spec) spec="${2:?}"; shift;; --record) record=1;; --mutant) mutant=1;; *) echo "usage: randomizer_solo.sh [--spec S] [--record] [--mutant]" >&2; exit 2;; esac; shift; done
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
d="$WITNESS_DIR1"
if [ $mutant = 1 ]; then
  "$TOOLS/install.sh" A >/dev/null || exit 1
  make -s -C "$ROOT/proxy" dsound_mut_rand_NO_APPLY.dll >/dev/null || exit 1
  rm -f "$d/dsound.dll"; cp "$ROOT/proxy/dsound_mut_rand_NO_APPLY.dll" "$d/dsound.dll"      # replaced, never written through a hard link
else
  "$TOOLS/install.sh" A >/dev/null || exit 1
fi
[ $record = 1 ] && spec=off
printf 'save_dir=instance\nshare_zip=1\nforce_active=1\ncap_fps=%s\nrandomizer=%s\n' "${WITNESS_FPS:-30}" "$spec" > "$d/witness_coop.cfg"
rm -f "$d/witness_proxy.log"
pid=$(timeout 30 "$PYW" -I "$(win "$TOOLS/win/launch.py")" "$(win "$d")" -skip_config_dialog -windowed -new_game | tail -1 | tr -d '\r')
[ -n "$pid" ] || { echo "randomizer_solo: the game did not start"; exit 1; }
echo "$pid" > "$RUN/pidA.txt"; [ -x "$WITNESS_RESOURCES" ] && "$WITNESS_RESOURCES" register-pid "$pid" >/dev/null 2>&1
alive() { tasklist.exe /FI "PID eq $pid" /NH 2>/dev/null | grep -c witness64; }
cmd() { local f="$d/witness_cmd.txt"; for i in $(seq 1 80); do [ -e "$f" ] || break; sleep 0.1; done; printf '%s\n' "$@" > "$f.tmp"; mv "$f.tmp" "$f"; for i in $(seq 1 80); do [ -e "$f" ] || break; sleep 0.1; done; }
applied=0
if [ $record = 1 ]; then
  for i in $(seq 1 60); do grep -q 'devmenu: 594 panels' "$d/witness_proxy.log" 2>/dev/null && break; [ "$(alive)" = 0 ] && break; sleep 3; done
  sleep 5
  mkdir -p "$ROOT/local/randomizer"; out="$ROOT/local/randomizer/11370412${RECORD_SPEC:+-$(echo "$RECORD_SPEC" | tr ';:' '__')}.world"; rm -f "$out"
  cmd "randrecord $(win "$out")${RECORD_SPEC:+ $RECORD_SPEC}"      # RECORD_SPEC=<spec>: only that randomizer (its own file)
  for i in $(seq 1 1200); do grep -qE 'randrecord: (done|.* is stuck)' "$d/witness_proxy.log" && break; [ "$(alive)" = 0 ] && break; sleep 2; done
  grep -E '^randrecord:' "$d/witness_proxy.log" | tail -9
  grep -q 'randrecord: done, wrote' "$d/witness_proxy.log" && [ "$(alive)" = 1 ] && { echo "randomizer_solo: RECORDED $out"; exit 0; }
  grep -q 'is stuck in its own computation.*: saved' "$d/witness_proxy.log" && { echo "randomizer_solo: RECORDED a stuck run $out (the game is closed: its generator still spins)"; exit 0; }
  echo "randomizer_solo: the recording did not finish"; exit 1
fi
for i in $(seq 1 ${RANDOMIZER_WAIT:-90}); do
  grep -qE 'randomizer: (applied|FAILED)' "$d/witness_proxy.log" 2>/dev/null && { applied=$i; break; }
  [ $((i % 15)) = 0 ] && { cmd randstat; grep -a '^randstat: generating' "$d/witness_proxy.log" | tail -1; }
  [ "$(alive)" = 0 ] && break
  sleep 2
done
grep -E '^randomizer:' "$d/witness_proxy.log" | head -8
cmd randstat "randpanel 64" "randpanel 182" "randpanel 293" "randpanel 5d"
sleep 2
grep -E '^(randstat|randpanel):' "$d/witness_proxy.log" | tail -6
sleep 10
a=$(alive)
st=$(grep -E '^randstat:' "$d/witness_proxy.log" | tail -1)
ents=$(echo "$st" | sed -n 's/.* entities \([0-9]*\) .*/\1/p')
echo "randomizer_solo: spec='$spec' mutant=$mutant applied_at=$((applied * 2))s alive=$a entities=${ents:-?}"
grep -q 'state applied' <<<"$st" && [ "${ents:-0}" -gt 100 ] && [ "$a" = 1 ] && pass=1 || pass=0
# the puzzles must really be in the game: the first tutorial panel carries the seed (upstream draws it there), so its dots are not the game's
if [ $pass = 1 ] && [ $mutant = 1 ]; then echo "randomizer_solo MUTANT: FAIL (the check passed with the puzzles never written)"; exit 1; fi
if [ $mutant = 1 ]; then echo "randomizer_solo MUTANT: PASS (the check failed, as it must)"; exit 0; fi
[ $pass = 1 ] && { echo "randomizer_solo: PASS"; exit 0; }
echo "randomizer_solo: FAIL"; grep -vE '^uiglyph' "$d/witness_proxy.log" | tail -12; exit 1
