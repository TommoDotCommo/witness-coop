#!/usr/bin/env python3
"""T2 a remote solve does what the solver's did (tools/win/wreact.py): the solver's line is in the other panel's trace while the game's own
reactions run there (the Swamp rotating bridge, the Town lights), with nothing sent back, and is then taken out, so the line is drawn once, by the
overlay in the solver's colour. Twins: no_remote_line_write, remote_line_kept, no_remote_react."""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import TOOLS, DIR1, DIR2, PYW, RUN, pid, win
import qa_sweep

def probe(mode):
    r = subprocess.run([PYW, '-I', win(os.path.join(TOOLS, 'win', 'wreact.py')), str(pid('A')), win(DIR1), str(pid('B')), win(DIR2), mode], capture_output=True, text=True, timeout=300)
    print(r.stdout, end='')
    if r.returncode != 0 and r.stderr.strip(): print('  (the bot said:)', r.stderr.strip()[-600:])
    return r.returncode == 0 and 'ALL OK' in r.stdout

def line(cfg_b=''):
    qa_sweep.restart('name=Alpha', ('name=Beta\n' + cfg_b).strip())
    d = subprocess.run(['python3', os.path.join(TOOLS, 'puzzle.py'), 'solve', '64', 'A'], capture_output=True, text=True, timeout=600, env={**os.environ, 'WITNESS_RUN': RUN})
    print('  (bot: %s)' % d.stdout.strip().splitlines()[-1][:120] if d.stdout.strip() else '  (bot: nothing)', flush=True)
    return probe('line')

def react(cfg_b=''):
    qa_sweep.restart('name=Alpha\nstart_progress=preset:tutorial', ('name=Beta\nstart_progress=preset:tutorial\n' + cfg_b).strip())
    return probe('react')

def main():
    results = []
    for name, fn, cfg, want in (('line: clean', line, '', True), ('react: clean', react, '', True),
                                ('MUTANT no_remote_line_write (B has no line)', line, 'no_remote_line_write=1', False),
                                ('MUTANT remote_line_kept (the line stays in B\'s trace: the game draws it too)', line, 'remote_line_kept=1', False),
                                ('MUTANT no_remote_react (the reactions do not run on B)', react, 'no_remote_react=1', False)):
        print('=== react: %s' % name, flush=True); got = fn(cfg); good = got == want; results.append((name, good))
        print('%s: %s' % (name.split(' (')[0], ('PASS' if got else 'FAIL') if want else ('caught' if not got else 'NOT CAUGHT')), flush=True)
    bad = [n for n, g in results if not g]
    print('ALL OK' if not bad else 'FAILED: %s' % bad); return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
