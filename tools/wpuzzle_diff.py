#!/usr/bin/env python3
"""The puzzle rules of common/wpuzzle.h against jbzdarkid's own engine (witnesspuzzles.com), on the puzzles published there.

    tools/wpuzzle_diff.py <a clone of github.com/jbzdarkid/jbzdarkid.github.io> [--count N] [--max M] [--cells C]

Takes the published puzzles (play/*.html) that the game could have (common/wpuzzle_json.h ImportJson), at most C cells (default 25: his solver
is slow beyond), solves each with both (up to M solutions, default 50) and compares the number of solutions and the first one (both search in the
same order). Puzzles our solver needs more than 2 s for are left out (his is as slow on them). Game-free; needs node and g++. His repository is not part of the mod: clone it anywhere (it is only read).
"""
import argparse, glob, json, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)

def play_json(path):
    s = open(path, encoding='utf-8', errors='replace').read()
    m = re.search(r'Puzzle\.deserialize\("((?:[^"\\]|\\.)*)"\)', s)
    return json.loads('"' + m.group(1) + '"') if m else None

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('repo'); ap.add_argument('--count', type=int, default=400); ap.add_argument('--max', type=int, default=50)
    ap.add_argument('--cells', type=int, default=25); a = ap.parse_args()
    out = tempfile.mkdtemp(prefix='wpuzzle-diff-'); cli = os.path.join(out, 'wpuzzle-cli')
    subprocess.run(['g++', '-std=c++17', '-O2', '-o', cli, os.path.join(ROOT, 'common', 'wpuzzle_cli.cpp')], check=True)
    lines = []
    for f in sorted(glob.glob(os.path.join(a.repo, 'play', '*.html'))):
        j = play_json(f)
        if not j: continue
        try: p = json.loads(j)
        except ValueError: continue
        g = p.get('grid') or [[]]; w, h = (len(g) - 1) // 2, (len(g[0]) - 1) // 2
        if w * h > a.cells or p.get('pillar'): continue
        lines.append((os.path.basename(f)[:-5], j))
    feed = '\n'.join(j for _, j in lines) + '\n'
    ours = subprocess.run([cli, str(a.max)], input=feed, capture_output=True, text=True, check=True).stdout.splitlines()
    keep = [not o.startswith('skip') and not o.split(' ')[0].endswith('+') for o in ours]      # (a solve cut short by the time limit is not compared)
    picked = [l for l, k in zip(lines, keep) if k][:a.count]
    ours = [o for o, k in zip(ours, keep) if k][:a.count]
    feed = '\n'.join(j for _, j in picked) + '\n'
    his = subprocess.run(['node', os.path.join(HERE, 'wpuzzle_diff.js'), os.path.join(a.repo, 'engine'), str(a.max)], input=feed, capture_output=True, text=True, check=True).stdout.splitlines()
    same = diff = 0
    for (name, _), o, h in zip(picked, ours, his):
        if o == h: same += 1
        else:
            diff += 1
            if diff <= 15: print('DIFFERENT %s\n  ours: %s\n  his:  %s' % (name, o[:200], h[:200]))
    print('wpuzzle_diff: %d puzzles, %d the same, %d different (of %d published that fit)' % (len(picked), same, diff, len(lines)))
    return 0 if diff == 0 and picked else 1

if __name__ == '__main__': sys.exit(main())
