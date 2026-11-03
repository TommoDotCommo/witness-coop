#!/usr/bin/env bash
# Game-free test of dist/install-linux.sh on fixture game folders (WSL or Linux): install (dsound.dll, the game's files untouched), update, repair,
# refusals, uninstall; the migration from the old layout (proxy in AnselSDK64.dll, the game's file as AnselSDK64_real.dll), interrupted and
# resumed; another mod's dsound.dll kept as dsound.chain.dll; the Proton prefix override (user.reg of fixture prefixes) and its exact undo.
# Mutants that this suite must fail: a backup skipped; uninstall removing a file nobody installed; the game's Ansel DLL put back unverified;
# the old proxy left behind; the chain file ignored; the override written for the wrong exe; uninstall leaving the override.
#   tools/test_install_linux.sh <package folder (the unpacked zip)> <folder holding witness64_d3d11.exe and the original AnselSDK64.dll>
set -uo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
pkg="$(cd "${1:?package folder}" && pwd)"; src="$(cd "${2:?folder of the supported build}" && pwd)"
EXE_SHA=8d672d444df6a6df7130f25a517bdab1af92731fe2138dc5b324d519561d55a5; ANSEL_SHA=3b0677ae1e01fc17645c903c8322497785768145abbe3758364299fdaa8aff15
ansel="$src/AnselSDK64.dll"; [ "$(sha256sum "$ansel" | cut -d' ' -f1)" = "$ANSEL_SHA" ] || ansel="$src/AnselSDK64_real.dll"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
h() { sha256sum "$1" 2>/dev/null | cut -d' ' -f1; }
SEC='[Software\\Wine\\AppDefaults\\witness64_d3d11.exe\\DllOverrides]'

