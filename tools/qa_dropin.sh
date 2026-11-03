#!/bin/bash
# Live check (one slot) of the drop-in layout's runtime rules, each with a mutant that must be caught:
#   chain:    another mod's dsound.dll kept as dsound.chain.dll is loaded by the proxy: the fixture (proxy/tests/live_chain.cpp) writes
#             witness_chain_loaded.txt with the game's process id when it loads; the game must also scan the world and stay alive.
#             Mutant: a proxy that ignores the chain file (make -C proxy dsound_mut_NO_CHAIN.dll): no file, caught.
#   old copy: the v0.1.0-alpha layout (its AnselSDK64.dll proxy, built from the public tag, and AnselSDK64_real.dll) with dsound.dll copied over it:
#             exactly one copy of the mod starts (one "proxy loaded" line), the dsound.dll says OTHER COPY, the world is scanned, the game stays alive.
#             Mutant: a proxy that does not look for the old copy (dsound_mut_NO_OTHER_COPY.dll): two copies start, caught.
# The slot copy is put back to the current layout at the end (tools/install.sh A). Needs the resource provider (local/env.sh), as qa.sh does.
set -u
WITNESS_ALLOCATING=1 . "$(dirname "$0")/config.sh" || exit $?
if [ -x "$WITNESS_RESOURCES" ] && [ -z "${WITNESS_SLOTS:-}" ]; then
  "$WITNESS_RESOURCES" acquire --slots 1 --ttl 1800 --wait "${QA_WAIT:-0}" || { echo "no game slot free (resource provider status); not waiting"; exit 3; }
  trap '"$WITNESS_RESOURCES" release' EXIT
  "$WITNESS_RESOURCES" provision witness || exit 1
  unset WITNESS_DIR1 WITNESS_DIR2 WITNESS_PORT1 WITNESS_PORT2 WITNESS_RUN
  . "$(dirname "$0")/config.sh" || exit $?
elif [ -x "$WITNESS_RESOURCES" ]; then
  trap '"$WITNESS_RESOURCES" release' EXIT
  "$WITNESS_RESOURCES" provision witness || exit 1
fi
d="$WITNESS_DIR1"; work="$RUN/dropin"; rm -rf "$work"; mkdir -p "$work"
"$TOOLS/install.sh" A >/dev/null || exit 1
make -s -C "$ROOT/proxy" dsound_mut_NO_CHAIN.dll dsound_mut_NO_OTHER_COPY.dll >/dev/null || { echo "dropin: could not build the mutants"; exit 1; }
x86_64-w64-mingw32-g++ -shared -O1 -static-libgcc -o "$work/chain.dll" "$ROOT/proxy/tests/live_chain.cpp" || exit 1
git -C "$ROOT" archive v0.1.0-alpha proxy common | tar -x -C "$work" && make -s -C "$work/proxy" AnselSDK64.dll >/dev/null 2>&1 || { echo "dropin: could not build the v0.1.0-alpha proxy"; exit 1; }
cp "$ROOT/proxy/dsound.dll" "$work/dsound_production.dll"
put() { rm -f "$2"; cp "$1" "$2"; }      # replace, never write through a hard link
pid=''
start() {   # start <label>: a fresh log, the game, its scan; sets scanned, alive
  printf 'save_dir=instance\nshare_zip=1\nforce_active=1\ncap_fps=%s\n' "${WITNESS_FPS:-30}" > "$d/witness_coop.cfg"
  rm -f "$d/witness_proxy.log" "$d/witness_chain_loaded.txt"
  pid=$(timeout 30 "$PYW" -I "$(win "$TOOLS/win/launch.py")" "$(win "$d")" -skip_config_dialog -windowed -new_game | tail -1 | tr -d '\r')
  [ -n "$pid" ] || { echo "dropin: the game did not start"; exit 1; }
  echo "$pid" > "$RUN/pidA.txt"; [ -x "$WITNESS_RESOURCES" ] && "$WITNESS_RESOURCES" register-pid "$pid" >/dev/null 2>&1
  scanned=0
  for i in $(seq 1 60); do
    grep -q 'devmenu: 594 panels' "$d/witness_proxy.log" 2>/dev/null && { scanned=1; break; }
    [ "$(tasklist.exe /FI "PID eq $pid" /NH 2>/dev/null | grep -c witness64)" = 0 ] && break
    sleep 3
  done
  sleep 10
  alive=$(tasklist.exe /FI "PID eq $pid" /NH 2>/dev/null | grep -c witness64)
  loaded=$(grep -c '^proxy loaded: ' "$d/witness_proxy.log" 2>/dev/null); loaded=${loaded:-0}
  marker=$(tr -d '\r\n' 2>/dev/null < "$d/witness_chain_loaded.txt")
  cp "$d/witness_proxy.log" "$work/proxy-$1.log" 2>/dev/null
  echo "$1: scanned=$scanned alive=$alive proxies_started=$loaded chain_marker_pid=${marker:-none} (game pid $pid)"
}
stop() { taskkill.exe /PID "$pid" /F >/dev/null 2>&1; sleep 3; }      # our own registered game only
fail=0
# ---- chain
put "$work/chain.dll" "$d/dsound.chain.dll"
start chain; stop
[ "$marker" = "$pid" ] && [ "$scanned" = 1 ] && [ "$alive" = 1 ] && grep -q '^chain: loaded dsound.chain.dll' "$work/proxy-chain.log" \
  && echo "chain: PASS (the chain file was loaded into the game process; co-op started; alive)" || { echo "chain: FAIL"; fail=1; }
put "$ROOT/proxy/dsound_mut_NO_CHAIN.dll" "$d/dsound.dll"
start chain-mutant; stop
[ "$marker" = "$pid" ] && { echo "chain MUTANT SURVIVED (the file appeared although the chain file was ignored)"; fail=1; } || echo "chain mutant: caught (no chain file loaded)"
put "$work/dsound_production.dll" "$d/dsound.dll"; rm -f "$d/dsound.chain.dll" "$d/witness_chain_loaded.txt"
# ---- the alpha's proxy beside dsound.dll
mv "$d/AnselSDK64.dll" "$d/AnselSDK64_real.dll"; put "$work/proxy/AnselSDK64.dll" "$d/AnselSDK64.dll"
start oldcopy; stop
[ "$loaded" = 1 ] && [ "$scanned" = 1 ] && [ "$alive" = 1 ] && grep -q '^OTHER COPY: ' "$work/proxy-oldcopy.log" \
  && echo "old copy: PASS (one copy of the mod started, the dsound.dll stayed passive; alive)" || { echo "old copy: FAIL"; fail=1; }
put "$ROOT/proxy/dsound_mut_NO_OTHER_COPY.dll" "$d/dsound.dll"
start oldcopy-mutant; stop
{ [ "$loaded" -ge 2 ] || [ "$alive" = 0 ]; } && echo "old copy mutant: caught (two copies of the mod started)" || { echo "old copy MUTANT SURVIVED"; fail=1; }
# ---- back to the current layout
rm -f "$d/AnselSDK64.dll"; mv "$d/AnselSDK64_real.dll" "$d/AnselSDK64.dll"
"$TOOLS/install.sh" A >/dev/null || fail=1
echo "evidence: $work"
[ $fail = 0 ] && echo "dropin: PASS" || echo "dropin: FAIL"
exit $fail
