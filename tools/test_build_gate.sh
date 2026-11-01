#!/usr/bin/env bash
# Game-free test of the proxy's build gate: the REAL proxy/ansel_proxy.cpp (its DllMain and start-up thread), with stubs for the parts it starts.
# A host program whose exe is not the known build loads it; the proxy must start nothing, patch nothing and still forward to the real Ansel DLL.
#   known:    a test build that treats the host as the known build -> every part must be started (the control: the gate lets a known build through)
#   unknown:  the production gate                                  -> nothing started, forwarding works, the log says co-op is off, no dialog when suppressed
#   mutant:   -DCOOP_MUT_ALWAYS_KNOWN (the check ignored)          -> the unknown run must now start things, and this test must fail on it
# Runs the .exe files on Windows through WSL interop, in a scratch folder on the Windows disk (WITNESS_TMPDIR, as qa_installer.sh).
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
[ -f "$here/local/env.sh" ] && . "$here/local/env.sh"
CXX=x86_64-w64-mingw32-g++
work="$(mktemp -d "${WITNESS_TMPDIR:-/mnt/c/coop/lab/run}/${LAB_OWNER:-gate}-gate-XXXXXXXX" 2>/dev/null || mktemp -d)"
[ -n "${KEEP:-}" ] || trap 'rm -rf "$work"' EXIT
t="$here/proxy/tests"
# like the real game exe: fixed base, no relocations, so its code in memory is the same every run
$CXX -std=c++17 -O1 -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section -Wl,--image-base=0x140000000 -o "$work/host.exe" "$t/gate_host.cpp"
$CXX -shared -o "$work/real.dll" "$t/gate_real.cpp"
spec="$("$work/host.exe" fingerprint | tr -d '\r')"
build() {   # <folder> <extra flags...>
    local d="$1"; shift; mkdir -p "$d"; cp "$work/host.exe" "$d/host.exe"; cp "$work/real.dll" "$d/AnselSDK64_real.dll"
    $CXX -std=c++17 -O1 -D_WIN32_WINNT=0x0601 -shared -static-libgcc -static-libstdc++ "$@" -o "$d/AnselSDK64.dll" "$here/proxy/ansel_proxy.cpp" "$t/gate_stubs.cpp" "$here/proxy/ansel_proxy.def"
}
run() {     # <folder> -> output of the host
    export WSLENV="WITNESS_COOP_NO_DIALOG${WSLENV:+:$WSLENV}"
    (cd "$1" && WITNESS_COOP_NO_DIALOG=1 ./host.exe run "$(wslpath -w "$1")" | tr -d '\r')
}
fail=0
check() { if [ "$2" = ok ]; then echo "  ok: $1"; else echo "  FAIL: $1"; fail=1; fi; }
expect_unknown() {  # <label> <folder> -> 0 if the proxy left the host alone
    local out; out="$(run "$2")"; local ok=0
    echo "$out" | grep -q '^gate=0,0,0,0,0$' || ok=1
    echo "$out" | grep -q '^getVersion=0x5a5a1234$' || ok=2
    grep -q 'BUILD: not Steam build 11370412' "$2/witness_proxy.log" || ok=3
    grep -q 'Co-op is OFF' "$2/witness_proxy.log" || ok=4
    grep -q 'message box suppressed' "$2/witness_proxy.log" || ok=5
    return $ok
}
echo "known build (control):"
build "$work/known" -DCOOP_TEST_SPEC="$spec"
out="$(run "$work/known")"; echo "$out" | sed 's/^/    /'
echo "$out" | grep -q '^gate=1,1,1,1,1$' && check 'a known build is hooked and started' ok || check 'a known build is hooked and started' no
echo "$out" | grep -q '^getVersion=0x5a5a1234$' && check 'a known build still forwards to the real Ansel DLL' ok || check 'a known build still forwards to the real Ansel DLL' no
echo "unknown build (production gate):"
build "$work/unknown"
expect_unknown unknown "$work/unknown" && check 'an unknown build: nothing hooked or started, forwarded, logged, no dialog' ok || check 'an unknown build: nothing hooked or started, forwarded, logged, no dialog' no
# a build whose code differs by one byte but whose header matches is also unknown: spec with a wrong code hash
bad="${spec%,\"*},\"0000000000000000000000000000000000000000000000000000000000000000\""
build "$work/otherCode" -DCOOP_TEST_SPEC="$bad"
expect_unknown otherCode "$work/otherCode" && check 'same header, other code: also left alone' ok || check 'same header, other code: also left alone' no
echo "mutant (check ignored) must be caught:"
build "$work/mutant" -DCOOP_MUT_ALWAYS_KNOWN
if expect_unknown mutant "$work/mutant"; then check 'MUTANT SURVIVED: an unknown build was left alone although the check was ignored' no; else check 'mutant caught (the unknown build was hooked)' ok; fi
[ $fail = 0 ] && echo "build gate: PASS" || { echo "build gate: FAIL"; exit 1; }
