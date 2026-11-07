"""Game-free test of wdraw's orientation rounds (tools/win/wrounds.py). Run: python3 tools/test_wrounds.py [--mutant NAME]"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'win'))
import wrounds

MUT = sys.argv[sys.argv.index('--mutant') + 1] if '--mutant' in sys.argv else None
if MUT == 'no_quarter': wrounds.QUARTER = ()
if MUT == 'no_flips': wrounds.QUARTER = wrounds.QUARTER[:1]
if MUT == 'quarter_diamond_cands': wrounds.QUARTER_CANDS = wrounds.ROLL_CANDS

def check(ok, what):
    if not ok: print('FAIL', what); sys.exit(1)

r = wrounds.orientation_rounds([], 0)
quarter = [x for x in r if len(x) > 3 and x[3] == 90]
check(len(quarter) == 4, 'four quarter-turn rounds (all flips): %r' % quarter)
check(len({(x[1], x[2]) for x in quarter}) == 4, 'every flip of the quarter turn is tried')
check(len({(x[1], x[2]) for x in r if len(x) == 3 and x[0] == 1.0} | {(1, 1)}) == 4, 'roll 0 has all four flips')
check(r.index(quarter[0]) < r.index((1.0, 1, 1, 45)), 'a quarter turn is tried before the diamond')
check(len(set(r)) == len(r) - 1, 'no round twice except the first (the cached guess, then the plain one)')
check(wrounds.candidates(quarter[0], 3, [(0.5, 0)]) == wrounds.QUARTER_CANDS, 'quarter turn scans plain sizes')
check(all(w < 1.0 for w, _ in wrounds.candidates(quarter[0], 3, [])), 'quarter turn does not scan diamond sizes')
check(wrounds.candidates((1.0, 1, 1, 45), 5, []) == wrounds.ROLL_CANDS, 'diamond scans big sizes')
check(wrounds.candidates((1.0, 1, 1), 0, [(0.5, 0)]) == [(0.5, 0)] and wrounds.candidates((1.0, 1, 1), 1, [(0.5, 0)]) == [(0.5, 0)], 'first two rounds use the cached list')
check(wrounds.candidates((1.4, -1, -1), 4, [(0.5, 0)]) is None, 'later plain rounds use the default scan')
# a refuted orientation is dropped and its mirrors come first (behaviour kept from before)
b = [(1.0, 1, 1, 0)]
r2 = wrounds.orientation_rounds(b, 0)
check((1.0, 1, 1) not in r2[2:], 'refuted orientation not retried')
check(r2[:2] == [(1.0, 1, -1), (1.0, -1, 1)], 'mirrors first: %r' % r2[:2])
# a refuted quarter turn: dropped, mirror images of it first
r3 = wrounds.orientation_rounds([(1.0, 1, 1, 90)], 0)
check((1.0, 1, 1, 90) not in r3 and r3[0] == (1.0, 1, -1, 90), 'refuted quarter turn: %r' % r3[:2])
print('ALL OK')
