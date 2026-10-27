#!/bin/bash
# Build the proxy and put it into the test copies (optional A/B selects one). The originals stay as AnselSDK64_real.dll.
# Never point this at your real install: it is for disposable copies (see README, "Test copies").
set -e; . "$(dirname "$0")/config.sh"
# The characters that are not part of the repo (the Magic Frog, Michelle, Xbot, ...: tools/get_showcase_models.sh) live in one folder; an install copies them in too, so that a reinstall never takes a person's character away.
WITNESS_EXTRA_MODELS="${WITNESS_EXTRA_MODELS:-$ROOT/local/showcase-models}"
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
  if [ ! -f "$d/AnselSDK64_real.dll" ]; then
    [ "$(sha256sum "$d/AnselSDK64.dll" | cut -d' ' -f1)" = "$ANSEL_SHA256" ] || { echo "$d: AnselSDK64.dll is not the original, refusing"; exit 1; }
    mv "$d/AnselSDK64.dll" "$d/AnselSDK64_real.dll"
  fi
  cp "$ROOT/proxy/AnselSDK64.dll" "$d/AnselSDK64.dll"
  mkdir -p "$d/witness_models" && rm -f "$d/witness_models/"* && cp -f "$ROOT"/assets/models/*.glb "$ROOT"/assets/models/*.txt "$d/witness_models/"
  if [ -n "${WITNESS_EXTRA_MODELS:-}" ] && [ -d "$WITNESS_EXTRA_MODELS" ]; then cp -f "$WITNESS_EXTRA_MODELS"/*.glb "$WITNESS_EXTRA_MODELS"/*.txt "$d/witness_models/" 2>/dev/null || true; fi      # more models from a folder of your own (not part of the repo): WITNESS_EXTRA_MODELS=<folder>      # the character models (model=<name> in the cfg picks one, model=none the flat puppet)
  echo "installed into $d"
done
