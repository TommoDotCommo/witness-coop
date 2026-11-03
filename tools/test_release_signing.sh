#!/usr/bin/env bash
# Game-free test of the signing hook in tools/release.sh with a stand-in signer (tools/fake_sign.py appends a meaningless certificate block).
#   - SIGN_CMD set: every exe and dll (the zip's, the one-file setup's payload, the one-file setup) is signed BEFORE it is hashed and embedded
#   - a signer that misses one file: release.sh fails
#   - parts signed only after embedding (the wrong order): check_release_package.py --require-signed fails
#   - SIGN_CMD unset: a warning, an unsigned build
set -uo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
rel="$here/local/release"; fail=0
ok() { echo "  ok: $1"; }
bad() { echo "  FAIL: $1"; fail=1; }

echo "signed build:"
v=v0.0.0-signed
SIGN_CMD="python3 -I $here/tools/fake_sign.py {}" "$here/tools/release.sh" $v > "$rel/sign.log" 2>&1 && ok 'release.sh succeeds with a signer' || { bad 'release.sh with a signer'; tail -5 "$rel/sign.log"; }
grep -q 'signed: Witness-Coop-Setup.exe' "$rel/sign.log" && grep -q 'signed: dsound.dll' "$rel/sign.log" && ok 'the log names the signed files' || bad 'signing log'
python3 -I "$here/tools/check_release_package.py" "$rel/witness-coop-$v.zip" --single "$rel/witness-coop-$v-single/Witness-Coop-Setup.exe" --require-signed >/dev/null && ok 'zip, one-file setup and its payload are all signed' || bad 'signatures present'
python3 -I "$here/tools/pe_signature.py" "$rel/witness-coop-$v-single/Witness-Coop-Setup.exe" && ok 'the main download carries a signature' || bad 'single signed'

echo "a signer that misses the launcher:"
if FAKE_SIGN_SKIP=witness_launcher SIGN_CMD="python3 -I $here/tools/fake_sign.py {}" "$here/tools/release.sh" v0.0.0-skip > "$rel/skip.log" 2>&1; then bad 'release.sh accepted an unsigned launcher'; else grep -q 'carries no signature' "$rel/skip.log" && ok 'release.sh stops: SIGN_CMD ran but a file has no signature' || bad 'wrong failure'; fi

echo "wrong order (parts signed after they were embedded):"
v=v0.0.0-late
"$here/tools/release.sh" $v > "$rel/late.log" 2>&1 || bad 'unsigned build'
grep -q 'WARNING: SIGN_CMD is not set' "$rel/late.log" && ok 'without SIGN_CMD a warning is printed' || bad 'no warning without SIGN_CMD'
python3 -I - "$rel/witness-coop-$v-single/Witness-Coop-Setup.exe" <<'PY'
import sys; sys.path.insert(0, sys.argv[0] and __import__('os').environ['PWD'] + '/tools')
from pathlib import Path
from pe_signature import fake_sign
p = Path(sys.argv[1]); p.write_bytes(fake_sign(p.read_bytes()))      # only the outer file is signed: the parts inside it are not
PY
if python3 -I "$here/tools/check_release_package.py" "$rel/witness-coop-$v.zip" --single "$rel/witness-coop-$v-single/Witness-Coop-Setup.exe" --require-signed >/dev/null 2>"$rel/late.err"; then bad 'a setup with unsigned parts inside passed'; else ok "caught: $(tail -1 "$rel/late.err" | cut -c1-110)"; fi
[ $fail = 0 ] && echo "release signing: PASS" || { echo "release signing: FAIL"; exit 1; }
