#!/bin/bash
# Downloads the one extra character model that may be redistributed (RobotExpressive, CC0 1.0, Tomas Laulhe, modifications by Don McCurdy; from the three.js examples)
# into $WITNESS_EXTRA_MODELS (default /mnt/c/coop/lab/run/showcase-models). The showcase uses every model it finds there next to the bundled ones; add your own .glb (and a
# <name>.glb.txt with height=<m>) to the same folder for more. Models of other people stay out of this repository: check each licence before you share a picture of one.
set -euo pipefail
dir="${WITNESS_EXTRA_MODELS:-/mnt/c/coop/lab/run/showcase-models}"; mkdir -p "$dir"
curl -fsSL -m 120 "https://raw.githubusercontent.com/mrdoob/three.js/dev/examples/models/gltf/RobotExpressive/RobotExpressive.glb" -o "$dir/robot-expressive.glb"
printf 'height=1.8\n' > "$dir/robot-expressive.glb.txt"
ls -la "$dir"
