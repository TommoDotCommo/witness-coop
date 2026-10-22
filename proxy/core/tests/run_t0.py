#!/usr/bin/env python3
"""T0 for the Witness sync core: no game, no slot, seconds.

Builds the fuzz target, runs it clean (must pass), then once per bug twin (each must be caught by the oracle that
names its protection). A twin that is not caught means the test cannot see that protection: that is a failure.
usage: run_t0.py [--count N] [--hub DIR]
"""
import argparse, os, re, subprocess, sys

TWINS = {  # twin -> oracles that may catch it
    "no_resend": {"not_quiescent", "converge"},
    "no_echo_guard": {"echo_sent"},
    "no_dedupe": {"double_apply"},
    "no_epoch_reset": {"out_of_order_apply"},
    "no_resync_on_restart": {"converge"},
    "no_gate": {"spurious_event", "converge"},
    "no_settle": {"spurious_event"},
    "wrong_ack_epoch": {"not_quiescent", "converge", "spurious_event"},
    "apply_out_of_order": {"out_of_order_apply", "converge"},
    "no_checksum": {"not_quiescent", "converge"},
}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--count", type=int, default=500)
    ap.add_argument("--hub", default=os.environ.get("HUB", os.path.expanduser("~/source/coop-anything")))
    a = ap.parse_args()
    here = os.path.dirname(os.path.abspath(__file__))
    core = os.path.dirname(here)
    b = subprocess.run(["make", "-C", core, f"HUB={a.hub}", "build/coopfuzz-witness"], capture_output=True, text=True)
    if b.returncode:
        print(b.stdout + b.stderr); print("BUILD FAILED"); return 2
    exe = os.path.join(core, "build", "coopfuzz-witness")
    def run(*extra):
        return subprocess.run([exe, "--target", "witness", "--count", str(a.count), "--players", "2", "--duration", "150000",
                               "--no-shrink", "--out", os.path.join(core, "build", "out"), *extra], capture_output=True, text=True).stdout
    bad = 0
    out = run()
    ok = "RESULT: ALL OK" in out
    m = re.search(r"(\d+) sessions, (\d+) failed", out)
    print(f"{'PASS' if ok else 'FAIL'}  clean: {m.group(0) if m else out[-200:]}")
    bad += not ok
    for twin, want in TWINS.items():
        out = run("--bug", twin)
        got = set(re.findall(r"^FAIL seed \d+ \(\d+ machines\): ([a-z_]+):", out, re.M))
        ok = bool(got & want)
        print(f"{'PASS' if ok else 'FAIL'}  twin {twin:<22} {'caught by ' + ', '.join(sorted(got)) if got else 'NOT CAUGHT'}")
        bad += not ok
    print("ALL OK" if not bad else f"{bad} FAILED")
    return 1 if bad else 0

sys.exit(main())
