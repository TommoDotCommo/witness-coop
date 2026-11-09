#!/bin/bash
# The vendored randomizer (third_party/witness-randomizer) against its upstream (NewSoupVi's Witness Random Puzzle Generator for Archipelago).
#   tools/randomizer_upstream.sh status           our changes: each vendored file against the upstream commit in third_party/witness-randomizer/UPSTREAM
#   tools/randomizer_upstream.sh update <commit>  upstream's changes from that commit to <commit> merged into ours (a three-way merge per file, git merge-file);
#                                                 UPSTREAM is moved on only when no file has a conflict. Then: make -C proxy/randomizer test, a new
#                                                 recording (tools/randomizer_solo.sh --record), tools/qa.sh randomizer (third_party/witness-randomizer/README.md).
set -eu
here=$(cd "$(dirname "$0")/.." && pwd); V="$here/third_party/witness-randomizer"; base=$(head -1 "$V/UPSTREAM")
repo=https://github.com/NewSoupVi/The-Witness-Randomizer-for-Archipelago
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
git clone -q --filter=blob:none --no-checkout "$repo" "$tmp/up"
up() { case "$1" in src/*) echo "Source/${1#src/}";; *) echo "$1";; esac; }      # our path -> upstream's
files=$(cd "$V" && find src App -type f | sort)
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
  # what upstream added beside the generator (a new source file it may now need)
  git -C "$tmp/up" diff --name-only --diff-filter=A "$base" "$new" -- Source/*.cpp Source/*.h | sed 's/^/new upstream file (not vendored): /'
  if [ "$conflicts" = 0 ]; then printf '%s\n' "$new" > "$V/UPSTREAM"; echo "UPSTREAM is now $new: build and run the tests (third_party/witness-randomizer/README.md)"
  else echo "$conflicts conflict(s): resolve them, then write $new into $V/UPSTREAM"; exit 1; fi;;
*) echo "usage: randomizer_upstream.sh status | update <commit>" >&2; exit 2;;
esac
