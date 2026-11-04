"""Signed-distance sculpting for the character generator (numpy only; runs inside Blender or plain Python).

Organic parts (the head with its face, ears and neck; the hands) are written as signed distance fields: primitives
blended with smooth unions and carved with smooth subtractions, like clay. `mesh()` turns a field into a quad mesh with
naive surface nets, then pulls every vertex onto the exact surface along the field's gradient, so the topology is even
and the shape is the field's, not the grid's. Normals for shading come from the field too.

Coordinates are the generator's: metres, z up, the character faces -y, its left is +x. A field is a function taking an
(N, 3) array of points and returning N distances (negative inside).
"""
import numpy as np

# ---- primitives (each returns a function of points)
def sphere(c, r):
    c = np.asarray(c, float)
    return lambda p: np.linalg.norm(p - c, axis=1) - r

def ellipsoid(c, r, rot=None):
    """Inigo Quilez's bound for an axis-aligned (or rotated) ellipsoid: exact on the surface, smooth near it."""
    c = np.asarray(c, float); r = np.asarray(r, float); R = None if rot is None else np.asarray(rot, float)
    def f(p):
        q = p - c
        if R is not None: q = q @ R      # rows of R are the ellipsoid's axes in world space
        k0 = np.linalg.norm(q / r, axis=1); k1 = np.linalg.norm(q / (r * r), axis=1)
        return k0 * (k0 - 1.0) / np.maximum(k1, 1e-9)
    return f

def capsule(a, b, r, r2=None):
    """A segment from a to b, radius r at a and r2 at b (a round cone when they differ)."""
    a = np.asarray(a, float); b = np.asarray(b, float); r2 = r if r2 is None else r2
    ab = b - a; L2 = float(ab @ ab)
    def f(p):
        t = np.clip(((p - a) @ ab) / L2, 0.0, 1.0)
        return np.linalg.norm(p - (a + t[:, None] * ab), axis=1) - (r + (r2 - r) * t)
    return f

def round_box(c, half, rr, rot=None):
    c = np.asarray(c, float); half = np.asarray(half, float) - rr; R = None if rot is None else np.asarray(rot, float)
    def f(p):
        q = p - c
        if R is not None: q = q @ R
        d = np.abs(q) - half
        return np.linalg.norm(np.maximum(d, 0.0), axis=1) + np.minimum(np.max(d, axis=1), 0.0) - rr
    return f

def plane(n, d):      # n . p + d (n unit): negative on the side the normal points away from
    n = np.asarray(n, float); return lambda p: p @ n + d

# ---- operators
def smin(a, b, k):
    """Polynomial smooth minimum: a and b blend over a band k metres wide."""
    if k <= 0: return np.minimum(a, b)
    h = np.clip(0.5 + 0.5 * (b - a) / k, 0.0, 1.0)
    return b + (a - b) * h - k * h * (1.0 - h)

def union(k, *fs):
    def f(p):
        d = fs[0](p)
        for g in fs[1:]: d = smin(d, g(p), k)
        return d
    return f

def subtract(base, cut, k=0.0):
    """base with cut removed, the edge rounded over k."""
    return lambda p: -smin(-base(p), cut(p), k)

def intersect(a, b, k=0.0):
    return lambda p: -smin(-a(p), -b(p), k)

def blend(base, parts):
    """Apply (field, k, 'add'|'cut') parts in order to a base field: the sculpt as a list of strokes."""
    def f(p):
        d = base(p)
        for g, k, op in parts:
            e = g(p)
            d = smin(d, e, k) if op == 'add' else -smin(-d, e, k)
        return d
    return f

def mirror_x(f):
    """Symmetric about x = 0 (author the left side, get both)."""
    def g(p):
        q = p.copy(); q[:, 0] = np.abs(q[:, 0]); return f(q)
    return g

def displace(f, g):
    """Add a small displacement field g (metres) to f: positive pushes the surface in."""
    return lambda p: f(p) + g(p)

def gauss(c, r, amp):
    """A soft bump of height amp (metres, outward) with radii r, for fine shaping on top of the blends."""
    c = np.asarray(c, float); r = np.asarray(r, float)
    return lambda p: -amp * np.exp(-np.sum(((p - c) / r) ** 2, axis=1))

def rot_x(a):
    c, s = np.cos(a), np.sin(a); return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])
def rot_y(a):
    c, s = np.cos(a), np.sin(a); return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
def rot_z(a):
    c, s = np.cos(a), np.sin(a); return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])

# ---- meshing
def gradient(f, p, h=2e-4):
    e = np.eye(3) * h
    g = np.stack([f(p + e[i]) - f(p - e[i]) for i in range(3)], axis=1) / (2 * h)
    return g / np.maximum(np.linalg.norm(g, axis=1, keepdims=True), 1e-12)

