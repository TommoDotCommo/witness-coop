#!/usr/bin/env python3
"""Checks a character model (.glb) against what the mod's loader needs and what a good player model has, standard library only (tools/glbtools.py reads the file).
usage: check_model.py <model.glb> [...]      exit 0: all fine; every problem is printed
  - one skin, at most 255 joints (the loader's and the shader's limit); every vertex has four joints and weights that add up to 1
  - clips named idle, walk and run, each a loop (its last key is its first), 0.4 to 5 seconds long
  - one to three embedded textures (colour atlas, and optionally a normal map and a metallic-roughness map on the same material); flat colours (a material's base colour) for everything else; at most 24 materials; at most 60 000 vertices
  - standing height between 1.5 and 2.1 m, feet at the ground; if the .glb.txt names an eye joint (eye_joint=), that joint exists and is between 1.4 and 1.9 m
  - the speeds in the .glb.txt are numbers the clips can be made for (speed=, run_speed=)"""
import json, math, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from glbtools import read_glb

_FMT = {5120: ('b', 1), 5121: ('B', 1), 5122: ('h', 2), 5123: ('H', 2), 5125: ('I', 4), 5126: ('f', 4)}
_N = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}
def read_acc(j, b, ai):
    """any accessor as a list of tuples of numbers (normalised integers become 0..1 / -1..1)"""
    A = j['accessors'][ai]; bv = j['bufferViews'][A['bufferView']]; fmt, size = _FMT[A['componentType']]; n = _N[A['type']]
    stride = bv.get('byteStride') or size * n; base = bv.get('byteOffset', 0) + A.get('byteOffset', 0); scale = {'b': 127.0, 'B': 255.0, 'h': 32767.0, 'H': 65535.0}.get(fmt, 1.0) if A.get('normalized') else 1.0
    return [tuple(v / scale for v in struct.unpack_from('<%d%s' % (n, fmt), b, base + i * stride)) for i in range(A['count'])]

def check(path):
    problems = []
    def bad(m): problems.append(m)
    j, b = read_glb(path); nodes = j.get('nodes', []); skins = j.get('skins', [])
    if len(skins) != 1: bad('%d skins (want exactly 1)' % len(skins)); return problems
    joints = skins[0]['joints']
    if len(joints) > 255: bad('%d joints (limit 255)' % len(joints))
    names = [nodes[i].get('name', '') for i in joints]
    verts = 0; zmin, zmax = 1e9, -1e9
    for mesh in j.get('meshes', []):
        for pr in mesh['primitives']:
            at = pr['attributes']
            if 'JOINTS_0' not in at or 'WEIGHTS_0' not in at: bad('a mesh without joints and weights'); continue
            if 'TEXCOORD_0' not in at: bad('a mesh without UV coordinates')
            else:
                uv = read_acc(j, b, at['TEXCOORD_0'])
                if min(min(u) for u in uv) < -0.01 or max(max(u) for u in uv) > 1.01: bad('UV coordinates outside the texture (%.2f..%.2f)' % (min(min(u) for u in uv), max(max(u) for u in uv)))
            pos = read_acc(j, b, at['POSITION']); w = read_acc(j, b, at['WEIGHTS_0']); jj = read_acc(j, b, at['JOINTS_0']); verts += len(pos)
            for p in pos: zmin, zmax = min(zmin, p[1]), max(zmax, p[1])      # glTF: Y is up
            off = [i for i, ws in enumerate(w) if abs(sum(ws) - 1) > 0.01]
            if off: bad('%d vertices whose weights do not add up to 1 (first: %d, sum %.3f)' % (len(off), off[0], sum(w[off[0]])))
            if any(max(row) >= len(joints) for row in jj): bad('a joint index past the skin')
    if verts > 60000: bad('%d vertices (more than 60000)' % verts)
    if len(j.get('materials', [])) > 24: bad('%d materials (more than 24)' % len(j['materials']))
    images = j.get('images', [])
    if len(images) > 3: bad('%d textures (the loader takes the colour, normal and material maps)' % len(images))
    if not images: bad('no textures (the authored character has a painted colour atlas)')
    elif not any('baseColorTexture' in m.get('pbrMetallicRoughness', {}) for m in j.get('materials', [])): bad('the texture is not the base colour of any material')
    for im in images:
        bv = j['bufferViews'][im['bufferView']] if 'bufferView' in im else None
        if bv is None or b[bv.get('byteOffset', 0):bv.get('byteOffset', 0) + 3] not in (b'\xff\xd8\xff', b'\x89PN'): bad('a texture is not an embedded JPEG or PNG')
    if len(images) == 3:
        mats = j.get('materials', [])
        if not any('normalTexture' in m for m in mats): bad('three textures but no material has a normal map')
        if not any('metallicRoughnessTexture' in m.get('pbrMetallicRoughness', {}) for m in mats): bad('three textures but no material has a metallic-roughness map')
    height = zmax - zmin
    if not 1.5 <= height <= 2.1: bad('%.2f m high (want 1.5 to 2.1)' % height)
    if abs(zmin) > 0.05: bad('the feet are %.2f m from the ground (want 0)' % zmin)
    clips = {a.get('name', ''): a for a in j.get('animations', [])}
    for want in ('idle', 'walk', 'run'):
        a = clips.get(want)
        if not a: bad('no clip named %s (clips: %s)' % (want, sorted(clips))); continue
        dur = 0.0
        for ch in a['channels']:
            s = a['samplers'][ch['sampler']]; t = read_acc(j, b, s['input']); v = read_acc(j, b, s['output']); dur = max(dur, t[-1][0])
            first, last = v[0], v[-1]
            if any(abs(x - y) > 2e-3 for x, y in zip(first, last)): bad('clip %s does not loop (a %s channel ends elsewhere than it starts)' % (want, ch['target']['path'])); break
        if not 0.4 <= dur <= 5.0: bad('clip %s is %.2f s long (want 0.4 to 5)' % (want, dur))
    cfg = {}
    if os.path.exists(path + '.txt'):
        for line in open(path + '.txt'):
            if '=' in line: k, v = line.strip().split('=', 1); cfg[k] = v
    for key in ('speed', 'run_speed'):
        if key in cfg:
            try: float(cfg[key])
            except ValueError: bad('%s= is not a number' % key)
    if 'eye_joint' in cfg:
        hit = [i for i, n in zip(joints, names) if cfg['eye_joint'] in n]
        if not hit: bad('no joint named like eye_joint=%s' % cfg['eye_joint'])
    return problems

if __name__ == '__main__':
    rc = 0
    for p in sys.argv[1:]:
        pr = check(p); print('%s: %s' % (os.path.basename(p), 'ok' if not pr else 'PROBLEMS')); [print('  -', m) for m in pr]; rc |= bool(pr)
    sys.exit(rc)
