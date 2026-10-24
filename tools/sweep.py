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

RESULTS_GLOB = os.path.join(RUN, 'sweep_results')      # sweep_results.<shard>.json, one per shard so parallel runs never write the same file
MANIFEST = os.path.join(RUN, 'panels.json')      # master copy, taken from a game before any line was drawn (a drawn line overwrites the stored solution)
KCACHE = os.path.join(RUN, 'panel_k.json')

def inventory(which='B', full=True):
    return json.loads(pyw('wpanels.py', pid(which), *(['--full'] if full else []), timeout=240).strip().splitlines()[-1])

def load(path, default):
    try: return json.load(open(path))
    except Exception: return default

def alive(which):
    try: out = subprocess.run(['tasklist.exe', '/FI', 'PID eq %s' % pid(which)], capture_output=True, text=True, timeout=30).stdout
    except Exception: return True
    return 'witness64' in out.lower()

def restart_game(which):
    """A game that died (a crash, mostly after a teleport to a place with nothing to stand on) is started again with the same owner's lease; it gets the
    solved panels back from its partner (a late join)."""
    print('game %s is gone: restarting it' % which, flush=True)
    subprocess.run([os.path.join(TOOLS, 'restart.sh'), which], capture_output=True, text=True, timeout=600)
    time.sleep(45); open(os.path.join(RUN, 'guard.stop'), 'w').write('bot starting')

def draw(panel, path, which, timeout=420):
    """One drawn attempt on one panel: returns the JSON record wdraw.py prints, or a record saying why there is none."""
    gdir = DIR2 if which == 'B' else DIR1
    env = {**os.environ, 'WDRAW_KCACHE': win(KCACHE), 'WSLENV': os.environ.get('WSLENV', '') + ':WDRAW_KCACHE'}
    try:
        out = subprocess.run([os.environ.get('PYW', '/mnt/c/Python311/python.exe'), '-I', win(os.path.join(TOOLS, 'win', os.environ.get('WDRAW_SCRIPT', 'wdraw.py'))), pid(which), win(gdir),
                              '%x' % panel['id'], ','.join(map(str, path))], capture_output=True, text=True, timeout=timeout, env=env).stdout
    except subprocess.TimeoutExpired:
        if not alive(which): restart_game(which)
        return {'solved': False, 'why': 'the drawing script timed out'}
    lines = [l for l in out.splitlines() if l.startswith('{')]
    if not lines and not alive(which): restart_game(which); return {'solved': False, 'why': 'the game died (restarted)', 'infrastructure': True}
    return json.loads(lines[-1]) if lines else {'solved': False, 'why': 'the drawing script printed nothing: ' + out[-200:], 'infrastructure': True}

def retry_possible(manifest, results, n_solved):
    return any(v.get('result') == 'failed' and v.get('tries', 1) < 8 and n_solved > v.get('at', 0) + 4 for v in results.values())

