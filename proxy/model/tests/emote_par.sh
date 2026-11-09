#!/bin/bash
# The emote quality gate, run side by side (each emote-quality run takes about 14 s; one after another they took 90 s): the clean gate over
# every clip in assets/emotes (its report printed, its JSON written), every bad clip in tests/emote_bad must be caught by the check its first
# line names, and every code mutant must fail the gate.
#   usage (from the Makefile): emote_par.sh <build dir> <model.glb> "<mutants>" <report.json>
b="$1"; glb="$2"; mutants="$3"; report="$4"
declare -A pid
"$b/emote-quality" "$glb" --json "$report" > "$b/emote-clean.log" 2>&1 & clean=$!
for f in tests/emote_bad/*.emote; do c=$(sed -n 's/^# expect: //p' "$f"); "$b/emote-quality" "$glb" "$f" --expect "$c" > /dev/null 2>&1 & pid[$f]=$!; done
for m in $mutants; do "$b/emote-mutant-$m" "$glb" > /dev/null 2>&1 & pid[m:$m]=$!; done
rc=0
if wait $clean; then cat "$b/emote-clean.log"; else cat "$b/emote-clean.log"; echo "THE CLEAN EMOTE GATE FAILED"; rc=1; fi
for f in tests/emote_bad/*.emote; do c=$(sed -n 's/^# expect: //p' "$f"); if wait "${pid[$f]}"; then echo "bad clip $f caught ($c)"; else echo "BAD CLIP $f NOT CAUGHT as $c"; rc=1; fi; done
for m in $mutants; do if wait "${pid[m:$m]}"; then echo "EMOTE MUTANT $m SURVIVED (the gate cannot fail for it)"; rc=1; else echo "emote mutant $m killed"; fi; done
exit $rc
