#!/usr/bin/env bash
# Game-free test of the publish command tools/release.sh prints and writes to local/release/PUBLISH-<version>.txt:
#   - it names every asset the build made (Setup.exe, the zip, both .sha256, the Vortex extension, the MO2 plug-in) and each exists
#   - a version with a pre-release part gets --prerelease and its release notes; a plain version gets neither --prerelease
#   - mutant RELEASE_MUT_NO_PLUGINS=1 (the old command, without the mod manager plug-ins) must fail this test
# release.sh rebuilds proxy/dsound.dll and the launcher in this checkout with a test version: do not run it beside a live game run from the same checkout.
set -uo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
rel="$here/local/release"; fail=0; mkdir -p "$rel"
ok() { echo "  ok: $1"; }
bad() { echo "  FAIL: $1"; fail=1; }

check() {      # check <version> <expect prerelease 0|1>: 0 when the publish command is complete
    local v=$1 pre=$2 cmd f e=0
    "$here/tools/release.sh" "$v" > "$rel/assets-$v.log" 2>&1 || { echo "    release.sh failed: $(tail -1 "$rel/assets-$v.log")"; return 1; }
    cmd="$(cat "$rel/PUBLISH-$v.txt" 2>/dev/null)" || { echo "    no PUBLISH-$v.txt"; return 1; }
    grep -qF "$cmd" "$rel/assets-$v.log" || { echo "    the command is not printed"; e=1; }
    [[ "$cmd" == "gh release create $v "* ]] || { echo "    not a gh release create for $v"; e=1; }
    for f in "witness-coop-$v-single/Witness-Coop-Setup.exe" "witness-coop-$v-single/Witness-Coop-Setup.exe.sha256" \
             "witness-coop-$v.zip" "witness-coop-$v.zip.sha256" game-thewitness- game_thewitness.py; do
        [[ "$cmd" == *"$rel/$f"* ]] || { echo "    missing asset: $f"; e=1; }
    done
    eval "set -- $cmd"; for f in "$@"; do [[ "$f" == "$rel"/* && ! -f "$f" ]] && { echo "    names a file that does not exist: $f"; e=1; }; done
    if [ "$pre" = 1 ]; then [[ "$cmd" == *" --prerelease "* ]] || { echo "    no --prerelease"; e=1; }
    else [[ "$cmd" != *--prerelease* ]] || { echo "    --prerelease on a plain version"; e=1; }; fi
    return $e
}

echo "pre-release version:"
check v0.0.0-assets.1 1 && ok 'every asset named and present, --prerelease' || bad 'pre-release publish command'
echo "plain version:"
check v0.0.0 0 && ok 'every asset named, not a pre-release' || bad 'plain publish command'
echo "release notes:"
v=v0.2.0-beta.1; [ -f "$here/docs/RELEASE-NOTES-$v.md" ] && { "$here/tools/release.sh" "$v" > "$rel/assets-notes.log" 2>&1 && grep -qF -- "--notes-file $here/docs/RELEASE-NOTES-$v.md" "$rel/PUBLISH-$v.txt" && ok "$v takes docs/RELEASE-NOTES-$v.md" || bad 'notes file not passed'; }
echo "mutant: the old command without the plug-ins:"
RELEASE_MUT_NO_PLUGINS=1 check v0.0.0-mut.1 1 > "$rel/assets-mut.out" && bad 'mutant survived' || { grep -q 'missing asset: game' "$rel/assets-mut.out" && ok "caught: $(grep -m1 'missing asset' "$rel/assets-mut.out" | sed 's/^ *//')" || bad "mutant failed for another reason: $(head -1 "$rel/assets-mut.out")"; }
rm -rf "$rel"/*v0.0.0-assets.1* "$rel"/*v0.0.0-mut.1* "$rel"/witness-coop-v0.0.0 "$rel"/witness-coop-v0.0.0[.-]* "$rel"/*-v0.0.0.txt "$rel"/assets-*.log
[ $fail = 0 ] && echo "release assets: PASS" || { echo "release assets: FAIL"; exit 1; }