has_override() {    # the exe's DllOverrides section of a user.reg holds "dsound"="native,builtin" (ENVIRON keeps the doubled backslashes)
    S="$SEC" awk 'BEGIN { s = ENVIRON["S"] } index($0, s) == 1 { f = 1; next } /^\[/ { f = 0 } f && $0 == "\"dsound\"=\"native,builtin\"" { ok = 1 } END { exit !ok }' "$1"
}
suite() {   # suite <install script copy> -> 0 when every check passes
    local script="$1" n=0 d out rc reg
    ck() { n=$((n + 1)); if ! eval "$1"; then echo "    FAIL: $2" >&2; return 1; fi; }
    fixture() { d="$work/g-$1"; rm -rf "$d"; mkdir -p "$d"; cp "$src/witness64_d3d11.exe" "$d/"; [ "${2:-yes}" = no ] || cp "$ansel" "$d/AnselSDK64.dll"; }
    legacy() {  # legacy <name> [setup]: an alpha install (by hand, or by Setup with receipts and the backup)
        fixture "$1" no; printf 'MZ fixture: the co-op proxy of v0.1.0-alpha (real Ansel DLL %%s)' > "$d/AnselSDK64.dll"; cp "$ansel" "$d/AnselSDK64_real.dll"
        if [ "${2:-}" = setup ]; then
            cp "$ansel" "$d/AnselSDK64.dll.coop-original"
            printf '{"schema":1,"original":"%s","installed":"%s","pending":null}' "$ANSEL_SHA" "$(h "$d/AnselSDK64.dll")" > "$d/AnselSDK64.dll.coop-receipt.json"
            printf '{"schema":1,"original":null,"installed":"%s","pending":null}' "$ANSEL_SHA" > "$d/AnselSDK64_real.dll.coop-receipt.json"
        fi
    }
    newlayout() { [ "$(h "$1/dsound.dll")" = "$proxy" ] && [ "$(h "$1/AnselSDK64.dll")" = "$ANSEL_SHA" ] && [ ! -e "$1/AnselSDK64_real.dll" ] && [ ! -e "$1/AnselSDK64.dll.coop-receipt.json" ] && [ ! -e "$1/AnselSDK64.dll.coop-original" ] && [ ! -e "$1/AnselSDK64_real.dll.coop-receipt.json" ] && [ -f "$1/witness_launcher.exe" ]; }
    snap() { (cd "$1" && find . -type f | sort | while read -r f; do printf '%s=%s;' "$f" "$(h "$f")"; done); }
    proton() {  # proton <name> [user.reg text]: a Steam library with the game and its prefix
        local lib="$work/lib-$1"; rm -rf "$lib"; d="$lib/steamapps/common/The Witness"; mkdir -p "$d" "$lib/steamapps/compatdata/210970/pfx"
        cp "$src/witness64_d3d11.exe" "$d/"; cp "$ansel" "$d/AnselSDK64.dll"; reg="$lib/steamapps/compatdata/210970/pfx/user.reg"
        printf '%s' "${2:-WINE REGISTRY Version 2
;; All keys relative to \\\\User\\\\S-1-5-21-0-0-0-1000

#arch=win64

[Software\\\\Wine\\\\AppDefaults\\\\other.exe\\\\DllOverrides] 1700000000
\"dsound\"=\"builtin\"

[Software\\\\Wine\\\\Fonts] 1700000000
\"x\"=\"y\"
}" > "$reg"; cp "$reg" "$work/reg-before-$1"
    }
    # the script needs its package files beside it
    local pk="$work/pkg-$(basename "$script")"; rm -rf "$pk"; mkdir -p "$pk"; cp -r "$pkg"/dsound.dll "$pkg"/witness_launcher.exe "$pkg"/witness_models "$pk"/; cp "$script" "$pk/install-linux.sh"; chmod +x "$pk/install-linux.sh"
    local sh="$pk/install-linux.sh"
    local proxy; proxy=$(h "$pk/dsound.dll")
    ck '[ "$proxy" != "$ANSEL_SHA" ] && grep -a -q -F "witness-coop:proxy:dsound" "$pk/dsound.dll"' 'the package holds the dsound.dll proxy' || return 1

    fixture fresh
    out=$(sh "$sh" status "$d" 2>&1); ck 'echo "$out" | grep -q "supported build"' 'status names the supported build' || return 1
    out=$(sh "$sh" install "$d" 2>&1); rc=$?; ck '[ $rc = 0 ]' "install succeeds: $out" || return 1
    ck 'newlayout "$d"' 'dsound.dll in place, the game'"'"'s own AnselSDK64.dll untouched, no renamed copy' || return 1
    ck '[ ! -e "$d/dsound.dll.coop-original" ] && [ ! -e "$d/dsound.chain.dll" ]' 'nothing backed up or chained in a clean folder' || return 1
    ck 'echo "$out" | grep -q "WINEDLLOVERRIDES=\"dsound=n,b\" %command%" && echo "$out" | grep -q "run once under Proton"' 'no prefix: the launch option is printed, and why' || return 1
    ck '[ "$(h "$d/witness_launcher.exe")" = "$(h "$pk/witness_launcher.exe")" ] && [ "$(h "$d/witness_models/adventurer.glb")" = "$(h "$pk/witness_models/adventurer.glb")" ]' 'launcher and model installed' || return 1
    ck '[ "$(h "$d/witness64_d3d11.exe")" = "$EXE_SHA" ]' 'the game exe is untouched' || return 1
    ck '[ ! -e "$d/witness_coop.cfg" ]' 'no cfg is written' || return 1
    for f in dsound.dll witness_launcher.exe witness_models/adventurer.glb witness_models/adventurer.glb.txt; do ck '[ -f "$d/$f.coop-receipt.json" ]' "receipt for $f" || return 1; done
    ck 'grep -q "\"schema\":1" "$d/dsound.dll.coop-receipt.json" && grep -q "\"original\":null" "$d/dsound.dll.coop-receipt.json"' 'the receipt records that nothing was there (Windows setup format)' || return 1
    out=$(sh "$sh" install "$d" 2>&1); rc=$?; ck '[ $rc = 0 ] && newlayout "$d"' 'a second install is harmless' || return 1
    # update / repair
    printf 'older proxy' > "$d/dsound.dll"; printf '{"schema":1,"original":null,"installed":"%s","pending":null}' "$(h "$d/dsound.dll")" > "$d/dsound.dll.coop-receipt.json"
    sh "$sh" install "$d" >/dev/null 2>&1; ck '[ "$(h "$d/dsound.dll")" = "$proxy" ]' 'an older owned proxy is updated' || return 1
    rm "$d/witness_launcher.exe"; sh "$sh" install "$d" >/dev/null 2>&1; ck '[ -f "$d/witness_launcher.exe" ]' 'a missing file is repaired' || return 1
    # a file changed by hand is kept
    printf 'user modification' > "$d/dsound.dll"; out=$(sh "$sh" install "$d" 2>&1); rc=$?
    ck '[ $rc != 0 ] && [ "$(cat "$d/dsound.dll")" = "user modification" ]' 'a hand-modified proxy blocks the install and is kept' || return 1
    cp "$pk/dsound.dll" "$d/dsound.dll"
    # uninstall
    printf 'name=Tester\n' > "$d/witness_coop.cfg"; printf 'log' > "$d/witness_proxy.log"
    out=$(sh "$sh" uninstall "$d" 2>&1); rc=$?; ck '[ $rc = 0 ]' "uninstall succeeds: $out" || return 1
    ck '[ "$(h "$d/AnselSDK64.dll")" = "$ANSEL_SHA" ] && [ ! -e "$d/dsound.dll" ]' 'uninstall removes the proxy; the game'"'"'s file is as it was' || return 1
    ck '[ ! -e "$d/witness_launcher.exe" ] && [ ! -e "$d/witness_models" ]' 'uninstall removes what it installed' || return 1
    ck '[ ! -e "$d/dsound.dll.coop-receipt.json" ] && [ ! -e "$d/dsound.dll.coop-original" ]' 'uninstall retires receipts' || return 1
    ck 'grep -q Tester "$d/witness_coop.cfg" && [ -f "$d/witness_proxy.log" ]' 'the cfg and the log are kept' || return 1
    # another build: refused in plain words, nothing changed
    fixture other; printf '\001' | dd of="$d/witness64_d3d11.exe" bs=1 seek=100000 conv=notrunc 2>/dev/null
    out=$(sh "$sh" install "$d" 2>&1); rc=$?
    ck '[ $rc != 0 ] && echo "$out" | grep -q "Steam build 11370412" && echo "$out" | grep -q "Verify integrity" && echo "$out" | grep -q "Found:" && echo "$out" | grep -q "faq-not-the-supported-build"' 'another build: what was found, what is needed, what to do, the FAQ' || return 1
    ck '[ ! -e "$d/dsound.dll" ] && [ "$(h "$d/AnselSDK64.dll")" = "$ANSEL_SHA" ] && [ ! -e "$d/witness_launcher.exe" ]' 'another build: nothing changed' || return 1
    fixture trunc; head -c 4096 "$src/witness64_d3d11.exe" > "$d/witness64_d3d11.exe"; out=$(sh "$sh" install "$d" 2>&1); rc=$?
    ck '[ $rc != 0 ] && [ ! -e "$d/dsound.dll" ]' 'a truncated exe is refused' || return 1
    # an AnselSDK64.dll that is neither the game's nor the mod's old proxy: refused, nothing changed
    fixture noansel no; printf 'something else' > "$d/AnselSDK64.dll"; out=$(sh "$sh" install "$d" 2>&1); rc=$?
    ck '[ $rc != 0 ] && [ "$(cat "$d/AnselSDK64.dll")" = "something else" ] && [ ! -e "$d/dsound.dll" ] && echo "$out" | grep -q "neither the game"' 'an unknown AnselSDK64.dll: refused in words, nothing changed' || return 1
    # ---- migration from the old layout
    legacy mig-setup setup; out=$(sh "$sh" status "$d" 2>&1); ck 'echo "$out" | grep -q "older co-op install (install migrates it)"' 'status names the old layout' || return 1
    out=$(sh "$sh" install "$d" 2>&1); rc=$?
    ck '[ $rc = 0 ] && newlayout "$d" && echo "$out" | grep -q "the game'"'"'s own file is back"' 'old layout (made by Setup) -> new layout: the game'"'"'s own AnselSDK64.dll back, old proxy and receipts gone' || return 1
    legacy mig-hand; sh "$sh" install "$d" >/dev/null 2>&1; ck 'newlayout "$d"' 'old layout (by hand, no receipts) -> new layout' || return 1
    legacy mig-cut-a setup; cp "$ansel" "$d/AnselSDK64.dll"; sh "$sh" install "$d" >/dev/null 2>&1
    ck 'newlayout "$d"' 'a migration interrupted after the restore finishes on the next run' || return 1
    legacy mig-cut-b setup; cp "$ansel" "$d/AnselSDK64.dll.coop-stage"; sh "$sh" install "$d" >/dev/null 2>&1
    ck 'newlayout "$d" && [ ! -e "$d/AnselSDK64.dll.coop-stage" ]' 'a migration interrupted with the staged copy written finishes on the next run' || return 1
    legacy mig-uninstall setup; sh "$sh" uninstall "$d" >/dev/null 2>&1
    ck '[ "$(h "$d/AnselSDK64.dll")" = "$ANSEL_SHA" ] && [ ! -e "$d/AnselSDK64_real.dll" ]' 'uninstall of an old install puts the game'"'"'s own AnselSDK64.dll back' || return 1
    legacy mig-tampered setup; printf 'X' | dd of="$d/AnselSDK64_real.dll" bs=1 seek=100 conv=notrunc 2>/dev/null; rm "$d/AnselSDK64.dll.coop-original"
    local before; before=$(snap "$d"); out=$(sh "$sh" install "$d" 2>&1); rc=$?
    ck '[ $rc != 0 ] && [ "$(snap "$d")" = "$before" ] && echo "$out" | grep -q "is not the game'"'"'s own Ansel DLL"' 'a renamed copy that is not the game'"'"'s own: refused in words, nothing changed (never put back unverified)' || return 1
    # ---- another mod's dsound.dll
    fixture chain; printf 'MZ another mod' > "$d/dsound.dll"; out=$(sh "$sh" install "$d" 2>&1); rc=$?
    ck '[ $rc = 0 ] && [ "$(h "$d/dsound.dll")" = "$proxy" ] && [ "$(cat "$d/dsound.chain.dll")" = "MZ another mod" ] && [ -f "$d/dsound.chain.dll.coop-receipt.json" ]' 'another mod'"'"'s dsound.dll is kept as dsound.chain.dll' || return 1
    sh "$sh" install "$d" >/dev/null 2>&1; ck '[ "$(cat "$d/dsound.chain.dll")" = "MZ another mod" ]' 'a second install keeps the chain file' || return 1
    sh "$sh" uninstall "$d" >/dev/null 2>&1
    ck '[ "$(cat "$d/dsound.dll")" = "MZ another mod" ] && [ ! -e "$d/dsound.chain.dll" ] && [ ! -e "$d/dsound.dll.coop-original" ]' 'uninstall puts the other mod'"'"'s dsound.dll back and removes the chain copy' || return 1
    fixture ours; cp "$pk/dsound.dll" "$d/dsound.dll"; sh "$sh" install "$d" >/dev/null 2>&1
    ck 'newlayout "$d" && [ ! -e "$d/dsound.chain.dll" ]' 'this mod'"'"'s own dsound.dll (copied in by hand) is not chained' || return 1
    fixture clash; printf 'MZ one' > "$d/dsound.dll"; printf 'MZ two' > "$d/dsound.chain.dll"; before=$(snap "$d"); sh "$sh" install "$d" >/dev/null 2>&1; rc=$?
    ck '[ $rc != 0 ] && [ "$(snap "$d")" = "$before" ]' 'a different dsound.chain.dll already there: refused, nothing changed' || return 1
    # ---- Proton prefix: the override for the game's exe, a backup of user.reg, the exact undo
    proton pfx; out=$(sh "$sh" install "$d" 2>&1); rc=$?
    ck '[ $rc = 0 ] && newlayout "$d"' "install into a Steam library with a prefix: $out" || return 1
    ck 'has_override "$reg"' 'user.reg: "dsound"="native,builtin" in the DllOverrides of witness64_d3d11.exe' || return 1
    ck 'cmp -s "$reg.witness-coop-backup" "$work/reg-before-pfx"' 'user.reg was backed up before the change' || return 1
    ck 'grep -q "dsound\"=\"builtin\"" "$reg" && grep -q "Fonts" "$reg"' 'other keys of user.reg are kept' || return 1
    out=$(sh "$sh" status "$d" 2>&1); ck 'echo "$out" | grep -q "Proton override: in place"' 'status reports the override' || return 1
    sh "$sh" install "$d" >/dev/null 2>&1; sh "$sh" uninstall "$d" >/dev/null 2>&1
    ck 'cmp -s "$reg" "$work/reg-before-pfx" && [ ! -e "$d/witness-coop-proton.receipt" ]' 'uninstall undoes the override exactly (user.reg as before the install)' || return 1
    out=$(sh "$sh" status "$d" 2>&1); ck 'echo "$out" | grep -q "Proton override: missing"' 'status reports the override missing after uninstall' || return 1
    proton pfx-existing "WINE REGISTRY Version 2

