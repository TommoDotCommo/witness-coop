#!/usr/bin/env python3
"""T2 scenario for "Your puzzles" (common/wpuzzle.h, RunCustom in proxy/randomizer/variants/nsv.cpp): a set of puzzles made as the editor makes
them goes on the game's panels, and the game itself must draw and check each one as the editor's rules (jbzdarkid's engine) say.
  1. both games start with randomizer=custom;set:<hash> and the set's file in their data folder: each applies it, with the same fingerprint
  2. each puzzle's panel holds it: the grid size, the start points and the number of symbols read from the game (tools/win/wpanels.py)
  3. a line the rules refuse (up the left edge and along the top of the colours puzzle: all its colours in one region) is drawn in A: the
     game must refuse it too (its verdict "wrong"), just before that puzzle's solution is drawn
  4. on every puzzle the line the repo's rule solver (tools/solver.py, psolve) finds from what the game's panel holds is drawn by the bot in A:
     the game and the editor's rules (wpuzzle-cli check) must judge it the same. When the game refuses it (or the bot did not get it to the
     game: a note), an editor solution is drawn (one starting up the left edge when there is one) and the game must take it. B must have each
     solved panel solved too; an in-game picture of each panel is kept (shots/custom_<n>.bmp in A's folder)
The set has every kind of symbol (squares and stars in several colours, stars counting triangles of their colour, triangles 1 to 3,
eliminators, turnable, fixed and negative shapes, dots on corners and lines, gaps, missing lines, symmetry with blue and yellow dots) and grids
from 3 x 3 to 7 x 7 and 7 x 2, on the Tutorial and the panels outside it.
The bot draws with the mouse (tools/win/wdraw.py), steering by feedback (WDRAW_STEER). On the first two Tutorial panels (doors in the tunnel)
it draws an editor solution that goes up first and straight into the end's stub last (it solved the first panel that way); where it does not
get a line to the game that is a note, and the panel is solved by the test-only command `nativesolve`, which powers the courtyard
(--no-tunnel: at once). At the Bend it stands closer (STAND): from 3 m out no click started a line there. The
courtyard's panels power one another (Front Center, then Center Left, then Front Left), so the next puzzle drawn is always one whose panel the
game reports powered: a black panel is never walked to. A drawing is tried again (up to 4 times) when the game did not judge it (the bot lost
the line, or did not reach the end), never when the game judged the line. What the bot remembers about these panels is forgotten first.
Mutant (--mutant): A runs the proxy built with WR_MUT_SHAPE_FLIPPED (polyominoes written upside down): the mirror puzzle (on Back Left, powered
from the start) still passes, and on the shapes puzzle the game and the editor's rules must disagree (the line the game holds a solution is not
one by the rules, or the game refuses it).
usage: qa_custom.py [--mutant] [--only N,N] [--no-tunnel]   (tools/qa.sh custom)"""
import json, os, re, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from witness import ROOT, TOOLS, DIR1, DIR2, RUN, pid, pyw, win
import qa_sweep, solver, sweep

