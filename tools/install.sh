#!/bin/bash
# Build the proxy and put it into the test copies (optional A/B selects one) as dsound.dll; the game's own files stay as they are.
# A copy in the old layout (the proxy in AnselSDK64.dll, the game's file renamed AnselSDK64_real.dll) gets the game's file back first.
# Never point this at your real install: it is for disposable copies (see README, "Test copies").
set -e; . "$(dirname "$0")/config.sh"
make -C "$ROOT/proxy" >/dev/null
case "${1:-both}" in
  both) dirs=("$WITNESS_DIR1" "$WITNESS_DIR2");;
  A) dirs=("$WITNESS_DIR1");;
  B) dirs=("$WITNESS_DIR2");;
  *) echo 'usage: install.sh [A|B|both]' >&2; exit 2;;
esac
for d in "${dirs[@]}"; do
  [ -f "$d/witness64_d3d11.exe" ] || { echo "no game copy at $d (see README, Test copies)"; exit 1; }
  [ "$(sha256sum "$d/witness64_d3d11.exe" | cut -d' ' -f1)" = "$EXE_SHA256" ] || { echo "$d: not Steam build 11370412, refusing"; exit 1; }
  if [ -f "$d/AnselSDK64_real.dll" ]; then
    [ "$(sha256sum "$d/AnselSDK64_real.dll" | cut -d' ' -f1)" = "$ANSEL_SHA256" ] || { echo "$d: AnselSDK64_real.dll is not the game's own Ansel DLL, refusing"; exit 1; }
    rm -f "$d/AnselSDK64.dll" "$d/AnselSDK64.dll.coop-receipt.json" "$d/AnselSDK64.dll.coop-original" "$d/AnselSDK64_real.dll.coop-receipt.json"
    mv "$d/AnselSDK64_real.dll" "$d/AnselSDK64.dll"
  fi
  [ "$(sha256sum "$d/AnselSDK64.dll" | cut -d' ' -f1)" = "$ANSEL_SHA256" ] || { echo "$d: AnselSDK64.dll is not the game's own, refusing"; exit 1; }
  src="$ROOT/proxy/dsound.dll"; [ "$d" = "$WITNESS_DIR2" ] && [ -n "${WITNESS_DLL_B:-}" ] && src="$WITNESS_DLL_B"      # WITNESS_DLL_B: a test build (a mutant) for game B only
  [ "$d" = "$WITNESS_DIR1" ] && [ -n "${WITNESS_DLL_A:-}" ] && src="$WITNESS_DLL_A"      # WITNESS_DLL_A: the same for game A only
  rm -f "$d/dsound.dll" "$d/dsound.dll.coop-receipt.json" "$d/dsound.dll.coop-original"; cp "$src" "$d/dsound.dll"      # replaced, never written through a hard link; a test copy keeps no Setup receipt
  mkdir -p "$d/witness_models/emotes" && find "$d/witness_models" -type f -delete && cp -f "$ROOT"/assets/models/*.glb "$ROOT"/assets/models/*.txt "$d/witness_models/"
  cp -f "$ROOT"/assets/emotes/*.emote "$d/witness_models/emotes/"      # for `emotes reload` (the proxy carries its own copy)
  echo "installed into $d"
done
