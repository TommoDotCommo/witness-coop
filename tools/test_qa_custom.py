#!/usr/bin/env python3
"""Game-free tests for the pieces tools/qa_custom.py relies on: the puzzle CLI's modes (common/wpuzzle_cli.cpp: print, set, solutions, check) and
the mapping between the editor's grid and a panel's nodes (node_map, to_nodes, to_line), on panels built here the way the game lays them out
(nodes row by row from the bottom, y up in panel space, an end's stub outside the grid, a node in the middle of an edge for a dot).
Mutant (--mutants): the orientation taken without matching the starts (the panel's y the wrong way up) must fail.   usage: python3 tools/test_qa_custom.py [--mutants]"""
import os, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qa_custom

FLIP_WRONG = os.environ.get('QA_CUSTOM_MUT') == 'flip'

def panel(W, H, start=(0, 0), end=None, dot_edge=None):
    """A panel like the game's: (W+1) x (H+1) nodes, index = col + row * (W+1), row 0 at the bottom; pos in 0.14..0.86; `start`/`end` in editor
    coordinates (y down); an end's stub to the right of its node; a dot node in the middle of the edge `dot_edge` (two editor corners)."""
    n = (W + 1) * (H + 1); pos = []; flags = [0x600000] * n; ca, cb = [], []
    def node(x, y): return (x // 2) + (H - y // 2) * (W + 1)      # editor (x, y) corner -> node index
    for r in range(H + 1):
        for c in range(W + 1):
            pos += [0.14 + 0.72 * c / max(1, W), 0.14 + 0.72 * r / max(1, H)]
            i = c + r * (W + 1)
            if c < W: ca.append(i); cb.append(i + 1)
            if r < H: ca.append(i); cb.append(i + W + 1)
    flags[node(*start)] |= 2
    end = end or (2 * W, 0); e = node(*end)
    flags.append(0x200001); pos += [pos[2 * e] + 0.05, pos[2 * e + 1]]; ca.append(e); cb.append(n); n += 1
    if dot_edge:
        a, b = node(*dot_edge[0]), node(*dot_edge[1]); k = [i for i in range(len(ca)) if {ca[i], cb[i]} == {a, b}][0]
        del ca[k]; del cb[k]
        flags.append(0x200020); pos += [(pos[2 * a] + pos[2 * b]) / 2, (pos[2 * a + 1] + pos[2 * b + 1]) / 2]; ca += [a, n]; cb += [n, b]; n += 1
    return {'id': 0, 'flags': flags, 'pos': pos, 'ca': ca, 'cb': cb, 'gx': W + 1, 'gy': H + 1}, node

def pick(q, W, H, starts):
    maps = qa_custom.node_map(q, W, H)
    if FLIP_WRONG: return maps[0]      # mutant: the first orientation, without matching the starts (the panel's y taken the way the editor's runs)
    return next((m for m in maps if {m[i] for i, f in enumerate(q['flags']) if f & 2 and not f & 1} == starts), maps[0])

def test_mapping():
    q, node = panel(3, 2, start=(0, 4), dot_edge=((2, 4), (4, 4)))
    m = pick(q, 3, 2, {(0, 4)})
    assert m[node(0, 4)] == (0, 4) and m[node(6, 0)] == (6, 0) and m[node(2, 2)] == (2, 2), 'corners'
    assert m[len(q['flags']) - 1] == (3, 4), 'the dot node is the middle of its edge: %s' % (m[len(q['flags']) - 1],)
    assert m[len(q['flags']) - 2] is None, "the end's stub is outside the grid"
    line = [(0, 4), (1, 4), (2, 4), (3, 4), (4, 4), (5, 4), (6, 4), (6, 3), (6, 2), (6, 1), (6, 0)]
    nodes = qa_custom.to_nodes(q, m, line)
    assert nodes[0] == node(0, 4) and nodes[-1] == len(q['flags']) - 2 and len(q['flags']) - 1 in nodes, nodes
    assert qa_custom.to_line(m, nodes) == line, (qa_custom.to_line(m, nodes), line)

def cli():
    exe = os.path.join(tempfile.mkdtemp(prefix='wpuzzle-cli-'), 'wpuzzle-cli')
    subprocess.run(['g++', '-std=c++17', '-O2', '-o', exe, os.path.join(qa_custom.ROOT, 'common', 'wpuzzle_cli.cpp')], check=True)
    return exe

def test_cli(exe):
    d = tempfile.mkdtemp(prefix='wpuzzle-set-'); raw = os.path.join(d, 'raw.txt'); open(raw, 'w').write(qa_custom.SET)
    text = subprocess.run([exe, 'print', raw], capture_output=True, text=True, check=True).stdout
    assert text.startswith('witness-coop puzzles 1\n') and text.count('\npuzzle ') == qa_custom.SET.count('\npuzzle '), 'print keeps every puzzle'
    canon = os.path.join(d, 'canon.txt'); open(canon, 'w', newline='\n').write(text)
    assert subprocess.run([exe, 'print', canon], capture_output=True, text=True).stdout == text, 'print is stable'
    info = subprocess.run([exe, 'set', canon], capture_output=True, text=True, check=True).stdout.splitlines()
    assert info[0].startswith('hash ') and len(info[0].split()[1]) == 16, info[0]
    assert len(info) == 1 + qa_custom.SET.count('\npuzzle '), 'a line per puzzle'
    for l in info[1:]: assert ' problems 0 ' in l and ' solutions 0 ' not in l, 'every test puzzle is fine and solvable: ' + l
    sols = subprocess.run([exe, 'solutions', canon, '4', '10'], capture_output=True, text=True).stdout.splitlines()
    assert len(sols) == 2, 'the colours puzzle has 2 solutions: %d' % len(sols)
    for s in sols: assert subprocess.run([exe, 'check', canon, '4'] + s.split(), capture_output=True, text=True).stdout.strip() == 'ok', s
    outer = ['%d,%d' % (0, y) for y in range(6, -1, -1)] + ['%d,%d' % (x, 0) for x in range(1, 9)]
    assert subprocess.run([exe, 'check', canon, '4'] + outer, capture_output=True, text=True).stdout.startswith('no '), 'the outer line is refused'

if __name__ == '__main__':
    if '--mutants' in sys.argv:
        r = subprocess.run([sys.executable, os.path.abspath(__file__)], env={**os.environ, 'QA_CUSTOM_MUT': 'flip'}, capture_output=True, text=True)
        print('mutant flip %s' % ('caught' if r.returncode else 'NOT CAUGHT')); sys.exit(0 if r.returncode else 1)
    test_mapping(); print('ok mapping')
    test_cli(cli()); print('ok cli')
    if not FLIP_WRONG:      # (and its mutant, so a plain run, as tools/t0.sh makes it, checks that the test can fail)
        r = subprocess.run([sys.executable, os.path.abspath(__file__)], env={**os.environ, 'QA_CUSTOM_MUT': 'flip'}, capture_output=True, text=True)
        print('mutant flip %s' % ('caught' if r.returncode else 'NOT CAUGHT'))
        if not r.returncode: sys.exit(1)
    print('qa_custom pieces: ok')