MUTANT = '--mutant' in sys.argv
SKIP_TUNNEL = '--no-tunnel' in sys.argv      # do not let the bot try the two tunnel panels (solve them with nativesolve at once)
ONLY = [int(v) for v in sys.argv[sys.argv.index('--only') + 1].split(',')] if '--only' in sys.argv else None
SET = '''witness-coop puzzles 1
set In-game test
puzzle 3 3 none 0x00064 Squares, a dot, a gap
at 0 6 start
at 6 0 end right
at 1 1 square black
at 1 3 square black
at 5 3 square white
at 5 5 square white
at 3 4 dot black
at 3 0 gap break
puzzle 3 3 none 0x00182 Stars and triangles
at 0 6 start
at 6 0 end top
at 1 1 star magenta
at 5 5 star magenta
at 3 1 triangle orange 1
at 1 5 triangle orange 3
puzzle 4 4 none 0x00293 Shapes
at 0 8 start
at 8 0 end right
at 1 1 poly yellow 0x0013 rotated
at 5 5 poly yellow 0x0017
at 7 1 ylop blue 0x0001
puzzle 3 3 none 0x0A3B2 Eliminator
at 0 6 start
at 6 0 end right
at 1 1 square black
at 3 3 square white
at 5 5 square black
at 1 5 eliminator white
puzzle 4 3 none 0x00295 Colours
at 0 6 start
at 8 0 end right
at 1 1 square red
at 3 3 square green
at 5 5 square cyan
at 7 1 square magenta
at 7 5 square purple
at 1 5 square red
puzzle 4 4 x 0x0A3B5 Mirror
at 0 8 start
at 8 8 start
at 0 0 end left
at 8 0 end right
at 2 4 dot blue
at 6 6 dot yellow
at 1 1 square black
at 7 1 square white
puzzle 4 4 none 0x0005D Dots and gaps
at 0 8 start
at 8 0 end right
at 2 8 dot black
at 4 4 dot black
at 6 3 dot black
at 5 8 dot black
at 1 0 gap break
at 8 5 gap full
at 4 1 gap break
at 1 1 square black
at 7 7 square white
puzzle 4 4 none 0x0005E Stars and triangles
at 0 8 start
at 8 0 end top
at 1 1 star orange
at 7 7 star orange
at 3 3 star magenta
at 5 5 star magenta
at 1 7 triangle white 1
at 7 1 triangle white 2
at 1 3 triangle white 3
puzzle 7 7 none 0x018AF Big
at 0 14 start
at 14 0 end right
at 1 1 square black
at 13 13 square white
at 7 7 star cyan
at 9 9 star cyan
at 5 11 triangle orange 2
at 11 4 dot black
puzzle 7 2 none 0x0001B Wide
at 0 4 start
at 14 0 end right
at 1 1 square black
at 13 3 square white
at 7 1 triangle orange 1
'''
SHAPES = 2      # the puzzle the mutant must break
WRONG = 4       # the puzzle a wrong line is drawn on first
TUNNEL = (0, 1)      # on the first two Tutorial panels (doors in the tunnel): tried by the bot, solved by nativesolve when it cannot
CONTROL = 5     # the mutant's control (no polyominoes; Back Left is powered from the start)

def log(d): return open(os.path.join(d, 'witness_proxy.log'), errors='ignore').read()
def cli():
    exe = os.path.join(RUN, 'wpuzzle-cli')
    subprocess.run(['g++', '-std=c++17', '-O2', '-o', exe, os.path.join(ROOT, 'common', 'wpuzzle_cli.cpp')], check=True)
    return exe

def node_map(q, W, H):
    """Each panel node -> its place in the editor's grid ((x, y), y down, 0..2W x 0..2H), from the node positions; None for a node outside the grid
    (an end's stub). The orientation (which way the game's y runs) is the one that puts the game's start nodes on the puzzle's starts."""
    pos = q['pos']; n = len(q['flags']); xs = [pos[2 * i] for i in range(n)]; ys = [pos[2 * i + 1] for i in range(n)]
    grid = [i for i in range(n) if not q['flags'][i] & 1]      # (END nodes are outside the grid)
    x0, x1 = min(xs[i] for i in grid), max(xs[i] for i in grid); y0, y1 = min(ys[i] for i in grid), max(ys[i] for i in grid)
    def place(i, flip):
        fx = (xs[i] - x0) / (x1 - x0) * 2 * W if x1 > x0 else 0; fy = (ys[i] - y0) / (y1 - y0) * 2 * H if y1 > y0 else 0
        if flip: fy = 2 * H - fy
        gx, gy = round(fx), round(fy)
        return (gx, gy) if abs(fx - gx) < 0.15 and abs(fy - gy) < 0.15 and 0 <= gx <= 2 * W and 0 <= gy <= 2 * H else None
    return [{i: place(i, flip) for i in range(n)} for flip in (False, True)]

def to_nodes(q, m, line):
    """The editor's line (every half step) -> the panel's node list, the end's stub included."""
    at = {v: i for i, v in m.items() if v is not None}
    nodes = [at[p] for p in line if p in at]
    adj = {}
    for a, b in zip(q['ca'], q['cb']): adj.setdefault(a, []).append(b); adj.setdefault(b, []).append(a)
    ends = [w for w in adj.get(nodes[-1], []) if q['flags'][w] & 1]
    return nodes + ends[:1]

