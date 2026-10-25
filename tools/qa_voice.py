#!/usr/bin/env python3
"""T2 scenario for voice chat (proxy/voice_audio.cpp, tools/win/wvoice.py): A transmits a test voice of three tones, B's mixer must hear it with the loudness and side the real geometry gives
(positions read from both games), turn it into a walkie-talkie far away, fade between, and stay silent beyond the radio range; push to talk must send only while the key is held; the
speaking flag must reach the other game; the game's volume must be set on its audio session. B runs with voice_volume=50, voice_radio_range=100 and game_volume=35 so the settings are checked too.
Mutants: A sends nothing (no_voice_send), B drops every voice (no_voice_play), B counts everyone at distance 0 (voice_no_pos), A never sets the speaking flag (no_speaking_flag), the game volume is not set (no_game_volume).
usage: qa_voice.py (called by tools/qa.sh voice)"""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, DIR2, PYW, pid, win
import qa_sweep

TONES = 'voice_test_tone=120,1000,5500\nvoice_mute_output=1\n'

def run(mode, cfg_a='', cfg_b='', gv=35):
    a = ('name=Alpha\n' + TONES + ('voice_mode=open\n' if mode in ('open', 'channels') else 'voice_mode=ptt\n') + cfg_a).strip()
    b = ('name=Beta\nvoice_mute_output=1\nvoice_mode=off\n' + ('voice_volume=50\nvoice_radio_range=100\ngame_volume=%d\n' % gv if mode == 'open' else 'voice_volume=50\n' if mode == 'channels' else '') + cfg_b).strip()
    qa_sweep.restart(a, b)
    args = [str(pid('A')), win(DIR1), str(pid('B')), win(DIR2), mode] + (['0.5', '100', '%.2f' % (gv / 100.0)] if mode == 'open' else ['0.5'] if mode == 'channels' else [])
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wvoice.py'))] + args, capture_output=True, text=True, timeout=600)
    print(r.stdout, end='')
    if r.returncode != 0 and r.stderr.strip(): print('  (the bot said:)', r.stderr.strip()[-600:])
    return r.returncode == 0 and 'ALL OK' in r.stdout

def main():
    results = []
    only = sys.argv[1:]      # e.g. `qa.sh voice channels`: only that mode (and its mutants)
    for mode in [m for m in ('open', 'ptt', 'channels') if not only or m in only]:
        print('=== voice: clean build (%s)' % mode, flush=True); ok = run(mode); results.append(('clean ' + mode, ok)); print('clean %s: %s' % (mode, 'PASS' if ok else 'FAIL'), flush=True)
    for name, ca, cb, mode in (('no_voice_send (A transmits nothing)', 'no_voice_send=1', '', 'open'), ('no_voice_play (B drops every voice)', '', 'no_voice_play=1', 'open'), ('voice_no_pos (everyone at distance 0)', '', 'voice_no_pos=1', 'open'),
                         ('no_speaking_flag (the flag is never set)', 'no_speaking_flag=1', '', 'open'), ('no_game_volume (the game volume is not set)', '', 'no_game_volume=1', 'open'),
                         ('no_voice_channel (the chosen channel is never sent)', 'no_voice_channel=1', '', 'channels'), ('no_voice_channel_key (the channel key does nothing)', 'no_voice_channel_key=1', '', 'channels')):
        if only and mode not in only: continue
        print('=== voice: MUTANT %s' % name, flush=True); caught = not run(mode, ca, cb, 60 if 'game_volume' in name else 35)      # Windows remembers a program's volume: the mutant run asks for a different one, so a volume left over from the last run cannot
        results.append(('mutant ' + name, caught)); print('mutant %s: %s' % (name, 'caught' if caught else 'SURVIVED'), flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
