#!/bin/bash
# One INSTALLED copy starts (T1, one slot): the copy is put together by the real Setup verb (Witness-Coop-Setup.exe install <folder> --yes), has no
# witness_coop.cfg beside the exe, and finds its settings in the per-user folder (co-op standard J4). LOCALAPPDATA is a scratch folder for this one
# process, so the owner's real profile is never read or written; the per-user cfg points the saves and the logs into scratch folders too.
# Requires, from state in the files the game process wrote: the proxy log is in the per-user folder with this game's pid, the settings of the per-user cfg
# took effect (the save folder redirected, the archive shared), nothing named witness_* appeared in the game folder, and the game is still alive 20 s later.
#   tools/userdata_live.sh <Witness-Coop-Setup.exe (the one-file setup of a release)>
set -u
setup="${1:?give the one-file Witness-Coop-Setup.exe}"
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
d="$WITNESS_DIR1"
# an installed copy starts from nothing of ours: remove the lab's private files (the slot folder is disposable), then let Setup install
rm -rf "$d/witness_models" "$d/witness_coop.cfg" "$d/witness_proxy.log" "$d/witness_proxy.previous.log" "$d/dsound.dll" "$d/witness_launcher.exe" "$d/dsound.chain.dll" "$d"/*.coop-*
out="$("$setup" install "$(win "$d")" --yes 2>&1 | tr -d '\r')"; rc=$?
[ "$rc" = 0 ] && [ -f "$d/dsound.dll.coop-receipt.json" ] || { echo "userdata_live: Setup install failed ($rc): $out"; exit 1; }
lad="$RUN/live-localappdata"; rm -rf "$lad"; mkdir -p "$lad/witness-coop" "$lad/saves" "$lad/logs"
printf 'save_dir=%s\nshare_zip=1\nforce_active=1\ncap_fps=%s\nvoice_mode=off\nvoice_mute_output=1\ndiag_dir=%s\n' "$(win "$lad/saves")" "${WITNESS_FPS:-30}" "$(win "$lad/logs")" > "$lad/witness-coop/witness_coop.cfg"
before="$(cd "$d" && find . -maxdepth 1 -name 'witness_*' | sort | tr '\n' ' ')"
export WSLENV="LOCALAPPDATA${WSLENV:+:$WSLENV}"
pid=$(LOCALAPPDATA="$(win "$lad")" timeout 30 "$PYW" -I "$(win "$TOOLS/win/launch.py")" "$(win "$d")" -skip_config_dialog -windowed -new_game | tail -1 | tr -d '\r')
[ -n "$pid" ] || { echo "userdata_live: the game did not start"; exit 1; }
echo "$pid" > "$RUN/pidA.txt"; [ -x "$WITNESS_RESOURCES" ] && "$WITNESS_RESOURCES" register-pid "$pid" >/dev/null 2>&1
alive() { tasklist.exe /FI "PID eq $pid" /NH 2>/dev/null | grep -c witness64; }
log="$lad/witness-coop/witness_proxy.log"; scanned=0
for i in $(seq 1 60); do
  grep -q 'devmenu: 594 panels' "$log" 2>/dev/null && { scanned=$i; break; }
  [ "$(alive)" = 0 ] && break
  sleep 3
done
sleep 20
a=$(alive)
after="$(cd "$d" && find . -maxdepth 1 -name 'witness_*' | sort | tr '\n' ' ')"
fail=0
ck() { if ! eval "$1"; then echo "userdata_live: FAIL: $2"; fail=1; fi; }
ck '[ -f "$log" ] && grep -q "^proxy loaded: pid=$pid " "$log"' "the proxy log is in the per-user folder and names this game's pid ($pid)"
ck 'grep -q "^hooks: SHGetFolderPathW(csidl 0x1a) -> redirected" "$log"' "the per-user cfg was read: the save folder is redirected"
ck 'grep -q "^hooks: CreateFileA opens data-pc.zip .* -> FILE_SHARE_READ added" "$log"' "the per-user cfg was read: the archive is shared"
ck '[ ! -e "$d/witness_proxy.log" ] && [ ! -e "$d/witness_coop.cfg" ]' "nothing written into the game folder: no log, no cfg"
ck '[ "$before" = "$after" ]' "no witness_* file appeared beside the exe while the game ran (before: $before | after: $after)"
ck '[ "$scanned" != 0 ] && [ "$a" = 1 ]' "the world was scanned and the game is still alive"
ck '[ -n "$(ls "$lad/logs" 2>/dev/null)" ]' "the session logs went where diag_dir says"
echo "userdata_live: pid=$pid scanned_at=$((scanned * 3))s alive=$a log=$log"
if [ $fail = 0 ]; then echo "userdata_live: PASS (an installed copy runs on its per-user settings and writes nothing beside the exe)"; exit 0; fi
grep -vE '^uiglyph' "$log" 2>/dev/null | tail -5; exit 1