def to_line(m, nodes):
    """A panel node list -> the editor's line (every half step)."""
    pts = [m[i] for i in nodes if m.get(i) is not None]; out = []
    for p in pts:
        if out and abs(p[0] - out[-1][0]) + abs(p[1] - out[-1][1]) == 2: out.append(((p[0] + out[-1][0]) // 2, (p[1] + out[-1][1]) // 2))
        if not out or out[-1] != p: out.append(p)
    return out

def powered(which, panel, wait=20.0):
    """Whether the panel is powered in that game (read from the game: a black panel takes no line), waiting up to `wait` s for the power to come
    (a courtyard panel is powered by the cable of the one solved before it)."""
    t = time.time()
    while True:
        out = pyw('wpanels.py', pid(which), '--ids', '%x' % panel, timeout=60)
        rows = []
        for l in out.splitlines():
            if l.startswith('['): rows += json.loads(l)
            elif l.startswith('{'): rows.append(json.loads(l))
        on = any(r.get('id') == panel and r.get('powered') for r in rows)
        if on or time.time() - t > wait: return on
        time.sleep(1.0)

# The bot steers by feedback (WDRAW_STEER: it nudges and reads which edge the line went onto) rather than by projecting the panel: on the panels
# this set rewrites, the projection's first guess of how the panel is turned was refuted every time, and its fallback ran the line past turns.
os.environ['WDRAW_STEER'] = '1'; os.environ['WSLENV'] = os.environ.get('WSLENV', '') + ':WDRAW_STEER'

def own_line(exe, raw, p):
    """An editor solution of puzzle p the bot draws best: its first step up the start's line (the bot's steering starts best that way) and its last
    step heading where the end's stub points (the bot finds the stub by going on the same way; turning into it, it stopped on a grid edge and its
    finishing click dropped the line, on the first Tutorial panel). The best that is there, else the first solution."""
    sols = [[tuple(map(int, v.split(','))) for v in l.split()] for l in subprocess.run([exe, 'solutions', raw, str(p['k']), '200'], capture_output=True, text=True).stdout.splitlines() if l.strip()]
    def score(l):
        up = len(l) > 1 and l[1][0] == l[0][0]
        return (into_stub(p, l), up)
    return max(sols, key=score) if sols else p['line']

def into_stub(p, line):
    """The line's last step heads where the end's stub points (the bot then finishes it)."""
    ends = {tuple(map(int, l.split()[1:3])): l.split()[4] for l in sets_of(p).splitlines() if ' end ' in l}
    d = {'right': (1, 0), 'left': (-1, 0), 'top': (0, -1), 'bottom': (0, 1)}.get(ends.get(line[-1], '') if line else '', (0, 0))
    return len(line) > 1 and (line[-1][0] - line[-2][0], line[-1][1] - line[-2][1]) == d

def sets_of(p): return SET.split('\npuzzle ')[1:][p['k']]

def judged(r):
    """The game judged the drawn line: solved, or its verdict "wrong" (state 2 after the finishing click, tools/win/wdraw.py)."""
    return bool(r.get('solved')) or '(state 2)' in r.get('why', '')

STAND = {0x00182: '0,1.3'}      # the Bend in the tunnel: from 3 m out no click started a line; 1.3 m out (1.8 m from the panel) it did (2026-11-10)

def draw(q, nodes, tries=4):
    """sweep.draw, again whenever the bot did not get the whole line to the game (its guess of the panel on screen, its mouse gain: it learns both
    per panel), never when the game judged the line. A panel that is not powered is not walked to: no line starts on it, and that is not the
    puzzle's fault."""
    if not powered('A', q['id']): return {'solved': False, 'why': 'the panel is not powered (black) in A', 'unpowered': True}
    stand = STAND.get(q['id'])      # (where the bot stands, when 3 m out does not let a click enter the panel)
    os.environ['WDRAW_STAND'] = stand or ''
    if stand and ':WDRAW_STAND' not in os.environ.get('WSLENV', ''): os.environ['WSLENV'] = os.environ.get('WSLENV', '') + ':WDRAW_STAND'
    for t in range(tries):
        r = sweep.draw(q, nodes, 'A')
        if judged(r): break
        print('  (panel %#x: %s; drawing it again)' % (q['id'], r.get('why')), flush=True)
    return r

def main():
    ok = True
    def check(what, cond, detail=''):
        nonlocal ok; ok = ok and bool(cond); print('  %-100s %s  %s' % (what, 'ok' if cond else 'FAILED', detail), flush=True)
    exe = cli(); raw = os.path.join(RUN, 'custom_set.txt'); open(raw, 'w').write(SET)
    text = subprocess.run([exe, 'print', raw], capture_output=True, text=True, check=True).stdout
    open(raw, 'w', newline='\n').write(text)
    info = subprocess.run([exe, 'set', raw], capture_output=True, text=True, check=True).stdout.splitlines()
    h = info[0].split()[1]; puzzles = []
    for l in info[1:]:
        t = l.split(' line')[0].split(); line = [tuple(map(int, v.split(','))) for v in l.split(' line')[1].split()]
        puzzles.append({'k': int(t[1]), 'panel': int(t[3], 16), 'W': int(t[4]), 'H': int(t[5]), 'problems': int(t[7]), 'solutions': t[9], 'line': line})
    print('=== the set %s: %s' % (h, ', '.join('%d on %#x (%s solutions)' % (p['k'], p['panel'], p['solutions']) for p in puzzles)), flush=True)
    for d in (DIR1, DIR2):      # the set's copy as the launcher writes it when a game starts
        os.makedirs(os.path.join(d, 'puzzles'), exist_ok=True); f = os.path.join(d, 'puzzles', h + '.txt')
        if os.path.exists(f): os.remove(f)
        open(f, 'w', newline='\n').write(text)
    if MUTANT:
        subprocess.run(['make', '-s', '-C', os.path.join(ROOT, 'proxy'), 'dsound_mut_nsv_SHAPE_FLIPPED.dll'], check=True)
        os.environ['WITNESS_DLL_A'] = os.path.join(ROOT, 'proxy', 'dsound_mut_nsv_SHAPE_FLIPPED.dll')      # (tools/install.sh puts it into A only)
    try:      # what the bot remembers about these panels (orientation, size, gain) is about other puzzles or other runs: forgotten
        kc = json.load(open(sweep.KCACHE))
        for p in puzzles:
            for key in ('w%x', 'r%x', 'bad%x'): kc.pop(key % p['panel'], None)
        json.dump(kc, open(sweep.KCACHE, 'w'))
    except (OSError, ValueError): pass
    spec = 'custom;set:' + h
    t0 = time.time(); qa_sweep.restart('name=Alpha\nrandomizer=' + spec, 'name=Beta\nrandomizer=' + spec)
    print('  both settled %.0f s after the restart was asked' % (time.time() - t0), flush=True)
    for d in (DIR1, DIR2):
        print('  ' + ' | '.join(l for l in log(d).splitlines() if l.startswith('randomizer:'))[:400], flush=True)
    (sa, _), (sb, _) = qa_randomizer_stat(DIR1), qa_randomizer_stat(DIR2)
    check('1. both games applied the set', sa.get('state') == 'applied' and sb.get('state') == 'applied', (sa, sb))
    check('   the same fingerprint in both, not 0', sa.get('fingerprint') == sb.get('fingerprint') and sa.get('fingerprint', '0').strip('0') != '', (sa.get('fingerprint'), sb.get('fingerprint')))
    out = pyw('wpanels.py', pid('A'), '--full', '--ids', ','.join('%x' % p['panel'] for p in puzzles), timeout=120)
    js = [json.loads(l) for l in out.splitlines() if l.startswith('[') or l.startswith('{')]
    rows = {r['id']: r for r in (js[-1] if js and isinstance(js[-1], list) else [j for j in js if isinstance(j, dict)])}
    json.dump(rows, open(os.path.join(RUN, 'custom_panels.json'), 'w'))
    sets = SET.split('\npuzzle ')[1:]
    for p in puzzles:
        if ONLY and p['k'] not in ONLY: continue
        q = rows.get(p['panel'])
        if not q: check('2. puzzle %d: panel %#x read' % (p['k'], p['panel']), False); continue
        body = sets[p['k']]; nsym = len(re.findall(r' (square|star|eliminator|triangle|poly|ylop) ', body)); nstart = body.count(' start')
        starts = sum(1 for f in q['flags'] if f & 2 and not f & 1)
        check('2. puzzle %d on %#x: grid %dx%d, %d starts, %d symbols' % (p['k'], p['panel'], p['W'], p['H'], nstart, nsym),
              (q['gx'], q['gy']) == (p['W'] + 1, p['H'] + 1) and starts == nstart and sum(1 for v in q['dec'] if v) == nsym,
              'game: grid %sx%s, %d starts, %d symbols' % (q['gx'] - 1, q['gy'] - 1, starts, sum(1 for v in q['dec'] if v)))
        maps = node_map(q, p['W'], p['H'])
        startset = {tuple(map(int, s.split()[1:3])) for s in body.splitlines() if s.endswith(' start')}
        m = next((mm for mm in maps if {mm[i] for i, f in enumerate(q['flags']) if f & 2 and not f & 1} == startset), None)
        if m is None: check('   the panel\'s nodes match the puzzle\'s starts', False, [sorted(set(v for v in mm.values() if v)) [:6] for mm in maps]); continue
        p['map'] = m; p['q'] = q
    check('   every test puzzle passes the editor\'s checks (none would be left out of the game)', all(p['problems'] == 0 for p in puzzles), [(p['k'], p['problems']) for p in puzzles])
    pyw('wcmd.py', win(DIR1), 'shot custom_start'); time.sleep(1.5)      # (the first panel, in front of the player at the start)
    # 4. the editor's solution is taken by the game, in A and then in B (in the set's order: the courtyard's panels power one another)
    results = {}
    todo = [p for p in sorted(puzzles, key=lambda p: (p['k'] != CONTROL) if MUTANT else 0)      # (the mutant: its control first)
            if 'map' in p and not (ONLY and p['k'] not in ONLY) and not (MUTANT and p['k'] not in (SHAPES, CONTROL))]
    while todo:      # the next one is the first whose panel is powered (the courtyard's panels power one another); none: wait for the cable a while
        p = next((p for p in todo if powered('A', p['panel'], wait=0)), None) or next((p for p in todo if powered('A', p['panel'])), None)
        if p is None:
            for p in todo: check('4. puzzle %d on %#x: its panel is powered (black panels are not drawn on)' % (p['k'], p['panel']), False)
            break
        todo.remove(p)
        if p['k'] == WRONG and not MUTANT:      # 3. first a line the rules refuse, up the left edge and along the top: the game must refuse it too
            line = [(0, y) for y in range(2 * p['H'], -1, -1)] + [(x, 0) for x in range(1, 2 * p['W'] + 1)]
            bad = to_nodes(p['q'], p['map'], line)
            verdict = subprocess.run([exe, 'check', raw, str(WRONG)] + ['%d,%d' % v for v in line], capture_output=True, text=True).stdout.strip()
            check('3. the outer line on puzzle %d is refused by the rules' % WRONG, verdict.startswith('no'), verdict)
            r = draw(p['q'], bad)
            check('   and drawn in the game, the game refuses it', judged(r) and not r.get('solved'), {k: r.get(k) for k in ('solved', 'why', 'traced')})
            time.sleep(14.0)      # (the game takes a refused line off after its error flash, about 10 s: a line started sooner was lost)
        name = sets[p['k']].splitlines()[0].split(' ', 4)[-1]
        nodes = solver.solve(dict(p['q'], seq=[]))
        if p['k'] in TUNNEL and SKIP_TUNNEL:
            pyw('wcmd.py', win(DIR1), 'shot custom_%d' % p['k']); time.sleep(1.5); pyw('wcmd.py', win(DIR1), 'nativesolve %x' % p['panel']); time.sleep(3.0); continue
        if p['k'] in TUNNEL and not MUTANT:      # an editor solution the bot can finish here (up first, into the end's stub last); a bot that never got the line to the game is a note
            line = own_line(exe, raw, p); nodes = to_nodes(p['q'], p['map'], line); r = draw(p['q'], nodes, tries=2)
            verdict = subprocess.run([exe, 'check', raw, str(p['k'])] + ['%d,%d' % v for v in line], capture_output=True, text=True).stdout.strip()
            if judged(r):
                check('4. puzzle %d (%s) on %#x, drawn by the bot: the game and the editor\'s rules judge it the same' % (p['k'], name, p['panel']), bool(r.get('solved')) == (verdict == 'ok'), (r.get('why'), verdict))
            else: print('  NOTE puzzle %d on %#x: the bot did not get a line to the game (%s)' % (p['k'], p['panel'], r.get('why')), flush=True)
            pyw('wcmd.py', win(DIR1), 'shot custom_%d' % p['k']); time.sleep(1.5)
            if not r.get('solved'): pyw('wcmd.py', win(DIR1), 'nativesolve %x' % p['panel']); time.sleep(3.0)      # (it powers the next panel)
            continue
        if nodes:      # the repo's solver's line: the game and the editor's rules must judge it the same
            line = to_line(p['map'], nodes)
            verdict = subprocess.run([exe, 'check', raw, str(p['k'])] + ['%d,%d' % v for v in line], capture_output=True, text=True).stdout.strip()
            r = draw(p['q'], nodes, tries=4 if into_stub(p, line) else 1); game = bool(r.get('solved'))      # (one that turns into the stub: the bot has never finished one; one try)
            results[p['k']] = (game, verdict == 'ok') if judged(r) else None      # (None: not judged by the game)
            if results[p['k']] is None: print('  NOTE puzzle %d on %#x: the bot did not get the solver\'s line to the game (%s); the editor\'s own solution is drawn' % (p['k'], p['panel'], r.get('why')), flush=True)
            else: check('4. puzzle %d (%s) on %#x: the game and the editor\'s rules judge the solver\'s line the same' % (p['k'], name, p['panel']),
                  game == (verdict == 'ok'), 'game %s, rules %s (%s; %s)' % ('took it' if game else 'refused it' if results[p['k']] is not None else 'did not judge it', verdict, r.get('why', ''), ' '.join('%d,%d' % v for v in line)))
        else:      # (the mutant's upside-down shapes have no solution at all: what the game holds is not the editor's puzzle)
            r = {}; print('  (puzzle %d: the repo\'s solver has no line for what the game holds)' % p['k'], flush=True)
            if not MUTANT: check('4. puzzle %d (%s): the repo\'s solver has a line for it' % (p['k'], name), False)
        if not r.get('solved'):      # the editor's own solution (one that starts up the left edge when there is one: the bot draws those)
            own = own_line(exe, raw, p)
            r = draw(p['q'], to_nodes(p['q'], p['map'], own))
            if MUTANT: results[p['k']] = (bool(r.get('solved')), True) if judged(r) else None      # (the rules accept it: it is theirs)
            check('   the editor\'s own solution is taken by the game', r.get('solved'), {k: r.get(k) for k in ('why', 'traced')} if not r.get('solved') else '')
        pyw('wcmd.py', win(DIR1), 'shot custom_%d' % p['k']); time.sleep(1.5)
        if r.get('solved'):
            t1 = time.time(); on_b = False
            while time.time() - t1 < 30 and not on_b: on_b = p['panel'] in qa_sweep.solved_set('B'); time.sleep(0.5) if not on_b else None
            check('   and in B (%.1f s later)' % (time.time() - t1), on_b)
    if MUTANT:      # caught: the bot drew the puzzles before it, and the game refused the shapes puzzle's solution
        caught = results.get(CONTROL) == (True, True) and results.get(SHAPES) is not None and results[SHAPES] != (True, True)
        print('qa_custom MUTANT: %s' % ('PASS (on the shapes puzzle the game and the rules disagree, as they must: %s)' % (results[SHAPES],) if caught else 'FAIL (%s)' % results), flush=True)
        return 0 if caught else 1
    print('qa_custom: %s' % ('PASS' if ok else 'FAIL'), flush=True)
    return 0 if ok else 1

def qa_randomizer_stat(d):
    import qa_randomizer
    return qa_randomizer.randstat(d)

if __name__ == '__main__': sys.exit(main())
