#!/usr/bin/env python3
"""Makes common/tests/wpuzzle_fixture.txt: random puzzles of every kind the editor makes, each with the number of solutions (up to 200) and the
first one, as jbzdarkid's own engine (witnesspuzzles.com) finds them. common/wpuzzle_test.cpp then checks the rules of common/wpuzzle.h against it
without node or his repository.

    tools/wpuzzle_fixture.py <a clone of github.com/jbzdarkid/jbzdarkid.github.io> [--count N] [--seed S]

The puzzles are random (ours, made here); only the answers come from his engine. Puzzles the game cannot show (common/wpuzzle.h Problems) are
dropped. Run it again only when the rules change on purpose.
"""
import argparse, json, os, random, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
COLOURS = ['black', 'white', 'orange', 'purple', 'green', 'cyan', 'magenta', 'yellow', 'blue', 'red']
SHAPES = [0x1, 0x3, 0x11, 0x13, 0x31, 0x7, 0x111, 0x33, 0x17, 0x71, 0x27, 0x36]      # monomino to tetrominoes (bits x * 4 + y)

def make(rng):
    W, H = rng.randint(1, 4), rng.randint(1, 4)
    if W * H < 2: W = 2
    sym = rng.random() < 0.25 and W >= 2
    sx = sym and rng.random() < 0.7; sy = sym and (not sx or rng.random() < 0.4)
    gw, gh = 2 * W + 1, 2 * H + 1
    g = [[None if x % 2 and y % 2 else {'type': 'line', 'line': 0} for y in range(gh)] for x in range(gw)]
    def mirror(x, y): return (gw - 1 - x if sx else x, gh - 1 - y if sy else y)
    def flip(d):
        if sx and d in ('left', 'right'): return 'right' if d == 'left' else 'left'
        if sy and d in ('top', 'bottom'): return 'bottom' if d == 'top' else 'top'
        return d
    corners = [(x, y) for x in range(0, gw, 2) for y in range(0, gh, 2)]
    for _ in range(rng.choice([1, 1, 2])):
        x, y = rng.choice(corners); g[x][y]['start'] = True
        if sym: mx, my = mirror(x, y); g[mx][my]['start'] = True
    border = [(x, y) for x in range(0, gw, 2) for y in range(0, gh, 2) if x in (0, gw - 1) or y in (0, gh - 1)]
    for _ in range(rng.choice([1, 1, 2])):
        x, y = rng.choice(border)
        dirs = [d for d, ok in (('left', x == 0), ('right', x == gw - 1), ('top', y == 0), ('bottom', y == gh - 1)) if ok]
        d = rng.choice(dirs); g[x][y]['end'] = d
        if sym: mx, my = mirror(x, y); g[mx][my]['end'] = flip(d)
    palette = rng.sample(COLOURS, rng.randint(1, 3))
    for x in range(1, gw, 2):
        for y in range(1, gh, 2):
            if rng.random() > 0.45: continue
            k = rng.choice(['square', 'square', 'star', 'star', 'nega', 'triangle', 'poly', 'poly', 'ylop'])
            c = {'type': k, 'color': rng.choice(palette)}
            if k == 'nega': c['color'] = rng.choice(['white'] + palette)
            if k == 'triangle': c['count'] = rng.randint(1, 3)
            if k in ('poly', 'ylop'): c['polyshape'] = rng.choice(SHAPES) | (1 << 20 if rng.random() < 0.4 else 0)
            g[x][y] = c
    for x in range(gw):
        for y in range(gh):
            if x % 2 and y % 2: continue
            c = g[x][y]; r = rng.random()
            if c.get('start') or c.get('end'): continue
            if r < 0.06: c['dot'] = rng.choice([2, 3]) if sym else 1
            elif r < 0.09 and (x + y) % 2: c['gap'] = 1
            elif r < 0.10 and (x + y) % 2: c['gap'] = 2
    g = [[0 if c == {'type': 'line', 'line': 0} else c for c in col] for col in g]      # (a plain line is 0: both readers take it as an empty line)
    p = {'name': 'random', 'grid': g, 'pillar': False}
    if sym: p['symmetry'] = {'x': sx, 'y': sy}
    return json.dumps(p, separators=(',', ':'))

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('repo'); ap.add_argument('--count', type=int, default=600); ap.add_argument('--seed', type=int, default=1)
    a = ap.parse_args(); rng = random.Random(a.seed)
    out = tempfile.mkdtemp(prefix='wpuzzle-fixture-'); cli = os.path.join(out, 'wpuzzle-cli')
    subprocess.run(['g++', '-std=c++17', '-O2', '-o', cli, os.path.join(ROOT, 'common', 'wpuzzle_cli.cpp')], check=True)
    cands = [make(rng) for _ in range(a.count * 8)]
    ours = subprocess.run([cli, '1'], input='\n'.join(cands) + '\n', capture_output=True, text=True, check=True).stdout.splitlines()
    cands = [c for c, o in zip(cands, ours) if not o.startswith('skip')]
    answers = subprocess.run(['node', os.path.join(HERE, 'wpuzzle_diff.js'), os.path.join(a.repo, 'engine'), '200'], input='\n'.join(cands) + '\n',
                             capture_output=True, text=True, check=True).stdout.splitlines()
    solv = [(c, h) for c, h in zip(cands, answers) if not h.startswith('0')]; unsolv = [(c, h) for c, h in zip(cands, answers) if h.startswith('0')]
    picked = solv[:a.count * 2 // 3]; picked += unsolv[:a.count - len(picked)]      # two thirds solvable
    keep = [c for c, _ in picked]; his = [h for _, h in picked]
    path = os.path.join(ROOT, 'common', 'tests', 'wpuzzle_fixture.txt'); os.makedirs(os.path.dirname(path), exist_ok=True)
    solvable = 0
    with open(path, 'w') as f:
        f.write('# random puzzles (tools/wpuzzle_fixture.py --seed %d) with the solutions jbzdarkid\'s engine finds: count (up to 200), first line; then the puzzle\n' % a.seed)
        for c, h in zip(keep, his):
            n, _, first = h.partition(' ')
            solvable += n != '0'
            f.write('%s\t%s\t%s\n' % (n, first or '-', c))
    print('wpuzzle_fixture: %d puzzles (%d solvable) in %s' % (len(keep), solvable, os.path.relpath(path, ROOT)))

if __name__ == '__main__': sys.exit(main())
