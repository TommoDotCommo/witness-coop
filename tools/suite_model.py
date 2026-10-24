#!/usr/bin/env python3
"""Character-model suite (T2): both games run, A looks at the player B (and at a pose B never sent), and tools/win/wmodel.py asserts on what
the proxy measured: pixels of the model that passed the depth test (a GPU occlusion query), the measured speed and the animation clip time.
MUTANT scenarios are builds with one protection off; they must fail exactly the checks that protection is for.
usage: suite_model.py [substring of scenario name ...]   (about 3 minutes per scenario)"""
import os, re, subprocess, sys, time
from witness import ROOT, TOOLS, DIR1, DIR2, PYW, RUN, pid, win, real_saves

OCC = 'a player behind the closed door is hidden (< 2% of the pixels in front)'
SCENARIOS = [   # name, cfg for A (the viewer), the set of checks that must FAIL (empty: all must pass), pixel scale, walk clip index, whether the idle clip animates (1) or holds a frame (0), regex the proxy log must match
  ('model: visible, animated, hidden by walls', '', set(), 1.0, 0, 0, r'model: loaded 3273 vertices.*1 clips'),
  ('model: another bundled model selected by name', 'model=fox', set(), 0.1, 1, 1, r'model: loaded .* 3 clips'),
  ('MUTANT: model drawn over walls', 'no_model_depth=1', {OCC}, 1.0, 0, 0, None),
  ('MUTANT: model never walks', 'no_model_gait=1', {'a walking player animates (clip time advances)'}, 1.0, 0, 0, None),
  ('MUTANT: model off (flat puppet)', 'model=none', {'B standing 5 m ahead is drawn and visible', 'a walking player has a measured speed of 1.5..5 m/s', 'a walking player animates (clip time advances)', 'the walking figure is visible', 'a player in front of the door is visible', OCC}, 1.0, 0, 0, None),
]
only = sys.argv[1:]; results = []; saves0 = real_saves()
for name, ca, must_fail, px, walk, idle_anim, logre in SCENARIOS:
    if only and not any(o in name for o in only): continue
    print('\n=== %s' % name, flush=True)
    subprocess.run([os.path.join(TOOLS, 'restart.sh')], env={**os.environ, 'CFG_A': ca, 'CFG_B': ''}, capture_output=True, text=True, timeout=600, check=True)
    time.sleep(40)      # the worlds must be loaded and the sync gate passed
    open(os.path.join(RUN, 'guard.stop'), 'w').write('bot starting')
    out = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wmodel.py')), pid('A'), win(DIR1), pid('B'), win(DIR2), str(px), str(walk), str(idle_anim)], capture_output=True, text=True, timeout=300).stdout
    print(out.strip(), flush=True)
    seen = {m[1]: m[2] == 'ok' for m in (re.match(r'^  (.+?)\s+(ok|FAILED)\s+\(', l) for l in out.splitlines()) if m}
    failed = {k for k, v in seen.items() if not v}
    expected_checks = 9
    ok = len(seen) == expected_checks and failed == must_fail
    why = 'every check passed' if not must_fail else 'exactly the checks this protection is for failed'
    if len(seen) != expected_checks: why = 'the check script did not finish (%d of %d checks reported)' % (len(seen), expected_checks)
    elif failed != must_fail: why = 'failed %s, expected %s' % (sorted(failed), sorted(must_fail))
    if logre and not re.search(logre, open(os.path.join(DIR1, 'witness_proxy.log'), errors='ignore').read()): ok, why = False, 'the proxy log has no line matching /%s/ (wrong model loaded?)' % logre
    safe = real_saves() == saves0
    print('  hygiene: real saves untouched %s' % ('ok' if safe else 'FAILED'))
    ok = ok and safe
    print('RESULT: %s  (%s)' % ('PASS' if ok else 'FAIL', why), flush=True); results.append((name, ok))
print('\n==== SUMMARY')
for n, ok in results: print('%-48s %s' % (n, 'PASS' if ok else 'FAIL'))
print('ALL OK' if results and all(ok for _, ok in results) else 'SOME FAILED')
sys.exit(0 if results and all(ok for _, ok in results) else 1)
