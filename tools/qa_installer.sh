#!/usr/bin/env bash
# Validate the installer, the one-file Setup.exe and the release zip against disposable fixtures on a Windows filesystem. No game is started.
#   tools/qa_installer.sh <folder of the supported build>     (its exe and the game's own AnselSDK64.dll are read, never changed)
# The fixtures go under WITNESS_TMPDIR (a scratch folder on the Windows disk, set in local/env.sh), per WITNESS_RESOURCE_OWNER when set.
# Runs: the release build and its package check; the build-id header against the real exe; the setup core natively; the PowerShell installer
# suite (install, update, repair, refusals in plain words, uninstall); the one-file Setup.exe (extract, hash, planted links, zip location, run);
# install-linux.sh on fixtures, and its install read back by the Windows installer. Every protection has a mutant that must fail.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
[ -f "$here/local/env.sh" ] && . "$here/local/env.sh"
src="${1:?give the folder of a copy of the supported build (witness64_d3d11.exe and its AnselSDK64.dll, or AnselSDK64_real.dll in the old layout)}"
[ -f "$src/witness64_d3d11.exe" ] || { echo "no witness64_d3d11.exe in $src" >&2; exit 2; }
ver=v0.0.0-test
name="witness-coop-$ver"
rel="$here/local/release"
"$here/tools/release.sh" "$ver"
python3 -I "$here/tools/check_release_package.py" "$rel/$name.zip" --single "$rel/$name-single/Witness-Coop-Setup.exe"
python3 -I "$here/tools/buildid_of.py" "$src/witness64_d3d11.exe" --check
make -s -C "$here/common" test BUILDID_EXE="$src/witness64_d3d11.exe" | tail -4
"$here/tools/test_setup_core.sh" "$rel/$name-payload.bin"
base="${WITNESS_TMPDIR:?set WITNESS_TMPDIR to a scratch folder on the Windows disk (local/env.sh)}/${WITNESS_RESOURCE_OWNER:-installer}"
mkdir -p "$base"
fixtures=$(mktemp -d "$base/installer-XXXXXXXX")
cp -a "$rel/$name" "$fixtures/package"
ps() { powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w "$1")" "${@:2}"; }

echo "== PowerShell installer suite"
ps "$here/tools/test_installer.ps1" -GameDir "$(wslpath -w "$src")" -PackageDir "$(wslpath -w "$fixtures/package")" -TestRoot "$(wslpath -w "$fixtures")"
echo "== mutant: a package whose installer does not check the build (an unknown exe would install) must fail the refusal checks"
mutpkg="$fixtures/package-nobuildcheck"; cp -a "$fixtures/package" "$mutpkg"
sed -i "s/if (\$build -ne 'current') { Fail/if (\$false) { Fail/" "$mutpkg/witness-coop.ps1"
cmp -s "$mutpkg/witness-coop.ps1" "$fixtures/package/witness-coop.ps1" && { echo "mutation did not apply"; exit 1; }
if ps "$here/tools/test_installer.ps1" -GameDir "$(wslpath -w "$src")" -PackageDir "$(wslpath -w "$mutpkg")" -TestRoot "$(wslpath -w "$fixtures")" -OnlyRefusals >/dev/null 2>&1; then
    echo "MUTANT SURVIVED: an unknown exe installed and the suite passed"; exit 1; else echo "mutant killed (unknown exe installs -> the suite fails)"; fi

echo "== mutants of the old-layout migration and the chain file (each must fail the migration checks)"
mig_mutant() {  # <label> <sed expression on install-state.ps1 or witness-coop.ps1> <file>
    local d="$fixtures/package-$1"; rm -rf "$d"; cp -a "$fixtures/package" "$d"
    sed -i "$2" "$d/$3"
    cmp -s "$d/$3" "$fixtures/package/$3" && { echo "mutation $1 did not apply"; exit 1; }
    if ps "$here/tools/test_installer.ps1" -GameDir "$(wslpath -w "$src")" -PackageDir "$(wslpath -w "$d")" -TestRoot "$(wslpath -w "$fixtures")" -OnlyMigration >/dev/null 2>&1; then
        echo "MUTANT $1 SURVIVED"; exit 1; else echo "mutant $1 killed"; fi
}
mig_mutant original-not-verified 's/return (\$hash -and \$hash -eq \$script:WitnessAnselSha) }      # verify-original/return [bool]$hash }      # verify-original/' install-state.ps1
mig_mutant old-proxy-left-behind 's/if (Invoke-WitnessMigration \$game) { Say \(.\)  AnselSDK64.dll/if ($false) { Say \1  AnselSDK64.dll/' witness-coop.ps1
mig_mutant chain-file-ignored    's/if (Install-WitnessChain \$game) {/if ($false) {/' witness-coop.ps1

