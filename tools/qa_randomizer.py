#!/usr/bin/env python3
"""T2 scenario for the built-in randomizers (proxy/randomizer, common/randomizers.h). Both games start a new game with randomizer=<spec>, as the
lobby writes it into every player's cfg:
  1. each game makes and applies this world's puzzles (randstat: state applied), both fingerprints are the same and not 0, and each game has the
     other's fingerprint (the WTR6 message)
  2. the puzzles read from outside each game (tools/win/wpanels.py: grid, decorations, dots) are the same in both games and are not the game's own
     (the first tutorial panels' decorations and dots, read from a game with no randomizer that this run starts first)
  3. a randomized panel drawn by the bot in A (its line from tools/solver.py, from what the panel holds now) is solved in A and then in B
  4. B started again with another seed: each game says the other's puzzles differ (wnotice::PuzzlesDiffer in its log)
Mutant (--mutant): B runs the proxy built with WR_MUT_NO_APPLY (it makes the puzzles and never writes them): check 2 must fail.
usage: qa_randomizer.py [--mutant] [--spec SPEC]   (tools/qa.sh randomizer)"""
import json, os, re, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import ROOT, TOOLS, DIR1, DIR2, pid, pyw, win
import qa_sweep, solver, sweep

SPEC = sys.argv[sys.argv.index('--spec') + 1] if '--spec' in sys.argv else 'normal;seed:31337'
MUTANT = '--mutant' in sys.argv
PANELS = [0x293, 0x5d, 0x5e, 0x5f, 0x60, 0x61]      # tutorial panels near the start that every randomizer makes anew (0x64 carries the seed, 0x182 the good-luck line)

def log(d): return open(os.path.join(d, 'witness_proxy.log'), errors='ignore').read()
def send(d, *lines):
    cmd = os.path.join(d, 'witness_cmd.txt'); t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)
    open(cmd + '.tmp', 'w').write('\n'.join(lines) + '\n'); os.replace(cmd + '.tmp', cmd)
    t = time.time()
    while os.path.exists(cmd) and time.time() - t < 8: time.sleep(0.05)
    time.sleep(1.0)
def randstat(d):
    send(d, 'randstat'); lines = [l for l in log(d).splitlines() if l.startswith('randstat:')]
    main = [l for l in lines if l.startswith('randstat: state ')][-1:]
    st = dict(re.findall(r' (state|fingerprint|entities|ms) (\S+)', main[0])) if main else {}
    peers = {m[0]: (m[1], m[2]) for m in re.findall(r'randstat: peer (\d+) fingerprint (\S+) state (\d+)', '\n'.join(lines[-6:]))}
    return st, peers
def panels(which):
    out = pyw('wpanels.py', pid(which), '--full', '--ids', ','.join('%x' % i for i in PANELS), timeout=120)
    js = [json.loads(l) for l in out.splitlines() if l.startswith('[') or l.startswith('{')]
    rows = js[-1] if js and isinstance(js[-1], list) else [j for j in js if isinstance(j, dict)]
    return {r['id']: r for r in rows}
def puzzle(r): return (r.get('gx'), r.get('gy'), r.get('style'), tuple(r.get('dec') or []), tuple(r.get('flags') or []), tuple(r.get('ca') or []), tuple(r.get('cb') or []), tuple(round(v, 4) for v in r.get('pos') or []))

