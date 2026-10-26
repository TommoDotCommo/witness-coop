#!/bin/bash
# Showcase: N real games (default 8) in one full-mesh group, each player a different character model, driven through every feature while the game's own
# screenshot command takes the pictures. usage: tools/showcase.sh [N] [scenes]   (scenes: all, or a comma list of: lineup walk pov devmenu pause social emote draw voice panel map launcher)
# Pictures and showcase.json go to $SHOWCASE_OUT (default: showcase/ in this checkout; not in git: tools/showcase_publish.sh turns them into the small JPEGs of docs/showcase/ that are).
# Extra models (more than the three bundled ones) come from $WITNESS_EXTRA_MODELS (default /mnt/c/coop/lab/run/showcase-models; tools/get_showcase_models.sh fills it with the CC0 one).
# RAM: a game costs about 1.3 GB of free memory here (measured: 5 games took free RAM from 15.6 to 9.3 GB; their working sets show 2 GB because the hard-linked game files are shared), so the
# check asks for 1.4 GB each plus the lab's reserve (SHOWCASE_RESERVE_GB, default 3). SHOWCASE_RESERVE_GB=1 on a PC nobody is using; the lab refuses new slots below 3 GB at lease time only.
set -euo pipefail
cd "$(dirname "$0")/.."
LAB="${LAB:-$HOME/source/coop-lab/lab}"
n="${1:-8}"; scenes="${2:-all}"
[[ "$n" =~ ^[2-8]$ ]] || { echo 'usage: tools/showcase.sh [2..8] [scenes]' >&2; exit 2; }
status=$("$LAB" status)
python3 -c 'import re,sys; free=float(re.search(r"RAM free ([0-9.]+)",sys.argv[1])[1]); need=float(sys.argv[3])+1.4*int(sys.argv[2]); print("RAM preflight: %.1f GB free, %.1f GB needed"%(free,need)); sys.exit(0 if free>=need else 3)' "$status" "$n" "${SHOWCASE_RESERVE_GB:-3}"
export WITNESS_EXTRA_MODELS="${WITNESS_EXTRA_MODELS:-/mnt/c/coop/lab/run/showcase-models}"
export SHOWCASE_OUT="${SHOWCASE_OUT:-$PWD/showcase}"
"$LAB" acquire --slots "$n" --ttl 3600 --wait 0
trap '"$LAB" release' EXIT
"$LAB" provision witness
eval "$("$LAB" env witness)"
make -C proxy
make -C launcher launcher
python3 tools/showcase.py "$n" "$scenes"
