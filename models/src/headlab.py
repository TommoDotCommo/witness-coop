# Head lab: mesh only the head (head.py) with its eyes, lids and brows and export a small GLB, for fast sculpt
# iterations with tools/char_lookdev (seconds, against a full build's minute and a half).
# usage: blender -b -P headlab.py -- <out.glb> [--cell 0.003]
import bpy, bmesh, math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import sdf, head
from character_spec import FACE as F

args = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
OUT = args[0]; CELL = float(args[args.index('--cell') + 1]) if '--cell' in args else 0.003
bpy.ops.wm.read_factory_settings(use_empty=True)

def material(name, hexc, rough=0.7):
    m = bpy.data.materials.new(name); m.use_nodes = True; b = m.node_tree.nodes['Principled BSDF']
    c = [int(hexc[i:i + 2], 16) / 255.0 for i in (1, 3, 5)]; c = [(v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4) for v in c]
    b.inputs['Base Color'].default_value = c + [1]; b.inputs['Roughness'].default_value = rough; return m

def from_field(name, f, lo, hi, cell, mat, relax=2):
    V, Q, N = sdf.mesh(f, lo, hi, cell)
    if relax: V, N = sdf.relax(f, V, Q, relax)
    me = bpy.data.meshes.new(name); me.from_pydata(V.tolist(), [], Q.tolist()); me.update()
    me.normals_split_custom_set_from_vertices(N.tolist())
    ob = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(ob); me.materials.append(mat)
    for p in me.polygons: p.use_smooth = True
    return ob

def cap(name, centre, r, angle, mat, segs=48):
    """A spherical cap of radius r about the forward (-y) axis, angle radians across from its middle."""
    bm = bmesh.new(); rings = 10; verts = [bm.verts.new((centre[0], centre[1] - r, centre[2]))]; prev = [verts[0]]
    for j in range(1, rings + 1):
        a = angle * j / rings; row = []
        for i in range(segs):
            t = 2 * math.pi * i / segs
            row.append(bm.verts.new((centre[0] + r * math.sin(a) * math.cos(t), centre[1] - r * math.cos(a), centre[2] + r * math.sin(a) * math.sin(t))))
        if j == 1:
            for i in range(segs): bm.faces.new((prev[0], row[(i + 1) % segs], row[i]))
        else:
            for i in range(segs): bm.faces.new((prev[i], prev[(i + 1) % segs], row[(i + 1) % segs], row[i]))
        prev = row
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name); bm.to_mesh(me); ob = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(ob); me.materials.append(mat)
    for p in me.polygons: p.use_smooth = True
    return ob

skin = material('skin', '#d8b4a0', 0.62); lashm = material('lash', '#2a1e18', 0.5); white = material('white', '#ece6da', 0.25); iris = material('iris', '#5d7148', 0.3); ring = material('ring', '#2b3326', 0.35)
pupil = material('pupil', '#0d0e0e', 0.2); hair = material('hair', '#5a4030', 0.75); lip = material('lips', '#b87566', 0.55)
def paint(ob, mask, mat):      # faces whose centre the mask selects get the material
    ob.data.materials.append(mat); k = len(ob.data.materials) - 1
    cs = np.array([p.center[:] for p in ob.data.polygons]); sel = mask(cs)
    for p, s_ in zip(ob.data.polygons, sel):
        if s_: p.material_index = k
hd = from_field('head', head.skull_and_face(), (-0.13, -0.16, 1.40), (0.13, 0.13, 1.84), CELL, skin)
paint(hd, head.lip_mask, lip)
from_field('hair', head.hair(head.skull_and_face()), (-0.13, -0.16, 1.52), (0.13, 0.15, 1.84), CELL * 1.2, hair, 1)
for side, f in head.upper_lids().items():
    c = head.eye_centre(side); ob = from_field('lid_upper.%d' % side, f, c - 0.022, c + 0.022, CELL * 0.4, skin, relax=1)
    paint(ob, lambda q, side=side: head.lash(side, q), lashm)
for side, f in head.brows().items():
    c = head.eye_centre(side) + np.array([0, -0.02, 0.032]); from_field('brow.%d' % side, f, c - 0.04, c + 0.04, CELL * 0.5, hair, relax=1)
for side in (1, -1):
    c = head.eye_centre(side); r = F['eye_r']
    cap('eye_white.%d' % side, c, r, math.radians(100), white)
    ia = math.asin(F['iris_r'] / r)
    cap('iris_ring.%d' % side, c, r + 0.0002, ia, ring)
    cap('iris.%d' % side, c, r + 0.00035, ia * 0.86, iris)
    cap('pupil.%d' % side, c, r + 0.0005, math.asin(F['pupil_r'] / r), pupil)
    cap('glint.%d' % side, c + np.array([-0.0035 * side * 0 - 0.003, 0, 0.0035]), r + 0.0008, 0.09, white, 12)
if '--hands' in args:
    import hands
    glovem = material('leather', '#7a4f33', 0.55)
    for side in (1, -1):
        lo, hi = hands.bounds(side); from_field('hand.%d' % side, hands.skin(side), lo, hi, 0.0012, skin, 1)
        from_field('glove.%d' % side, hands.glove(side), lo, hi, 0.0012, glovem, 1)
bpy.ops.export_scene.gltf(filepath=OUT, export_format='GLB', export_apply=True)
print('HEADLAB', OUT, sum(len(o.data.polygons) for o in bpy.context.scene.objects if o.type == 'MESH'), 'faces')
