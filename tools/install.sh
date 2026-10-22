#!/bin/bash
# Build the proxy and put it into the two test copies. The originals stay as AnselSDK64_real.dll.
# Never point this at your real install: it is for disposable copies (see README, "Test copies").
set -e; . "$(dirname "$0")/config.sh"
make -C "$ROOT/proxy" >/dev/null
for d in "$WITNESS_DIR1" "$WITNESS_DIR2"; do
  [ -f "$d/witness64_d3d11.exe" ] || { echo "no game copy at $d (see README, Test copies)"; exit 1; }
  [ "$(sha256sum "$d/witness64_d3d11.exe" | cut -d' ' -f1)" = "$EXE_SHA256" ] || { echo "$d: not Steam build 11370412, refusing"; exit 1; }
  if [ ! -f "$d/AnselSDK64_real.dll" ]; then
    [ "$(sha256sum "$d/AnselSDK64.dll" | cut -d' ' -f1)" = "$ANSEL_SHA256" ] || { echo "$d: AnselSDK64.dll is not the original, refusing"; exit 1; }
    mv "$d/AnselSDK64.dll" "$d/AnselSDK64_real.dll"
  fi
  cp "$ROOT/proxy/AnselSDK64.dll" "$d/AnselSDK64.dll"
  echo "installed into $d"
done
