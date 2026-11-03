#!/bin/sh
# witness-coop for The Witness on Linux and the Steam Deck (Proton): install and uninstall by hand-made steps, with backups.
# Plain POSIX sh. It does what Witness-Coop-Setup.exe does on Windows, with the same ownership receipts, so either can uninstall the other's install.
#   ./install-linux.sh install   [GAME_FOLDER]   put the mod into the game folder (the folder with witness64_d3d11.exe)
#   ./install-linux.sh uninstall [GAME_FOLDER]   put the game's own files back; only files this script or the Windows setup installed are touched
#   ./install-linux.sh status    [GAME_FOLDER]   say what is there
# The mod is dsound.dll in the game folder (the game's own files are never changed). Proton loads its own dsound unless told otherwise, so the
# install also writes one DLL override for the game's exe into the game's Proton prefix (steamapps/compatdata/210970/pfx/user.reg), with a
# backup of user.reg and an exact undo on uninstall. Run it with the game closed: Proton rewrites user.reg when the game exits.
# A dsound.dll that was in the game folder before (another mod) is kept as dsound.chain.dll; the proxy loads it after itself.
# An older install (the proxy in AnselSDK64.dll, the game's file renamed AnselSDK64_real.dll) is migrated: the game's own file goes back, verified.
# Needs: sh, sha256sum (or shasum), cp, mv, sed, grep, awk. Nothing is changed outside the game folder but that one registry value.
# Untested under Proton on a real Steam Deck as of this release: tested on fixture folders in WSL (tools/test_install_linux.sh).
set -u
EXE_SHA=8d672d444df6a6df7130f25a517bdab1af92731fe2138dc5b324d519561d55a5       # witness64_d3d11.exe, Steam build 11370412
ANSEL_SHA=3b0677ae1e01fc17645c903c8322497785768145abbe3758364299fdaa8aff15     # the game's own AnselSDK64.dll
FAQ=https://github.com/TommoDotCommo/witness-coop#faq-not-the-supported-build
here=$(cd "$(dirname "$0")" && pwd)
MANAGED="dsound.dll witness_launcher.exe witness_models/adventurer.glb witness_models/adventurer.glb.txt"
CHAIN=dsound.chain.dll
PROXY_MARKER='witness-coop:proxy:dsound'      # every dsound.dll of this mod carries it; anything else of that name is another mod's
OLD_MARKER='real Ansel DLL'                   # the AnselSDK64.dll proxy of v0.1.0-alpha and earlier
APPID=210970
PROTON_EXE=witness64_d3d11.exe                # the exe the override is for   # MUT:exe
OVERRIDE_DLL=dsound
OVERRIDE_VALUE='native,builtin'
PROTON_RECEIPT=witness-coop-proton.receipt    # in the game folder: which prefix was changed and what was there before

say() { printf '%s\n' "$*"; }
die() { printf '\n%s\n' "$*" >&2; exit 1; }

hash_of() {     # hash_of FILE -> lower-case sha256, empty when the file is absent
    [ -f "$1" ] || { printf ''; return 0; }
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
    else shasum -a 256 "$1" | cut -d' ' -f1; fi
}
refuse_link() { for p in "$@"; do [ ! -L "$p" ] || die "Refusing to touch '$p': it is a symbolic link. Nothing was changed."; done; }
contains() { [ -f "$1" ] && LC_ALL=C grep -a -q -F "$2" "$1"; }
is_original() { [ -n "$1" ] && [ "$1" = "$ANSEL_SHA" ]; }      # verify-original

find_game() {
    for lib in "$HOME/.local/share/Steam" "$HOME/.steam/steam" "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam"; do
        [ -d "$lib/steamapps" ] || continue
        [ -f "$lib/steamapps/common/The Witness/witness64_d3d11.exe" ] && { printf '%s' "$lib/steamapps/common/The Witness"; return 0; }
        if [ -f "$lib/steamapps/libraryfolders.vdf" ]; then      # other libraries (an SD card, another drive)
            for extra in $(sed -n 's/.*"path"[[:space:]]*"\([^"]*\)".*/\1/p' "$lib/steamapps/libraryfolders.vdf" | sed 's/ /%20/g'); do
                extra=$(printf '%s' "$extra" | sed 's/%20/ /g')
                [ -f "$extra/steamapps/common/The Witness/witness64_d3d11.exe" ] && { printf '%s' "$extra/steamapps/common/The Witness"; return 0; }
            done
        fi
    done
    return 1
}