$SEC 1700000000
\"d3d11\"=\"native\"
\"dsound\"=\"builtin\"
"
    sh "$sh" install "$d" >/dev/null 2>&1
    ck 'grep -q "^\"d3d11\"=\"native\"\$" "$reg" && grep -q "^\"dsound\"=\"native,builtin\"\$" "$reg" && [ "$(grep -c dsound "$reg")" = 1 ]' 'an existing section for the exe: the value is changed in place, other overrides kept' || return 1
    sh "$sh" uninstall "$d" >/dev/null 2>&1
    ck 'cmp -s "$reg" "$work/reg-before-pfx-existing"' 'uninstall puts the earlier value back exactly' || return 1
    # uninstall never removes a file nobody installed
    fixture unowned; printf 'unowned launcher' > "$d/witness_launcher.exe"
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
mutate original-not-verified 's/^is_original() { \[ -n "\$1" \] \&\& \[ "\$1" = "\$ANSEL_SHA" \]; }/is_original() { [ -n "$1" ]; }/'
mutate old-proxy-left-behind 's/^    migrate || true$/    :/'
mutate chain-file-ignored 's/^    chain_install || true$/    :/'
mutate override-wrong-exe '/MUT:exe/s/witness64_d3d11.exe/witness_d3d11.exe/'
mutate uninstall-leaves-override '/MUT:unoverride/c\            :'
exit $fail
