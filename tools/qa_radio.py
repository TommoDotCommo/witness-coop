#!/usr/bin/env python3
"""Live radio scenario and rendering/transport/presence mutants; tools/qa.sh radio.
Requires Windows and two test copies. Never run as a T0 check.
"""
import os, shutil, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import ROOT, TOOLS, DIR1, DIR2, PYW, pid, win, real_saves
import qa_sweep

def run(a='', b='', mode='enabled'):
    qa_sweep.restart('voice_mode=ptt\nvoice_test_tone=120,1000,5500\nvoice_mute_output=1\nkey_radio=R\nradio_enabled=1\n' + a,
                     'voice_mode=off\nvoice_mute_output=1\nradio_enabled=1\n' + b)
    result = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wradio.py')), str(pid('A')), win(DIR1), str(pid('B')), win(DIR2), mode],
                            capture_output=True, text=True, timeout=180)
    print(result.stdout, end='')
    if result.returncode and result.stderr: print(result.stderr, end='')
    return result.returncode == 0, 'PASS A starts idle' in result.stdout and 'RADIO_ASSERTION:' in result.stdout

def main():
    before = real_saves(); results = []
    try:
        clean = run()[0]; results.append(('clean', clean))
        if clean:
            out = os.path.join(ROOT, 'local', 'radio-captures'); os.makedirs(out, exist_ok=True)
            for directory, name in [(DIR1, 'radio_fp'), (DIR2, 'radio_tp'), (DIR1, 'radio_stowed')]:
                shutil.copyfile(os.path.join(directory, 'shots', name+'.bmp'), os.path.join(out, name+'.bmp'))
        results.append(('disabled', run('radio_enabled=0\n', mode='disabled')[0]))
        for name, a, b in [('no voice send', 'no_voice_send=1\n', ''), ('no radio channel', 'no_voice_channel=1\n', ''), ('no speaking presence', 'no_speaking_flag=1\n', ''),
                           ('no first-person draw', 'no_radio_view=1\n', ''), ('no remote arm pose', '', 'no_radio_pose=1\n')]:
            results.append(('mutant '+name, run(a,b)[1]))
    finally:
        results.append(('real saves byte-identical', real_saves() == before))
    for name, ok in results: print(('PASS ' if ok else 'FAIL ') + name)
    return 0 if all(ok for _, ok in results) else 1

if __name__ == '__main__': sys.exit(main())
