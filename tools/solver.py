"""Game-free path solver for the panels in the manifest written by wpanels.py --full (T0: runs on data, needs no game).

A panel is a graph: nodes with flags (0x1 end, 0x2 start, 0x8 dot, ...), edges as node pairs, and, for grid puzzles, a cell array of
decorations (value = shape << 16 | type << 8 | colour). solve(panel) returns a node list from a start to an end that satisfies every rule it
understands, or None. `unsupported(panel)` names the rules it does not model, so the sweep can fall back to a forced solve and say so."""
import collections, os, subprocess, sys

END, START = 0x1, 0x2

def graph(p):
    adj = collections.defaultdict(list)
    for a, b in zip(p['ca'], p['cb']): adj[a].append(b); adj[b].append(a)
    return adj

def solve_plain(p, limit=2_000_000):
    """Shortest start-to-end path that ignores decorations (BFS): enough for panels with no rule beyond 'reach the end'."""
    adj = graph(p); starts = [i for i, f in enumerate(p['flags']) if f & START and not f & END]; ends = {i for i, f in enumerate(p['flags']) if f & END}
    for s in starts:
        prev = {s: None}; q = collections.deque([s])
        while q:
            v = q.popleft()
            if v in ends:
                out = [v]
                while prev[out[-1]] is not None: out.append(prev[out[-1]])
                return out[::-1]
            for w in adj[v]:
                if w not in prev: prev[w] = v; q.append(w)
    return None

def unsupported(p):
    """Rules on this panel that the solver does not model (empty = a plain path is a full solution)."""
    why = set()
    if stored(p): return []
    if p.get('refl') and len(p.get('mirror') or []) != p['nodes']: why.add('symmetry without a mirror map')
    if any(p['dec']) and not (p['gx'] > 1 and p['ndec'] == (p['gx'] - 1) * (p['gy'] - 1)): why.add('decorations on a panel that is not a grid')
    for f in p['flags']:
        if f & ~(0x700000 | 0x40000 | 0x20 | 0x100 | 0x200 | 0x1000 | 0x10000 | 0x20000 | 0x30000 | 0x8 | 0x3): why.add('node flags %#x' % (f & ~0x7f1323))
    return sorted(why)

PSOLVE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'psolve', 'psolve')

def psolve_text(p):
    """The panel description psolve reads (see tools/psolve/psolve.cpp)."""
    n, m = p['nodes'], p['edges']
    cells = list(p['dec']) if p['gx'] > 1 and p['ndec'] == (p['gx'] - 1) * (p['gy'] - 1) else []
    mir = list(p.get('mirror') or [])
    return '%d %d %d %d %d\n%s\n%s\n%s\n%d %s\n' % (n, m, p['gx'] if cells or p['gx'] > 1 else 0, p['gy'] if cells or p['gx'] > 1 else 0, len(cells),
        ' '.join(str(f) for f in p['flags']), ' '.join('%d %d' % (a, b) for a, b in zip(p['ca'], p['cb'])), ' '.join(str(v) for v in cells), len(mir), ' '.join(map(str, mir)))

def solve_rules(p, limit=300_000_000, timeout=120):
    """Run psolve (the rule solver: dots, squares, stars, triangles, polyominoes, erasers on grids); returns the node list or None."""
    if not os.path.exists(PSOLVE): subprocess.run(['make', '-C', os.path.dirname(PSOLVE)], check=True, capture_output=True)
    try: r = subprocess.run([PSOLVE, str(limit)], input=psolve_text(p), capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired: return None
    out = r.stdout.strip()
    return [int(v) for v in out.split(',')] if r.returncode == 0 and out and out != 'none' else None

def stored(p):
    """The game ships a solution line (panel +0x454 length, +0x458 nodes) for 97 panels, present from the first frame of a new game: the
    ones whose rule is not in the cell decorations (the plain-looking grids that reject a shortest path). It is data of the game, not a flag
    we set: the bot still has to draw it with the mouse. Returns the nodes, or None."""
    seq = p.get('seq') or []
    if len(seq) >= 2 and seq[0] in [i for i, f in enumerate(p['flags']) if f & START] and p['flags'][seq[-1]] & END: return list(seq)
    return None

def solve(p):
    """A node list the bot should draw for this panel, or None (see unsupported). Order: the game's stored solution, then a plain path."""
    s = stored(p)
    if s: return s
    if not unsupported(p):
        if any(p['dec']) or any(f & 0x20 for f in p['flags']) or p.get('mirror'): return solve_rules(p)
        return solve_plain(p)
    return None

if __name__ == '__main__':
    import json
    inv = json.load(open(sys.argv[1])); c = collections.Counter()
    for p in inv:
        u = unsupported(p); c[tuple(u)] += 1
    for k, v in c.most_common(40): print(v, k)
