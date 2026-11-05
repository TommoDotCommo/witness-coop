#!/bin/bash
# The character gate (tools/check_character.py) must pass the built adventurer and fail each broken one for the right reason.
# Builds the mutants with Blender (models/src/adventurer.py flags), so it takes a few minutes; T0, no game.
set -euo pipefail
cd "$(dirname "$0")/.."
[ ! -f local/env.sh ] || . local/env.sh
blender="${BLENDER:-blender.exe}"
tmp=$(mktemp -d "${WITNESS_TMPDIR:-${TMPDIR:-/tmp}}/character-test.XXXXXXXX")
trap 'rm -rf "$tmp"' EXIT
build() { "$blender" -b --python-exit-code 1 -P "$(wslpath -w models/src/adventurer.py)" -- "$(wslpath -w "$tmp/$1.glb")" $2 > "$tmp/$1.log" 2>&1 || { tail -20 "$tmp/$1.log" >&2; exit 1; }; }
expect() {      # name, the target that must miss
  if python3 tools/check_character.py "$tmp/$1.glb" > "$tmp/$1.gate" 2>&1; then echo "MUTANT $1 PASSED the character gate" >&2; cat "$tmp/$1.gate" >&2; exit 1; fi
  grep -q "^$2 .*MISSES" "$tmp/$1.gate" || { echo "mutant $1 failed, but not on $2:" >&2; cat "$tmp/$1.gate" >&2; exit 1; }
  echo "mutant $1 rejected ($2)"
}
python3 tools/check_character.py assets/models/adventurer.glb > /dev/null && echo 'clean: the bundled adventurer meets the character spec'
cp assets/models/adventurer.glb "$tmp/code.glb"; cp assets/models/adventurer.glb.txt "$tmp/code.glb.txt"
if CHAR_MEASURE_FLAGS=-DWM_MUT_NO_LID_TURN python3 tools/check_character.py "$tmp/code.glb" > "$tmp/code.gate" 2>&1; then echo 'code mutant NO_LID_TURN passed' >&2; exit 1; fi
grep -q '^blink_left .*MISSES' "$tmp/code.gate" && echo 'code mutant NO_LID_TURN rejected (blink_left)'
build iris '--no-texture --iris-mutant'; expect iris iris_fraction
build lidsign '--no-texture --lid-sign-mutant'; expect lidsign blink_left
build eye '--no-texture --eye-mutant'; expect eye blink_left
build eyeao '--eye-ao-mutant'; expect eyeao eye_white
echo 'character gate: clean model passed; iris, lid direction, protruding eye, eye occlusion and blink code mutants rejected'
