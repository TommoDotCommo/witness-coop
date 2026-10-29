#!/usr/bin/env python3
"""Targeted real-renderer layout checks and captures. Run: tools/qa.sh probe tools/qa_cozy.py"""
import os, subprocess, sys
from witness import ROOT, TOOLS, DIR1, DIR2, RUN, PYW, pid, win, real_saves
import qa_sweep

def restart(cfg, first=False):
    # A failed native launch is a precondition failure, never a successful UI test.
    for attempt in range(2):
        subprocess.run([os.path.join(TOOLS, 'restart.sh')] + ([] if first else ['A']),
                       env={**os.environ, 'CFG_A': cfg, 'CFG_B': 'name=Companion'},
                       check=True, capture_output=True, text=True, timeout=240)
        with open(os.path.join(RUN, 'guard.stop'), 'w') as f: f.write('UI bot starting')
        dirs = [DIR1, DIR2] if first else [DIR1]
        ready = all(os.path.exists(os.path.join(d, 'witness_proxy.log')) and
                    'overlay: resources ready' in open(os.path.join(d, 'witness_proxy.log')).read() for d in dirs)
        if ready and qa_sweep.wait_settled(dirs, timeout=60): return
        print('RETRY: game did not reach the renderer/world precondition', flush=True)
    raise RuntimeError('game never reached the renderer/world precondition')

def main():
    before = real_saves(); os.environ['WITNESS_KEEP_VIDEO'] = '1'      # the scenario chooses the window size itself
    video = os.path.join(DIR1, 'appdata', 'The Witness', 'Launcher.variables')
    original = open(video, 'rb').read() if os.path.exists(video) else None
    native = '--native' in sys.argv
    args = [arg for arg in sys.argv[1:] if arg != '--native']
    sizes = args or (['1280x720'] if native else ['1280x720', '1920x1080', '2560x1440', '3440x1440', '3840x2160', '1024x768'])
    if native: assert len(sizes) == 1, 'native mode checks one window size per run'
    output = os.path.join(ROOT, 'local', 'cozy-native-captures' if native else 'cozy-captures')
    boot_width, boot_height = map(int, sizes[0].split('x')) if native else (1280, 720)
    try:
        os.makedirs(os.path.dirname(video), exist_ok=True)
        text = (':/launcher\nrender_profile 2\nfullscreen false\nvsync false\nhighdpi false\nresolution 4\n'
                'multisampling 0\ncustom_width %d\ncustom_height %d\nshading_quality 0\nreflection_quality 0\n'
                'texture_quality 2\ntexture_detail 1\ngeometry_detail 0\n') % (boot_width, boot_height)
        with open(video + '.tmp', 'w') as f: f.write(text)
        os.replace(video + '.tmp', video)
        restart('name=Explorer', first=True)
        for size in sizes:
            width, height = map(int, size.split('x'))
            result = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wcozy.py')),
                                     pid('A'), win(DIR1), win(output),
                                     str(width), str(height), 'native' if native else 'offscreen'], text=True, timeout=240)
            if result.returncode: return result.returncode
        if native: return 0
        print('=== MUTANT no_key_swallow: roster Escape must be caught', flush=True)
        restart('name=Explorer\nno_key_swallow=1')
        result = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wcozy.py')),
                                 pid('A'), win(DIR1), win(output),
                                 str(width), str(height), 'input'], capture_output=True, text=True, timeout=60)
        assert result.returncode != 0 and 'UI leaked input to pause menu' in result.stderr, result.stdout + result.stderr
        print('PASS no_key_swallow mutant caught by process pause state', flush=True)
        if not native:
            print('=== MUTANT no_detail_fit: the old detail pane layout must show clipped lines at 1920x1080', flush=True)
            restart('name=Explorer\nno_detail_fit=1')
            result = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wcozy.py')), pid('A'), win(DIR1), win(output), '1920', '1080', 'layout'], capture_output=True, text=True, timeout=60)
            assert 'layout clipped' in result.stdout and int(result.stdout.split('layout clipped')[1].split()[0]) > 0, 'the old layout was not reported as clipped: ' + result.stdout + result.stderr
            print('PASS no_detail_fit mutant: ' + result.stdout.strip(), flush=True)
        return 0
    finally:
        if original is not None:
            with open(video + '.tmp', 'wb') as f: f.write(original)
            os.replace(video + '.tmp', video)
        elif os.path.exists(video): os.remove(video)
        assert real_saves() == before, 'real saves changed'
        print('PASS real saves byte-identical', flush=True)
if __name__ == '__main__': sys.exit(main())
