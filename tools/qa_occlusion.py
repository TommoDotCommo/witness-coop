#!/usr/bin/env python3
"""T2 scenario: another player's body must be hidden by the game's geometry (owner playtest 2026-10-31: players were seen through walls).
For each video profile the two games start with that Launcher.variables; tools/win/wocc.py draws B's figure on A in the open and behind the closed
door (wocc.py) and in the open world against the terrain (wocc_out.py), and asserts on the proxy's occlusion query (pixels of the figure that survive the scene depth test) and on the scene_depth state.
The mutant no_model_depth=1 (figure drawn with no scene depth) must fail. usage: qa_occlusion.py [substring of scenario name ...]  (called by tools/qa.sh occlusion)"""
import os, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import ROOT, TOOLS, DIR1, DIR2, PYW, RUN, pid, win, real_saves
def video(profile=2, msaa=2, shading=2, reflection=2, texture=2, texture_detail=2, geometry=2, w=1920, h=1080):      # the same format as tools/playthrough/run.py video_options (1080p window)
    return (':/launcher\nrender_profile %d\nfullscreen false\nvsync false\nhighdpi false\nresolution 1\nmultisampling %d\ncustom_width %d\ncustom_height %d\n'
            'shading_quality %d\nreflection_quality %d\ntexture_quality %d\ntexture_detail %d\ngeometry_detail %d\n') % (profile, msaa, w, h, shading, reflection, texture, texture_detail, geometry)
# name, Launcher.variables (None: no file, the game's automatic profile = the owner's "high"), cfg for A, must the check pass
SCENARIOS = [
  ('occlusion: automatic high profile (4x MSAA)', None, 'see_through=0', True),
  ('occlusion: explicit high, 4x MSAA', video(), 'see_through=0', True),
  ('occlusion: high, multisampling off', video(msaa=0), 'see_through=0', True),
  ('occlusion: high, 2x MSAA', video(msaa=1), 'see_through=0', True),
  ('occlusion: high, 8x MSAA', video(msaa=3), 'see_through=0', True),
  ('occlusion: lowest options', video(profile=0, msaa=0, shading=0, reflection=0, texture=0, texture_detail=0, geometry=0), 'see_through=0', True),
  ('MUTANT: figure drawn without scene depth (automatic high)', None, 'see_through=0\nno_model_depth=1', False),
  ('MUTANT: figure drawn without scene depth (lowest options)', video(profile=0, msaa=0, shading=0, reflection=0, texture=0, texture_detail=0, geometry=0), 'see_through=0\nno_model_depth=1', False),
]
only = sys.argv[1:]; results = []; saves0 = real_saves()
for name, var, ca, expect in SCENARIOS:
    if only and not any(o in name for o in only): continue
    print('\n=== %s' % name, flush=True)
    env = {**os.environ, 'CFG_A': ca + os.environ.get('QA_OCC_EXTRA', ''), 'CFG_B': 'see_through=0', 'WITNESS_KEEP_VIDEO': '1'}
    for d in (DIR1, DIR2):
        sd = os.path.join(d, 'appdata', 'The Witness'); os.makedirs(sd, exist_ok=True); vp = os.path.join(sd, 'Launcher.variables')
        if os.path.exists(vp): os.remove(vp)
        # Only A's scene depth is measured; the pose-sending peer can use low options.
        options = var if d == DIR1 else video(profile=0, msaa=0, shading=0, reflection=0, texture=0, texture_detail=0, geometry=0)
        if options: open(vp, 'w').write(options)
    subprocess.run([os.path.join(TOOLS, 'restart.sh')], env=env, capture_output=True, text=True, timeout=600, check=True)
    time.sleep(40)
    open(os.path.join(RUN, 'guard.stop'), 'w').write('bot starting')
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wocc.py')), pid('A'), win(DIR1), pid('B'), win(DIR2), name.split(':', 1)[1].strip()], capture_output=True, text=True, timeout=300)
    print(r.stdout.strip(), flush=True)
    r2 = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wocc_out.py')), pid('A'), win(DIR1), pid('B'), win(DIR2), name.split(':', 1)[1].strip()], capture_output=True, text=True, timeout=600)      # the open world's terrain
    print(r2.stdout.strip(), r2.stderr.strip()[-600:], flush=True)
    log = open(os.path.join(DIR1, 'witness_proxy.log'), errors='ignore').read()
    if os.environ.get('QA_OCC_KEEP'):
        import shutil
        evidence = os.environ['QA_OCC_KEEP']; os.makedirs(evidence, exist_ok=True)
        label = ''.join(c if c.isalnum() else '_' for c in name)[:60]
        for side, directory in (('A', DIR1), ('B', DIR2)):
            shutil.copy(os.path.join(directory, 'witness_proxy.log'), os.path.join(evidence, label + '_' + side + '.log'))
        open(os.path.join(evidence, label + '_result.txt'), 'w').write(r.stdout + r.stderr + r2.stdout + r2.stderr)
    scene_lines = [l for l in log.splitlines() if 'depth: scene depth buffer' in l]
    print('  proxy log: %s' % (scene_lines[-1] if scene_lines else 'no "scene depth buffer" line'), flush=True)
    passed = r.returncode == 0 and r2.returncode == 0
    safe = real_saves() == saves0; print('  hygiene: real saves untouched %s' % ('ok' if safe else 'FAILED'))
    ok = (passed == expect) and safe
    print('RESULT: %s  (%s)' % ('PASS' if ok else 'FAIL', 'checks passed' if passed else 'checks failed') + ('' if expect else ' (a mutant: failing is the point)'), flush=True); results.append((name, ok))
for d in (DIR1, DIR2):
    vp = os.path.join(d, 'appdata', 'The Witness', 'Launcher.variables')
    if os.path.exists(vp): os.remove(vp)
print('\n==== SUMMARY')
for n, ok in results: print('%-62s %s' % (n, 'PASS' if ok else 'FAIL'))
print('ALL OK' if results and all(ok for _, ok in results) else 'SOME FAILED')
sys.exit(0 if results and all(ok for _, ok in results) else 1)
