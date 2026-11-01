#!/usr/bin/env bash
# Game-free test of the one-file setup's portable decisions (dist/setup-core.h): native build, AddressSanitizer, plus a mutant that must fail.
#   tools/test_setup_core.sh [payload.bin]
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
b="$(mktemp -d)"; trap 'rm -rf "$b"' EXIT
cc="${CC:-gcc}"
$cc -std=c99 -Wall -Wextra -Werror -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -o "$b/test" "$here/dist/setup_core_test.c"
"$b/test" ${1:+"$1"}
$cc -std=c99 -Wall -Wextra -g -DSETUP_MUT_ANY_NAME -o "$b/mutant" "$here/dist/setup_core_test.c"
if "$b/mutant" >/dev/null 2>&1; then echo "MUTANT ANY_NAME SURVIVED (a path-escaping name was accepted)"; exit 1; else echo "mutant ANY_NAME killed"; fi
