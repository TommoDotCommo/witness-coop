#!/usr/bin/env python3
"""T2 scenario for the race mode (proxy/race/race.h, proxy/race_game.cpp, tools/win/wrace.py): two games with game_mode=race and race_goal=count:3 wait for each other, count down and
start together; panels solved in the countdown do not count; A's panels are not solved in B's world but B sees A's progress; A wins; both HUDs are drawn.
Mutants: the world is shared after all (race_emits on A and race_applies on B switch both protections off) and the HUD is not drawn (no_race_hud on B).
usage: qa_race.py (called by tools/qa.sh race)"""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, DIR2, PYW, pid, win
import qa_sweep

RACE = 'game_mode=race\nrace_goal=count:3\nrace_countdown_ms=45000\n'      # (a long countdown: the bot attaches when the games have settled, which can take a while beside other games)

def run(cfg_a='', cfg_b='', isolated='1'):
    qa_sweep.restart(('name=Alpha\n' + RACE + cfg_a).strip(), ('name=Beta\n' + RACE + cfg_b).strip())
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wrace.py')), str(pid('A')), win(DIR1), str(pid('B')), win(DIR2), isolated], capture_output=True, text=True, timeout=900)
    print(r.stdout, end='')
    if r.returncode != 0 and r.stderr.strip(): print('  (the bot said:)', r.stderr.strip()[-600:])
    return r.returncode == 0 and 'ALL OK' in r.stdout

def main():
    results = []
    print('=== race: clean build', flush=True); ok = run(); results.append(('clean', ok)); print('clean: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    print('=== race: MUTANT the world is shared (race_emits on A, race_applies on B)', flush=True); caught = not run('race_emits=1', 'race_applies=1', '1'); results.append(('mutant shared world', caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    print('=== race: MUTANT no HUD on B', flush=True); caught = not run('', 'no_race_hud=1'); results.append(('mutant no HUD', caught)); print('mutant: %s' % ('caught' if caught else 'NOT CAUGHT'), flush=True)
    bad = [n for n, ok in results if not ok]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
