#!/usr/bin/env python3
"""Validate tint-region labels and budgets on a generated adventurer GLB.

This is a release gate: legacy unlabelled assets remain loadable, but cannot offer
per-feature customization. Runtime labels do not add triangles or textures.
"""
import math
import struct
import sys
from pathlib import Path
from glbtools import read_glb, read_acc

def texture_size(j, b, index=0):
    assert 1 <= len(j.get('images', [])) <= 3, 'expected the colour atlas, with at most a normal and a material map beside it'
    view = j['bufferViews'][j['images'][index]['bufferView']]
    data = b[view.get('byteOffset', 0):view.get('byteOffset', 0)+view['byteLength']]
    if data[:8] == b'\x89PNG\r\n\x1a\n': return struct.unpack_from('>II', data, 16)
    assert data[:2] == b'\xff\xd8', 'atlas is not JPEG or PNG'
    i = 2
    while i < len(data):
        assert data[i] == 255, 'invalid JPEG marker'
        while data[i] == 255: i += 1
        marker = data[i]; i += 1
        length = struct.unpack_from('>H', data, i)[0]
        if marker in (0xc0,0xc1,0xc2):
            h,w = struct.unpack_from('>HH', data, i+3); return w,h
        i += length
    raise AssertionError('JPEG has no dimensions')

def check(path):
    j, b = read_glb(path); groups = set(); triangles = 0
    for mesh in j['meshes']:
        for p in mesh['primitives']:
            assert p.get('mode', 4) == 4, 'only triangles supported'
            assert '_APPEARANCE' in p['attributes'], 'missing _APPEARANCE; regenerate the adventurer'
            ai = p['attributes']['_APPEARANCE']; a = j['accessors'][ai]
            assert a['type'] == 'SCALAR' and a['componentType'] == 5126, 'appearance must be float scalar'
            assert a['count'] == j['accessors'][p['attributes']['POSITION']]['count'], 'appearance count differs from positions'
            values = [v[0] for v in read_acc(j,b,ai)]
            assert all(math.isfinite(v) and 0 <= v <= 8 and int(v) == v for v in values), 'invalid appearance region'
            groups.update(values)
            ix = j['accessors'][p['indices']]; bv = j['bufferViews'][ix['bufferView']]
            fmt = {5121:'B',5123:'H',5125:'I'}[ix['componentType']]; width = struct.calcsize(fmt)
            offset = bv.get('byteOffset',0) + ix.get('byteOffset',0)
            indices = [struct.unpack_from('<'+fmt,b,offset+i*bv.get('byteStride',width))[0] for i in range(ix['count'])]
            assert len(indices) % 3 == 0, 'incomplete triangle'
            for t in range(0,len(indices),3):
                assert len({values[i] for i in indices[t:t+3]}) == 1, 'appearance region crosses a triangle'
            triangles += len(indices)//3
    assert groups == set(range(9)), 'missing appearance region'
    assert triangles <= 100000, 'triangle budget exceeded'
    w,h = texture_size(j,b)
    assert 0 < w <= 2048 and 0 < h <= 2048, 'atlas exceeds 2048 square budget'
    extra = []
    for k in range(1, len(j['images'])):
        ew, eh = texture_size(j, b, k); assert 0 < ew <= 2048 and 0 < eh <= 2048, 'a map exceeds the 2048 square budget'; extra.append(f'{ew}x{eh}')
    assert Path(path).stat().st_size <= 9*1024*1024, 'GLB budget exceeded (9 MiB: the colour atlas plus the normal and material maps)'
    print(f'appearance asset: 9 regions, {triangles} triangles, {w}x{h} atlas' + (' + maps ' + ', '.join(extra) if extra else '') + f', {Path(path).stat().st_size} bytes; labels and budgets passed')

if __name__ == '__main__': check(sys.argv[1] if len(sys.argv)>1 else 'assets/models/adventurer.glb')
