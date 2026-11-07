"""Where to stand to see a panel (pure, game-free): standing places with a clear line from the eye to the panel's middle, read from the walking map.

wdraw first tries the panel's normal at a few distances. A panel behind a rail, a ledge or a bush (Quarry's Lower Ramp Control, Symmetry's Left 3, Shadows'
Far 2, Swamp's Sliding Bridge) has nothing to stand on along its normal, or something in the way: this looks all round it instead, in the map's cells
(wnav.Map: floor z, a mask of 0.25 m height bins that hold something, flags)."""
import math

CELL, BIN, EYE = 0.5, 0.25, 1.65


def cell_of(x, y): return (int(math.floor(x / CELL)), int(math.floor(y / CELL)))


def blocked_at(cells, x, y, z):
    """True when the map holds something at this point (a height bin above the cell's floor with a surface in it). Never-seen cells are clear."""
    r = cells.get(cell_of(x, y))
    if r is None or r[0] < -1e3: return False
    b = int(math.floor((z - r[0]) / BIN))
    if b < 0 or b > 63: return False
    return bool(r[1] >> b & 1)


def clear_line(cells, eye, target, skip=0.7, step=0.2):
    """No cell between the eye and the target (the last `skip` metres excluded: the panel's own wall) holds anything at the height of the line."""
    d = math.dist(eye, target)
    if d <= skip: return True
    n = int((d - skip) / step)
    for k in range(1, n + 1):
        t = k * step / d
        if blocked_at(cells, eye[0] + (target[0] - eye[0]) * t, eye[1] + (target[1] - eye[1]) * t, eye[2] + (target[2] - eye[2]) * t): return False
    return True


def floors(cells, c, band=(0.75, 1.9)):
    """Heights a player could stand on in cell c: a bin with something and nothing in the body band above it."""
    r = cells.get(c)
    if r is None or r[0] < -1e3: return []
    lo, hi = int(band[0] / BIN), int(math.ceil(band[1] / BIN)); band_mask = sum(1 << b for b in range(lo, hi))
    return [r[0] + b * BIN for b in range(64) if r[1] >> b & 1 and not (r[1] >> b) & band_mask]


def standing_places(cells, panel, normal, rmin=1.4, rmax=6.0, max_angle=55.0, dz=2.5):
    """[(distance error, (x, y, feet z))]: cells within rmin..rmax of the panel, in front of it (within max_angle of its normal), with a floor within dz of the
    panel's height range and a clear line from the eye to the panel's middle. Sorted by how near 3 m out and how square on they are."""
    px, py, pz = panel; nn = math.hypot(normal[0], normal[1]) or 1.0; nx, ny = normal[0] / nn, normal[1] / nn
    out = []; cmin = cell_of(px - rmax, py - rmax); cmax = cell_of(px + rmax, py + rmax); cosmax = math.cos(math.radians(max_angle))
    for cx in range(cmin[0], cmax[0] + 1):
        for cy in range(cmin[1], cmax[1] + 1):
            x, y = (cx + 0.5) * CELL, (cy + 0.5) * CELL; d = math.hypot(x - px, y - py)
            if not rmin <= d <= rmax: continue
            cosang = ((x - px) * nx + (y - py) * ny) / d
            if cosang < cosmax: continue
            for f in floors(cells, (cx, cy)):
                if not pz - dz - 1.0 <= f <= pz + 0.5: continue      # (eye 1.65 m above the feet: a panel at the eye's height or somewhat above)
                if clear_line(cells, (x, y, f + EYE), panel): out.append((abs(d - 3.0) + 2.0 * (1.0 - cosang), (x, y, f)))
    out.sort(key=lambda q: q[0]); return out


def spread(places, k=4, gap=1.5):
    """The best k places at least `gap` metres apart (several floors of one cell, or neighbours, are one place): [(x, y, feet z)]."""
    out = []
    for _, q in places:
        if all(math.hypot(q[0] - o[0], q[1] - o[1]) >= gap for o in out): out.append(q)
        if len(out) >= k: break
    return out
