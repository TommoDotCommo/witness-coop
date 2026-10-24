"""Game-free path solver for the panels in the manifest written by wpanels.py --full (T0: runs on data, needs no game).

A panel is a graph: nodes with flags (0x1 end, 0x2 start, 0x8 dot, ...), edges as node pairs, and, for grid puzzles, a cell array of
decorations (value = shape << 16 | type << 8 | colour). solve(panel) returns a node list from a start to an end that satisfies every rule it
understands, or None. `unsupported(panel)` names the rules it does not model, so the sweep can fall back to a forced solve and say so."""
import collections, sys

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
    if any(p['dec']): why.add('decorations')
    for f in p['flags']:
        if f & ~(0x600000 | 0x400000 | 0x200000 | 0x100000 | 0x40000 | 0x3): why.add('node flags %#x' % (f & ~0x700003))
    return sorted(why)

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
    return solve_plain(p) if not unsupported(p) else None

if __name__ == '__main__':
    import json
    inv = json.load(open(sys.argv[1])); c = collections.Counter()
    for p in inv:
        u = unsupported(p); c[tuple(u)] += 1
    for k, v in c.most_common(40): print(v, k)
