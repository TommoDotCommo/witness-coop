#!/usr/bin/env bash
# Build the player zip: local/release/witness-coop-<version>.zip with the proxy (AnselSDK64.dll), the launcher, the player
# model, the graphical installer (Witness-Coop-Setup.exe, which runs the PowerShell scripts beside it), README.txt and LICENSE.
#   tools/release.sh            version from `git describe --tags --always`
#   tools/release.sh v0.1.0     explicit version
# Publishing is a separate step (ask the owner first): gh release create <version> --prerelease local/release/witness-coop-<version>.zip*
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
ver="${1:-$(git -C "$here" describe --tags --always --dirty)}"
if [[ ! "$ver" =~ ^[A-Za-z0-9][A-Za-z0-9._+-]*$ ]]; then
    echo 'Version must contain only letters, digits, dot, underscore, plus or hyphen' >&2
    exit 2
fi
name="witness-coop-$ver"
out="$here/local/release"
stage="$out/$name"

make -s -C "$here/proxy"
make -s -C "$here/launcher" launcher
rm -rf "$stage" "$out/$name.zip" "$out/$name.zip.sha256"
mkdir -p "$stage/witness_models"
(cd "$here/dist" && i686-w64-mingw32-windres installer.rc -o "$out/installer-res.o")
i686-w64-mingw32-gcc -Os -Wall -Wextra -mwindows -o "$stage/Witness-Coop-Setup.exe" \
    "$here/dist/installer.c" "$out/installer-res.o" -lshell32
rm -f "$out/installer-res.o"
cp "$here/proxy/AnselSDK64.dll" "$stage/"
cp "$here/launcher/build/witness_launcher.exe" "$stage/"
cp "$here/assets/models/adventurer.glb" "$here/assets/models/adventurer.glb.txt" "$stage/witness_models/"
"$here/tools/build_install_policy.sh" "$stage/install-policy.exe"
for f in witness-coop.ps1 installer-gui.ps1 install-state.ps1; do
    sed 's/\r$//; s/$/\r/' "$here/dist/$f" > "$stage/$f"; done
sed 's/\r$//; s/$/\r/; s/@VERSION@/'"$ver"'/' "$here/dist/README.txt" > "$stage/README.txt"
sed 's/\r$//; s/$/\r/' "$here/LICENSE" > "$stage/LICENSE.txt"
(cd "$out" && python3 -c 'import sys, zipfile, os
z = zipfile.ZipFile(sys.argv[1] + ".zip", "w", zipfile.ZIP_DEFLATED)
for root, _, files in os.walk(sys.argv[1]):
    for f in sorted(files):
        z.write(os.path.join(root, f))
z.close()' "$name")
(cd "$out" && sha256sum "$name.zip" > "$name.zip.sha256")
echo "$out/$name.zip ($(du -h "$out/$name.zip" | cut -f1))"
