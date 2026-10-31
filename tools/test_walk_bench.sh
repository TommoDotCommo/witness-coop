#!/bin/bash
# Walking benchmark (tools/walk_bench.py has the panel list): one or two games from a save, each walks its share of the 15 legs
# (tools/win/wbench.py); results in local/bench/<time>/ (results.jsonl, table.txt, the walkers' traces). Leases the slots without
# waiting (exit 3 = busy or not enough RAM) and always releases them. Every game starts from the same walking map (local/nav_map.bin).
# usage: tools/test_walk_bench.sh [1|2] [save file] [--compare BEFORE.jsonl]      (WALK_BENCH_RECORDINGS=0: without local/recordings)
#        (the save: a .witness_campaign file; default local/run/checkpoint-0850.witness_campaign)
#        WBENCH_ONLY=0,3,7 (1 game): walk only these legs (numbers in tools/walk_bench.py order; each starts at the place of the leg before)
#        WALK_BENCH_MAP=<map file>: start from that map instead of local/nav_map.bin (a pass that learns: the map the walkers saved in the
#        run before, merged with Map.load, python3 tools/win/wnav.py --merge-maps OUT.bin A.bin B.bin); the run keeps the walkers' end maps in its folder
set -euo pipefail
cd "$(dirname "$0")/.."
[ ! -f local/env.sh ] || . local/env.sh
WITNESS_ALLOCATING=1 . tools/config.sh || exit $?
: "${WITNESS_RESOURCES:?configure an external resource provider in local/env.sh}"
n="${1:-1}"; [[ "$n" =~ ^[12]$ ]] && shift || n=1
SAVE="${1:-local/run/checkpoint-0850.witness_campaign}"; [ $# -gt 0 ] && shift
BEFORE=""; [ "${1:-}" = "--compare" ] && BEFORE="$2"
[ -f "$SAVE" ] && [ -f local/nav_map.bin ] || { echo "need $SAVE and local/nav_map.bin" >&2; exit 2; }
status=$("$WITNESS_RESOURCES" status)
python3 -c 'import re,sys; free=float(re.search(r"RAM free ([0-9.]+)",sys.argv[1])[1]); need=3+1.8*int(sys.argv[2]); print("RAM preflight: %.1f GB free, %.1f GB needed"%(free,need)); sys.exit(0 if free>=need else 3)' "$status" "$n"
OUT="local/bench/$(date +%Y%m%d-%H%M%S)"; mkdir -p "$OUT"
"$WITNESS_RESOURCES" acquire --slots "$n" --ttl 14400 --wait 0
trap '"$WITNESS_RESOURCES" release >/dev/null' EXIT
"$WITNESS_RESOURCES" provision witness >/dev/null
eval "$("$WITNESS_RESOURCES" env witness)"
make -C proxy >/dev/null
PANELS="$(python3 tools/walk_bench.py panels)"
# a person's recorded walks are known-good cells (tools/win/wnav.py, WNAV_RECORDINGS); WALK_BENCH_RECORDINGS=0 leaves them out
RECS=""; if [ "${WALK_BENCH_RECORDINGS:-1}" != 0 ]; then for f in local/recordings/*/session.jsonl; do [ -f "$f" ] && RECS="$RECS${RECS:+;}$(wslpath -w "$PWD/$f")"; done; fi
export WNAV_RECORDINGS="$RECS"
cd tools/playthrough
N="$n" SAVE="$SAVE" OUT="$OUT" PANELS="$PANELS" python3 - <<'EOF'
import concurrent.futures as cf, os, shutil, subprocess, time
from pathlib import Path
import run
from witness import win, PYW
from group_suite import replace_copy
n = int(os.environ['N']); root = Path(run.ROOT); out = root / os.environ['OUT']; save = root / os.environ['SAVE']
legs = os.environ['PANELS'].split(','); half = (len(legs) + 1) // 2
shares = [legs] if n == 1 else [legs[:half], legs[half:]]
bots = [run.Bot(i + 1) for i in range(n)]
for b in bots:
    b.prepare('id=1\nlisten=%d\nsave_dir=instance\nshare_zip=1\n' % b.port, 60, 0)
    saves = b.dir / 'appdata' / 'The Witness'; saves.mkdir(parents=True, exist_ok=True); replace_copy(save, saves / save.name)
    for f in ('wnav_map.bin', 'wnav_trace.jsonl'): (b.dir / f).unlink(missing_ok=True)
    shutil.copyfile(root / os.environ.get('WALK_BENCH_MAP', 'local/nav_map.bin'), b.dir / 'wnav_map.bin')      # every run starts from the same map (WALK_BENCH_MAP: a learnt one, see the usage)
    b.start(new_game=False)
for b in bots: run.wait(lambda: 'resources ready' in (b.dir / 'witness_proxy.log').read_text(errors='ignore'), 'bot %d loaded' % b.id, 400)
time.sleep(20)
env = {**os.environ, 'WNAV_TRACE': '1', 'WSLENV': os.environ.get('WSLENV', '') + ':WNAV_TRACE:WNAV_RECORDINGS:WNAV_KEEP:WBENCH_ONLY'}
def walk(k):
    b = bots[k]; res = out / ('results_b%d.jsonl' % b.id)
    r = subprocess.run([PYW, '-I', win(os.path.join(run.TOOLS, 'win', 'wbench.py')), b.pid, win(str(b.dir)), win(str(res)), ','.join(shares[k])],
                       env=env, capture_output=True, text=True, errors='replace', timeout=7200)
    (out / ('wbench_b%d.log' % b.id)).write_text(r.stdout + '\n--- stderr\n' + r.stderr[-20000:])
    for f in ('wnav_trace.jsonl', 'witness_proxy.log', 'wnav_map.bin'):
        if (b.dir / f).exists(): shutil.copyfile(b.dir / f, out / ('%s_b%d%s' % (Path(f).stem, b.id, Path(f).suffix)))
    for f in (b.dir).glob('wnav_*stuck*.bin'): shutil.copyfile(f, out / ('%s_b%d.bin' % (f.stem, b.id)))      # (WNAV_KEEP=1: a depth picture at each stuck)
    return res
with cf.ThreadPoolExecutor(max_workers=n) as pool: files = list(pool.map(walk, range(n)))
with open(out / 'results.jsonl', 'w') as f:
    for fn in files:
        if fn.exists(): f.write(fn.read_text())
for b in bots:
    if b.alive(): subprocess.run(['taskkill.exe', '/PID', b.pid, '/F'], capture_output=True)
EOF
cd ../..
python3 tools/walk_bench.py table "$OUT/results.jsonl" | tee "$OUT/table.txt"
echo "results: $OUT"
if [ -n "$BEFORE" ]; then python3 tools/walk_bench.py compare "$BEFORE" "$OUT/results.jsonl" | tee "$OUT/compare.txt"; fi
