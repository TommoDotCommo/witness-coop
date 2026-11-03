#!/usr/bin/env bash
# Game-free test of the proxy's start-up: the REAL proxy/dsound_proxy.cpp (its DllMain, start-up thread, export stubs), with stubs for the parts it starts.
# A host program loads it as dsound.dll; the test build takes its "system folder" from a folder beside it (sys\) holding a fake dsound.dll with markers.
#   known:    a test build that treats the host as the known build -> every part must be started (the control: the gate lets a known build through)
#   unknown:  the production gate                                  -> nothing started, forwarding works, the log says co-op is off, no dialog when suppressed
#   mutant:   -DCOOP_MUT_ALWAYS_KNOWN (the check ignored)          -> the unknown run must now start things, and this test must fail on it
#   forward:  every call reaches the system copy, by name and by the ordinal the game imports (11); with no system copy the call fails, no crash;
#             mutant -DCOOP_MUT_BARE_NAME (LoadLibrary("dsound.dll"), which is the proxy itself) must be caught
#   chain:    a dsound.chain.dll beside the proxy is loaded (known and unknown builds) and logged; mutant -DCOOP_MUT_NO_CHAIN must be caught
#   old copy: the alpha's AnselSDK64.dll proxy already mapped -> this proxy installs nothing, logs it; mutant -DCOOP_MUT_NO_OTHER_COPY must be caught
#   ordinals: the production proxy/dsound.dll exports the system dsound.dll's 12 names at its ordinals
# Runs the .exe files on Windows through WSL interop, in a scratch folder on the Windows disk (WITNESS_TMPDIR, as qa_installer.sh).
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
[ -f "$here/local/env.sh" ] && . "$here/local/env.sh"
CXX=x86_64-w64-mingw32-g++
work="$(mktemp -d "${WITNESS_TMPDIR:?set WITNESS_TMPDIR to a scratch folder on the Windows disk (local/env.sh)}/${WITNESS_RESOURCE_OWNER:-gate}-gate-XXXXXXXX")"
[ -n "${KEEP:-}" ] || trap 'rm -rf "$work"' EXIT
t="$here/proxy/tests"
# like the real game exe: fixed base, no relocations, so its code in memory is the same every run
$CXX -std=c++17 -O1 -static -Wl,--disable-dynamicbase -Wl,--disable-reloc-section -Wl,--image-base=0x140000000 -o "$work/host.exe" "$t/gate_host.cpp"
$CXX -shared -o "$work/real.dll" "$t/gate_real.cpp"
$CXX -shared -o "$work/chain.dll" "$t/gate_chain.cpp"
$CXX -shared -o "$work/oldproxy.dll" "$t/gate_oldproxy.cpp"
spec="$("$work/host.exe" fingerprint | tr -d '\r')"
build() {   # <folder> <extra flags...>: the host, the fake system dsound.dll in sys\, a chain file unless NOCHAIN=1, the proxy as dsound.dll
    local d="$1"; shift; mkdir -p "$d/sys"; cp "$work/host.exe" "$d/host.exe"; cp "$work/real.dll" "$d/sys/dsound.dll"
    [ "${NOCHAIN:-0}" = 1 ] || cp "$work/chain.dll" "$d/dsound.chain.dll"
    $CXX -std=c++17 -O1 -D_WIN32_WINNT=0x0601 -shared -static-libgcc -static-libstdc++ -DCOOP_TEST_SYSTEM_SUBDIR='L"sys"' "$@" -o "$d/dsound.dll" "$here/proxy/dsound_proxy.cpp" "$t/gate_stubs.cpp" "$here/proxy/dsound.def"
}
forwarded() { echo "$1" | grep -q '^GetDeviceID=0x5a5a1234$' && echo "$1" | grep -q '^ordinal11=0x5a5a0011$'; }
run() {     # <folder> -> output of the host
    export WSLENV="WITNESS_COOP_NO_DIALOG${WSLENV:+:$WSLENV}"
    (cd "$1" && WITNESS_COOP_NO_DIALOG=1 ./host.exe run "$(wslpath -w "$1")" | tr -d '\r')
}
fail=0
check() { if [ "$2" = ok ]; then echo "  ok: $1"; else echo "  FAIL: $1"; fail=1; fi; }
expect_unknown() {  # <label> <folder> -> 0 if the proxy left the host alone
    local out; out="$(run "$2")"; local ok=0
    echo "$out" | grep -q '^gate=0,0,0,0,0$' || ok=1
    forwarded "$out" || ok=2
    grep -q 'BUILD: not Steam build 11370412' "$2/witness_proxy.log" || ok=3
    grep -q 'Co-op is OFF' "$2/witness_proxy.log" || ok=4
    grep -q 'message box suppressed' "$2/witness_proxy.log" || ok=5
    return $ok
}
echo "known build (control):"
build "$work/known" -DCOOP_TEST_SPEC="$spec"
out="$(run "$work/known")"; echo "$out" | sed 's/^/    /'
echo "$out" | grep -q '^gate=1,1,1,1,1$' && check 'a known build is hooked and started' ok || check 'a known build is hooked and started' no
forwarded "$out" && check 'a known build forwards to the system dsound.dll (by name and by ordinal 11)' ok || check 'a known build forwards to the system dsound.dll (by name and by ordinal 11)' no
echo "$out" | grep -q '^chain=1$' && grep -q '^chain: loaded dsound.chain.dll' "$work/known/witness_proxy.log" && check 'the chain file was loaded and logged' ok || check 'the chain file was loaded and logged' no
echo "unknown build (production gate):"
build "$work/unknown"
expect_unknown unknown "$work/unknown" && check 'an unknown build: nothing hooked or started, forwarded, logged, no dialog' ok || check 'an unknown build: nothing hooked or started, forwarded, logged, no dialog' no
echo "$(run "$work/unknown")" | grep -q '^chain=1$' && check 'an unknown build still loads the chain file (another mod keeps working)' ok || check 'an unknown build still loads the chain file (another mod keeps working)' no
# a build whose code differs by one byte but whose header matches is also unknown: spec with a wrong code hash
bad="${spec%,\"*},\"0000000000000000000000000000000000000000000000000000000000000000\""
build "$work/otherCode" -DCOOP_TEST_SPEC="$bad"
expect_unknown otherCode "$work/otherCode" && check 'same header, other code: also left alone' ok || check 'same header, other code: also left alone' no
echo "mutant (check ignored) must be caught:"
build "$work/mutant" -DCOOP_MUT_ALWAYS_KNOWN
if expect_unknown mutant "$work/mutant"; then check 'MUTANT SURVIVED: an unknown build was left alone although the check was ignored' no; else check 'mutant caught (the unknown build was hooked)' ok; fi
echo "no chain file:"
NOCHAIN=1 build "$work/nochain" -DCOOP_TEST_SPEC="$spec"
out="$(run "$work/nochain")"
echo "$out" | grep -q '^chain=0$' && grep -q '^chain: no dsound.chain.dll' "$work/nochain/witness_proxy.log" && check 'no chain file: nothing chained, logged' ok || check 'no chain file: nothing chained, logged' no
echo "mutant (chain file ignored) must be caught:"
build "$work/mutchain" -DCOOP_TEST_SPEC="$spec" -DCOOP_MUT_NO_CHAIN
out="$(run "$work/mutchain")"
echo "$out" | grep -q '^chain=1$' && check 'MUTANT SURVIVED: the chain file counted as loaded although it was ignored' no || check 'mutant caught (the chain file was not loaded)' ok
echo "no system dsound.dll:"
build "$work/nosys" -DCOOP_TEST_SPEC="$spec"; rm -f "$work/nosys/sys/dsound.dll"
out="$(run "$work/nosys")"
echo "$out" | grep -q '^GetDeviceID=0x80004005$' && echo "$out" | grep -q '^ordinal11=0x80004005$' && grep -q 'system dsound.dll FAILED to load' "$work/nosys/witness_proxy.log" && check 'no system copy: every call fails cleanly (E_FAIL), the host runs on, logged' ok || check 'no system copy: every call fails cleanly (E_FAIL), the host runs on, logged' no
echo "mutant (the real DLL loaded by bare name, which is the proxy itself) must be caught:"
build "$work/mutbare" -DCOOP_TEST_SPEC="$spec" -DCOOP_MUT_BARE_NAME
forwarded "$(run "$work/mutbare")" && check 'MUTANT SURVIVED: the calls reached the system copy although it was loaded by bare name' no || check 'mutant caught (a bare-name load does not reach the system copy)' ok
echo "the alpha's AnselSDK64.dll proxy loaded too:"
build "$work/oldcopy" -DCOOP_TEST_SPEC="$spec"; cp "$work/oldproxy.dll" "$work/oldcopy/AnselSDK64.dll"
out="$(run "$work/oldcopy")"
echo "$out" | grep -q '^old=1$' && echo "$out" | grep -q '^gate=0,0,0,0,0$' && forwarded "$out" && grep -q '^OTHER COPY: ' "$work/oldcopy/witness_proxy.log" && check 'with the old proxy mapped: nothing hooked or started, forwarded, logged' ok || check 'with the old proxy mapped: nothing hooked or started, forwarded, logged' no
build "$work/oldreal" -DCOOP_TEST_SPEC="$spec"; cp "$work/real.dll" "$work/oldreal/AnselSDK64.dll"
echo "$(run "$work/oldreal")" | grep -q '^gate=1,1,1,1,1$' && check 'the game'"'"'s own AnselSDK64.dll (not the old proxy) does not hold the mod back' ok || check 'the game'"'"'s own AnselSDK64.dll (not the old proxy) does not hold the mod back' no
echo "mutant (the old proxy not looked for) must be caught:"
build "$work/mutold" -DCOOP_TEST_SPEC="$spec" -DCOOP_MUT_NO_OTHER_COPY; cp "$work/oldproxy.dll" "$work/mutold/AnselSDK64.dll"
echo "$(run "$work/mutold")" | grep -q '^gate=0,0,0,0,0$' && check 'MUTANT SURVIVED: two copies of the mod would both hook the game' no || check 'mutant caught (both copies would hook)' ok
echo "export table of the production proxy:"
if [ -f "$here/proxy/dsound.dll" ]; then
    want='1 DirectSoundCreate|2 DirectSoundEnumerateA|3 DirectSoundEnumerateW|4 DllCanUnloadNow|5 DllGetClassObject|6 DirectSoundCaptureCreate|7 DirectSoundCaptureEnumerateA|8 DirectSoundCaptureEnumerateW|9 GetDeviceID|10 DirectSoundFullDuplexCreate|11 DirectSoundCreate8|12 DirectSoundCaptureCreate8'
    have="$(x86_64-w64-mingw32-objdump -p "$here/proxy/dsound.dll" | awk '/Ordinal\/Name Pointer/{f=1;next} f&&/^$/{f=0} f{gsub(/[][]/,""); print $1+1, $2}' | sort -n | paste -sd'|')"
    [ "$have" = "$want" ] && check 'proxy/dsound.dll exports the 12 dsound names at the system ordinals' ok || { check 'proxy/dsound.dll exports the 12 dsound names at the system ordinals' no; echo "    have: $have"; }
else check 'proxy/dsound.dll exists (make -C proxy first)' no; fi
[ $fail = 0 ] && echo "build gate: PASS" || { echo "build gate: FAIL"; exit 1; }
