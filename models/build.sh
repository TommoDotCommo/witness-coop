#!/bin/bash
# Builds the characters of models/src with Blender (headless), puts them into assets/models/, checks them, and writes preview pictures.
# Blender: the portable build, unzipped (no installer): BLENDER=<path to blender.exe> (default blender.exe, 4.2 LTS from blender.org).
# usage: models/build.sh [adventurer ...]      previews go to $MODEL_PREVIEW (default: models/preview/, not in git)
set -euo pipefail
cd "$(dirname "$0")/.."
[ ! -f local/env.sh ] || . local/env.sh
BLENDER="${BLENDER:-blender.exe}"
[ -x "$BLENDER" ] || { echo "no Blender at $BLENDER (set BLENDER=...)" >&2; exit 1; }
names=("$@"); [ ${#names[@]} -gt 0 ] || names=(adventurer)
out="${MODEL_PREVIEW:-$PWD/models/preview}"; mkdir -p "$out"
tmp=$(mktemp -d "${WITNESS_TMPDIR:-${TMPDIR:-/tmp}}/character-build.XXXXXXXX")
trap 'rm -rf "$tmp"' EXIT
for n in "${names[@]}"; do
  "$BLENDER" -b --python-exit-code 1 -P "$(wslpath -w "models/src/$n.py")" -- "$(wslpath -w "$tmp/$n.glb")" --preview "$(wslpath -w "$tmp/preview-$n")" > "$tmp/$n.log" 2>&1 || { cat "$tmp/$n.log" >&2; exit 1; }
  grep -E 'REPORT|Error|Traceback' "$tmp/$n.log" || true
  python3 tools/check_model.py "$tmp/$n.glb"
  python3 tools/test_idle_seam.py "$tmp/$n.glb"
  if [ "$n" = adventurer ]; then python3 tools/test_run_pose.py "$tmp/$n.glb"; python3 tools/check_appearance_asset.py "$tmp/$n.glb"; fi
  cp "$tmp/$n.glb" "$tmp/$n.glb.txt" assets/models/; mkdir -p "$out/$n"; cp "$tmp/preview-$n/"*.png "$out/$n/"
done
