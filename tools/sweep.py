#!/usr/bin/env python3
"""Panel sweep: teleport to every puzzle on the island with the dev menu, solve it by drawing the solution with the in-process mouse, and
check that the game says solved. No shortcuts: every result is the game's own solved flag after a drawn line; a panel nothing can solve yet is
reported as such (with the reason), never forced.

usage: sweep.py inventory                  write the panel manifest (id, pose, graph, decorations) of the running game A or B
       sweep.py run [--ids 139,293] [--limit N] [--game B]
Results go to <run folder>/sweep_results.json (one record per panel: id, result, why, seconds) and are resumed on the next run."""
import json, os, subprocess, sys, time
from witness import DIR1, DIR2, RUN, pid, pyw, win, TOOLS
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import solver

RESULTS = os.path.join(RUN, 'sweep_results.json')
MANIFEST = os.path.join(RUN, 'panels.json')
KCACHE = os.path.join(RUN, 'panel_k.json')

def inventory(which='B', full=True):
    return json.loads(pyw('wpanels.py', pid(which), *(['--full'] if full else []), timeout=240).strip().splitlines()[-1])

def load(path, default):
    try: return json.load(open(path))
    except Exception: return default

def draw(panel, path, which, timeout=420):
    """One drawn attempt on one panel: returns the JSON record wdraw.py prints, or a record saying why there is none."""
    gdir = DIR2 if which == 'B' else DIR1
    env = {**os.environ, 'WDRAW_KCACHE': win(KCACHE), 'WSLENV': os.environ.get('WSLENV', '') + ':WDRAW_KCACHE'}
    try:
        out = subprocess.run([os.environ.get('PYW', '/mnt/c/Python311/python.exe'), '-I', win(os.path.join(TOOLS, 'win', 'wdraw.py')), pid(which), win(gdir),
                              '%x' % panel['id'], ','.join(map(str, path))], capture_output=True, text=True, timeout=timeout, env=env).stdout
    except subprocess.TimeoutExpired:
        return {'solved': False, 'why': 'the drawing script timed out'}
    lines = [l for l in out.splitlines() if l.startswith('{')]
    return json.loads(lines[-1]) if lines else {'solved': False, 'why': 'the drawing script printed nothing: ' + out[-200:]}

def run(args):
    which = 'B'
    if '--game' in args: which = args[args.index('--game') + 1]
    manifest = inventory(which); json.dump(manifest, open(MANIFEST, 'w'))
    results = load(RESULTS, {})
    ids = [int(v, 16) for v in args[args.index('--ids') + 1].split(',')] if '--ids' in args else None
    limit = int(args[args.index('--limit') + 1]) if '--limit' in args else 10 ** 9
    done_n = 0
    for p in sorted(manifest, key=lambda q: (not q['powered'], solver.solve(q) is None, q['id'])):      # powered and solvable first
        if ids is not None and p['id'] not in ids: continue
        key = '%x' % p['id']
        if key in results and results[key].get('result') == 'solved': continue
        if done_n >= limit: break
        t0 = time.time()
        path = solver.solve(p)
        if path is None:
            results[key] = {'result': 'no solver', 'why': ', '.join(solver.unsupported(p)) or 'no path found', 'seconds': 0}
        else:
            r = draw(p, path, which)
            results[key] = {'result': 'solved' if r.get('solved') else 'failed', 'why': r.get('why', ''), 'seconds': round(time.time() - t0), 'path': path}
        done_n += 1
        print('%s: %s %s (%ds)' % (key, results[key]['result'], results[key]['why'], results[key]['seconds']), flush=True)
        json.dump(results, open(RESULTS, 'w'), indent=1)

if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'run'
    if cmd == 'inventory':
        inv = inventory(sys.argv[2] if len(sys.argv) > 2 else 'B'); json.dump(inv, open(MANIFEST, 'w'))
        print('%d panels' % len(inv))
    else: run(sys.argv[2:])
