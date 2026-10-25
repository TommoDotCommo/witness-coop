"""Small glTF binary (.glb) helpers, standard library only: read/write the JSON and BIN chunks, read accessors, append accessors, quaternion maths."""
import json, math, struct

def read_glb(path):
    b = open(path, 'rb').read()
    cl, ct = struct.unpack('<II', b[12:20]); j = json.loads(b[20:20 + cl]); bo = 20 + cl
    bl, bt = struct.unpack('<II', b[bo:bo + 8]); return j, bytearray(b[bo + 8:bo + 8 + bl])

def write_glb(path, j, bin_):
    while len(bin_) % 4: bin_.append(0)
    j['buffers'][0]['byteLength'] = len(bin_); js = json.dumps(j, separators=(',', ':')).encode()
    while len(js) % 4: js += b' '
    total = 12 + 8 + len(js) + 8 + len(bin_)
    open(path, 'wb').write(struct.pack('<4sII', b'glTF', 2, total) + struct.pack('<II', len(js), 0x4E4F534A) + js + struct.pack('<II', len(bin_), 0x004E4942) + bytes(bin_))

COMP = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}

def read_acc(j, bin_, ai):
    """Floats of accessor ai as a list of tuples (float accessors only)."""
    A = j['accessors'][ai]; bv = j['bufferViews'][A['bufferView']]; n = A['count']; c = COMP[A['type']]
    assert A['componentType'] == 5126, 'float accessors only'
    off = bv.get('byteOffset', 0) + A.get('byteOffset', 0); stride = bv.get('byteStride', c * 4)
    return [struct.unpack_from('<%df' % c, bin_, off + i * stride) for i in range(n)]

def add_acc(j, bin_, rows, typ, minmax=False):
    """Append float rows (tuples) to the BIN chunk as a new accessor; returns its index."""
    while len(bin_) % 4: bin_.append(0)
    off = len(bin_); c = COMP[typ]
    for r in rows: bin_ += struct.pack('<%df' % c, *r)
    j['bufferViews'].append({'buffer': 0, 'byteOffset': off, 'byteLength': len(rows) * c * 4})
    a = {'bufferView': len(j['bufferViews']) - 1, 'componentType': 5126, 'count': len(rows), 'type': typ}
    if minmax: a['min'] = [min(r[k] for r in rows) for k in range(c)]; a['max'] = [max(r[k] for r in rows) for k in range(c)]
    j['accessors'].append(a); return len(j['accessors']) - 1

# quaternions are (x, y, z, w)
def qmul(a, b):
    return (a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1], a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0],
            a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3], a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2])
def qconj(q): return (-q[0], -q[1], -q[2], q[3])
def qnorm(q):
    l = math.sqrt(sum(x * x for x in q)); return tuple(x / l for x in q)
def qaxis(axis, ang):
    s = math.sin(ang / 2); return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(ang / 2))
def qrot(q, v):
    x, y, z, w = q; t = (2*(y*v[2] - z*v[1]), 2*(z*v[0] - x*v[2]), 2*(x*v[1] - y*v[0]))
    return (v[0] + w*t[0] + y*t[2] - z*t[1], v[1] + w*t[1] + z*t[0] - x*t[2], v[2] + w*t[2] + x*t[1] - y*t[0])
def qslerp_fix(q, prev):      # keep the sign continuous so linear interpolation takes the short way
    return q if sum(a * b for a, b in zip(q, prev)) >= 0 else tuple(-x for x in q)
