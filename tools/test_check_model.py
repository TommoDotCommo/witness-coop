#!/usr/bin/env python3
"""Game-free test of tools/check_model.py: the authored character must pass, and each kind of broken copy of it must be refused for its own reason (the checker's mutants).
usage: test_check_model.py [model.glb]      (default assets/models/adventurer.glb)"""
import copy, os, struct, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from glbtools import read_glb, write_glb
from check_model import check, _FMT, _N

src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'assets', 'models', 'adventurer.glb')
j0, b0 = read_glb(src); fails = 0; checks = 0
def expect(name, mutate, needle):
    global fails, checks
    j = copy.deepcopy(j0); b = bytearray(b0); mutate(j, b); d = tempfile.mkdtemp(); p = os.path.join(d, 'm.glb'); write_glb(p, j, bytes(b))
    if os.path.exists(src + '.txt'): open(p + '.txt', 'w').write(open(src + '.txt').read())
    pr = check(p); checks += 1; ok = (not pr) if needle is None else any(needle in m for m in pr)
    print('%-58s %s' % (name, 'ok' if ok else 'FAILED  (problems: %s)' % pr)); fails += 0 if ok else 1
def acc_span(j, ai, element):
    A = j['accessors'][ai]; bv = j['bufferViews'][A['bufferView']]; fmt, size = _FMT[A['componentType']]; n = _N[A['type']]; stride = bv.get('byteStride') or size * n
    return bv.get('byteOffset', 0) + A.get('byteOffset', 0) + element * stride, fmt, n
def clip(j, name): return next(a for a in j['animations'] if a['name'] == name)
expect('the authored character is accepted', lambda j, b: None, None)
def no_run(j, b): j['animations'] = [a for a in j['animations'] if a['name'] != 'run']
expect('MUTANT: no run clip', no_run, 'no clip named run')
def no_loop(j, b):
    a = clip(j, 'walk'); ch = next(c for c in a['channels'] if c['target']['path'] == 'rotation'); s = a['samplers'][ch['sampler']]; at, fmt, n = acc_span(j, s['output'], j['accessors'][s['output']]['count'] - 1)
    v = list(struct.unpack_from('<%d%s' % (n, fmt), b, at)); v[0] += 0.4; struct.pack_into('<%d%s' % (n, fmt), b, at, *v)
expect('MUTANT: a clip that does not end where it starts', no_loop, 'does not loop')
def bad_w(j, b):
    ai = j['meshes'][0]['primitives'][0]['attributes']['WEIGHTS_0']; at, fmt, n = acc_span(j, ai, 0); struct.pack_into('<%d%s' % (n, fmt), b, at, *([0] * n))
expect('MUTANT: weights that do not add up to 1', bad_w, 'do not add up')
def no_eye(j, b):
    for n in j['nodes']:
        if n.get('name', '').startswith('eye'): n['name'] = 'e'
expect('MUTANT: the eye joint the config names is gone', no_eye, 'eye_joint')
def two(j, b): j['skins'].append(copy.deepcopy(j['skins'][0]))
expect('MUTANT: two skins', two, 'skins')
def tall(j, b):
    for mesh in j['meshes']:
        for pr in mesh['primitives']:
            ai = pr['attributes']['POSITION']; A = j['accessors'][ai]; A['max'] = [m * 2 for m in A['max']]; A['min'] = [m * 2 for m in A['min']]
            for i in range(A['count']):
                at, fmt, n = acc_span(j, ai, i); v = struct.unpack_from('<3f', b, at); struct.pack_into('<3f', b, at, v[0], v[1] * 2, v[2])
expect('MUTANT: twice as tall', tall, 'm high')
def no_tex(j, b):
    for m in j['materials']: m.get('pbrMetallicRoughness', {}).pop('baseColorTexture', None)
expect('MUTANT: the texture is not used by any material', no_tex, 'not the base colour')
def no_img(j, b): j.pop('images', None); j.pop('textures', None)
expect('MUTANT: no texture at all', no_img, 'textures')
def no_uv(j, b):
    for me in j['meshes']:
        for pr in me['primitives']: pr['attributes'].pop('TEXCOORD_0', None)
expect('MUTANT: no UV coordinates', no_uv, 'UV coordinates')
def far_uv(j, b):
    for me in j['meshes']:
        for pr in me['primitives']: ai = pr['attributes']['TEXCOORD_0']; at, fmt, n = acc_span(j, ai, 0); struct.pack_into('<2f', b, at, 9.0, 9.0)
expect('MUTANT: UV coordinates far outside the texture', far_uv, 'outside the texture')
print('%d checks, %d failed' % (checks, fails)); sys.exit(1 if fails else 0)
