#!/usr/bin/env bash
# Validate the installer and the release zip against disposable fixtures on a Windows filesystem. No game is started.
#   tools/qa_installer.sh <folder of the supported build>     (its exe and original Ansel DLL are read, never changed)
# The fixtures go under WITNESS_TMPDIR (local/env.sh) or /mnt/c/coop/lab/run, per LAB_OWNER when set.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
[ -f "$here/local/env.sh" ] && . "$here/local/env.sh"
src="${1:?give the folder of a copy of the supported build (witness64_d3d11.exe and its AnselSDK64.dll)}"
[ -f "$src/witness64_d3d11.exe" ] || { echo "no witness64_d3d11.exe in $src" >&2; exit 2; }
ver=v0.0.0-test
"$here/tools/release.sh" "$ver"
python3 -I "$here/tools/check_release_package.py" "$here/local/release/witness-coop-$ver.zip"
base="${WITNESS_TMPDIR:-/mnt/c/coop/lab/run}/${LAB_OWNER:-installer}"
mkdir -p "$base"
fixtures=$(mktemp -d "$base/installer-XXXXXXXX")
cp -a "$here/local/release/witness-coop-$ver" "$fixtures/package"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w "$here/tools/test_installer.ps1")" \
    -GameDir "$(wslpath -w "$src")" -PackageDir "$(wslpath -w "$fixtures/package")" -TestRoot "$(wslpath -w "$fixtures")"
echo "installer fixtures: $fixtures"
