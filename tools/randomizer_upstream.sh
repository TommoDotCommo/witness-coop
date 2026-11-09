#!/bin/bash
# A vendored randomizer (third_party/<folder>: witness-randomizer, variety-randomizer, despair-randomizer, symbol-shuffler, star-randomizer,
# tutorializer) against its upstream. Its UPSTREAM file: line 1 the upstream commit the files come from, line 2 the repository and branch.
#   tools/randomizer_upstream.sh <folder> status           our changes: each vendored file against that commit
#   tools/randomizer_upstream.sh <folder> update <commit>  upstream's changes from that commit to <commit> merged into ours (a three-way merge per
#                                                          file, git merge-file); UPSTREAM moves on only when no file has a conflict. Then:
#                                                          make -C proxy/randomizer test, a new recording (tools/randomizer_solo.sh --record),
#                                                          tools/qa.sh randomizer (third_party/witness-randomizer/README.md, "Updating").
#   tools/randomizer_upstream.sh all status                every folder's changes
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
if [ "${1:-}" = all ]; then for d in "$here"/third_party/*/UPSTREAM; do n=$(basename "$(dirname "$d")"); echo "######## $n"; "$0" "$n" "${2:-status}"; done; exit 0; fi
folder="${1:?give the folder under third_party (or all)}"; shift
V="$here/third_party/$folder"; [ -f "$V/UPSTREAM" ] || { echo "no $V/UPSTREAM" >&2; exit 2; }
base=$(sed -n 1p "$V/UPSTREAM"); repo=$(sed -n 2p "$V/UPSTREAM" | cut -d' ' -f1)
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
git clone -q --filter=blob:none --no-checkout "$repo" "$tmp/up"
up() { case "$1" in src/*) echo "Source/${1#src/}";; *) echo "$1";; esac; }      # our path -> upstream's
files=$(cd "$V" && find src App -type f 2>/dev/null | sort)
case "${1:-status}" in
status)
  for f in $files; do
    git -C "$tmp/up" show "$base:$(up "$f")" > "$tmp/old" 2>/dev/null || { echo "== $f: not in upstream $base"; continue; }
    diff -u --strip-trailing-cr --label "upstream/$f" --label "ours/$f" "$tmp/old" "$V/$f" || true
  done;;
update)
  new="${2:?give the upstream commit}"; new=$(git -C "$tmp/up" rev-parse "$new"); conflicts=0
  for f in $files; do
    p=$(up "$f")
    git -C "$tmp/up" show "$base:$p" > "$tmp/base" 2>/dev/null || { echo "$f: not in the old upstream"; continue; }
    if ! git -C "$tmp/up" show "$new:$p" > "$tmp/theirs" 2>/dev/null; then echo "$f: GONE upstream (decide by hand)"; conflicts=$((conflicts + 1)); continue; fi
    cp "$V/$f" "$tmp/ours"
    if git merge-file -L ours -L "upstream $base" -L "upstream ${new:0:12}" "$tmp/ours" "$tmp/base" "$tmp/theirs"; then
      cmp -s "$tmp/ours" "$V/$f" || { cp "$tmp/ours" "$V/$f"; echo "$f: merged"; }
    else n=$?; cp "$tmp/ours" "$V/$f"; echo "$f: $n CONFLICT(S), marked in the file"; conflicts=$((conflicts + n)); fi
  done
  # what upstream added beside the generator (a new source file it may now need: proxy/randomizer/variants.mk)
  git -C "$tmp/up" diff --name-only --diff-filter=A "$base" "$new" -- 'Source/*.cpp' 'Source/*.h' | sed 's/^/new upstream file (not vendored): /'
  if [ "$conflicts" = 0 ]; then { printf '%s\n' "$new"; sed -n '2,$p' "$V/UPSTREAM"; } > "$tmp/U" && cp "$tmp/U" "$V/UPSTREAM"; echo "UPSTREAM is now $new: build and run the tests"
  else echo "$conflicts conflict(s): resolve them, then write $new as the first line of $V/UPSTREAM"; exit 1; fi;;
*) echo "usage: randomizer_upstream.sh <folder>|all status | <folder> update <commit>" >&2; exit 2;;
esac
