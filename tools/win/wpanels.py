"""Inventory of every placed panel as one JSON line: id, pose, state, and (with --full) the whole puzzle: graph, decorations, grid.
Usage: Windows Python wpanels.py <pid> [--full]
Offsets for build 11370412 (the randomizer's layout B): grid 0x440/0x444, decorations 0x418 (cells), flags 0x420, colours 0x428, count 0x430."""
import os, sys, json, math, struct
exec(compile(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wlib.py')).read(), 'wlib.py', 'exec'))
p = attach(int(sys.argv[1])); full = '--full' in sys.argv; out = []

def arr(e, off, fmt, count):
    if not 0 < count < 20000: return []
    ptr = p.u64(e + off); raw = p.rd(ptr, 4 * count) if ptr else None
    return list(struct.unpack('<%d%s' % (count, fmt), raw)) if raw else []

for i, e in enumerate(p.entity_table()):
    if not e or p.type_of(e) != TI_PANEL: continue
    x, y, z = struct.unpack('<3f', p.rd(e + 0x24, 12))
    if abs(x) + abs(y) < 1: continue
    qz, qw = struct.unpack('<2f', p.rd(e + 0x3c, 8)); n, m = p.u32(e + 0x3b4), p.u32(e + 0x3b8)
    flags = arr(e, 0x3c8, 'I', n)
    d = {'id': i, 'x': x, 'y': y, 'z': z, 'rz': 2 * math.atan2(qz, qw), 'nodes': n, 'edges': m,
         'starts': sum(1 for f in flags if f & 2 and not f & 1), 'ends': sum(1 for f in flags if f & 1),
         'solved': p.u32(e + 0x294) == 0x3f800000 and p.u32(e + 0x298) == 1,
         'powered': p.u32(e + 0x2a0) == 0x3f800000 and p.u32(e + 0x2a4) == 0x3f800000, 'target': p.u32(e + 0x2b4), 'link': p.u32(e + 0x2c8),
         'gx': p.u32(e + 0x440), 'gy': p.u32(e + 0x444), 'style': p.u32(e + 0x448), 'ndec': p.u32(e + 0x430)}
    if full:
        d.update({'flags': flags, 'pos': arr(e, 0x3c0, 'f', 2 * n), 'ca': arr(e, 0x3d0, 'I', m), 'cb': arr(e, 0x3d8, 'I', m),
                  'dec': arr(e, 0x418, 'I', d['ndec']), 'decf': arr(e, 0x420, 'I', d['ndec']),
                  'deccol': arr(e, 0x428, 'I', d['ndec']), 'refl': p.u32(e + 0x438), 'pw': p.u32(e + 0x39c),
                  'mirror': [v if v < 0x7fffffff else -1 for v in arr(e, 0x438, 'i', n)] if p.u64(e + 0x438) > 0x10000 else [], 'seq': arr(e, 0x458, 'I', p.u32(e + 0x454)), 'dseq': arr(e, 0x468, 'I', p.u32(e + 0x460))})
    out.append(d)
print(json.dumps(out))
