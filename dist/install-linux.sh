#!/bin/sh
# witness-coop for The Witness on Linux and the Steam Deck (Proton): install and uninstall by hand-made steps, with backups.
# Plain POSIX sh. It does what Witness-Coop-Setup.exe does on Windows, with the same ownership receipts, so either can uninstall the other's install.
#   ./install-linux.sh install   [GAME_FOLDER]   put the mod into the game folder (the folder with witness64_d3d11.exe)
#   ./install-linux.sh uninstall [GAME_FOLDER]   put the game's own files back; only files this script or the Windows setup installed are touched
#   ./install-linux.sh status    [GAME_FOLDER]   say what is there
# Needs: sh, sha256sum (or shasum), cp, mv, sed. Nothing is installed anywhere but the game folder; the game's exe is never changed.
# Untested under Proton on a real Steam Deck as of this release: tested on a fixture folder in WSL (tools/test_install_linux.sh).
set -u
EXE_SHA=8d672d444df6a6df7130f25a517bdab1af92731fe2138dc5b324d519561d55a5       # witness64_d3d11.exe, Steam build 11370412
ANSEL_SHA=3b0677ae1e01fc17645c903c8322497785768145abbe3758364299fdaa8aff15     # the game's own AnselSDK64.dll
FAQ=https://github.com/TommoDotCommo/witness-coop#faq-not-the-supported-build
here=$(cd "$(dirname "$0")" && pwd)
MANAGED="AnselSDK64_real.dll AnselSDK64.dll witness_launcher.exe witness_models/adventurer.glb witness_models/adventurer.glb.txt"

say() { printf '%s\n' "$*"; }
die() { printf '\n%s\n' "$*" >&2; exit 1; }

hash_of() {     # hash_of FILE -> lower-case sha256, empty when the file is absent
    [ -f "$1" ] || { printf ''; return 0; }
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
    else shasum -a 256 "$1" | cut -d' ' -f1; fi
}
refuse_link() { for p in "$@"; do [ ! -L "$p" ] || die "Refusing to touch '$p': it is a symbolic link. Nothing was changed."; done; }

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

do_install() {
    check_build
    for f in AnselSDK64.dll witness_launcher.exe witness_models/adventurer.glb witness_models/adventurer.glb.txt; do
        [ -f "$here/$f" ] || die "$f is missing next to this script. Unpack the whole zip and run the script from there."
    done
    orig=''
    for n in AnselSDK64.dll AnselSDK64_real.dll AnselSDK64.dll.coop-original; do
        [ "$(hash_of "$game/$n")" = "$ANSEL_SHA" ] && { orig="$game/$n"; break; }
    done
    [ -n "$orig" ] || die "The game's own AnselSDK64.dll was not found in '$game' (expected SHA-256 starting $(printf '%s' "$ANSEL_SHA" | cut -c1-12)). Verify the game files in Steam, then run this again. Nothing was changed."
    tmp=$(mktemp "${TMPDIR:-/tmp}/witness-coop-ansel.XXXXXX") || die "No temporary file."
    trap 'rm -f "$tmp"' EXIT
    cp "$orig" "$tmp"
    say "Installing into $game"
    for rel in $MANAGED; do
        case $rel in AnselSDK64_real.dll) src=$tmp ;; *) src="$here/$rel" ;; esac
        install_file "$game/$rel" "$src"
    done
    say ""
    say "Done. The mod is in the game folder. Start the co-op launcher (witness_launcher.exe) inside the game's Proton prefix, as the README describes:"
    say "  Steam > Add a Non-Steam Game > witness_launcher.exe, Compatibility: the same Proton as The Witness, and the launch option below."
    say "  STEAM_COMPAT_DATA_PATH=\"<steam library>/steamapps/compatdata/210970\" %command%"
    say "Proton is untested for this mod. The README says what to expect and how to report what you see."
}

do_uninstall() {
    # check everything first, then change anything
    for rel in AnselSDK64.dll AnselSDK64_real.dll witness_launcher.exe witness_models/adventurer.glb witness_models/adventurer.glb.txt; do
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
    for rel in AnselSDK64.dll AnselSDK64_real.dll witness_launcher.exe witness_models/adventurer.glb witness_models/adventurer.glb.txt; do
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
}

cmd=${1:-}
case $cmd in install|uninstall|status) ;; *) say "usage: $0 install|uninstall|status [GAME_FOLDER]"; exit 2 ;; esac
game=${2:-}
if [ -z "$game" ]; then game=$(find_game) || die "The Witness was not found in the usual Steam folders. Give the game folder: $0 $cmd \"/path/to/steamapps/common/The Witness\""; fi
[ -d "$game" ] || die "'$game' is not a folder."
game=$(cd "$game" && pwd)
case $cmd in install) do_install ;; uninstall) do_uninstall ;; status) do_status ;; esac