def main():
    ok = True
    def check(what, cond, detail=''):
        nonlocal ok; ok = ok and bool(cond); print('  %-100s %s  %s' % (what, 'ok' if cond else 'FAILED', detail), flush=True)
    env_b = {}
    if MUTANT:
        subprocess.run(['make', '-s', '-C', os.path.join(ROOT, 'proxy'), 'dsound_mut_rand_NO_APPLY.dll'], check=True)
        env_b = {'WITNESS_DLL_B': os.path.join(ROOT, 'proxy', 'dsound_mut_rand_NO_APPLY.dll')}
    os.environ.update(env_b)
    # the game's own puzzles, for check 2: one game with no randomizer
    print('=== the game\'s own puzzles (no randomizer)', flush=True)
    qa_sweep.restart('name=Alpha\nrandomizer=off', 'name=Beta\nrandomizer=off')
    vanilla = panels('A')
    print('=== both games with randomizer=%s%s' % (SPEC, ' (B: the NO_APPLY mutant)' if MUTANT else ''), flush=True)
    t0 = time.time(); qa_sweep.restart('name=Alpha\nrandomizer=' + SPEC, 'name=Beta\nrandomizer=' + SPEC)
    print('  both settled %.0f s after the restart was asked' % (time.time() - t0), flush=True)
    for d in (DIR1, DIR2):
        print('  ' + ' | '.join(l for l in log(d).splitlines() if l.startswith('randomizer: applied') or l.startswith('randomizer: FAILED') or 'watchdog stopped' in l), flush=True)
    (sa, pa), (sb, pb) = randstat(DIR1), randstat(DIR2)
    fa, fb = sa.get('fingerprint', '0'), sb.get('fingerprint', '0')
    check('1. both games applied the puzzles', sa.get('state') == 'applied' and sb.get('state') == 'applied', (sa, sb))
    check('   the same fingerprint in both, not 0', fa == fb and fa.strip('0') != '', (fa, fb))
    check('   each game has the other\'s fingerprint', any(v[0] == fb for v in pa.values()) and any(v[0] == fa for v in pb.values()), (pa, pb))
    ra, rb = panels('A'), panels('B')
    same = all(i in ra and i in rb and puzzle(ra[i]) == puzzle(rb[i]) for i in PANELS)
    changed = [i for i in PANELS if i in ra and i in vanilla and puzzle(ra[i]) != puzzle(vanilla[i])]
    check('2. the puzzles read from each game are the same in both', same, [hex(i) for i in PANELS if not (i in ra and i in rb and puzzle(ra[i]) == puzzle(rb[i]))])
    check('   and they are not the game\'s own (panels changed: %d of %d)' % (len(changed), len(PANELS)), len(changed) >= len(PANELS) // 2, [hex(i) for i in changed])
    if MUTANT:
        print('qa_randomizer MUTANT: %s' % ('PASS (the check failed, as it must)' if not ok else 'FAIL (the checks passed with B never writing its puzzles)'), flush=True)
        return 0 if not ok else 1
    # 3. a randomized panel solved in A reaches B
    target, res = None, {}
    for i in changed:      # the first randomized panel the solver has a line for and the bot can draw (it cannot draw every shape Expert makes)
        q = ra[i]; path = solver.solve(dict(q, seq=[]))      # (not the game's stored line: it belongs to the panel's old puzzle)
        if not path: continue
        target = (q, path); res = sweep.draw(q, path, 'A')
        if res.get('solved'): break
        print('  (panel %#x: the bot could not draw it: %s; the next one)' % (i, res.get('why', '')), flush=True)
    check('3. the solver found a line for one of the randomized panels', target is not None)
    if target:
        q, path = target; t1 = time.time()
        on_b = False
        while time.time() - t1 < 30 and not on_b: on_b = q['id'] in qa_sweep.solved_set('B'); time.sleep(0.5) if not on_b else None
        check('   panel %#x drawn in A is solved in A' % q['id'], res.get('solved'), res.get('why', ''))
        check('   and in B (%.1f s later)' % (time.time() - t1), on_b)
    # 4. other puzzles in B: both games say so
    other = re.sub(r'seed:\d+', 'seed:4', SPEC) if 'seed:' in SPEC else SPEC + ';seed:4'
    qa_sweep.restart('', 'name=Beta\nrandomizer=' + other, which='B')
    t2 = time.time(); said = (False, False)
    while time.time() - t2 < 40 and not all(said):
        said = ('has different puzzles' in log(DIR1), 'has different puzzles' in log(DIR2)); time.sleep(1)
    check('4. B with another seed: A says Beta has different puzzles', said[0])
    check('   and B says Alpha has different puzzles', said[1])
    print('qa_randomizer: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    return 0 if ok else 1

if __name__ == '__main__': sys.exit(main())
