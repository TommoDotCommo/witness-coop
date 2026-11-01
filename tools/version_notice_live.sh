#!/bin/bash
# T1: a packet of another mod version from a configured peer is said once in the game, not dropped in silence (proxy/core/version_notice.h, proxy/coop.cpp). One slot, one game with a roster
# (the partner is a datagram socket on the partner's port, written by tools/win/wsendmagic.py). Requires: a packet with this build's own magic (garbage inside) raises nothing; five packets with an
# older magic (WTP5) raise exactly one line "Player 2 is on v0.0.9, you are on <this build>: update both" in the game's log.
#   tools/version_notice_live.sh            the check
#   tools/version_notice_live.sh --mutant   the proxy whose notice is silent: the check must fail (exit 0 = the mutant was caught)
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
d="$WITNESS_DIR1"; gp="$WITNESS_PORT1"; pp="$WITNESS_PORT2"
make -s -C "$ROOT/proxy" || exit 1
"$TOOLS/install.sh" A >/dev/null || exit 1
if [ $mutant = 1 ]; then make -s -C "$ROOT/proxy" AnselSDK64_mut_silent.dll || exit 1; rm -f "$d/AnselSDK64.dll"; cp "$ROOT/proxy/AnselSDK64_mut_silent.dll" "$d/AnselSDK64.dll"; fi
mine=$(sed -n 's/^#define MOD_VERSION "\(.*\)"/\1/p' "$ROOT/proxy/build/version.h")
printf 'id=1\nlisten=%s\npeer=2,127.0.0.1,%s\npeer_version=2,v0.0.9\nsave_dir=instance\nshare_zip=1\nforce_active=1\ncap_fps=%s\nname=Alpha\n' "$gp" "$pp" "${WITNESS_FPS:-30}" > "$d/witness_coop.cfg"
rm -f "$d/witness_proxy.log"
pid=$(timeout 30 "$PYW" -I "$(win "$TOOLS/win/launch.py")" "$(win "$d")" -skip_config_dialog -windowed -new_game | tail -1 | tr -d '\r')
[ -n "$pid" ] || { echo "version_notice_live: the game did not start"; exit 1; }
echo "$pid" > "$RUN/pidA.txt"; [ -x "$WITNESS_RESOURCES" ] && "$WITNESS_RESOURCES" register-pid "$pid" >/dev/null 2>&1
for i in $(seq 1 60); do grep -q 'coop: ' "$d/witness_proxy.log" 2>/dev/null && grep -q 'devmenu: 594 panels' "$d/witness_proxy.log" 2>/dev/null && break; sleep 3; done
send() { "$PYW" -I "$(win "$TOOLS/win/wsendmagic.py")" "$gp" "$pp" "$@" | tr -d '\r'; }
lines() { grep -c 'update both' "$d/witness_proxy.log" 2>/dev/null; }
send 2 WTP6 3 >/dev/null; sleep 1.5
n0=$(lines); echo "version_notice_live: after 3 datagrams with this build's own magic: $n0 notice lines (must be 0)"
send 2 WTP5 5 >/dev/null; sleep 2
n1=$(lines); line=$(grep 'update both' "$d/witness_proxy.log" | head -1)
echo "version_notice_live: after 5 datagrams with the older magic WTP5: $n1 notice line(s): $line"
want="Player 2 is on v0.0.9, you are on $mine: update both"
if [ $mutant = 1 ]; then
  if [ "$n1" = 0 ]; then echo "version_notice_live MUTANT: PASS (the silent proxy says nothing, so the check catches it)"; exit 0; fi
  echo "version_notice_live MUTANT: FAIL (the silent proxy still said something)"; exit 1
fi
alive=$(tasklist.exe /FI "PID eq $pid" /NH 2>/dev/null | grep -c witness64)
if [ "$n0" = 0 ] && [ "$n1" = 1 ] && echo "$line" | grep -qF "$want" && [ "$alive" = 1 ]; then echo "version_notice_live: PASS (\"$want\", once; the game is still running)"; exit 0; fi
echo "version_notice_live: FAIL (wanted exactly one line with: $want)"; tail -5 "$d/witness_proxy.log"; exit 1
