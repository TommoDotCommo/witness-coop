#!/bin/bash
# The replay test (generate-test) as parallel processes, each a shard of the recorded runs, with its mutants (--fail-fast: they only need to
# fail once) running beside them. Prints the shards' output in order, then each mutant's verdict. Exit 0 when every shard passed and every
# mutant failed.   usage (from the Makefile): generate_par.sh <shards> "<mutants>" <worlds...>
n="$1"; mutants="$2"; shift 2; here="$(dirname "$0")"
declare -A pid
for i in $(seq 1 "$n"); do "$here/generate-test" --shard "$i/$n" "$@" > "$here/generate-test.$i.log" 2>&1 & pid[s$i]=$!; done
for m in $mutants; do "$here/generate-mut-$m" --fail-fast "$@" > "$here/generate-mut-$m.log" 2>&1 & pid[m$m]=$!; done
rc=0
for i in $(seq 1 "$n"); do wait "${pid[s$i]}" || rc=1; cat "$here/generate-test.$i.log"; done
for m in $mutants; do if wait "${pid[m$m]}"; then echo "MUTANT $m SURVIVED (the test cannot fail for it)"; rc=1; else echo "mutant $m killed"; fi; done
exit $rc
