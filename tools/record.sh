#!/bin/bash
# Record a person playing what the bot cannot do yet. One command: it leases a slot, starts one game from a save on the first monitor
# (the real mouse and keyboard work), and tools/win/wrecord.py puts the player in front of each task in turn with a message on the
# screen. The recording goes to local/recordings/<time>/ (session.jsonl, results.txt) and the walks into the shared walking map.
# usage: tools/record.sh [save file] [panel,panel,...]      (default: the newest checkpoint of a playthrough run, the bot's blockers)
# Close the game when the message says all tasks are done (or any time: what was done so far is kept); the slot is then released.
set -euo pipefail
cd "$(dirname "$0")/.."
[ ! -f local/env.sh ] || . local/env.sh
WITNESS_ALLOCATING=1 . tools/config.sh || exit $?
: "${WITNESS_RESOURCES:?configure an external resource provider in local/env.sh}"
SAVE="${1:-$(ls -t "${WITNESS_RUN:-local/run}"/../*/playthrough/*/checkpoint.witness_campaign 2>/dev/null | head -1)}"
# the bot's blockers (2026-10-30): line never started, stuck, or the game said wrong
TASKS="${2:-698,28998,9ff7,386fa,34d4,21d5,21b3,65,9f92,79df,28a79,3678,17ca6,a054,9fd7}"
[ -f "$SAVE" ] || { echo "no save to start from: give one (a .witness_campaign file)" >&2; exit 2; }
OUT="local/recordings/$(date +%Y%m%d-%H%M%S)"; mkdir -p "$OUT"
"$WITNESS_RESOURCES" acquire --slots 1 --ttl 14400 --wait 0
trap '"$WITNESS_RESOURCES" release >/dev/null' EXIT
"$WITNESS_RESOURCES" provision witness >/dev/null
eval "$("$WITNESS_RESOURCES" env witness)"
make -C proxy >/dev/null
read -r x y w h <<< "${WITNESS_RECT_A:-320 180 1920 1080}"
cd tools/playthrough
SAVE="$SAVE" OUT="$OUT" TASKS="$TASKS" RX="$x" RY="$y" RW="$w" RH="$h" python3 - <<'EOF'
import json, os, subprocess, time
import run
from logic import Logic
from witness import win, PYW
from group_suite import replace_copy
b = run.Bot(1); b.prepare('id=1\nlisten=%d\nsave_dir=instance\nshare_zip=1\n' % b.port, 60, 0)
cfg = b.dir / 'witness_coop.cfg'; cfg.write_text(cfg.read_text() + 'force_active=0\nrun_unfocused=1\n')      # a person plays: real input
save = os.environ['SAVE']; saves = b.dir / 'appdata' / 'The Witness'; saves.mkdir(parents=True, exist_ok=True)
replace_copy(save, saves / os.path.basename(save))
b.start(new_game=False)
run.wait(lambda: 'resources ready' in (b.dir / 'witness_proxy.log').read_text(errors='ignore'), 'the game loaded', 400); time.sleep(8)
subprocess.run([PYW, '-I', win(os.path.join(run.TOOLS, 'win', 'wplace.py')), b.pid, os.environ['RX'], os.environ['RY'], os.environ['RW'], os.environ['RH']], capture_output=True)
L = Logic(); names = {'%#x' % h: '%s %s' % (e.area, e.name) for h, e in L.entities.items() if e.kind == 'Panel'}
out = os.path.abspath(os.path.join(run.ROOT, os.environ['OUT']))
print('The game is on your first monitor. Click into it and follow the messages on the screen.', flush=True)
env = {**os.environ, 'WRECORD_NAMES': json.dumps(names), 'WSLENV': os.environ.get('WSLENV', '') + ':WRECORD_NAMES'}
r = subprocess.run([PYW, '-I', win(os.path.join(run.TOOLS, 'win', 'wrecord.py')), b.pid, win(str(b.dir)), win(out), os.environ['TASKS']], env=env, capture_output=True, text=True, errors='replace')
open(os.path.join(out, 'results.txt'), 'w').write(r.stdout + r.stderr[-2000:])
print(r.stdout, flush=True)
print('Close the game when you are done; the recording is in %s' % out, flush=True)
while b.alive(): time.sleep(2)
EOF
