#!/bin/bash
# Every game-free test (T0) at once: the make suites and the Python tests that need no game, no Windows tool and no argument, side by side
# (they are independent: each make suite has its own build folder). Prints a table of suite, seconds and result, longest first, and the wall
# time; each one's output is in a log in the printed folder. Exit 0 when all passed. About 2 minutes on 16 CPUs (--quick about 1.5): with
# everything at once the wall time is the CPU work, so a test that repeats work costs everyone.
#   tools/t0.sh [--quick] [-j N] [FILTER ...]      --quick: without the three heaviest (navsim, playthrough, diag); FILTER: only names
#                                                  containing one of the words; -j: at most N at once (default: the number of CPUs)
# Not here: tests that need arguments or a Windows scratch folder (test_idle_seam, test_run_pose, test_update_installer,
# test_launcher_update_win), the shell tests that build installers or need Blender, and every scenario with a game (tools/qa.sh).
set -u
cd "$(dirname "$0")/.."
quick=0; jobs=$(nproc 2>/dev/null || echo 4); filters=()
while [ $# -gt 0 ]; do case "$1" in --quick) quick=1;; -j) jobs="${2:?}"; shift;; *) filters+=("$1");; esac; shift; done
suites=(
  "make common|make -s -C common test"
  "make launcher|make -s -C launcher test"
  "make proxy/core|make -s -C proxy/core test"
  "make proxy/ui|make -s -C proxy/ui test"
  "make proxy/progress|make -s -C proxy/progress test"
  "make proxy/randomizer|make -s -C proxy/randomizer test"
  "make tools/psolve|make -s -C tools/psolve test"
)
long=" tools/test_navsim.py tools/playthrough/test_playthrough.py tools/test_diag.py "
skip=" tools/test_idle_seam.py tools/test_run_pose.py tools/test_update_installer.py tools/test_launcher_update_win.py "
for f in tools/test_*.py tools/playthrough/test_*.py; do
  case "$skip" in *" $f "*) continue;; esac
  [ $quick = 1 ] && case "$long" in *" $f "*) continue;; esac
  suites+=("$f|python3 -B $f")
done
# the make suites leave out the Python tests this script runs on its own (proxy/core, launcher), unless a filter may have left those out
[ ${#filters[@]} = 0 ] && export T0_SEPARATE=1
logs=$(mktemp -d "${TMPDIR:-/tmp}/t0.XXXXXX"); t0=$(date +%s); n=0
run_one() {      # name|command -> logs/<n>.log and a line "seconds rc name" in logs/<n>.res
  local i="$1" name="${2%%|*}" cmd="${2#*|}" s; s=$(date +%s)
  bash -c "$cmd" > "$logs/$i.log" 2>&1; echo "$(( $(date +%s) - s )) $? $name" > "$logs/$i.res"
}
for s in "${suites[@]}"; do
  name="${s%%|*}"
  if [ ${#filters[@]} -gt 0 ]; then hit=0; for w in "${filters[@]}"; do case "$name" in *"$w"*) hit=1;; esac; done; [ $hit = 1 ] || continue; fi
  n=$((n + 1)); run_one "$n" "$s" &
  while [ "$(jobs -rp | wc -l)" -ge "$jobs" ]; do wait -n; done
done
wait
[ $n = 0 ] && { echo "no test matches: ${filters[*]}"; exit 2; }
echo; printf '%-46s %6s  %s\n' test seconds result
bad=0
for r in "$logs"/*.res; do read -r secs rc name < "$r"; [ "$rc" = 0 ] && res=PASS || res=FAIL; printf '%s\t%-46s %6s  %s   (%s)\n' "$secs" "$name" "$secs" "$res" "$logs/$(basename "$r" .res).log"; done | sort -t$'\t' -k1,1 -n -r | cut -f2-
for r in "$logs"/*.res; do read -r secs rc name < "$r"; [ "$rc" = 0 ] || bad=$((bad + 1)); done
echo; echo "$n tests, $bad failed, $(( $(date +%s) - t0 )) s wall (logs in $logs)"
[ $bad = 0 ]