def mesh(f, lo, hi, cell, project=3):
    """Naive surface nets over the box lo..hi with cells `cell` metres wide.
    Returns (vertices (V, 3), quads (Q, 4) as vertex indices, outward and counter-clockwise seen from outside, normals (V, 3))."""
    lo = np.asarray(lo, float); hi = np.asarray(hi, float)
    n = np.maximum(np.ceil((hi - lo) / cell).astype(int) + 1, 2)
    axes = [lo[i] + cell * np.arange(n[i]) for i in range(3)]
    P = np.stack(np.meshgrid(*axes, indexing='ij'), axis=-1).reshape(-1, 3)
    D = f(P).reshape(n)
    inside = D < 0
    # cells with a sign change among their 8 corners get one vertex
    c8 = [inside[i:n[0] - 1 + i, j:n[1] - 1 + j, k:n[2] - 1 + k] for i in (0, 1) for j in (0, 1) for k in (0, 1)]
    anyin = np.zeros_like(c8[0]); allin = np.ones_like(c8[0])
    for c in c8: anyin |= c; allin &= c
    active = anyin & ~allin
    idx = -np.ones(active.shape, int); cells = np.argwhere(active); idx[active] = np.arange(len(cells))
    # vertex position: the mean of the edge crossings in its cell
    acc = np.zeros((len(cells), 3)); cnt = np.zeros(len(cells))
    corner = lambda di, dj, dk: (cells[:, 0] + di, cells[:, 1] + dj, cells[:, 2] + dk)
    for (a, b) in [((0,0,0),(1,0,0)),((0,1,0),(1,1,0)),((0,0,1),(1,0,1)),((0,1,1),(1,1,1)),
                   ((0,0,0),(0,1,0)),((1,0,0),(1,1,0)),((0,0,1),(0,1,1)),((1,0,1),(1,1,1)),
                   ((0,0,0),(0,0,1)),((1,0,0),(1,0,1)),((0,1,0),(0,1,1)),((1,1,0),(1,1,1))]:
        da, db = D[corner(*a)], D[corner(*b)]; s = (da < 0) != (db < 0)
        t = np.where(s, da / np.where(s, da - db, 1), 0)
        pa = np.asarray(a, float); pb = np.asarray(b, float)
        acc[s] += (pa + t[s, None] * (pb - pa)); cnt[s] += 1
    V = lo + cell * (cells + acc / np.maximum(cnt, 1)[:, None])
    # one quad per grid edge with a sign change, joining the four cells around it
    quads = []
    for axis in range(3):
        u, v = (axis + 1) % 3, (axis + 2) % 3
        sl = [slice(None)] * 3
        a = inside[tuple(sl)]
        s0 = [slice(0, n[i] - 1) if i == axis else slice(1, n[i] - 1) for i in range(3)]
        s1 = [slice(1, n[i]) if i == axis else slice(1, n[i] - 1) for i in range(3)]
        e0 = inside[tuple(s0)]; e1 = inside[tuple(s1)]; cross = e0 != e1
        pts = np.argwhere(cross)
        if not len(pts): continue
        base = pts.copy(); base[:, u] += 1; base[:, v] += 1      # grid index of the edge's start (the slices began at 1 on u and v)
        def cell_at(du, dv):
            c = base.copy(); c[:, u] -= du; c[:, v] -= dv; return idx[c[:, 0], c[:, 1], c[:, 2]]
        q = np.stack([cell_at(1, 1), cell_at(0, 1), cell_at(0, 0), cell_at(1, 0)], axis=1)
        flip = e0[cross]      # inside at the start: the surface faces +axis, keep; else reverse
        q[~flip] = q[~flip][:, ::-1]
        quads.append(q[(q >= 0).all(axis=1)])
    Q = np.concatenate(quads) if quads else np.zeros((0, 4), int)
    # onto the surface: Newton steps along the gradient
    for _ in range(project):
        g = gradient(f, V); V = V - f(V)[:, None] * g
    N = gradient(f, V)
    # orientation: make each quad's normal agree with the field's
    a, b, c = V[Q[:, 0]], V[Q[:, 1]], V[Q[:, 2]]
    fn = np.cross(b - a, c - a); wrong = np.sum(fn * (N[Q[:, 0]] + N[Q[:, 1]] + N[Q[:, 2]]), axis=1) < 0
    Q[wrong] = Q[wrong][:, ::-1]
    return V, Q, N

def relax(f, V, Q, iters=4, amount=0.5):
    """Even out the vertex spacing (Laplacian smoothing) and put the vertices back on the surface after each pass."""
    nb = [[] for _ in range(len(V))]
    for q in Q:
        for i in range(4): nb[q[i]].append(q[(i + 1) % 4]); nb[q[i]].append(q[(i + 3) % 4])
    nbs = [np.unique(x) for x in nb]
    for _ in range(iters):
        avg = np.array([V[x].mean(axis=0) if len(x) else V[i] for i, x in enumerate(nbs)])
        V = V + amount * (avg - V)
        for _ in range(2):
            g = gradient(f, V); V = V - f(V)[:, None] * g
    return V, gradient(f, V)

if __name__ == '__main__':      # a self-test: a blended snowman must mesh closed, outward and on its surface
    f = union(0.02, sphere((0, 0, 0), 0.1), sphere((0, 0, 0.13), 0.06))
    V, Q, N = mesh(f, (-.12, -.12, -.12), (.12, .12, .22), 0.006)
    edges = {}
    for q in Q:
        for i in range(4):
            e = tuple(sorted((q[i], q[(i + 1) % 4]))); edges[e] = edges.get(e, 0) + 1
    assert all(c == 2 for c in edges.values()), 'open or non-manifold mesh'
    assert np.abs(f(V)).max() < 1e-4, 'vertices off the surface'
    c = V.mean(axis=0); a, b, cc = V[Q[:, 0]], V[Q[:, 1]], V[Q[:, 2]]
    out = np.sum(np.cross(b - a, cc - a) * ((a + b + cc) / 3 - c), axis=1)
    assert (out > 0).mean() > 0.97, 'faces point inward'
    print('sdf self-test: %d vertices, %d quads, closed, outward, on the surface' % (len(V), len(Q)))
