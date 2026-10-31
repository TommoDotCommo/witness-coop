#!/usr/bin/env python3
"""T2 near-camera body/tag fade, with GPU pixel counts and a disabled-fade mutant.
Run through tools/qa.sh --serial near_fade [scenario substring]."""
import json, os, shutil, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, DIR2, PYW, RUN, pid, win, real_saves
def video(profile=2, msaa=2, shading=2, reflection=2, texture=2, texture_detail=2, geometry=2, w=1920, h=1080):      # the same format as tools/playthrough/run.py video_options (1080p window)
    return (':/launcher\nrender_profile %d\nfullscreen false\nvsync false\nhighdpi false\nresolution 1\nmultisampling %d\ncustom_width %d\ncustom_height %d\n'
            'shading_quality %d\nreflection_quality %d\ntexture_quality %d\ntexture_detail %d\ngeometry_detail %d\n') % (profile, msaa, w, h, shading, reflection, texture, texture_detail, geometry)
# name, Launcher.variables (None: no file, the game's automatic profile = the owner's "high"), cfg for A, must the check pass
SCENARIOS = [
    ('near fade: automatic high', None, 'see_through=0', True),
    ('near fade: lowest options', video(profile=0,msaa=0,shading=0,reflection=0,texture=0,texture_detail=0,geometry=0), 'see_through=0', True),
    ('MUTANT near fade: automatic high', None, 'see_through=0\nnear_fade=0', False),
    ('MUTANT near fade: lowest options', video(profile=0,msaa=0,shading=0,reflection=0,texture=0,texture_detail=0,geometry=0), 'see_through=0\nnear_fade=0', False),
]
REQUIRED = {'viewer camera is at the tutorial viewpoint',
            'far body and tag visible', 'restored body and tag visible',
            'overlapping body invisible', 'overlapping tag invisible',
            'close body invisible', 'close tag invisible'} | {
                label + suffix for label in ('far','overlap','close','restored')
                for suffix in (' pose reached',' scene and body query valid')}
def verdict(returncode, output, expect):
    """Only the near-body assertions may fail in the disabled-fade mutant."""
    try:
        data=json.loads(next(l[len('NEAR_FADE_RESULT '):] for l in output.splitlines() if l.startswith('NEAR_FADE_RESULT ')))
        checks=data['checks']
        required={'overlapping body invisible','overlapping tag invisible','close body invisible','close tag invisible'}
        if not isinstance(checks, dict) or not REQUIRED <= checks.keys(): return False
        if expect: return returncode==0 and all(checks.values())
        return (returncode==1 and not checks['overlapping body invisible'] and not checks['close body invisible']
                and all(v for k,v in checks.items() if k not in required))
    except (ValueError, StopIteration, KeyError, TypeError): return False


def main():
    only = sys.argv[1:]; results = []; saves0 = real_saves()
    evidence = os.environ.get('QA_NEAR_KEEP')
    if evidence: os.makedirs(evidence, exist_ok=True)
    for name, var, ca, expect in SCENARIOS:
        if only and not any(o in name for o in only): continue
        print('\n=== %s' % name, flush=True)
        env = {**os.environ, 'CFG_A': 'name=Alpha\n' + ca, 'CFG_B': 'name=Beta', 'WITNESS_KEEP_VIDEO': '1'}
        for d in (DIR1, DIR2):
            sd = os.path.join(d, 'appdata', 'The Witness'); os.makedirs(sd, exist_ok=True); vp = os.path.join(sd, 'Launcher.variables')
            if os.path.exists(vp): os.remove(vp)
            # Only A's depth buffer is under test; the sending peer can use the lowest options.
            options = var if d == DIR1 else video(profile=0,msaa=0,shading=0,reflection=0,texture=0,texture_detail=0,geometry=0)
            if options: open(vp, 'w').write(options)
        label = ''.join(c if c.isalnum() else '_' for c in name)[:60]
        launch = subprocess.run([os.path.join(TOOLS, 'restart.sh')], env=env, capture_output=True, text=True, timeout=600)
        if evidence: open(os.path.join(evidence, label + '_launch.txt'), 'w').write(launch.stdout + launch.stderr)
        if launch.returncode: raise RuntimeError('client startup failed: ' + launch.stderr[-1200:])
        time.sleep(40)
        open(os.path.join(RUN, 'guard.stop'), 'w').write('bot starting')
        r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wnear_fade.py')), pid('A'), win(DIR1), pid('B'), win(DIR2), name.split(':', 1)[1].strip()], capture_output=True, text=True, timeout=300)
        print(r.stdout.strip(), r.stderr.strip(), flush=True)
        if evidence:
            for side, directory in (('A', DIR1), ('B', DIR2)):
                shutil.copy(os.path.join(directory, 'witness_proxy.log'), os.path.join(evidence, label + '_' + side + '.log'))
            open(os.path.join(evidence, label + '_result.txt'), 'w').write(r.stdout + r.stderr)
        safe = real_saves() == saves0; print('  hygiene: real saves untouched %s' % ('ok' if safe else 'FAILED'))
        ok = verdict(r.returncode, r.stdout, expect) and safe
        print('RESULT: %s%s' % ('PASS' if ok else 'FAIL', '' if expect else ' (mutant must fail near-body checks)'), flush=True)
        results.append((name, ok))
    for d in (DIR1, DIR2):
        vp = os.path.join(d, 'appdata', 'The Witness', 'Launcher.variables')
        if os.path.exists(vp): os.remove(vp)
    print('\n==== SUMMARY')
    for n, ok in results: print('%-62s %s' % (n, 'PASS' if ok else 'FAIL'))
    print('ALL OK' if results and all(ok for _, ok in results) else 'SOME FAILED')
    return 0 if results and all(ok for _, ok in results) else 1

if __name__ == '__main__': sys.exit(main())