check_build() {     # the exe must be the one build the mod fits; otherwise say what was found and what to do
    exe_hash=$(hash_of "$game/witness64_d3d11.exe")
    [ -n "$exe_hash" ] || die "No witness64_d3d11.exe in '$game'. Give the folder of The Witness (the one that holds witness64_d3d11.exe)."
    if [ "$exe_hash" != "$EXE_SHA" ]; then
        die "This copy of The Witness is not Steam build 11370412, the build the co-op mod is made for. Nothing was changed.
Found:  witness64_d3d11.exe with SHA-256 starting $(printf '%s' "$exe_hash" | cut -c1-12)
Needed: SHA-256 starting $(printf '%s' "$EXE_SHA" | cut -c1-12) (Steam build 11370412)
What to do: in Steam, right-click The Witness > Properties > Betas > None, then Properties > Installed Files > Verify integrity of game files, then run this again.
Copies from other stores are not supported yet. More: $FAQ"
    fi
}

# ---- receipts: one small JSON file beside each managed file, the format the Windows setup writes too
rcpt_field() {  # rcpt_field FILE NAME -> value (empty for null or absent)
    sed -n "s/.*\"$2\":\"\\([a-f0-9]*\\)\".*/\\1/p" "$1" 2>/dev/null | head -n 1
}
rcpt_write() {  # rcpt_write TARGET ORIGINAL INSTALLED PENDING
    o='null'; [ -n "$2" ] && o="\"$2\""; p='null'; [ -n "$4" ] && p="\"$4\""
    printf '{"schema":1,"original":%s,"installed":"%s","pending":%s}' "$o" "$3" "$p" > "$1.coop-receipt.json.tmp" || die "Could not write a receipt in the game folder."
    mv -f "$1.coop-receipt.json.tmp" "$1.coop-receipt.json"
}

install_file() {    # install_file TARGET SOURCE
    target=$1; source=$2; installed=''; original=''; pending=''
    refuse_link "$target" "$target.coop-original" "$target.coop-receipt.json" "$target.coop-stage" "$(dirname "$target")"
    [ ! -e "$target.coop-stage" ] && [ ! -e "$target.coop-receipt.json.tmp" ] || die "A recovery file from an interrupted run is next to $target. Look at it and remove it by hand, then run this again."
    expected=$(hash_of "$source"); observed=$(hash_of "$target"); backup=$(hash_of "$target.coop-original")
    receipt="$target.coop-receipt.json"
    if [ -f "$receipt" ]; then
        installed=$(rcpt_field "$receipt" installed); original=$(rcpt_field "$receipt" original); pending=$(rcpt_field "$receipt" pending)
        [ -n "$installed" ] || die "The ownership receipt $receipt is unreadable. Nothing was changed."
        if [ -n "$original" ] && [ "$backup" != "$original" ]; then die "The backup of the original $target is missing or changed. Nothing was changed."; fi
        if [ -z "$original" ] && [ -n "$backup" ]; then die "Unexpected backup next to $target. Nothing was changed."; fi
        if [ -n "$observed" ] && [ "$observed" = "$expected" ]; then say "  up to date: ${target#"$game"/}"; return 0; fi
        if [ -n "$observed" ] && [ "$observed" != "$installed" ] && [ "$observed" != "$pending" ] && [ "$observed" != "$original" ]; then
            die "$target was changed by hand; it is kept. Nothing more was changed."
        fi
    else
        original=''; [ -z "$backup" ] || die "There is a backup file next to $target that this setup did not make. Nothing was changed."
        if [ -n "$observed" ]; then
            cp -p "$target" "$target.coop-original" || die "Could not back up $target."      # MUT:backup
            [ "$(hash_of "$target.coop-original")" = "$observed" ] || die "The backup of $target does not match. Nothing was changed."      # MUT:backup2
            original=$observed
        fi
    fi
    mkdir -p "$(dirname "$target")"
    rcpt_write "$target" "$original" "${installed:-$expected}" "$expected"
    cp "$source" "$target.coop-stage" || die "Could not write into the game folder."
    [ "$(hash_of "$target.coop-stage")" = "$expected" ] || { rm -f "$target.coop-stage"; die "A copy did not verify. Nothing was replaced."; }
    mv -f "$target.coop-stage" "$target"
    rcpt_write "$target" "$original" "$expected" ""
    say "  ${target#"$game"/}"
}