echo "== one-file Setup.exe"
exes="$fixtures/exes"; mkdir -p "$exes"
runner="$fixtures/runner-stage"; mkdir -p "$runner/witness_models"
printf '[IO.File]::WriteAllText($env:WITNESS_SETUP_MARKER, $PSScriptRoot)\r\n' > "$runner/installer-gui.ps1"; echo data > "$runner/witness_models/x.bin"
python3 -I "$here/tools/pack_payload.py" "$runner" "$fixtures/runner.bin" >/dev/null
build_set() {   # <dir> <what: all|one|thin> <flags...>
    local d="$1" what="$2"; shift 2; mkdir -p "$d"
    if [ "$what" != thin ]; then
        "$here/tools/build_setup.sh" "$d/one.exe" "$ver" "$rel/$name-payload.bin" "$@"
        python3 -I "$here/tools/corrupt_payload.py" "$d/one.exe" "$d/corrupt.exe"
        "$here/tools/build_setup.sh" "$d/runner.exe" "$ver" "$fixtures/runner.bin" "$@"
    fi
    [ "$what" = one ] || "$here/tools/build_setup.sh" "$d/thin.exe" "$ver" "" "$@"
}
build_set "$exes" all
mkdir -p "$fixtures/gui"; cp "$rel/$name-single/Witness-Coop-Setup.exe" "$fixtures/gui/one.exe"
cmp -s "$exes/one.exe" "$rel/$name-single/Witness-Coop-Setup.exe" && echo "(the test build equals the release build)" || true
ps "$here/tools/test_setup_exe.ps1" -Exes "$(wslpath -w "$exes")" -TestRoot "$(wslpath -w "$fixtures")" -Scenario all -PackageDir "$(wslpath -w "$fixtures/package")"
[ -z "${QA_GUI:-}" ] || ps "$here/tools/test_setup_exe.ps1" -Exes "$(wslpath -w "$fixtures/gui")" -TestRoot "$(wslpath -w "$fixtures")" -Scenario gui    # opens the real setup window for a moment: QA_GUI=1
mutant() {  # <label> <scenario> <what> <flag>
    local d="$fixtures/exes-$1"; build_set "$d" "$3" "$4"
    if ps "$here/tools/test_setup_exe.ps1" -Exes "$(wslpath -w "$d")" -TestRoot "$(wslpath -w "$fixtures")" -Scenario "$2" >/dev/null 2>&1; then
        echo "MUTANT $1 SURVIVED"; exit 1; else echo "mutant $1 killed"; fi
}
mutant payload-hash-accepted hash one -DSETUP_MUT_NO_HASH_CHECK
mutant follows-planted-link  links one -DSETUP_MUT_FOLLOW_LINKS
mutant reuses-existing-dir   reuse one -DSETUP_MUT_REUSE_DIR
mutant no-zip-message        zip  thin -DSETUP_MUT_NO_ZIP_MESSAGE

echo "== install-linux.sh"
"$here/tools/test_install_linux.sh" "$fixtures/package" "$src"
echo "== install-linux.sh install read by the Windows installer (same receipts)"
interop="$fixtures/interop"; mkdir -p "$interop"; cp "$src/witness64_d3d11.exe" "$src/AnselSDK64.dll" "$interop/"
sh "$fixtures/package/install-linux.sh" install "$interop" >/dev/null
status="$(ps "$fixtures/package/witness-coop.ps1" -GameDir "$(wslpath -w "$interop")" -StatusOnly -Yes | tr -d '\r')"
echo "$status" | python3 -I -c 'import json,sys; s=json.load(sys.stdin); a=s["actions"]; assert not a["install"] and a["uninstall"] and not a["blocked"], a; print("  ok: the Windows installer sees the Linux install as current and owned")'
ps "$fixtures/package/witness-coop.ps1" -GameDir "$(wslpath -w "$interop")" -Uninstall -Yes >/dev/null
[ "$(sha256sum "$interop/AnselSDK64.dll" | cut -d' ' -f1)" = 3b0677ae1e01fc17645c903c8322497785768145abbe3758364299fdaa8aff15 ] && [ ! -e "$interop/witness_launcher.exe" ] && [ ! -e "$interop/dsound.dll" ] && echo "  ok: the Windows uninstall removed a Linux install; the game's own files are as they were" || { echo "  FAIL: the Windows uninstall after a Linux install"; exit 1; }

echo "installer fixtures: $fixtures"
echo "qa_installer: ALL PASS"
