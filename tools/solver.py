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

def pillar(p):
    """A pillar (the grid wraps round a cylinder): the cell array is gx x (gy-1) instead of (gx-1) x (gy-1). Polyominoes across the seam are not modelled."""
    return p['gx'] > 2 and p['ndec'] == p['gx'] * (p['gy'] - 1) and p['ndec'] != (p['gx'] - 1) * (p['gy'] - 1)

def unsupported(p):
    """Rules on this panel that the solver does not model (empty = a plain path is a full solution)."""
    why = set()
    if stored(p): return []
    if p.get('refl') and len(p.get('mirror') or []) != p['nodes']: why.add('symmetry without a mirror map')
    if any(p['dec']) and not (p['gx'] > 1 and p['ndec'] == (p['gx'] - 1) * (p['gy'] - 1)) and not pillar(p): why.add('decorations on a panel that is not a grid')
    if pillar(p) and any((v >> 8) & 0xf == 4 and v & 0x2000 for v in p['dec']): why.add('negative polyominoes on a pillar')      # positive ones across the seam: psolve fits them
    # 0x80: on one start of each mirrored pair of Glass Factory Back Wall 5, the only panel that has it (the start the player draws from; psolve's line starts there)
    known = 0x700000 | 0x40000 | 0x20 | 0x100 | 0x200 | 0x1000 | 0x10000 | 0x20000 | 0x30000 | 0x8 | 0x3 | 0xe000 | 0x80      # sound dots (0x2000/0x4000/0x8000): met in the panel's own sequence (psolve), plain dots when it has none
    for f in p['flags']:
        if f & ~known: why.add('node flags %#x' % (f & ~known))
    return sorted(why)

PSOLVE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'psolve', 'psolve')

def psolve_text(p):
    """The panel description psolve reads (see tools/psolve/psolve.cpp)."""
    n, m = p['nodes'], p['edges']
    cells = list(p['dec']) if p['gx'] > 1 and (p['ndec'] == (p['gx'] - 1) * (p['gy'] - 1) or pillar(p)) else []
    mir = list(p.get('mirror') or [])
    ds = list(p.get('dseq') or [])
    return '%d %d %d %d %d\n%s\n%s\n%s\n%d %s\n%d %s\n' % (n, m, p['gx'] if cells or p['gx'] > 1 else 0, p['gy'] if cells or p['gx'] > 1 else 0, len(cells),
        ' '.join(str(f) for f in p['flags']), ' '.join('%d %d' % (a, b) for a, b in zip(p['ca'], p['cb'])), ' '.join(str(v) for v in cells), len(mir), ' '.join(map(str, mir)), len(ds), ' '.join(map(str, ds)))

def solve_rules(p, limit=3_000_000_000, timeout=180):
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

def with_only_edges(p, edges):
    """A copy of the panel that has only the given edges (a floor puzzle: an edge without a pressure plate cannot be walked). An edge that a marker node splits in two
    (a dot or a gap on it) counts as one edge between the nodes at its ends; the stubs at the start and the end are always kept."""
    adj = collections.defaultdict(list)
    for a_, b_ in zip(p['ca'], p['cb']): adj[a_].append(b_); adj[b_].append(a_)
    direct = {frozenset(x) for x in zip(p['ca'], p['cb'])}
    mid = {}
    for m_, ns in adj.items():
        if len(ns) == 2 and frozenset(ns) not in direct and not p['flags'][m_] & 3: mid[m_] = frozenset(ns)
    keep = []
    for a_, b_ in zip(p['ca'], p['cb']):
        fa = frozenset((a_, b_)); ends = p['flags'][a_] & 1 or p['flags'][b_] & 1
        if ends or fa in edges or (a_ in mid and mid[a_] in edges) or (b_ in mid and mid[b_] in edges): keep.append((a_, b_))
    q = dict(p); q['ca'] = [x for x, y in keep]; q['cb'] = [y for x, y in keep]; q['edges'] = len(keep); return q

def solve(p):
    """A node list the bot should draw for this panel, or None (see unsupported). Order: the game's stored solution, then a plain path."""
    if p.get("only_edges") is not None: p = with_only_edges(p, p["only_edges"])
    s = stored(p)
    if s: return s
    if not unsupported(p):
        if any(p['dec']) or any(f & 0x20 for f in p['flags']) or p.get('mirror'): return solve_rules(p)
        return solve_plain(p)
    return None

def variants(p):
    """Distinct solutions that differ in where the line starts or ends: the usual one (solve) first, then one per other start/end pair.
    A panel with two exits (or two starts) can be solved either way, but only one way powers what it is cabled to (the tutorial's
    Back Right: only the bottom exit lights the gate's latch): the caller tries the next one when the right thing did not turn on."""
    out = []; first = solve(dict(p))
    if first: out.append(first)
    if p.get('mirror') and any(f & 0x20 and f & 0x300 for f in p['flags']):
        # Symmetry with coloured dots: each line must touch the dots of its colour, and which of the two lines is blue is not in the panel data
        # (Symmetry Island Left 1 was refused every time). The same panel with blue and yellow swapped gives the line for the other colouring.
        q = dict(p); q['flags'] = [(f & ~0x300) | (0x200 if f & 0x100 else 0) | (0x100 if f & 0x200 else 0) for f in p['flags']]
        try: v = solve(q)
        except Exception: v = None
        if v and v not in out: out.append(v)
    starts = [i for i, f in enumerate(p['flags']) if f & START and not f & END]; ends = [i for i, f in enumerate(p['flags']) if f & END]
    if len(starts) < 2 and len(ends) < 2: return out
    for s_ in starts:
        for e_ in ends:
            q = dict(p); q['flags'] = [f & ~START if (f & START and i != s_) else f for i, f in enumerate(p['flags'])]
            q['flags'] = [f & ~END if (f & END and i != e_) else f for i, f in enumerate(q['flags'])]
            try: v = solve(q)
            except Exception: v = None
            if v and v not in out: out.append(v)
    return out

if __name__ == '__main__':
    import json
    inv = json.load(open(sys.argv[1])); c = collections.Counter()
    for p in inv:
        u = unsupported(p); c[tuple(u)] += 1
    for k, v in c.most_common(40): print(v, k)