def run(args):
    which = 'B'
    if '--game' in args: which = args[args.index('--game') + 1]
    manifest = load(MANIFEST, None)
    if manifest is None:      # first time: the game must be fresh (see MANIFEST)
        manifest = inventory(which); json.dump(manifest, open(MANIFEST, 'w'))
    live = {q['id']: q for q in inventory(which, full=False)}      # what changes: solved and powered
    for q in manifest:
        if q['id'] in live: q['solved'] = live[q['id']]['solved']; q['powered'] = live[q['id']]['powered']; q['link'] = live[q['id']].get('link', q.get('link'))
    results = {}
    import glob
    for fn in glob.glob(RESULTS_GLOB + '.*.json'): results.update(load(fn, {}))
    ids = [int(v, 16) for v in args[args.index('--ids') + 1].split(',')] if '--ids' in args else None
    limit = int(args[args.index('--limit') + 1]) if '--limit' in args else 10 ** 9
    shard = tuple(map(int, args[args.index('--shard') + 1].split('/'))) if '--shard' in args else (1, 1)
    done_n = 0; infra = {}
    while True:      # passes: solving a panel can power others, so go again until a pass solves nothing
        live = {q['id']: q for q in inventory(which, full=False)}
        for q in manifest:
            if q['id'] in live: q['solved'] = live[q['id']]['solved']; q['powered'] = live[q['id']]['powered']; q['link'] = live[q['id']].get('link', q.get('link'))
        solved_in_pass = 0; skipped_unpowered = 0; n_solved = sum(1 for q in manifest if q['solved'])
        for p in sorted(manifest, key=lambda q: q['id']):
            if ids is not None and p['id'] not in ids: continue
            key = '%x' % p['id']
            if p['solved']:
                if results.get(key, {}).get('result') != 'solved': results[key] = {'result': 'solved', 'why': 'already solved', 'seconds': 0}
                continue
            if p['id'] % shard[1] != shard[0] - 1: continue
            r0 = results.get(key, {})
            if r0.get('result') == 'no solver' and not '--retry' in args: continue
            # a failed panel is tried again when other panels were solved since (that may have opened a door to it or powered it), at most 4 times
            if r0.get('result') == 'failed' and not '--retry' in args and (r0.get('tries', 1) >= 8 or n_solved <= r0.get('at', 0) + 4): continue
            if done_n >= limit: break
            path = solver.solve(p)
            if path is None:
                results[key] = {'result': 'no solver', 'why': ', '.join(solver.unsupported(p)) or 'no path found', 'seconds': 0}
            elif not (p['powered'] or not p.get('link')):      # waits for power only when a cable is supposed to bring it and it has not come

                skipped_unpowered += 1; continue
            else:
                t0 = time.time(); r = draw(p, path, which)
                if r.get('infrastructure'):      # not the panel's fault (the game was gone): try again, twice at most
                    infra[key] = infra.get(key, 0) + 1
                    if infra[key] < 3: continue
                results[key] = {'result': 'solved' if r.get('solved') else 'failed', 'why': r.get('why', ''), 'seconds': round(time.time() - t0), 'path': path, 'tries': r0.get('tries', 0) + 1, 'at': n_solved}
                solved_in_pass += bool(r.get('solved'))
            done_n += 1
            print('%s: %s %s (%ds)' % (key, results[key]['result'], results[key]['why'], results[key]['seconds']), flush=True)
            json.dump(results, open('%s.%d.json' % (RESULTS_GLOB, shard[0]), 'w'), indent=1)
        print('pass done: %d solved, %d powered-later panels waiting' % (solved_in_pass, skipped_unpowered), flush=True)
        if done_n >= limit or ids is not None or (not solved_in_pass and not retry_possible(manifest, results, n_solved + solved_in_pass)): break
        time.sleep(5)

def report(folders=None):
    """Merge the result files of every run folder (one per lab owner) and say what the sweep has done."""
    import glob, collections
    folders = folders or sorted(glob.glob(os.path.join(os.path.dirname(RUN), 'panel-sweep*')))
    results = {}
    for fo in folders:
        for fn in glob.glob(os.path.join(fo, 'sweep_results.*.json')):
            for k, v in load(fn, {}).items():
                if results.get(k, {}).get('result') != 'solved': results[k] = v
    manifest = load(MANIFEST, [])
    print('%d panels in the manifest, %d with a result' % (len(manifest), len(results)))
    for name, n in collections.Counter(v['result'] for v in results.values()).most_common(): print('  %-10s %d' % (name, n))
    why = collections.Counter(v['why'][:70] for v in results.values() if v['result'] != 'solved')
    for w_, n in why.most_common(14): print('    %3d  %s' % (n, w_))
    missing = [q['id'] for q in manifest if '%x' % q['id'] not in results]
    print('  no result yet: %d' % len(missing))
    return results

if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'run'
    if cmd == 'report': report(); sys.exit(0)
    if cmd == 'inventory':
        inv = inventory(sys.argv[2] if len(sys.argv) > 2 else 'B'); json.dump(inv, open(MANIFEST, 'w'))
        print('%d panels' % len(inv))
    else: run(sys.argv[2:])
