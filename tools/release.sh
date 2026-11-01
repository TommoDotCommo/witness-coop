#!/usr/bin/env bash
# Build the player downloads into local/release/:
#   witness-coop-<version>.zip                 the manual-install zip: the proxy (AnselSDK64.dll), the launcher, the player model, the thin Setup.exe
#                                              (runs the scripts beside it), the PowerShell scripts, install-linux.sh, README.txt, LICENSE.txt
#   witness-coop-<version>-single/Witness-Coop-Setup.exe   THE MAIN DOWNLOAD: one file that carries all of the above (SHA-256 checked, unpacked to a fresh
#                                              per-version folder under %LOCALAPPDATA%\witness-coop\setup\), plus its .sha256
#   tools/release.sh            version from `git describe --tags --always`
#   tools/release.sh v0.1.0     explicit version
#
# CODE SIGNING (docs/SIGNING.md). Set SIGN_CMD to sign every .exe and .dll in place; leave it unset for an unsigned test build (a loud warning is printed).
#   SIGN_CMD         a command run once per file; "{}" becomes the file's path, "{win}" its Windows path (WSL). Without "{}" the path is appended.
#                    e.g. SIGN_CMD='signtool.exe sign /fd sha256 /tr http://timestamp.digicert.com /td sha256 /sha1 <thumbprint> {win}'
#   SIGN_VERIFY_CMD  optional, same placeholders: a real verification, e.g. 'signtool.exe verify /pa {win}'. A structural check (tools/pe_signature.py:
#                    the file carries a certificate block) always runs after signing.
# Order matters: the parts are signed first, THEN hashed and embedded in the one-file setup, THEN the setup itself is signed, THEN the zip is built.
# Publishing is a separate step (ask the owner first): gh release create <version> --prerelease <the zip, the Setup.exe and their .sha256 files>
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
ver="${1:-$(git -C "$here" describe --tags --always --dirty)}"
if [[ ! "$ver" =~ ^[A-Za-z0-9][A-Za-z0-9._+-]*$ || "$ver" == *. ]]; then
    echo 'Version must contain only letters, digits, dot, underscore, plus or hyphen, and not end in a dot' >&2
    exit 2
fi
name="witness-coop-$ver"
out="$here/local/release"
stage="$out/$name"
single="$out/$name-single"

sign() {    # sign <file>...: in place, then verified
    [ -n "${SIGN_CMD:-}" ] || return 0
    local f cmd
    for f in "$@"; do
        cmd="${SIGN_CMD//\{win\}/\"\$(wslpath -w \"\$1\")\"}"
        if [[ "$SIGN_CMD" == *"{}"* ]]; then cmd="${cmd//\{\}/\"\$1\"}"; else cmd="$cmd \"\$1\""; fi
        bash -c "$cmd" _ "$f" || { echo "signing failed for $f" >&2; exit 1; }
        python3 -I "$here/tools/pe_signature.py" "$f" || { echo "SIGN_CMD ran but $f carries no signature" >&2; exit 1; }
        if [ -n "${SIGN_VERIFY_CMD:-}" ]; then
            cmd="${SIGN_VERIFY_CMD//\{win\}/\"\$(wslpath -w \"\$1\")\"}"; cmd="${cmd//\{\}/\"\$1\"}"
            bash -c "$cmd" _ "$f" || { echo "SIGN_VERIFY_CMD rejected $f" >&2; exit 1; }
        fi
        echo "signed: $(basename "$f")"
    done
}
if [ -z "${SIGN_CMD:-}" ]; then
    echo "WARNING: SIGN_CMD is not set. The exe and dll files in this build are UNSIGNED: Windows SmartScreen and antivirus programs will warn about them." >&2
    echo "         Fine for a test build. A public release should be signed: see docs/SIGNING.md." >&2
fi

make -s -C "$here/proxy"
make -s -C "$here/launcher" launcher
rm -rf "$stage" "$single" "$out/$name.zip" "$out/$name.zip.sha256"
mkdir -p "$stage/witness_models" "$single"
cp "$here/proxy/AnselSDK64.dll" "$stage/"
cp "$here/launcher/build/witness_launcher.exe" "$stage/"
cp "$here/assets/models/adventurer.glb" "$here/assets/models/adventurer.glb.txt" "$stage/witness_models/"
"$here/tools/build_install_policy.sh" "$stage/install-policy.exe"
sign "$stage/AnselSDK64.dll" "$stage/witness_launcher.exe" "$stage/install-policy.exe"
for f in witness-coop.ps1 installer-gui.ps1 install-state.ps1; do
    sed 's/\r$//; s/$/\r/' "$here/dist/$f" > "$stage/$f"; done
cp "$here/dist/install-linux.sh" "$stage/install-linux.sh"; chmod +x "$stage/install-linux.sh"      # LF line endings: it runs under sh
sed 's/\r$//; s/$/\r/; s/@VERSION@/'"$ver"'/' "$here/dist/README.txt" > "$stage/README.txt"
sed 's/\r$//; s/$/\r/' "$here/LICENSE" > "$stage/LICENSE.txt"

# the thin Setup.exe of the zip, then the one-file setup that carries everything but itself and the Linux script
"$here/tools/build_setup.sh" "$stage/Witness-Coop-Setup.exe" "$ver"
sign "$stage/Witness-Coop-Setup.exe"
python3 -I "$here/tools/pack_payload.py" "$stage" "$out/$name-payload.bin" --skip Witness-Coop-Setup.exe --skip install-linux.sh > "$out/$name-payload.sha256"
"$here/tools/build_setup.sh" "$single/Witness-Coop-Setup.exe" "$ver" "$out/$name-payload.bin"
sign "$single/Witness-Coop-Setup.exe"
(cd "$single" && sha256sum Witness-Coop-Setup.exe > Witness-Coop-Setup.exe.sha256)

(cd "$out" && python3 -c 'import sys, zipfile, os
z = zipfile.ZipFile(sys.argv[1] + ".zip", "w", zipfile.ZIP_DEFLATED)
for root, _, files in os.walk(sys.argv[1]):
    for f in sorted(files):
        p = os.path.join(root, f)
        info = zipfile.ZipInfo.from_file(p, p)
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, open(p, "rb").read())
z.close()' "$name")
(cd "$out" && sha256sum "$name.zip" > "$name.zip.sha256")
check=(python3 -I "$here/tools/check_release_package.py" "$out/$name.zip" --single "$single/Witness-Coop-Setup.exe")
[ -n "${SIGN_CMD:-}" ] && check+=(--require-signed)
"${check[@]}"
# the proxy's linker map and DLL of this release, kept (not shipped): tools/diag_report.py --map ... --dll ... names the function a crash report from this version is in
rm -rf "$out/$name.symbols"; mkdir -p "$out/$name.symbols"; cp "$here/proxy/AnselSDK64.map" "$here/proxy/AnselSDK64.dll" "$out/$name.symbols/"
echo "$out/$name.zip ($(du -h "$out/$name.zip" | cut -f1)); symbols for crash reports: $out/$name.symbols"
echo "$single/Witness-Coop-Setup.exe ($(du -h "$single/Witness-Coop-Setup.exe" | cut -f1)) <- the main download"
