#!/bin/bash
# Live check (one slot) of what the mod does when the game exe is not the build it knows, e.g. after a game update.
# The slot copy gets the current proxy; then ITS exe is replaced by a private copy with one byte changed inside the code section (the copy under the
# lab's base folder is never touched; the owner's real install and saves are never involved). The game is started the way a player does and must:
#   - keep running (the mod must not crash it), with the game's own Ansel DLL still in the path,
#   - show exactly one message box, "The Witness Co-op", and
#   - patch nothing and start nothing of the mod: no hook, no overlay, no world scan in the proxy log.
# Then the same with a proxy whose build check is ignored (make -C proxy AnselSDK64_mut_gate.dll): that run must hook the game, which this script reports as a
# test failure of the mutant, i.e. the check is meaningful.
#   tools/unknown_build_slot.sh          exit 0 = the unknown build was left alone and the mutant was caught
set -u
WITNESS_ALLOCATING=1 . "$(dirname "$0")/config.sh" || exit $?
if [ -x "$WITNESS_RESOURCES" ] && [ -z "${WITNESS_SLOTS:-}" ]; then
  "$WITNESS_RESOURCES" acquire --slots 1 --ttl 1200 --wait "${QA_WAIT:-0}" || { echo "no game slot free; not waiting"; exit 3; }
  trap '"$WITNESS_RESOURCES" release' EXIT
  "$WITNESS_RESOURCES" provision witness || exit 1
  unset WITNESS_DIR1 WITNESS_DIR2 WITNESS_PORT1 WITNESS_PORT2 WITNESS_RUN
  . "$(dirname "$0")/config.sh" || exit $?
elif [ -x "$WITNESS_RESOURCES" ]; then
  trap '"$WITNESS_RESOURCES" release' EXIT
  "$WITNESS_RESOURCES" provision witness || exit 1
fi
# whatever happens, the slot gets its real exe back (a hard link to the shared base copy, as the resource provider makes it) before the slot is released
restore_exe() { d="$WITNESS_DIR1"; base="$(dirname "$d")/base/witness64_d3d11.exe"; if [ -f "$base" ] && [ "$(sha256sum "$d/witness64_d3d11.exe" | cut -d' ' -f1)" != "$EXE_SHA256" ]; then rm -f "$d/witness64_d3d11.exe"; ln "$base" "$d/witness64_d3d11.exe" 2>/dev/null || cp "$base" "$d/witness64_d3d11.exe"; fi; }
trap 'restore_exe; [ -x "$WITNESS_RESOURCES" ] && "$WITNESS_RESOURCES" release' EXIT
"$TOOLS/install.sh" A >/dev/null || exit 1                 # checks the exe hash while the exe is still the real one
make -s -C "$ROOT/proxy" AnselSDK64_mut_gate.dll >/dev/null || { echo "could not build the mutant proxy"; exit 1; }
d="$WITNESS_DIR1"
# one byte changed in the last bytes of the code section (padding: never executed), in a private copy of the exe
python3 -I - "$d/witness64_d3d11.exe" <<'PY'
import os, struct, sys
path = sys.argv[1]
data = bytearray(open(path, 'rb').read())
pe, = struct.unpack_from('<I', data, 0x3c); nsec, = struct.unpack_from('<H', data, pe + 6); osz, = struct.unpack_from('<H', data, pe + 20)
s = pe + 24 + osz                                   # first section is .text
vs, va, rs, rp = struct.unpack_from('<IIII', data, s + 8)
at = rp + min(vs, rs) - 3
print('patching exe byte at file offset 0x%x (was 0x%02x)' % (at, data[at]))
data[at] ^= 0x01
os.remove(path)                                     # the slot's exe may be a hard link to the shared base: never write through it
open(path, 'wb').write(data)
PY
sha_now=$(sha256sum "$d/witness64_d3d11.exe" | cut -d' ' -f1)
[ "$sha_now" != "$EXE_SHA256" ] || { echo "the exe was not changed"; exit 1; }
echo "slot exe SHA-256 now ${sha_now:0:12}... (not the supported build)"
printf 'save_dir=instance\nshare_zip=1\nforce_active=1\ncap_fps=%s\n' "${WITNESS_FPS:-30}" > "$d/witness_coop.cfg"

run_case() {    # <label> -> sets: alive boxes hooks scanned
  rm -f "$d/witness_proxy.log"
  pid=$(timeout 30 "$PYW" -I "$(win "$TOOLS/win/launch.py")" "$(win "$d")" -skip_config_dialog -windowed -new_game | tail -1 | tr -d '\r')
  [ -n "$pid" ] || { echo "$1: the game did not start"; return 1; }
  echo "$pid" > "$RUN/pidA.txt"; [ -x "$WITNESS_RESOURCES" ] && "$WITNESS_RESOURCES" register-pid "$pid" >/dev/null 2>&1
  for i in $(seq 1 40); do grep -q 'BUILD: message box shown\|devmenu: 594 panels' "$d/witness_proxy.log" 2>/dev/null && break; sleep 3; done
  sleep 8
  boxes=$(powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(win "$TOOLS/win/wmsgbox.ps1")" -ProcessId "$pid" -CloseTitle "The Witness Co-op" | tr -d '\r' | grep -c '^window|The Witness Co-op|visible')
  sleep 12      # the game has to keep running after the box is closed (and before: the box is modal only to its own thread)
  alive=$(tasklist.exe /FI "PID eq $pid" /NH 2>/dev/null | grep -c witness64)
  hooks=$(grep -c '^hooks:\|^devmenu:\|^overlay\|^coop:' "$d/witness_proxy.log" 2>/dev/null)
  unknown=$(grep -c '^BUILD: not Steam build 11370412' "$d/witness_proxy.log" 2>/dev/null)
  cp "$d/witness_proxy.log" "$RUN/proxy-$1.log" 2>/dev/null
  echo "$1: alive=$alive message_boxes=$boxes mod_log_lines_from_hooked_parts=$hooks unknown_build_lines=$unknown"
  taskkill.exe /PID "$pid" /F >/dev/null 2>&1      # our own registered pid only
  sleep 3
}
fail=0
run_case production || exit 1
[ "$alive" = 1 ] && [ "$boxes" = 1 ] && [ "$hooks" = 0 ] && [ "$unknown" = 1 ] && echo "unknown build, production proxy: PASS (game alive, one message box, nothing hooked or started)" || { echo "unknown build, production proxy: FAIL"; fail=1; }
cp "$d/AnselSDK64.dll" "$RUN/AnselSDK64_production.dll"; cp "$ROOT/proxy/AnselSDK64_mut_gate.dll" "$d/AnselSDK64.dll"
run_case mutant || true
if [ "$hooks" -gt 0 ] && [ "$unknown" = 0 ]; then echo "unknown build, mutant (check ignored): CAUGHT (the mod hooked an unknown build, so the check is what prevents it)"; else echo "unknown build, mutant: SURVIVED (the test cannot tell)"; fail=1; fi
cp "$RUN/AnselSDK64_production.dll" "$d/AnselSDK64.dll"
exit $fail