# ---- the old layout (up to v0.1.0-alpha): is it there, can it be undone safely, where is the game's own Ansel DLL (verified by hash)?
legacy_check() {    # sets legacy (0/1), legacy_block (message or empty), legacy_src (a verified copy of the game's file)
    a="$game/AnselSDK64.dll"; r="$game/AnselSDK64_real.dll"; legacy=0; legacy_block=''; legacy_src=''
    refuse_link "$a" "$r" "$a.coop-original" "$a.coop-receipt.json" "$a.coop-stage" "$r.coop-receipt.json"
    ha=$(hash_of "$a"); hr=$(hash_of "$r"); hb=$(hash_of "$a.coop-original"); hs=$(hash_of "$a.coop-stage")
    if [ -n "$hr$hb$hs" ] || [ -f "$a.coop-receipt.json" ] || [ -f "$r.coop-receipt.json" ] || { [ -n "$ha" ] && [ "$ha" != "$ANSEL_SHA" ]; }; then legacy=1; else return 0; fi
    help="Nothing was changed. In Steam: right-click The Witness > Properties > Installed Files > Verify integrity of game files, then run this again."
    for pair in "AnselSDK64_real.dll:$hr" "AnselSDK64.dll.coop-original:$hb" "AnselSDK64.dll.coop-stage:$hs"; do
        n=${pair%%:*}; v=${pair#*:}
        if [ -n "$v" ] && ! is_original "$v"; then
            legacy_block="$n is not the game's own Ansel DLL (SHA-256 starting $(printf '%s' "$v" | cut -c1-12), expected $(printf '%s' "$ANSEL_SHA" | cut -c1-12)). It will not be put back as AnselSDK64.dll. $help"; return 0
        fi
    done
    if [ -n "$ha" ] && ! is_original "$ha"; then
        if ! contains "$a" "$OLD_MARKER" && [ "$(rcpt_field "$a.coop-receipt.json" installed)" != "$ha" ]; then
            legacy_block="AnselSDK64.dll is neither the game's own file nor this mod's old proxy (SHA-256 starting $(printf '%s' "$ha" | cut -c1-12)). $help"; return 0
        fi
    fi
    for pair in "$a:$ha" "$r:$hr" "$a.coop-original:$hb" "$a.coop-stage:$hs"; do
        if is_original "${pair##*:}"; then legacy_src=${pair%:*}; break; fi
    done
    [ -n "$legacy_src" ] || legacy_block="The game's own AnselSDK64.dll was not found (expected SHA-256 starting $(printf '%s' "$ANSEL_SHA" | cut -c1-12)), so the old co-op layout cannot be undone. $help"
}
migrate() {         # the game's own AnselSDK64.dll back under its name (staged, verified), then the old pieces go; repeatable after an interruption
    legacy_check
    [ "$legacy" = 1 ] || return 1
    [ -z "$legacy_block" ] || die "An older co-op install is in '$game', and it cannot be undone safely:
$legacy_block"
    a="$game/AnselSDK64.dll"; r="$game/AnselSDK64_real.dll"
    if ! is_original "$(hash_of "$a")"; then
        if [ "$legacy_src" != "$a.coop-stage" ]; then
            cp "$legacy_src" "$a.coop-stage" || die "Could not write into the game folder."
            is_original "$(hash_of "$a.coop-stage")" || { rm -f "$a.coop-stage"; die "A copy of the game's AnselSDK64.dll did not verify. Nothing was replaced."; }
        fi
        mv -f "$a.coop-stage" "$a"      # MUT:restore
    fi
    is_original "$(hash_of "$a")" || die "AnselSDK64.dll could not be restored; the old files were kept."
    rm -f "$a.coop-receipt.json" "$r.coop-receipt.json" "$a.coop-receipt.json.tmp" "$r.coop-receipt.json.tmp"
    for p in "$r" "$a.coop-original" "$a.coop-stage"; do
        if [ -f "$p" ] && is_original "$(hash_of "$p")"; then rm -f "$p"; fi
    done
    say "  AnselSDK64.dll: the game's own file is back (verified); the old co-op proxy is removed"
    return 0
}
chain_install() {   # another mod's dsound.dll is kept as dsound.chain.dll (the proxy loads it); uninstall puts it back
    t="$game/dsound.dll"; c="$game/$CHAIN"
    refuse_link "$t" "$c"
    [ -f "$t" ] || return 1
    [ ! -f "$t.coop-receipt.json" ] || return 1      # installed by this setup
    contains "$t" "$PROXY_MARKER" && return 1         # this mod's own, copied in by hand or by a mod manager
    if [ -f "$c" ] && [ ! -f "$c.coop-receipt.json" ] && [ "$(hash_of "$c")" != "$(hash_of "$t")" ]; then
        die "$CHAIN is already in the game folder and differs from dsound.dll; move one of them away and run this again. Nothing was changed."
    fi
    install_file "$c" "$t"
    say "  $CHAIN: the dsound.dll that was here (another mod) is kept and loaded by the co-op proxy"
    return 0
}

# ---- Proton: the game's prefix must load the dsound.dll in the game folder ("native") before its own ("builtin")
proton_userreg() {  # the game's prefix user.reg, from the library the game folder is in; empty when the game never ran under Proton
    lib=$(cd "$game/../../.." 2>/dev/null && pwd) || return 0
    [ -f "$lib/steamapps/compatdata/$APPID/pfx/user.reg" ] && printf '%s' "$lib/steamapps/compatdata/$APPID/pfx/user.reg"
    return 0
}
override_section() { printf '[Software\\\\Wine\\\\AppDefaults\\\\%s\\\\DllOverrides]' "$PROTON_EXE"; }
override_get() {    # override_get USERREG -> the value of "dsound" in the exe's DllOverrides section; "<absent>" or "<nosection>"
    OV_SEC="$(override_section)" OV_KEY="\"$OVERRIDE_DLL\"" awk '      # ENVIRON, not -v: -v would eat the doubled backslashes
        BEGIN { sec = ENVIRON["OV_SEC"]; key = ENVIRON["OV_KEY"] }
        index($0, sec) == 1 { ins = 1; found = 1; next }
        /^\[/ { ins = 0 }
        ins && index($0, key "=") == 1 { v = substr($0, length(key) + 2); gsub(/^"|"$/, "", v); print v; got = 1; exit }
        END { if (!got) print (found ? "<absent>" : "<nosection>") }' "$1"
}
override_set() {    # override_set USERREG VALUE|<absent>|<nosection> : rewrite user.reg through a temporary file
    OV_SEC="$(override_section)" OV_KEY="\"$OVERRIDE_DLL\"" OV_VAL="$2" OV_NOW="$(date +%s)" awk '
        BEGIN { sec = ENVIRON["OV_SEC"]; key = ENVIRON["OV_KEY"]; val = ENVIRON["OV_VAL"]; now = ENVIRON["OV_NOW"] }
        function emit_value() { if (val != "<absent>" && val != "<nosection>") print key "=\"" val "\"" }
        index($0, sec) == 1 {
            if (val == "<nosection>") { drop = 1; pend = ""; next }      # the section this script made goes, with the blank line before it
            if (pend != "") { printf "%s", pend; pend = "" }
            print; ins = 1; found = 1; done = 0; next }
        /^\[/ { if (ins && !done) emit_value(); ins = 0; drop = 0 }
        drop { next }
        ins && index($0, key "=") == 1 { emit_value(); done = 1; next }
        /^$/ { pend = pend "\n"; next }
        { if (pend != "") { printf "%s", pend; pend = "" }; print }
        END {
            if (ins && !done) emit_value()
            if (pend != "") printf "%s", pend
            if (!found && val != "<absent>" && val != "<nosection>") { print ""; print sec " " now; emit_value() }
        }' "$1" > "$1.witness-coop-tmp" && mv -f "$1.witness-coop-tmp" "$1"
}
proton_fallback() {
    say "  Proton: no prefix for The Witness was found (it appears after the game has run once under Proton)."
    say "  Either start The Witness once from Steam and run this again, or set this launch option in Steam (The Witness > Properties > Launch options):"
    say "    WINEDLLOVERRIDES=\"$OVERRIDE_DLL=n,b\" %command%"
}
proton_install() {
    reg=$(proton_userreg)
    [ -n "$reg" ] || { proton_fallback; return 0; }
    refuse_link "$reg" "$game/$PROTON_RECEIPT"
    before=$(override_get "$reg")
    if [ "$before" = "$OVERRIDE_VALUE" ]; then say "  Proton: the dsound override is already in place ($reg)"; return 0; fi
    cp -p "$reg" "$reg.witness-coop-backup" || die "Could not back up $reg. Nothing was changed."
    if [ ! -f "$game/$PROTON_RECEIPT" ]; then
        printf 'userreg=%s\nexe=%s\ndll=%s\nbefore=%s\n' "$reg" "$PROTON_EXE" "$OVERRIDE_DLL" "$before" > "$game/$PROTON_RECEIPT.tmp" && mv -f "$game/$PROTON_RECEIPT.tmp" "$game/$PROTON_RECEIPT" || die "Could not write $PROTON_RECEIPT."
    fi
    override_set "$reg" "$OVERRIDE_VALUE" || die "Could not write $reg; the backup is $reg.witness-coop-backup."
    [ "$(override_get "$reg")" = "$OVERRIDE_VALUE" ] || die "The override did not read back from $reg; the backup is $reg.witness-coop-backup."
    say "  Proton: $PROTON_EXE loads the mod's dsound.dll first (\"$OVERRIDE_DLL\"=\"$OVERRIDE_VALUE\" in $reg; backup beside it)"
}
proton_uninstall() {
    rc="$game/$PROTON_RECEIPT"
    [ -f "$rc" ] || return 0
    reg=$(sed -n 's/^userreg=//p' "$rc"); before=$(sed -n 's/^before=//p' "$rc")
    if [ -f "$reg" ]; then
        now=$(override_get "$reg")
        if [ "$now" = "$OVERRIDE_VALUE" ]; then
            override_set "$reg" "$before"      # MUT:unoverride
            say "  Proton: the dsound override is undone in $reg"
        else say "  Proton: the dsound override in $reg was changed since ($now); it is kept"; fi
    fi
    rm -f "$rc"
}

do_install() {
    check_build
    for f in $MANAGED; do
        [ -f "$here/$f" ] || die "$f is missing next to this script. Unpack the whole zip and run the script from there."
    done
    legacy_check
    [ -z "$legacy_block" ] || die "An older co-op install is in '$game', and it cannot be undone safely:
$legacy_block"
    say "Installing into $game"
    migrate || true
    chain_install || true
    for rel in $MANAGED; do install_file "$game/$rel" "$here/$rel"; done
    proton_install
    say ""
    say "Done. For co-op start the launcher (witness_launcher.exe) in the game's Proton prefix, as the README describes"
    say "(a non-Steam game with the same Proton). The Witness started from Steam as usual loads the mod alone."
    say "Proton is untested for this mod. The README says what to expect and how to report what you see."
}

do_uninstall() {
    # check everything first, then change anything
    for rel in dsound.dll $CHAIN witness_launcher.exe witness_models/adventurer.glb witness_models/adventurer.glb.txt; do
        target="$game/$rel"
        refuse_link "$target" "$target.coop-original" "$target.coop-receipt.json" "$target.coop-stage"
        [ ! -e "$target.coop-stage" ] && [ ! -e "$target.coop-receipt.json.tmp" ] || die "A recovery file from an interrupted run is next to $target. Look at it and remove it by hand, then run this again."
        if [ -f "$target.coop-receipt.json" ]; then
            installed=$(rcpt_field "$target.coop-receipt.json" installed); original=$(rcpt_field "$target.coop-receipt.json" original)
            [ -n "$installed" ] || die "The ownership receipt for $rel is unreadable. Nothing was changed."
            obs=$(hash_of "$target"); [ -z "$obs" ] || [ "$obs" = "$installed" ] || die "$rel was changed after it was installed; it is kept. Nothing was changed."
            [ -z "$original" ] || [ "$(hash_of "$target.coop-original")" = "$original" ] || die "The backup of the original $rel is missing or changed. Nothing was changed."
        fi
    done
    legacy_check
    for rel in dsound.dll $CHAIN witness_launcher.exe witness_models/adventurer.glb witness_models/adventurer.glb.txt; do
        target="$game/$rel"; receipt="$target.coop-receipt.json"
        if [ ! -f "$receipt" ]; then     # MUT:owned
            [ -e "$target" ] && say "  no ownership receipt for $rel: kept it"
            continue
        fi
        original=$(rcpt_field "$receipt" original)
        if [ -n "$original" ]; then
            cp "$target.coop-original" "$target.coop-stage" && mv -f "$target.coop-stage" "$target"
            [ "$(hash_of "$target")" = "$original" ] || die "Restoring $rel did not verify. The backup is kept at $target.coop-original."
        else
            rm -f "$target"
        fi
        rm -f "$receipt" "$target.coop-original"
        say "  restored/removed $rel"
    done
    if [ -n "$legacy_block" ]; then say "  $legacy_block"; else migrate || true; fi
    proton_uninstall
    rmdir "$game/witness_models" 2>/dev/null || true
    say ""
    say "Co-op is off. witness_coop.cfg and witness_proxy.log stay in the game folder; delete them if you like. Saves are untouched."
}

do_status() {
    check_build
    say "Game folder: $game (the supported build)"
    for rel in $MANAGED; do
        t="$game/$rel"; r="$t.coop-receipt.json"
        if [ -f "$r" ]; then
            if [ "$(hash_of "$t")" = "$(rcpt_field "$r" installed)" ]; then say "  $rel: installed by this setup"; else say "  $rel: installed, then changed"; fi
        elif [ -e "$t" ]; then say "  $rel: present, not installed by this setup"
        else say "  $rel: absent"; fi
    done
    [ -f "$game/$CHAIN" ] && say "  $CHAIN: present (another mod's dsound.dll, loaded by the co-op proxy)"
    legacy_check
    if [ "$legacy" = 1 ]; then
        if [ -n "$legacy_block" ]; then say "  AnselSDK64.dll: an older co-op install that cannot be undone: $legacy_block"
        else say "  AnselSDK64.dll: an older co-op install (install migrates it)"; fi
    fi
    reg=$(proton_userreg)
    if [ -z "$reg" ]; then say "  Proton override: no prefix found (the game has not run under Proton yet), or not on Proton"
    elif [ "$(override_get "$reg")" = "$OVERRIDE_VALUE" ]; then say "  Proton override: in place ($reg)"
    else say "  Proton override: missing ($reg): run install again, or use the launch option WINEDLLOVERRIDES=\"$OVERRIDE_DLL=n,b\" %command%"; fi
}

cmd=${1:-}
case $cmd in install|uninstall|status) ;; *) say "usage: $0 install|uninstall|status [GAME_FOLDER]"; exit 2 ;; esac
game=${2:-}
if [ -z "$game" ]; then game=$(find_game) || die "The Witness was not found in the usual Steam folders. Give the game folder: $0 $cmd \"/path/to/steamapps/common/The Witness\""; fi
[ -d "$game" ] || die "'$game' is not a folder."
game=$(cd "$game" && pwd)
case $cmd in install) do_install ;; uninstall) do_uninstall ;; status) do_status ;; esac
