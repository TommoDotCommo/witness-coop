#!/bin/bash
# Survey legs for the walking map (tools/win/wsurvey.py walks them): one game from a save on the shared map; the map it learnt and the
# legs' results go to local/survey/<time>/ (map_b1.bin, results.jsonl, trace).  Leases one slot without waiting (exit 3 = busy or RAM
# short) and always releases it.
# usage: tools/survey.sh LEGS.json [save file] [seconds budget]          (legs: python3 tools/nav_coverage.py MAP --legs LEGS.json)
#        SURVEY_MAP=<map>: start from that map instead of local/nav_map.bin
set -euo pipefail
cd "$(dirname "$0")/.."
[ ! -f local/env.sh ] || . local/env.sh
WITNESS_ALLOCATING=1 . tools/config.sh || exit $?
: "${WITNESS_RESOURCES:?configure an external resource provider in local/env.sh}"
LEGS="${1:?legs.json}"; SAVE="${2:-local/run/checkpoint-0850.witness_campaign}"; BUDGET="${3:-3000}"
MAP="${SURVEY_MAP:-local/nav_map.bin}"
[ -f "$SAVE" ] && [ -f "$MAP" ] && [ -f "$LEGS" ] || { echo "need $SAVE, $MAP and $LEGS" >&2; exit 2; }
status=$("$WITNESS_RESOURCES" status)
python3 -c 'import re,sys; free=float(re.search(r"RAM free ([0-9.]+)",sys.argv[1])[1]); need=3+1.8; print("RAM preflight: %.1f GB free, %.1f GB needed"%(free,need)); sys.exit(0 if free>=need else 3)' "$status"
OUT="local/survey/$(date +%Y%m%d-%H%M%S)"; mkdir -p "$OUT"
"$WITNESS_RESOURCES" acquire --slots 1 --ttl 14400 --wait 0
trap '"$WITNESS_RESOURCES" release >/dev/null' EXIT
"$WITNESS_RESOURCES" provision witness >/dev/null
eval "$("$WITNESS_RESOURCES" env witness)"
make -C proxy >/dev/null
cd tools/playthrough
OUT="$OUT" SAVE="$SAVE" MAP="$MAP" LEGS="$LEGS" BUDGET="$BUDGET" python3 - <<'PY'
import os, shutil, subprocess, time
from pathlib import Path
import run
from witness import win, PYW
from group_suite import replace_copy
root = Path(run.ROOT); out = root / os.environ['OUT']; save = root / os.environ['SAVE']
b = run.Bot(1)
try:
    b.prepare('id=1\nlisten=%d\nsave_dir=instance\nshare_zip=1\n' % b.port, 60, 0)
    saves = b.dir / 'appdata' / 'The Witness'; saves.mkdir(parents=True, exist_ok=True); replace_copy(save, saves / save.name)
    for f in ('wnav_map.bin', 'wnav_trace.jsonl'): (b.dir / f).unlink(missing_ok=True)
    shutil.copyfile(root / os.environ['MAP'], b.dir / 'wnav_map.bin')
    b.start(new_game=False)
    run.wait(lambda: 'resources ready' in (b.dir / 'witness_proxy.log').read_text(errors='ignore'), 'bot loaded', 400); time.sleep(20)
    env = {**os.environ, 'WNAV_TRACE': '1', 'WSLENV': os.environ.get('WSLENV', '') + ':WNAV_TRACE:WNAV_RECORDINGS'}
    res = out / 'results.jsonl'
    r = subprocess.run([PYW, '-I', win(os.path.join(run.TOOLS, 'win', 'wsurvey.py')), b.pid, win(str(b.dir)), win(str(res)), win(os.environ['LEGS'] if os.path.isabs(os.environ['LEGS']) else str(root / os.environ['LEGS'])), os.environ['BUDGET']],
                       env=env, capture_output=True, text=True, errors='replace', timeout=7200)
    (out / 'wsurvey.log').write_text(r.stdout + '\n--- stderr\n' + r.stderr[-30000:])
finally:
    for f in ('wnav_trace.jsonl', 'witness_proxy.log', 'wnav_map.bin'):
        if (b.dir / f).exists(): shutil.copyfile(b.dir / f, out / ('%s_b1%s' % (Path(f).stem, Path(f).suffix)))
    if b.alive(): subprocess.run(['taskkill.exe', '/PID', b.pid, '/F'], capture_output=True)
PY
cd ../..
echo "survey: $OUT"; tail -n 3 "$OUT/results.jsonl" 2>/dev/null || true
