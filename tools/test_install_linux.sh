#!/usr/bin/env bash
# Game-free test of dist/install-linux.sh on fixture game folders (WSL or Linux): install with backups, update, repair, refusals, uninstall, and
# mutants (the Ansel DLL overwritten without a backup; uninstall removing a file nobody installed) that this suite must fail.
#   tools/test_install_linux.sh <package folder (the unpacked zip)> <folder holding witness64_d3d11.exe and the original AnselSDK64.dll>
set -uo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
pkg="$(cd "${1:?package folder}" && pwd)"; src="$(cd "${2:?folder of the supported build}" && pwd)"
EXE_SHA=8d672d444df6a6df7130f25a517bdab1af92731fe2138dc5b324d519561d55a5; ANSEL_SHA=3b0677ae1e01fc17645c903c8322497785768145abbe3758364299fdaa8aff15
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
h() { sha256sum "$1" 2>/dev/null | cut -d' ' -f1; }

suite() {   # suite <install script copy> -> 0 when every check passes
    local script="$1" n=0 d out rc
    ck() { n=$((n + 1)); if ! eval "$1"; then echo "    FAIL: $2" >&2; return 1; fi; }
    fixture() { d="$work/g-$1"; rm -rf "$d"; mkdir -p "$d"; cp "$src/witness64_d3d11.exe" "$d/"; [ "${2:-yes}" = no ] || cp "$src/AnselSDK64.dll" "$d/"; }
    # the script needs its package files beside it
    local pk="$work/pkg-$(basename "$script")"; rm -rf "$pk"; mkdir -p "$pk"; cp -r "$pkg"/AnselSDK64.dll "$pkg"/witness_launcher.exe "$pkg"/witness_models "$pk"/; cp "$script" "$pk/install-linux.sh"; chmod +x "$pk/install-linux.sh"
    local sh="$pk/install-linux.sh"
    local proxy; proxy=$(h "$pk/AnselSDK64.dll")
    ck '[ "$proxy" != "$ANSEL_SHA" ]' 'the package holds the proxy, not the original' || return 1

    fixture fresh
    out=$(sh "$sh" status "$d" 2>&1); ck 'echo "$out" | grep -q "supported build"' 'status names the supported build' || return 1
    out=$(sh "$sh" install "$d" 2>&1); rc=$?; ck '[ $rc = 0 ]' "install succeeds: $out" || return 1
    ck '[ "$(h "$d/AnselSDK64.dll")" = "$proxy" ] && [ "$(h "$d/AnselSDK64_real.dll")" = "$ANSEL_SHA" ]' 'proxy in place, original under the proxy name' || return 1
    ck '[ "$(h "$d/AnselSDK64.dll.coop-original")" = "$ANSEL_SHA" ]' 'the original Ansel DLL is backed up' || return 1
    ck '[ "$(h "$d/witness_launcher.exe")" = "$(h "$pk/witness_launcher.exe")" ] && [ "$(h "$d/witness_models/adventurer.glb")" = "$(h "$pk/witness_models/adventurer.glb")" ]' 'launcher and model installed' || return 1
    ck '[ "$(h "$d/witness64_d3d11.exe")" = "$EXE_SHA" ]' 'the game exe is untouched' || return 1
    ck '[ ! -e "$d/witness_coop.cfg" ]' 'no cfg is written' || return 1
    for f in AnselSDK64_real.dll AnselSDK64.dll witness_launcher.exe witness_models/adventurer.glb witness_models/adventurer.glb.txt; do ck '[ -f "$d/$f.coop-receipt.json" ]' "receipt for $f" || return 1; done
    ck 'grep -q "\"schema\":1" "$d/AnselSDK64.dll.coop-receipt.json" && grep -q "\"original\":\"$ANSEL_SHA\"" "$d/AnselSDK64.dll.coop-receipt.json"' 'the receipt records the original (Windows setup format)' || return 1
    out=$(sh "$sh" install "$d" 2>&1); rc=$?; ck '[ $rc = 0 ] && [ "$(h "$d/AnselSDK64.dll")" = "$proxy" ]' 'a second install is harmless' || return 1
    # update / repair
    printf 'older proxy' > "$d/AnselSDK64.dll"; printf '{"schema":1,"original":"%s","installed":"%s","pending":null}' "$ANSEL_SHA" "$(h "$d/AnselSDK64.dll")" > "$d/AnselSDK64.dll.coop-receipt.json"
    sh "$sh" install "$d" >/dev/null 2>&1; ck '[ "$(h "$d/AnselSDK64.dll")" = "$proxy" ]' 'an older owned proxy is updated' || return 1
    rm "$d/witness_launcher.exe"; sh "$sh" install "$d" >/dev/null 2>&1; ck '[ -f "$d/witness_launcher.exe" ]' 'a missing file is repaired' || return 1
    # a file changed by hand is kept
    printf 'user modification' > "$d/AnselSDK64.dll"; out=$(sh "$sh" install "$d" 2>&1); rc=$?
    ck '[ $rc != 0 ] && [ "$(cat "$d/AnselSDK64.dll")" = "user modification" ]' 'a hand-modified proxy blocks the install and is kept' || return 1
    cp "$pk/AnselSDK64.dll" "$d/AnselSDK64.dll"
    # uninstall
    printf 'name=Tester\n' > "$d/witness_coop.cfg"; printf 'log' > "$d/witness_proxy.log"
    out=$(sh "$sh" uninstall "$d" 2>&1); rc=$?; ck '[ $rc = 0 ]' "uninstall succeeds: $out" || return 1
    ck '[ "$(h "$d/AnselSDK64.dll")" = "$ANSEL_SHA" ]' 'uninstall puts the original Ansel DLL back' || return 1
    ck '[ ! -e "$d/AnselSDK64_real.dll" ] && [ ! -e "$d/witness_launcher.exe" ] && [ ! -e "$d/witness_models" ]' 'uninstall removes what it installed' || return 1
    ck '[ ! -e "$d/AnselSDK64.dll.coop-original" ] && [ ! -e "$d/AnselSDK64.dll.coop-receipt.json" ]' 'uninstall retires backups and receipts' || return 1
    ck 'grep -q Tester "$d/witness_coop.cfg" && [ -f "$d/witness_proxy.log" ]' 'the cfg and the log are kept' || return 1
    # another build: refused in plain words, nothing changed
    fixture other; printf '\001' | dd of="$d/witness64_d3d11.exe" bs=1 seek=100000 conv=notrunc 2>/dev/null
    out=$(sh "$sh" install "$d" 2>&1); rc=$?
    ck '[ $rc != 0 ] && echo "$out" | grep -q "Steam build 11370412" && echo "$out" | grep -q "Verify integrity" && echo "$out" | grep -q "Found:" && echo "$out" | grep -q "faq-not-the-supported-build"' 'another build: what was found, what is needed, what to do, the FAQ' || return 1
    ck '[ ! -e "$d/AnselSDK64_real.dll" ] && [ "$(h "$d/AnselSDK64.dll")" = "$ANSEL_SHA" ] && [ ! -e "$d/witness_launcher.exe" ]' 'another build: nothing changed' || return 1
    fixture trunc; head -c 4096 "$src/witness64_d3d11.exe" > "$d/witness64_d3d11.exe"; out=$(sh "$sh" install "$d" 2>&1); rc=$?
    ck '[ $rc != 0 ] && [ ! -e "$d/AnselSDK64_real.dll" ]' 'a truncated exe is refused' || return 1
    # the game's own Ansel DLL is not there
    fixture noansel no; printf 'something else' > "$d/AnselSDK64.dll"; sh "$sh" install "$d" >/dev/null 2>&1; rc=$?
    ck '[ $rc != 0 ] && [ "$(cat "$d/AnselSDK64.dll")" = "something else" ] && [ ! -e "$d/AnselSDK64_real.dll" ]' 'no original Ansel DLL: refused, nothing changed' || return 1
    # a hand-prepared copy (original already renamed, an older proxy in place)
    fixture manual no; cp "$src/AnselSDK64.dll" "$d/AnselSDK64_real.dll"; printf 'old proxy by hand' > "$d/AnselSDK64.dll"
    sh "$sh" install "$d" >/dev/null 2>&1; ck '[ "$(h "$d/AnselSDK64.dll")" = "$proxy" ]' 'a hand-prepared copy installs' || return 1
    ck '[ "$(cat "$d/AnselSDK64.dll.coop-original")" = "old proxy by hand" ]' 'the hand-made proxy was backed up first' || return 1
    sh "$sh" uninstall "$d" >/dev/null 2>&1
    ck '[ "$(cat "$d/AnselSDK64.dll")" = "old proxy by hand" ] && [ "$(h "$d/AnselSDK64_real.dll")" = "$ANSEL_SHA" ]' 'uninstall restores exactly what was there' || return 1
    # uninstall never removes a file nobody installed
    fixture unowned; printf 'unowned launcher' > "$d/witness_launcher.exe"; printf 'unowned model' > "$d/witness_models.txt"
    sh "$sh" uninstall "$d" >/dev/null 2>&1
    ck '[ "$(cat "$d/witness_launcher.exe")" = "unowned launcher" ] && [ "$(h "$d/AnselSDK64.dll")" = "$ANSEL_SHA" ]' 'uninstall keeps an unowned launcher' || return 1
    # a symbolic link in the way is refused
    fixture link; printf 'elsewhere' > "$work/elsewhere.txt"; ln -s "$work/elsewhere.txt" "$d/witness_launcher.exe"
    sh "$sh" install "$d" >/dev/null 2>&1; ck '[ "$(cat "$work/elsewhere.txt")" = "elsewhere" ]' 'a link is never written through' || return 1
    echo "    $n checks"
    return 0
}

fail=0
echo "install-linux.sh:"; suite "$here/dist/install-linux.sh" && echo "  PASS" || { echo "  FAIL"; fail=1; }
mutate() {  # <name> <sed expression>
    sed "$2" "$here/dist/install-linux.sh" > "$work/mut-$1.sh"
    cmp -s "$work/mut-$1.sh" "$here/dist/install-linux.sh" && { echo "  mutant $1: the mutation did not apply"; fail=1; return; }
    if suite "$work/mut-$1.sh" >/dev/null 2>&1; then echo "  MUTANT $1 SURVIVED"; fail=1; else echo "  mutant $1 killed"; fi
}
mutate no-backup '/MUT:backup/c\            :'
mutate no-backup-but-checked '/# MUT:backup$/c\            :'
mutate removes-unowned '/MUT:owned/c\        if false; then'
exit $fail
