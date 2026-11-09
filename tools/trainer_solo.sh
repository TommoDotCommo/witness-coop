#!/bin/bash
# One game with the Witness Trainer's co-op options (proxy/trainer.cpp) (T1): every scan must find its value, and the game must hold what the options
# ask for, read back from the game (trainerstat): the run speed and walking acceleration at 200 % of the game's own, the field of view of a 120 degree
# horizontal angle (88.5 degrees vertical at 16:9), the challenge's time limit jumped over, the environmental-puzzle overlay on, and noclip toggled by
# the noclip key (N) pressed through the game's own input, on and off again.
#   tools/trainer_solo.sh [--mutant]   --mutant: the same game with the options off: the check must fail
# The owner's saves are never touched (save_dir=instance). Needs the resource provider (local/env.sh), as qa.sh does.
set -u
mutant=0; [ "${1:-}" = --mutant ] && mutant=1
WITNESS_ALLOCATING=1 . "$(dirname "$0")/config.sh" || exit $?
if [ -x "$WITNESS_RESOURCES" ] && [ -z "${WITNESS_SLOTS:-}" ]; then
  "$WITNESS_RESOURCES" acquire --slots 1 --ttl 1200 --wait "${QA_WAIT:-0}" || { echo "no game slot free (resource provider status); not waiting"; exit 3; }
  trap '"$WITNESS_RESOURCES" release' EXIT
  "$WITNESS_RESOURCES" provision witness || exit 1
  unset WITNESS_DIR1 WITNESS_DIR2 WITNESS_PORT1 WITNESS_PORT2 WITNESS_RUN
  . "$(dirname "$0")/config.sh" || exit $?
fi
d="$WITNESS_DIR1"
"$TOOLS/install.sh" A >/dev/null || exit 1
opts='trainer_speed=200\ntrainer_fov=120\ntrainer_noclip=1\ntrainer_no_challenge_limit=1\ntrainer_ep_overlay=1\n'
[ $mutant = 1 ] && opts='trainer_speed=100\ntrainer_fov=0\ntrainer_noclip=0\ntrainer_no_challenge_limit=0\ntrainer_ep_overlay=0\n'
printf "save_dir=instance\nshare_zip=1\nforce_active=1\ncap_fps=%s\n$opts" "${WITNESS_FPS:-30}" > "$d/witness_coop.cfg"
rm -f "$d/witness_proxy.log"
pid=$(timeout 30 "$PYW" -I "$(win "$TOOLS/win/launch.py")" "$(win "$d")" -skip_config_dialog -windowed -new_game | tail -1 | tr -d '\r')
[ -n "$pid" ] || { echo "trainer_solo: the game did not start"; exit 1; }
echo "$pid" > "$RUN/pidA.txt"; [ -x "$WITNESS_RESOURCES" ] && "$WITNESS_RESOURCES" register-pid "$pid" >/dev/null 2>&1
cmd() { local f="$d/witness_cmd.txt"; for i in $(seq 1 80); do [ -e "$f" ] || break; sleep 0.1; done; printf '%s\n' "$@" > "$f.tmp"; mv "$f.tmp" "$f"; for i in $(seq 1 80); do [ -e "$f" ] || break; sleep 0.1; done; sleep 1; }
stat() { cmd trainerstat; grep -a '^trainerstat:' "$d/witness_proxy.log" | tail -1; }
for i in $(seq 1 60); do grep -q 'devmenu: 594 panels' "$d/witness_proxy.log" 2>/dev/null && break; sleep 3; done
sleep 8
grep -aE '^trainer:' "$d/witness_proxy.log" | head -3
s0=$(stat); cmd "key N down" "key N up"; sleep 1; s1=$(stat); cmd "key N down" "key N up"; sleep 1; s2=$(stat)
echo "$s0"; echo "$s1"; echo "$s2"
base=$(grep -a '^trainer: applied' "$d/witness_proxy.log" | sed -n 's/.*(run \([0-9.]*\)).*/\1/p' | head -1)
python3 - "$s0" "$s1" "$s2" "${base:-0}" <<'EOF'
import re, sys
def kv(s): return dict(re.findall(r' (\w+) (\S+)', s))
a, b, c, base = kv(sys.argv[1]), kv(sys.argv[2]), kv(sys.argv[3]), float(sys.argv[4])
ok = True
def check(what, cond, detail=''):
    global ok; ok = ok and bool(cond); print('  %-80s %s  %s' % (what, 'ok' if cond else 'FAILED', detail))
check('the scans ran and the game\'s own run speed was read', a.get('found') == '1' and base > 0, (a.get('found'), base))
check('the run speed is 200 % of the game\'s own', base > 0 and abs(float(a.get('run', 0)) - 2 * base) < 0.002 * base, (a.get('run'), base))
check('the field of view is a 120 degree horizontal angle (88.5 vertical)', abs(float(a.get('fov', 0)) - 88.48) < 0.2, a.get('fov'))
check('the challenge\'s time limit is jumped over (eb)', a.get('challenge') == 'eb', a.get('challenge'))
check('the environmental-puzzle overlay is on', a.get('eps') == '1', a.get('eps'))
check('the noclip key turns noclip on, and off again', a.get('noclip') == '0' and b.get('noclip') == '1' and c.get('noclip') == '0', (a.get('noclip'), b.get('noclip'), c.get('noclip')))
sys.exit(0 if ok else 1)
EOF
r=$?
if [ $mutant = 1 ]; then [ $r != 0 ] && { echo "trainer_solo MUTANT: PASS (the check failed with the options off, as it must)"; exit 0; }; echo "trainer_solo MUTANT: FAIL"; exit 1; fi
[ $r = 0 ] && { echo "trainer_solo: PASS"; exit 0; }; echo "trainer_solo: FAIL"; exit 1
