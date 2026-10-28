# The adventurer: a weathered traveller with a tailored coat, knitted scarf, felt hat and handmade leather gear, built in a warm stylized realism.
# Built entirely by this script (shapes lofted from rings, a skeleton, weights by distance, idle / walk / run clips), exported as a binary glTF. Original work: CC0 1.0.
# usage: blender -b -P adventurer.py -- <out.glb> [--preview <folder>]
# Walk and run use a two-link leg solution and a planted-foot trajectory at the reference speeds written to the model config.
# Swing, contact and pelvis reach are authored in model space; exported interpolation is checked separately by tools/gait_probe.cpp.
import bpy, bmesh, math, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from appearance import material_group, parse_appearance, tint_colour
from mathutils import Vector, Matrix

args = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
TEXTURE = '--no-texture' not in args; FLAT_MATERIALS = '--flat-materials' in args; TEXSIZE = int(args[args.index('--texsize') + 1]) if '--texsize' in args else 2048
APPEARANCE = parse_appearance(args[args.index('--appearance') + 1] if '--appearance' in args else '')
OUT = args[0]; PREVIEW = args[args.index('--preview') + 1] if '--preview' in args else None
WALK_SPEED, RUN_SPEED = 1.2, 2.8
RUN_DURATION = 0.70
FPS = 30
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene; scene.render.fps = FPS; scene.unit_settings.system = 'METRIC'

def srgb(h, material=None):      # '#rrggbb' -> linear rgba
    c = [int(h[i:i + 2], 16) / 255.0 for i in (1, 3, 5)]
    rgb = [(v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4) for v in c]
    return tuple(tint_colour(rgb, material, APPEARANCE) + [1.0])
COLOURS = dict(nail='#bd967f', coat='#4a7872', coat_dark='#294b47', trousers='#8f8164', boots='#3c2b22', scarf='#bd8a35', hat='#aa9570', band='#754435', skin='#d3a180', hair='#43362c',
               leather='#70472e', brass='#aa8a4b', mouth_dark='#6d3930', skin_dark='#ae775e', vest='#725138', shirt='#c9bda5', sole='#211914', iris='#413b30', lips='#9f766a', boots_cuff='#584031', scarf_dark='#916528', white='#b8b6a6', mouth='#8e5549', bedroll='#a99b7c', feather='#40544d', strap='#49301f', metal='#9c9688', eye='#17191a', thread='#a99b78', stitching='#b49a70')
MATS = {}
def mat(name):
    if name not in MATS:
        m = bpy.data.materials.new(name); m.use_nodes = True; b = m.node_tree.nodes['Principled BSDF']
        b.inputs['Base Color'].default_value = srgb(COLOURS[name], name); b.inputs['Roughness'].default_value = 1.0; b.inputs['Metallic'].default_value = 0.0
        m.diffuse_color = srgb(COLOURS[name], name); MATS[name] = m      # (the viewport colour is what the preview shows; the exported colour is the node's)
    return MATS[name]

# ---- the skeleton (the character faces -Y; its left is +X). name: (head, tail, parent)
def V(x, y, z): return Vector((x, y, z))
B = {
    'root': (V(0, 0, 0), V(0, 0, 0.08), None),
    'pelvis': (V(0, 0, 0.95), V(0, 0, 1.03), 'root'),
    'spine': (V(0, 0, 1.03), V(0, 0, 1.22), 'pelvis'),
    'chest': (V(0, 0, 1.22), V(0, 0, 1.45), 'spine'),
    'neck': (V(0, 0, 1.45), V(0, 0, 1.54), 'chest'),
    'head': (V(0, 0, 1.54), V(0, 0, 1.80), 'neck'),
    'eye.L': (V(0.042, -0.100, 1.688), V(0.042, -0.120, 1.688), 'head'),
    'eye.R': (V(-0.042, -0.100, 1.688), V(-0.042, -0.120, 1.688), 'head'),
}
for s, x in (('L', 1), ('R', -1)):
    B['thigh.' + s] = (V(0.10 * x, 0, 0.93), V(0.10 * x, 0, 0.50), 'pelvis')
    B['shin.' + s] = (V(0.10 * x, 0, 0.50), V(0.10 * x, 0, 0.09), 'thigh.' + s)
    B['foot.' + s] = (V(0.10 * x, 0, 0.09), V(0.10 * x, -0.14, 0.04), 'shin.' + s)
    B['upper_arm.' + s] = (V(0.215 * x, 0, 1.42), V(0.255 * x, 0, 1.16), 'chest')
    B['forearm.' + s] = (V(0.255 * x, 0, 1.16), V(0.265 * x, 0, 0.91), 'upper_arm.' + s)
    B['hand.' + s] = (V(0.265 * x, 0, 0.91), V(0.265 * x, 0, 0.82), 'forearm.' + s)

# ---- shapes
col = bpy.data.collections.new('parts'); scene.collection.children.link(col)
def finish(name, bm, material, smooth=True, subsurf=0, bevel=0.0, solid=0.0, front_facing=False):
    me = bpy.data.meshes.new(name); bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    # Open facial patches have no enclosed volume from which Blender can infer
    # an outside. Orient them AFTER recalculation; doing it before is overwritten.
    if front_facing and '--eye-normal-mutant' not in args:
        bm.normal_update()
        for face in bm.faces:
            if face.normal.y > 0: face.normal_flip()
    bm.to_mesh(me); bm.free()
    ob = bpy.data.objects.new(name, me); col.objects.link(ob)
    if bevel: m = ob.modifiers.new('B', 'BEVEL'); m.width = bevel; m.segments = 2; m.limit_method = 'ANGLE'
    if solid: m = ob.modifiers.new('T', 'SOLIDIFY'); m.thickness = solid; m.offset = 0.0
    if subsurf: m = ob.modifiers.new('S', 'SUBSURF'); m.levels = subsurf; m.render_levels = subsurf
    if ob.modifiers:
        dg = bpy.context.evaluated_depsgraph_get(); ev = ob.evaluated_get(dg); me2 = bpy.data.meshes.new_from_object(ev); ob.modifiers.clear(); old = ob.data; ob.data = me2; bpy.data.meshes.remove(old)
    me = ob.data
    if smooth: me.polygons.foreach_set('use_smooth', [True] * len(me.polygons))
    me.materials.append(mat(material))
    # POINT labels survive joins, UV seams and the single-atlas bake. glTF custom
    # attributes leave conventional viewers' colours untouched.
    label = me.attributes.new('_APPEARANCE', 'FLOAT', 'POINT')
    for d in label.data: d.value = material_group(material)
    return ob
def loft(name, rings, material, n=14, subsurf=1, caps=True, solid=0.0, folds=0.0):      # rings: (z, cx, cy, rx, ry [, gap]) in the order they are joined; a gap (radians, from the front) makes an open shell (a coat that does not close at the front)
    # Add control rings before subdivision so cloth creases change the surface, not just its colour.
    # The taper at the ends keeps connected cuffs, waist and hems aligned.
    if folds:
        expanded = []
        for a, b in zip(rings, rings[1:]):
            for step in range(4):
                t = step / 4; expanded.append(tuple(x * (1-t) + y * t for x, y in zip(a, b)))
        rings = expanded + [rings[-1]]
    bm = bmesh.new(); rows = []; opened = any(len(r) > 5 and r[5] > 0 for r in rings)
    z0, z1 = rings[0][0], rings[-1][0]
    def point(r, angle):
        z, cx, cy, rx, ry = r[:5]
        if folds:
            u = (z-z0) / (z1-z0)
            envelope = math.sin(math.pi * u) ** 0.7
            crease = math.sin(6 * math.pi * u + 1.2 * math.sin(angle * 2) + 0.35 * math.sin(angle * 5))
            scale = 1 + folds * envelope * crease
            rx *= scale; ry *= scale
        return (cx + rx * math.sin(angle), cy - ry * math.cos(angle), z)
    for r in rings:
        z, cx, cy, rx, ry = r[:5]; g = r[5] if len(r) > 5 else 0.0
        if opened:
            g = max(g, 0.01); rows.append([bm.verts.new(point(r, g + (2 * math.pi - 2 * g) * k / n)) for k in range(n + 1)])
        else: rows.append([bm.verts.new(point(r, 2 * math.pi * k / n + math.pi / 2)) for k in range(n)])
    for a, b in zip(rows, rows[1:]):
        for k in range(len(a) - (1 if opened else 0)): bm.faces.new((a[k], a[(k + 1) % len(a)], b[(k + 1) % len(a)], b[k]))
    if caps and not opened: bm.faces.new(rows[0][::-1]); bm.faces.new(rows[-1])
    return finish(name, bm, material, True, subsurf, 0.0, solid)
def ellipsoid(name, c, r, material, seg=14, rings=9):
    bm = bmesh.new(); bmesh.ops.create_uvsphere(bm, u_segments=seg, v_segments=rings, radius=1.0)
    for v in bm.verts: v.co = Vector((c[0] + v.co.x * r[0], c[1] + v.co.y * r[1], c[2] + v.co.z * r[2]))
    return finish(name, bm, material, True, 0)
def box(name, c, size, material, bevel=0.012, rot=None, subsurf=0):
    bm = bmesh.new(); bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts: v.co = Vector((v.co.x * size[0], v.co.y * size[1], v.co.z * size[2]))
    if rot is not None: bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=rot)
    for v in bm.verts: v.co += Vector(c)
    return finish(name, bm, material, True, subsurf, bevel)
def tube(name, p1, p2, r, material, seg=10, r2=None):
    p1, p2 = Vector(p1), Vector(p2); d = p2 - p1; bm = bmesh.new(); bmesh.ops.create_cone(bm, cap_ends=True, segments=seg, radius1=r, radius2=r if r2 is None else r2, depth=d.length)
    rot = Vector((0, 0, 1)).rotation_difference(d.normalized()).to_matrix().to_4x4(); mid = (p1 + p2) / 2
    for v in bm.verts: v.co = rot @ v.co + mid
    return finish(name, bm, material, True, 0)

parts = []      # (object, allowed bones | single rigid bone)
def part(ob, bones): parts.append((ob, bones))
TORSO = ['pelvis', 'spine', 'chest']
# -- layered travelling clothes: a shaped waistcoat, shirt and an open tailored coat
part(loft('hips', [(0.84, 0, 0.005, 0.178, 0.122), (0.94, 0, 0, 0.185, 0.126), (1.02, 0, 0, 0.174, 0.120)], 'trousers', 20), ['pelvis', 'thigh.L', 'thigh.R'])
part(loft('vest', [(0.98, 0, 0, 0.176, 0.122), (1.10, 0, -0.002, 0.164, 0.115), (1.24, 0, 0, 0.188, 0.128), (1.36, 0, 0.005, 0.211, 0.137), (1.43, 0, 0.010, 0.190, 0.116)], 'vest', 24, folds=0.012), TORSO)
part(loft('shirt_collar', [(1.42, 0, 0.005, 0.112, 0.096), (1.50, 0, 0.008, 0.092, 0.082)], 'shirt', 20), ['chest', 'neck'])
COAT = [(0.57, 0, 0.018, 0.275, 0.192, 0.13), (0.70, 0, 0.012, 0.253, 0.177, 0.11), (0.87, 0, 0.004, 0.220, 0.151, 0.08), (1.04, 0, 0, 0.194, 0.137, 0.055), (1.18, 0, 0, 0.207, 0.141, 0.18),
        (1.31, 0, 0.003, 0.232, 0.150, 0.45), (1.40, 0, 0.008, 0.232, 0.145, 0.62), (1.455, 0, 0.012, 0.200, 0.122, 0.74)]
def coat_drape(co):
    p = Vector(co); angle = math.atan2(p.x, -(p.y-0.010))
    hang = max(0.0, min(1.0, (1.04-p.z)/0.47)); waist = math.exp(-((p.z-1.045)/0.095)**2)
    compression = math.exp(-((p.z-1.12)/0.11)**2); side = 0.35+0.65*abs(math.sin(angle))
    fold = side * (0.008*hang**0.7*math.sin(7*angle+0.6*math.sin(2*angle)) + 0.0035*hang*math.sin(13*angle-2*hang))
    fold += 0.0035*compression*math.sin(58*(p.z-1.04)+4*math.sin(angle))
    p.x = p.x*(1-0.018*waist) + math.sin(angle)*fold
    p.y = 0.010+(p.y-0.010)*(1-0.018*waist) - math.cos(angle)*fold
    p.z += hang**2*(0.006*math.sin(3*angle+0.4)+0.003*math.cos(7*angle)) + 0.003*waist*math.cos(4*angle)
    return p
coat_body = loft('coat', COAT, 'coat', 32, 1, False, 0.012, 0.010)
for v in coat_body.data.vertices: v.co = coat_drape(v.co)
part(coat_body, ['pelvis', 'spine', 'chest', ('thigh.L', 0.55), ('thigh.R', 0.55)])
coat_facing = loft('coat_facing', [(0.57, 0, 0.018, 0.277, 0.194, 0.13), (0.68, 0, 0.012, 0.258, 0.182, 0.12)], 'coat_dark', 36, 0, False, 0.014)
for v in coat_facing.data.vertices: v.co = coat_drape(v.co)
part(coat_facing, ['pelvis', ('thigh.L', 0.55), ('thigh.R', 0.55)])
coat_collar = loft('collar', [(1.43, 0, 0.016, 0.112, 0.095, 0.52), (1.48, 0, 0.018, 0.098, 0.083, 0.58), (1.535, 0, 0.020, 0.070, 0.060, 0.64)], 'coat_dark', 28, 1, False, 0.010)
part(coat_collar, ['chest', 'neck'])
for sd, x in (('L', 1), ('R', -1)):
    flap = box('pocket.' + sd, (0.125 * x, -0.174, 0.805), (0.105, 0.018, 0.064), 'coat_dark', 0.005)
    flap.data.transform(Matrix.Translation((0.125*x, -0.174, 0.805)) @ Matrix.Rotation(x * 0.08, 4, 'Y') @ Matrix.Translation((-0.125*x, 0.174, -0.805)))
    part(flap, ['pelvis'])
    part(ellipsoid('pocket_btn.' + sd, (0.125 * x, -0.187, 0.810), (0.008, 0.004, 0.008), 'brass', 10, 6), ['pelvis'])
for z in (1.11, 1.22, 1.33):
    part(ellipsoid('vest_button', (0, -0.127 if z < 1.2 else -0.137, z), (0.008, 0.0045, 0.008), 'brass', 10, 6), TORSO)
part(loft('belt', [(1.015, 0, 0, 0.218, 0.154), (1.072, 0, 0, 0.218, 0.154)], 'strap', 32, 0, False, 0.005), ['pelvis', 'spine'])
part(box('buckle', (0, -0.158, 1.044), (0.064, 0.015, 0.054), 'brass', 0.005), ['pelvis', 'spine'])
part(box('buckle_inset', (0, -0.168, 1.044), (0.043, 0.006, 0.034), 'strap', 0.003), ['pelvis', 'spine'])
part(box('buckle_tongue', (0, -0.174, 1.044), (0.035, 0.006, 0.007), 'brass', 0.002), ['pelvis', 'spine'])
# -- articulated sleeves, fingerless gloves and layered leather wrist guards
for s, x in (('L', 1), ('R', -1)):
    arms = ['chest', 'upper_arm.' + s, 'forearm.' + s]
    part(ellipsoid('shoulder.' + s, (0.211 * x, 0.004, 1.397), (0.070, 0.068, 0.064), 'coat'), ['chest', 'upper_arm.' + s])
    sleeve = loft('sleeve.'+s, [(1.035, 0.263*x, 0.002, 0.046, 0.050), (1.085, 0.260*x, 0.002, 0.052, 0.056), (1.17, 0.256*x, -0.002, 0.058, 0.064), (1.23, 0.249*x, 0.006, 0.065, 0.072), (1.32, 0.236*x, 0.002, 0.071, 0.076), (1.415, 0.214*x, 0.006, 0.074, 0.077)], 'coat', 28, folds=0.0)
    for v in sleeve.data.vertices:
        p = v.co; angle = math.atan2(p.x, -p.y); elbow = math.exp(-((p.z-1.16)/0.085)**2)
        radial = 1 + 0.045*elbow*math.sin(48*(p.z-1.16)+1.4*math.sin(angle)) + 0.012*math.sin(7*angle+3*p.z)
        p.x *= radial; p.y *= radial
    part(sleeve, arms)
    part(loft('cuff.'+s, [(1.024, 0.263*x, 0.002, 0.049, 0.054), (1.074, 0.261*x, 0.002, 0.052, 0.058)], 'coat_dark', 28, 0, False, 0.005), ['forearm.'+s, 'upper_arm.'+s])
    part(loft('forearm.' + s, [(0.94, 0.265*x, 0, 0.033, 0.037), (0.96, 0.265*x, 0, 0.038, 0.043), (1.04, 0.263*x, 0, 0.043, 0.049), (1.12, 0.259*x, 0, 0.048, 0.052)], 'skin', 18), ['upper_arm.' + s, 'forearm.' + s, 'hand.' + s])
    part(loft('bracer.' + s, [(0.928, 0.265*x, 0, 0.044, 0.049), (0.970, 0.264*x, 0, 0.047, 0.052), (1.035, 0.263*x, 0, 0.050, 0.055)], 'leather', 24, 0, False, 0.004), ['forearm.' + s])
    for z in (0.955, 1.012):
        part(loft('bracer_strap.' + s, [(z-0.010, 0.265*x, 0, 0.049, 0.055), (z+0.010, 0.265*x, 0, 0.049, 0.055)], 'strap', 20, 0, False, 0.003), ['forearm.' + s])
        part(box('bracer_buckle.' + s, (0.314*x, -0.005, z), (0.008, 0.025, 0.023), 'brass', 0.002), ['forearm.' + s])
    # A tapered palm and fingers with knuckle volume and a relaxed curl.
    # The hand is turned sideways: its knuckles span the Y axis and its
    # outward-facing surface is toward +/- X.
    part(loft('palm.' + s, [(0.819, 0.265*x, 0.000, 0.017, 0.035),
                            (0.836, 0.265*x, 0.001, 0.022, 0.039),
                            (0.866, 0.264*x, 0.001, 0.024, 0.040),
                            (0.891, 0.264*x, 0.002, 0.020, 0.031),
                            (0.905, 0.265*x, 0.002, 0.024, 0.027),
                            (0.925, 0.265*x, 0.002, 0.030, 0.034),
                            (0.950, 0.265*x, 0.002, 0.036, 0.040)], 'leather', 18), ['hand.' + s, 'forearm.' + s])
    for i, y in enumerate((-0.027, -0.009, 0.009, 0.027)):
        length = (0.062, 0.071, 0.066, 0.052)[i]
        finger = [(0.833, 0.266*x, y, 0.008, 0.009),
                  (0.824, 0.266*x, y, 0.009, 0.009),
                  (0.813, 0.267*x, y - 0.001, 0.0085, 0.0085),
                  (0.833 - length * 0.52, 0.268*x, y - 0.003, 0.008, 0.008),
                  (0.833 - length * 0.82, 0.269*x, y - 0.007, 0.007, 0.007),
                  (0.833 - length, 0.269*x, y - 0.010, 0.0045, 0.005)]
        part(loft('finger.' + s + '.' + str(i), finger, 'skin', 10, 1), ['hand.' + s])
        part(ellipsoid('nail.' + s + '.' + str(i), (0.275*x, y - 0.008, 0.833 - length + 0.010),
                       (0.0018, 0.0045, 0.007), 'nail', 10, 7), ['hand.' + s])
    part(loft('thumb.' + s, [(0.814, 0.246*x, -0.050, 0.007, 0.008),
                              (0.827, 0.246*x, -0.047, 0.010, 0.010),
                              (0.850, 0.248*x, -0.043, 0.012, 0.012),
                              (0.874, 0.252*x, -0.035, 0.014, 0.014),
                              (0.884, 0.255*x, -0.029, 0.013, 0.013)], 'skin', 12), ['hand.' + s, 'forearm.' + s])
# -- legs: breeches, a knee strap, tall boots with a turned cuff, a sole and a heel
for s, x in (('L', 1), ('R', -1)):
    part(loft('trousers.' + s, [(0.46, 0.10 * x, 0, 0.082, 0.084), (0.58, 0.10 * x, 0, 0.092, 0.094), (0.72, 0.10 * x, 0, 0.106, 0.108), (0.88, 0.10 * x, 0, 0.116, 0.118), (0.98, 0.10 * x, 0, 0.122, 0.122)], 'trousers', 14, folds=0.05), ['pelvis', 'thigh.' + s, 'shin.' + s])
    part(loft('knee_strap.' + s, [(0.50, 0.10 * x, 0, 0.078, 0.080), (0.535, 0.10 * x, 0, 0.078, 0.080)], 'strap', 12, 0), ['thigh.' + s, 'shin.' + s])
    part(loft('boot_shaft.' + s, [(0.07, 0.10 * x, 0.004, 0.066, 0.075), (0.16, 0.10 * x, 0.004, 0.064, 0.072), (0.30, 0.10 * x, 0.004, 0.072, 0.078), (0.42, 0.10 * x, 0.004, 0.082, 0.084), (0.470, 0.10 * x, 0.004, 0.088, 0.088)], 'boots', 14), ['shin.' + s, 'foot.' + s])
    part(loft('boot_cuff.' + s, [(0.44, 0.10 * x, 0.004, 0.092, 0.092), (0.50, 0.10 * x, 0.004, 0.096, 0.096)], 'boots_cuff', 14, 0), ['shin.' + s])
    part(ellipsoid('boot_toe.' + s, (0.10 * x, -0.052, 0.062), (0.074, 0.168, 0.066), 'boots', 14, 9), ['foot.' + s])
    part(box('boot_sole.' + s, (0.10 * x, -0.066, 0.020), (0.140, 0.300, 0.034), 'sole', 0.016), ['foot.' + s])
    part(box('boot_heel.' + s, (0.10 * x, 0.048, 0.033), (0.112, 0.080, 0.050), 'sole', 0.010), ['foot.' + s, 'shin.' + s])
    part(box('boot_lace.' + s, (0.10 * x, -0.095, 0.112), (0.034, 0.100, 0.014), 'boots_cuff', 0.004, Matrix.Rotation(-0.55, 3, 'X')), ['foot.' + s])
# -- an adult face with recessed almond-shaped eyes and restrained sculptural detail
part(loft('neck', [(1.44, 0, 0, 0.052, 0.052), (1.56, 0, -0.004, 0.047, 0.049)], 'skin', 20), ['neck', 'head', 'chest'])
def sculpt_head():
    ctrl = [(1.548, -0.041, 0.024, 0.034), (1.565, -0.037, 0.055, 0.063), (1.590, -0.023, 0.080, 0.091), (1.640, -0.004, 0.099, 0.108), (1.690, 0.003, 0.104, 0.115), (1.738, 0.008, 0.099, 0.109), (1.776, 0.012, 0.077, 0.086), (1.794, 0.012, 0.034, 0.042)]
    def cr(p0, p1, p2, p3, t): return 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t ** 3)
    def ring(z):
        for k in range(len(ctrl) - 1):
            if ctrl[k][0] <= z <= ctrl[k + 1][0]:
                t = (z - ctrl[k][0]) / (ctrl[k + 1][0] - ctrl[k][0]); a, b, c, d = ctrl[max(k - 1, 0)], ctrl[k], ctrl[k + 1], ctrl[min(k + 2, len(ctrl) - 1)]
                return [cr(a[i], b[i], c[i], d[i], t) for i in (1, 2, 3)]
        return list(ctrl[-1][1:])
    # Sculpt in facial depth, rather than along a ray from the cranium: the
    # bridge, muzzle and chin must keep their anatomical height as they project.
    def disp(p):
        if p.y > -0.035: return p
        x, z = p.x, p.z
        def g(cx, cz, rx, rz):
            return math.exp(-((x-cx)/rx)**2-((z-cz)/rz)**2)
        def pair(cx, cz, rx, rz): return g(cx, cz, rx, rz)+g(-cx, cz, rx, rz)
        depth = 0.0
        depth += 0.0060*pair(0.039, 1.696, 0.028, 0.010)  # supraorbital ridge
        depth -= 0.0045*pair(0.039, 1.677, 0.023, 0.011)  # orbital hollow
        depth += 0.0040*pair(0.055, 1.653, 0.027, 0.016)  # cheekbone plane
        depth -= 0.0025*pair(0.064, 1.615, 0.024, 0.023)  # cheek hollow
        depth += 0.013*g(0, 1.669, 0.010, 0.027)          # continuous nasal bridge
        depth += 0.024*g(0, 1.643, 0.012, 0.012)          # tip
        depth += 0.009*pair(0.013, 1.637, 0.007, 0.007)   # alar wings
        depth -= 0.003*pair(0.010, 1.634, 0.0035, 0.0025) # nostril recesses
        depth -= 0.003*g(0, 1.629, 0.011, 0.004)          # under nose
        depth += 0.0035*g(0, 1.612, 0.030, 0.019)        # muzzle blends into cheek
        depth += 0.0020*pair(0.007, 1.620, 0.0045, 0.008) # philtral columns
        depth -= 0.0020*g(0, 1.620, 0.004, 0.007)
        depth += 0.0035*g(0, 1.608, 0.020, 0.004)
        depth += 0.0045*g(0, 1.599, 0.019, 0.004)
        depth -= 0.0018*g(0, 1.603, 0.023, 0.0015)        # mouth closure
        depth -= 0.0025*g(0, 1.587, 0.022, 0.004)         # labiomental fold
        depth += 0.010*g(0, 1.574, 0.029, 0.012)          # chin pad
        depth += 0.002*pair(0.063, 1.590, 0.024, 0.016)   # jaw angles
        gate = min(1.0, max(0.0, (-p.y-0.035)/0.040))
        if '--flat-face-mutant' in args: depth = 0.0
        return Vector((x, p.y-depth*gate, z))
    n = 68; zs = [1.548 + (1.794 - 1.548) * k / 68 for k in range(69)]
    bm = bmesh.new(); rows = []
    for z in zs:
        cy, rx, ry = ring(z); rows.append([bm.verts.new(disp(Vector((rx * math.cos(2 * math.pi * k / n), cy + ry * math.sin(2 * math.pi * k / n), z)))) for k in range(n)])
    for a, b in zip(rows, rows[1:]):
        for k in range(n): bm.faces.new((a[k], a[(k + 1) % n], b[(k + 1) % n], b[k]))
    bm.faces.new(rows[0][::-1]); bm.faces.new(rows[-1])
    return finish('head', bm, 'skin', True, 0)
head_surface = sculpt_head(); part(head_surface, ['head'])
from mathutils.bvhtree import BVHTree
head_bvh = BVHTree.FromObject(head_surface, bpy.context.evaluated_depsgraph_get())
def face_y(x, z):
    hit, _, _, _ = head_bvh.ray_cast(Vector((x, -0.4, z)), Vector((0, 1, 0)))
    if hit is None: raise ValueError('facial detail misses head at %.4f, %.4f' % (x, z))
    return hit.y
# Depth relationships sampled from the actual evaluated mesh, in metres.
# These reject a smooth oval even if it has the same resolution and materials.
face_depths = dict(
    nose=face_y(0.025, 1.643)-face_y(0, 1.643),
    bridge=face_y(0.020, 1.668)-face_y(0, 1.668),
    philtrum=face_y(0, 1.620)-face_y(0.007, 1.620),
    chin=face_y(0, 1.587)-face_y(0, 1.574))
print('REPORT facial depth', face_depths)
if not (0.017 < face_depths['nose'] < 0.040 and
        0.008 < face_depths['bridge'] < 0.025 and
        face_depths['philtrum'] > 0.0001 and face_depths['chin'] > 0.001):
    raise ValueError('facial anatomy depth relationships failed')
def face_finish(name, bm, material, smooth=True, subsurf=0, bevel=0.0, solid=0.0):
    return finish(name, bm, material, smooth, subsurf, bevel, solid, front_facing=True)
def eye_limits(t):
    middle = 1.6755 + 0.0020*t; opening = max(0.0, math.sin(math.pi*t)) ** 0.72
    return middle - 0.0039*opening, middle + 0.0056*opening
def eye_surface(x, z, offset):
    t = max(0.0, min(1.0, (abs(x)-0.020)/0.037))
    corner = 0.0018 if '--eye-recess-mutant' in args else -0.00025
    return face_y(x, z) - offset + corner*abs(2*t-1)**3
def eye_disc(name, sx, rx, rz, material, offset):
    bm = bmesh.new(); cx, cz = sx*0.039, 1.6766
    centre = bm.verts.new((cx, eye_surface(cx, cz, offset), cz)); rings = []
    for j in range(1, 6):
        row = []
        for i in range(40):
            a = 2*math.pi*i/40; xx = cx + rx*(j/5)*math.cos(a); zz = cz + rz*(j/5)*math.sin(a)
            t = max(0.0, min(1.0, (abs(xx)-0.020)/0.037)); lo, hi = eye_limits(t)
            zz = max(lo + 0.00008, min(hi - 0.00008, zz))
            row.append(bm.verts.new((xx, eye_surface(xx, zz, offset), zz)))
        rings.append(row)
    for i in range(40): bm.faces.new((centre, rings[0][i], rings[0][(i+1)%40]))
    for a, b in zip(rings, rings[1:]):
        for i in range(40): bm.faces.new((a[i], b[i], b[(i+1)%40], a[(i+1)%40]))
    ob = face_finish(name, bm, material)
    if '--eye-mutant' in args: ob.data.transform(Matrix.Translation((0, -0.008, 0)))
    part(ob, ['head'])
for s, x in (('L', 1), ('R', -1)):
    bm = bmesh.new(); rows = []
    for i in range(33):
        t = i/32; lo, hi = eye_limits(t); row = []
        for j in range(9):
            z = lo+(hi-lo)*j/8; xx = x*(0.020+0.037*t)
            row.append(bm.verts.new((xx, eye_surface(xx, z, 0.00065), z)))
        rows.append(row)
    for a, b in zip(rows, rows[1:]):
        for j in range(8): bm.faces.new((a[j], b[j], b[j+1], a[j+1]))
    white = face_finish('eye_white.'+s, bm, 'white')
    if any(p.normal.y > 0.01 for p in white.data.polygons): raise ValueError('eye white faces away from the camera')
    if '--eye-mutant' in args: white.data.transform(Matrix.Translation((0, -0.008, 0)))
    for v in white.data.vertices:
        clearance = face_y(v.co.x, v.co.z)-v.co.y
        if clearance < 0.00005: raise ValueError('eye white is buried in its socket')
        if not -0.002 <= clearance <= 0.0025: raise ValueError('eye white does not fit its socket')
    part(white, ['head'])
    iris_rz = 0.0051 if '--iris-mutant' in args else 0.0036
    lo, hi = eye_limits((0.039-0.020)/0.037)
    if 2*iris_rz > 0.78*(hi-lo): raise ValueError('iris covers the eye white')
    eye_disc('iris.'+s, x, 0.0052, iris_rz, 'iris', 0.00110)
    eye_disc('pupil.'+s, x, 0.00180, 0.00185, 'eye', 0.00150)
    part(ellipsoid('eye_glint.'+s, (x*0.0375, eye_surface(x*0.0375, 1.6782, 0.0018), 1.6782), (0.00055, 0.00018, 0.00055), 'white', 8, 6), ['head'])
    for upper in (False, True):
        bm = bmesh.new(); rows = []
        for i in range(33):
            t = i/32; lo, hi = eye_limits(t); edge = hi if upper else lo; width = (0.0023 if upper else 0.0014)*math.sin(math.pi*t)**0.7
            row = []
            for j in range(3):
                z = edge + (width*j/2 if upper else -width*j/2); xx = x*(0.020+0.037*t)
                row.append(bm.verts.new((xx, face_y(xx, z)-0.00020, z)))
            rows.append(row)
        for a, b in zip(rows, rows[1:]):
            for j in range(2): bm.faces.new((a[j], b[j], b[j+1], a[j+1]))
        part(face_finish('lid.'+s, bm, 'skin'), ['head'])
    bm = bmesh.new(); rows = []
    for i in range(25):
        t = i/24; xx = x*(0.018 + 0.046*t); z = 1.696 + 0.003*math.sin(math.pi*t) + 0.001*t
        width = 0.0002 + 0.00145*math.sin(math.pi*t)**0.8
        rows.append([bm.verts.new((xx, face_y(xx, zz)-0.00045, zz)) for zz in (z-width, z+width)])
    for a, b in zip(rows, rows[1:]): bm.faces.new((a[0], b[0], b[1], a[1]))
    part(face_finish('brow.'+s, bm, 'hair', True), ['head'])
    part(ellipsoid('ear.'+s, (0.098*x, 0.006, 1.657), (0.011, 0.017, 0.027), 'skin', 20, 12), ['head'])
    part(ellipsoid('ear_inner.'+s, (0.104*x, 0.002, 1.657), (0.0025, 0.009, 0.016), 'skin_dark', 16, 10), ['head'])
for upper in (False, True):
    bm = bmesh.new(); rows = []
    for i in range(33):
        t = i/32; xx = -0.024+0.048*t; profile = math.sin(math.pi*t)**0.7
        seam = 1.603 + 0.0005*math.sin(math.pi*t)
        edge = seam + (0.0045*profile if upper else -0.0036*profile)
        if upper: edge += 0.0007*math.exp(-((abs(xx)-0.007)/0.003)**2)*profile
        row = []
        for j in range(5):
            v = j/4; z = seam+(edge-seam)*v
            row.append(bm.verts.new((xx, face_y(xx, z)-0.00012-0.00035*math.sin(math.pi*v)*profile, z)))
        rows.append(row)
    for a, b in zip(rows, rows[1:]):
        for j in range(4): bm.faces.new((a[j], b[j], b[j+1], a[j+1]))
    part(face_finish('lip.'+str(upper), bm, 'lips'), ['head'])
for sx in (-1, 1):
    xx, zz = sx*0.010, 1.634
    part(ellipsoid('nostril.'+str(sx), (xx, face_y(xx, zz)-0.00045, zz), (0.0024, 0.0004, 0.0011), 'mouth_dark', 12, 8), ['head'])
def scalp_point(angle, z, offset=0.0035):
    radial = V(math.cos(angle), math.sin(angle), 0)
    hit, _, _, _ = head_bvh.ray_cast(V(0.16*radial.x, 0.008+0.16*radial.y, z), -radial)
    if hit is None: raise ValueError('hair misses head')
    return hit + radial*offset
bm = bmesh.new(); rows = []; hair_n = 44
for j in range(17):
    t = j/16; row = []
    for i in range(hair_n):
        angle = 2*math.pi*i/hair_n; low = 1.660-0.035*math.sin(angle)+0.055*max(0, -math.sin(angle)); z = low+(1.793-low)*t
        p = scalp_point(angle, z, 0.0030+0.0010*math.sin(angle*14+z*70))
        row.append(bm.verts.new(p))
    rows.append(row)
for a, b in zip(rows, rows[1:]):
    for i in range(hair_n): bm.faces.new((a[i], a[(i+1)%hair_n], b[(i+1)%hair_n], b[i]))
part(face_finish('hair_scalp', bm, 'hair', True), ['head'])
for i, angle in enumerate((0.0, 0.65, 1.30, 1.95, 2.60, 3.10)):
    low = 1.660-0.035*math.sin(angle)+0.005
    top = scalp_point(angle, 1.735, 0.006); mid = scalp_point(angle, (1.735+low)*0.5, 0.008); tip = scalp_point(angle, low, 0.004)
    part(loft('hair_lock.'+str(i), [(top.z, top.x, top.y, 0.008, 0.006), (mid.z, mid.x, mid.y, 0.010, 0.007), (tip.z, tip.x, tip.y, 0.002, 0.003)], 'hair', 8), ['head'])
HAT0 = len(parts)
# -- weathered felt hat, with an asymmetric brim, a pinched crown and a feather vane
brim = loft('hat_brim', [(1.768, 0, 0, 0.128, 0.128), (1.758, 0, 0, 0.180, 0.178), (1.748, 0, 0, 0.222, 0.216), (1.750, 0, -0.004, 0.252, 0.244), (1.762, 0, -0.006, 0.264, 0.254)], 'hat', 48, 1, False, 0.007)
for v in brim.data.vertices:
    v.co.z += 0.014 * (abs(v.co.x) / 0.264) ** 2 + 0.004 * math.sin(v.co.y * 15)
part(brim, ['head'])
crown = loft('hat_crown', [(1.768, 0, 0, 0.128, 0.128), (1.806, 0, 0, 0.124, 0.124), (1.844, 0.004, 0.006, 0.116, 0.119), (1.877, 0.006, 0.010, 0.100, 0.108), (1.895, 0.004, 0.012, 0.072, 0.088), (1.899, 0, 0.014, 0.038, 0.046)], 'hat', 40, 1)
for v in crown.data.vertices:
    if v.co.z > 1.85:
        v.co.z -= 0.013 * math.exp(-((v.co.x / 0.045) ** 2 + ((v.co.y - 0.01) / 0.065) ** 2))
part(crown, ['head'])
part(loft('hat_band', [(1.776, 0, 0, 0.132, 0.132), (1.803, 0, 0, 0.130, 0.130)], 'band', 40, 0, False, 0.002), ['head'])
part(ellipsoid('hat_knot', (0.100, -0.082, 1.790), (0.015, 0.012, 0.013), 'band', 14, 8), ['head'])
f0, f1 = V(0.108, -0.066, 1.798), V(0.152, 0.075, 1.900)
bm = bmesh.new(); rows = []
for i in range(17):
    t = i / 16; c = f0.lerp(f1, t); w = 0.020 * math.sin(math.pi*t) ** 0.75 * (1 + 0.08*math.sin(i*2.7))
    rows.append([bm.verts.new(c + V(-w, 0, 0)), bm.verts.new(c + V(w, 0, 0))])
for a, b in zip(rows, rows[1:]): bm.faces.new((a[0], b[0], b[1], a[1]))
part(face_finish('feather_vane', bm, 'feather', True, 0, 0, 0.001), ['head'])
part(tube('feather_shaft', f0, f1, 0.0018, 'bedroll', 6), ['head'])
def tilt(ob, rx, pivot=(0, -0.02, 1.74)):
    p = Vector(pivot); ob.data.transform(Matrix.Translation(p) @ Matrix.Rotation(rx, 4, 'X') @ Matrix.Translation(-p))
for o, _ in parts[HAT0:]: tilt(o, -0.07); o.data.transform(Matrix.Translation((0, 0, -0.010)))
# -- a softer scarf and well-used travelling gear
scarf_wrap = loft('scarf', [(1.425, 0, 0.008, 0.126, 0.112), (1.465, 0.003, 0.004, 0.116, 0.102), (1.505, -0.003, 0.004, 0.104, 0.092), (1.535, -0.004, 0.006, 0.094, 0.083)], 'scarf', 40, folds=0.018)
if '--scarf-mutant' in args:
    for v in scarf_wrap.data.vertices: v.co.x *= 0.85; v.co.y *= 0.85
part(scarf_wrap, ['neck', 'chest'])
scarf_bvh = BVHTree.FromObject(scarf_wrap, bpy.context.evaluated_depsgraph_get())
collar_bvh = BVHTree.FromObject(coat_collar, bpy.context.evaluated_depsgraph_get())
for zi in range(23):
    z = 1.425 + 0.005*zi; origin = V(0, 0.01, z)
    for i in range(96):
        angle = 2*math.pi*i/96; radial = V(math.cos(angle), math.sin(angle), 0)
        collar_hit = collar_bvh.ray_cast(origin, radial)[0]; scarf_hit = scarf_bvh.ray_cast(origin, radial)[0]
        if collar_hit is not None and scarf_hit is not None and (scarf_hit-origin).length < (collar_hit-origin).length + 0.002:
            raise ValueError('scarf intersects the coat collar (z %.3f angle %.1f collar %.5f scarf %.5f)' % (z, math.degrees(angle), (collar_hit-origin).length, (scarf_hit-origin).length))
part(ellipsoid('scarf_knot', (0.038, -0.116, 1.457), (0.034, 0.023, 0.027), 'scarf', 20, 12), ['neck', 'chest'])
part(loft('scarf_tail_a', [(1.45, 0.064, -0.145, 0.027, 0.006), (1.36, 0.072, -0.151, 0.031, 0.007), (1.25, 0.077, -0.158, 0.033, 0.007), (1.16, 0.085, -0.161, 0.027, 0.006)], 'scarf', 16, folds=0.06), ['chest', 'spine'])
part(loft('scarf_tail_b', [(1.44, 0.015, -0.143, 0.024, 0.006), (1.37, 0.017, -0.146, 0.028, 0.007), (1.30, 0.024, -0.151, 0.026, 0.006)], 'scarf_dark', 16, folds=0.05), ['chest'])
part(box('satchel', (-0.312, 0.040, 0.86), (0.092, 0.270, 0.230), 'leather', 0.020), ['pelvis', 'spine'])
part(box('satchel_gusset', (-0.316, 0.040, 0.850), (0.098, 0.238, 0.196), 'strap', 0.016), ['pelvis', 'spine'])
part(box('satchel_flap', (-0.315, 0.040, 0.950), (0.102, 0.282, 0.100), 'leather', 0.014), ['pelvis', 'spine'])
part(box('satchel_clasp', (-0.370, 0.040, 0.915), (0.009, 0.034, 0.044), 'brass', 0.003), ['pelvis', 'spine'])
part(box('satchel_clasp_inset', (-0.376, 0.040, 0.915), (0.004, 0.020, 0.027), 'strap', 0.002), ['pelvis', 'spine'])
from mathutils.bvhtree import BVHTree as SurfaceBVH
strap_surfaces = [SurfaceBVH.FromObject(ob, bpy.context.evaluated_depsgraph_get()) for ob, _ in parts if ob.name == 'vest']
def strap_surface(x, z):
    hits = [tree.ray_cast(Vector((x, -0.5, z)), Vector((0, 1, 0)))[0] for tree in strap_surfaces]
    hits = [p.y for p in hits if p is not None]
    if not hits: raise ValueError('satchel strap misses body at %.3f, %.3f' % (x, z))
    return min(hits) - 0.003
bm = bmesh.new(); rows = []
for i in range(33):
    t = i / 32; x, z = 0.105 - 0.240*t, 1.390 - 0.250*t
    row = []
    for side in (-1, 0, 1):
        xx, zz = x + side*0.015, z - side*0.015
        row.append(bm.verts.new((xx, strap_surface(xx, zz), zz)))
    rows.append(row)
for a, b in zip(rows, rows[1:]):
    for i in range(2): bm.faces.new((a[i], b[i], b[i+1], a[i+1]))
front_strap = finish('satchel_front_strap', bm, 'strap', True, 0, 0, 0.002)
for vertex in front_strap.data.vertices:
    target = strap_surface(vertex.co.x, vertex.co.z)
    if not abs(vertex.co.y - target) < 0.002: raise ValueError('satchel strap clearance outside tolerance')
part(front_strap, TORSO)
for i in range(3):
    t = 0.44 + i*0.12; xx, zz = 0.105 - 0.240*t, 1.390 - 0.250*t
    part(ellipsoid('strap_hole.'+str(i), (xx, strap_surface(xx, zz)-0.0016, zz), (0.0025, 0.0009, 0.0025), 'eye', 8, 6), TORSO)
part(tube('strap_back', (0.150, 0.080, 1.455), (-0.300, 0.040, 0.96), 0.016, 'strap'), ['chest', 'spine'])
def pack_drape(ob):
    for v in ob.data.vertices:
        p = v.co; bulge = math.exp(-((p.x/0.12)**4+((p.z-1.065)/0.16)**4))
        p.y += 0.008*bulge; p.z += 0.004*math.cos(p.x*18)*math.exp(-((p.z-0.91)/0.06)**2)
        p.x *= 1+0.025*math.sin((p.z-1.06)*9)
    return ob
part(pack_drape(box('rucksack', (0, 0.205, 1.06), (0.250, 0.115, 0.310), 'leather', 0.028, None, 1)), ['spine', 'pelvis', 'chest'])
part(pack_drape(box('rucksack_flap', (0, 0.212, 1.185), (0.256, 0.122, 0.095), 'strap', 0.020, None, 1)), ['spine', 'chest'])
for x in (-0.075, 0.075):
    part(box('pack_strap', (x, 0.275, 1.065), (0.025, 0.012, 0.235), 'strap', 0.004), ['spine', 'pelvis', 'chest'])
    part(box('pack_keeper', (x, 0.283, 1.13), (0.034, 0.008, 0.030), 'brass', 0.003), ['spine', 'chest'])
    part(box('pack_keeper_inset', (x, 0.288, 1.13), (0.019, 0.005, 0.015), 'strap', 0.001), ['spine', 'chest'])
part(tube('bedroll', (-0.165, 0.205, 1.305), (0.165, 0.205, 1.305), 0.058, 'bedroll', 24), ['chest', 'spine'])
for bx in (-0.09, 0.09): part(loft('bedroll_strap', [(1.244, bx, 0.205, 0.014, 0.065), (1.366, bx, 0.205, 0.014, 0.065)], 'strap', 16, 0), ['chest', 'spine'])
part(ellipsoid('canteen', (0.185, 0.215, 1.02), (0.058, 0.037, 0.078), 'metal', 24, 14), ['spine', 'pelvis'])
part(tube('canteen_cap', (0.185, 0.215, 1.092), (0.185, 0.215, 1.120), 0.015, 'leather', 16), ['spine', 'pelvis'])

# -- construction details: folded lapels, topstitching, shoulder seams and real boot laces
def cord(name, points, radius, material, bones, seg=6):
    for a, b in zip(points, points[1:]): part(tube(name, a, b, radius, material, seg), bones)
for sx in (-1, 1):
    spiral = []
    for i in range(31):
        t = i/30; angle = t*math.pi*5.4; radius = 0.004+0.050*t
        spiral.append(V(sx*0.166, 0.205+radius*math.cos(angle), 1.305+radius*math.sin(angle)))
    cord('bedroll_spiral', spiral, 0.0014, 'trousers', ['chest', 'spine'], 4)
def stitches(name, a, b, count, bones, material='stitching', radius=0.0011):
    a, b = Vector(a), Vector(b)
    for i in range(count):
        p = a.lerp(b, (i + 0.18) / count); q = a.lerp(b, (i + 0.68) / count)
        part(tube(name, p, q, radius, material, 5), bones)
def coat_ring(z):
    for a, b in zip(COAT, COAT[1:]):
        if a[0] <= z <= b[0]:
            t = (z-a[0])/(b[0]-a[0]); return [a[i]*(1-t)+b[i]*t for i in range(1, 6)]
    return list(COAT[-1][1:])
def coat_point(z, angle, offset=0.003):
    cx, cy, rx, ry, gap = coat_ring(z)
    return coat_drape(Vector((cx+(rx+offset)*math.sin(angle), cy-(ry+offset)*math.cos(angle), z)))
for sd, sx in (('L', 1), ('R', -1)):
    bm = bmesh.new(); rows = []
    for z, width in ((1.17, 0.006), (1.25, 0.025), (1.36, 0.043), (1.445, 0.022)):
        cx, cy, rx, ry, gap = coat_ring(z)
        edge = V(cx+sx*(rx+0.005)*math.sin(gap), cy-(ry+0.005)*math.cos(gap), z)
        outer = edge + V(sx*width, -0.009, 0.002); inner = edge - V(sx*0.004, 0, 0)
        rows.append([bm.verts.new(inner), bm.verts.new(outer)])
    for a, b in zip(rows, rows[1:]): bm.faces.new((a[0], b[0], b[1], a[1]))
    stitch_a, stitch_b = rows[0][1].co.copy(), rows[-1][1].co.copy()
    part(face_finish('lapel.'+sd, bm, 'coat_dark', True, 0, 0, 0.005), ['spine', 'chest'])
    stitches('lapel_stitch.'+sd, stitch_a, stitch_b, 16, ['spine', 'chest'], 'thread')
    stitches('pocket_stitch.'+sd, (sx*0.080, -0.187, 0.827), (sx*0.170, -0.187, 0.827), 9, ['pelvis'])
    shoulder = [V(sx*(0.213+0.069*math.sin(a)), 0.071*math.cos(a), 1.393) for a in [2*math.pi*i/24 for i in range(25)]]
    cord('shoulder_seam.'+sd, shoulder, 0.0016, 'coat_dark', ['chest', 'upper_arm.'+sd])
    for i in range(5):
        z = 0.155 + i*0.047
        for side in (-1, 1):
            part(ellipsoid('boot_eyelet.'+sd, (sx*0.10+side*0.025, -0.073, z), (0.004, 0.0025, 0.004), 'brass', 8, 6), ['shin.'+sd])
        if i < 4:
            cord('boot_lacing.'+sd, [(sx*0.10-0.022, -0.078, z), (sx*0.10+0.022, -0.078, z+0.047)], 0.0020, 'boots_cuff', ['shin.'+sd])
            cord('boot_lacing.'+sd, [(sx*0.10+0.022, -0.079, z), (sx*0.10-0.022, -0.079, z+0.047)], 0.0020, 'boots_cuff', ['shin.'+sd])
    welt = [V(sx*0.10+0.069*math.cos(a), -0.052+0.134*math.sin(a), 0.039) for a in [2*math.pi*i/24 for i in range(25)]]
    cord('boot_welt.'+sd, welt, 0.0018, 'boots_cuff', ['foot.'+sd], 4)
for i in range(36):
    a0, a1 = 0.20+i*(2*math.pi-0.40)/36, 0.20+(i+0.52)*(2*math.pi-0.40)/36
    cord('hem_stitch', [coat_point(0.598, a0, 0.005), coat_point(0.598, a1, 0.005)], 0.0011, 'thread', ['pelvis', 'thigh.L', 'thigh.R'], 5)
cord('back_seam', [coat_point(z, math.pi, 0.004) for z in (0.60, 0.75, 0.90, 1.05, 1.20, 1.35, 1.43)], 0.0018, 'coat_dark', TORSO)
stitches('satchel_stitch', (-0.371, -0.085, 0.775), (-0.371, -0.085, 0.945), 15, ['pelvis', 'spine'])
stitches('satchel_stitch', (-0.371, 0.165, 0.775), (-0.371, 0.165, 0.945), 15, ['pelvis', 'spine'])

# Relax the bind pose without changing the animation's angular authoring. The
# satchel-side arm comes forward slightly to keep the hand clear of the bag.
for s, sx in (('L', 1), ('R', -1)):
    pivot = V(0.215*sx, 0, 1.42)
    if s == 'L': rotation = Matrix.Rotation(math.radians(-8), 4, 'Y')
    else: rotation = Matrix.Rotation(math.radians(-9), 4, 'X') @ Matrix.Rotation(math.radians(3), 4, 'Y')
    xform = Matrix.Translation(pivot) @ rotation @ Matrix.Translation(-pivot)
    arm_bones = {'upper_arm.'+s, 'forearm.'+s, 'hand.'+s}
    for ob, bones in parts:
        names = {b[0] if isinstance(b, tuple) else b for b in bones}
        if names & arm_bones: ob.data.transform(xform)
    for name in arm_bones:
        h, t, parent = B[name]; B[name] = (xform @ h, xform @ t, parent)

# ---- weights: by distance to the bones a part may follow, blended near the joints (at most four per vertex)
def seg_dist(p, a, b):
    ab = b - a; t = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.length_squared, 1e-9))); return (p - (a + ab * t)).length
SIGMA = 0.075
for ob, bones in parts:
    mult = {(b[0] if isinstance(b, tuple) else b): (b[1] if isinstance(b, tuple) else 1.0) for b in bones}; allowed = [b for b in mult if b in B]
    groups = {b: ob.vertex_groups.new(name=b) for b in allowed}
    for v in ob.data.vertices:
        if len(allowed) == 1: ws = [(allowed[0], 1.0)]
        else:
            raw = [(b, mult[b] * math.exp(-(seg_dist(v.co, B[b][0], B[b][1]) / SIGMA) ** 2)) for b in allowed]; raw.sort(key=lambda r: -r[1]); raw = raw[:4]; tot = sum(w for _, w in raw)
            ws = [(b, w / tot) for b, w in raw] if tot > 1e-6 else [(min(allowed, key=lambda b: seg_dist(v.co, B[b][0], B[b][1])), 1.0)]
        for b, w in ws:
            if w > 0.01: groups[b].add([v.index], w, 'REPLACE')
bpy.ops.object.select_all(action='DESELECT')
for ob, _ in parts: ob.select_set(True)
bpy.context.view_layer.objects.active = parts[0][0]; bpy.ops.object.join(); body = bpy.context.view_layer.objects.active; body.name = 'Adventurer_mesh'
for p in body.data.polygons: pass
bpy.ops.object.shade_smooth()


# ---- the texture: UV unwrap, procedural cloth / leather / skin variation and dirt on the materials, baked (colour x ambient occlusion) into one image, which replaces the flat materials
def paint_materials():
    def node(t, loc, **kw):
        n = nt.nodes.new(t); n.location = loc
        for k, v in kw.items(): setattr(n, k, v)
        return n
    global nt
    for name, m in MATS.items():
        nt = m.node_tree; bsdf = nt.nodes['Principled BSDF']; base = Vector(bsdf.inputs['Base Color'].default_value[:3])
        tc = node('ShaderNodeTexCoord', (-1400, 0)); cur = node('ShaderNodeRGB', (-1200, 300)); cur.outputs[0].default_value = (*base, 1.0); out = cur.outputs[0]
        def noise_mul(scale, amt, stretch=(1, 1, 1), seed=0.0):
            nonlocal out
            mp = node('ShaderNodeMapping', (-1200, -100 - 200 * seed)); mp.inputs['Scale'].default_value = (scale * stretch[0], scale * stretch[1], scale * stretch[2]); mp.inputs['Location'].default_value = (seed * 7.1, seed * 3.3, seed * 5.9)
            nt.links.new(tc.outputs['Object'], mp.inputs['Vector'])
            nz = node('ShaderNodeTexNoise', (-1000, -100 - 200 * seed)); nz.inputs['Scale'].default_value = 1.0; nz.inputs['Detail'].default_value = 4.0; nz.inputs['Roughness'].default_value = 0.6; nt.links.new(mp.outputs[0], nz.inputs['Vector'])
            rp = node('ShaderNodeValToRGB', (-800, -100 - 200 * seed)); rp.color_ramp.elements[0].color = (1 - amt, 1 - amt, 1 - amt, 1); rp.color_ramp.elements[1].color = (1, 1, 1, 1); nt.links.new(nz.outputs['Fac'], rp.inputs['Fac'])
            mx = node('ShaderNodeMixRGB', (-600, 300 - 200 * seed), blend_type='MULTIPLY'); mx.inputs['Fac'].default_value = 1.0; nt.links.new(out, mx.inputs['Color1']); nt.links.new(rp.outputs['Color'], mx.inputs['Color2']); out = mx.outputs['Color']
        def blend_to(colour, mask_out, strength):
            nonlocal out
            mx = node('ShaderNodeMixRGB', (-300, 300), blend_type='MIX'); fm = node('ShaderNodeMath', (-500, 100), operation='MULTIPLY'); fm.inputs[1].default_value = strength; nt.links.new(mask_out, fm.inputs[0])
            nt.links.new(fm.outputs[0], mx.inputs['Fac']); nt.links.new(out, mx.inputs['Color1']); mx.inputs['Color2'].default_value = (*colour, 1.0); out = mx.outputs['Color']
        def z_mask(z0, z1):      # 1 below z0 fading to 0 at z1 (object height), roughened by noise
            sep = node('ShaderNodeSeparateXYZ', (-1000, 500)); nt.links.new(tc.outputs['Object'], sep.inputs[0])
            mr = node('ShaderNodeMapRange', (-800, 500)); mr.inputs['From Min'].default_value = z0; mr.inputs['From Max'].default_value = z1; mr.inputs['To Min'].default_value = 1.0; mr.inputs['To Max'].default_value = 0.0; nt.links.new(sep.outputs['Z'], mr.inputs['Value']); mr.clamp = True
            mp = node('ShaderNodeMapping', (-1200, 700)); mp.inputs['Scale'].default_value = (14, 14, 14); nt.links.new(tc.outputs['Object'], mp.inputs['Vector'])
            nz = node('ShaderNodeTexNoise', (-1000, 700)); nz.inputs['Detail'].default_value = 5.0; nt.links.new(mp.outputs[0], nz.inputs['Vector'])
            mu = node('ShaderNodeMath', (-600, 560), operation='MULTIPLY'); nt.links.new(mr.outputs[0], mu.inputs[0]); nt.links.new(nz.outputs['Fac'], mu.inputs[1]); ad = node('ShaderNodeMath', (-400, 560), operation='ADD'); nt.links.new(mr.outputs[0], ad.inputs[0]); nt.links.new(mu.outputs[0], ad.inputs[1])
            cl = node('ShaderNodeMath', (-300, 560), operation='MULTIPLY'); cl.inputs[1].default_value = 0.6; nt.links.new(ad.outputs[0], cl.inputs[0]); return cl.outputs[0]
        def spot(centre, radius, colour, strength, squash=(1, 1, 1)):      # a soft round patch of colour in object space (a blush, a nose tip)
            nonlocal out
            vm = node('ShaderNodeVectorMath', (-1000, -700), operation='SUBTRACT'); vm.inputs[1].default_value = centre; nt.links.new(tc.outputs['Object'], vm.inputs[0])
            sc = node('ShaderNodeVectorMath', (-900, -700), operation='MULTIPLY'); sc.inputs[1].default_value = squash; nt.links.new(vm.outputs[0], sc.inputs[0])
            ln = node('ShaderNodeVectorMath', (-800, -700), operation='LENGTH'); nt.links.new(sc.outputs[0], ln.inputs[0])
            mr = node('ShaderNodeMapRange', (-600, -700)); mr.inputs['From Min'].default_value = 0.0; mr.inputs['From Max'].default_value = radius; mr.inputs['To Min'].default_value = 1.0; mr.inputs['To Max'].default_value = 0.0; mr.clamp = True; nt.links.new(ln.outputs['Value'], mr.inputs['Value'])
            blend_to(colour, mr.outputs[0], strength)
        def weave(scale, amount):
            nonlocal out
            sep = node('ShaderNodeSeparateXYZ', (-1100, -1100)); nt.links.new(tc.outputs['Object'], sep.inputs[0])
            waves = []
            for axis in ('X', 'Z'):
                freq = node('ShaderNodeMath', (-900, -1100), operation='MULTIPLY'); freq.inputs[1].default_value = scale; nt.links.new(sep.outputs[axis], freq.inputs[0])
                wave = node('ShaderNodeMath', (-750, -1100), operation='SINE'); nt.links.new(freq.outputs[0], wave.inputs[0])
                half = node('ShaderNodeMath', (-600, -1100), operation='MULTIPLY_ADD'); half.inputs[1].default_value = 0.5; half.inputs[2].default_value = 0.5; nt.links.new(wave.outputs[0], half.inputs[0]); waves.append(half.outputs[0])
            cross = node('ShaderNodeMath', (-450, -1100), operation='MULTIPLY'); nt.links.new(waves[0], cross.inputs[0]); nt.links.new(waves[1], cross.inputs[1])
            ramp = node('ShaderNodeValToRGB', (-300, -1100)); ramp.color_ramp.elements[0].color = (1-amount, 1-amount, 1-amount, 1); nt.links.new(cross.outputs[0], ramp.inputs[0])
            mix = node('ShaderNodeMixRGB', (-100, 200), blend_type='MULTIPLY'); mix.inputs['Fac'].default_value = 1.0; nt.links.new(out, mix.inputs['Color1']); nt.links.new(ramp.outputs[0], mix.inputs['Color2']); out = mix.outputs[0]
        if name in ('coat', 'coat_dark'): noise_mul(90, 0.18, (1, 1, 4)); noise_mul(7, 0.28, (1, 1, 1.6), 1); weave(2200, 0.13); blend_to((0.02, 0.05, 0.04), z_mask(0.45, 0.85), 0.36)
        elif name in ('trousers', 'vest', 'bedroll', 'shirt'): noise_mul(80, 0.16, (4, 1, 1)); noise_mul(7, 0.24, (1, 1, 1), 1); weave(1900, 0.12); blend_to((0.05, 0.035, 0.02), z_mask(0.50, 1.10), 0.35)
        elif name in ('hat', 'band'): noise_mul(80, 0.16); noise_mul(6, 0.24, (1, 1, 1), 1); weave(1700, 0.10)
        elif name in ('boots', 'boots_cuff', 'leather', 'strap', 'sole'): noise_mul(140, 0.12); noise_mul(6, 0.28, (1, 1, 1), 1); weave(2600, 0.07); blend_to((0.04, 0.03, 0.02), z_mask(0.0, 0.40), 0.50)
        elif name in ('scarf', 'scarf_dark'): noise_mul(90, 0.17, (1, 4, 1)); noise_mul(8, 0.22, (1, 1, 1), 1); weave(1200, 0.20)
        elif name == 'skin':
            noise_mul(65, 0.065)
            for sx in (1, -1): spot((0.058*sx, -0.098, 1.620), 0.030, (0.62, 0.25, 0.18), 0.12)
            spot((0, -0.121, 1.644), 0.015, (0.65, 0.25, 0.18), 0.20)
            spot((0, -0.111, 1.603), 0.019, (0.38, 0.11, 0.085), 0.48, (1, 0.25, 3.2))
            spot((0, -0.112, 1.6025), 0.019, (0.11, 0.030, 0.024), 0.62, (1, 0.25, 11))
            spot((0, -0.070, 1.580), 0.073, (0.075, 0.045, 0.028), 0.22, (1.25, 0.65, 0.80))
            spot((0, -0.109, 1.617), 0.023, (0.065, 0.038, 0.024), 0.20, (1, 0.25, 2.4))
            for sx in (1, -1): spot((0.084*sx, -0.025, 1.630), 0.042, (0.070, 0.042, 0.026), 0.38, (0.75, 1.0, 1.4))
            noise_mul(180, 0.12, (1, 1, 1), 2)
        elif name == 'hair': noise_mul(90, 0.25, (1, 1, 5)); noise_mul(12, 0.15, (1, 1, 1), 1); noise_mul(180, 0.16, (1, 1, 8), 2)
        else: noise_mul(40, 0.08)
        nt.links.new(out, bsdf.inputs['Base Color'])

def bake_texture(body, size, outdir):
    if not FLAT_MATERIALS: paint_materials()
    bpy.ops.object.select_all(action='DESELECT'); body.select_set(True); bpy.context.view_layer.objects.active = body
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT'); bpy.ops.uv.smart_project(angle_limit=math.radians(70), island_margin=0.0025); bpy.ops.object.mode_set(mode='OBJECT')
    scene.render.engine = 'CYCLES'; scene.cycles.device = 'CPU'; scene.cycles.samples = 24; scene.cycles.use_denoising = False
    if scene.world is None: scene.world = bpy.data.worlds.new('w')
    scene.world.light_settings.distance = 0.09
    imgs = {}
    for kind in ('DIFFUSE', 'AO'):
        im = bpy.data.images.new('bake_' + kind, size, size, alpha=False, float_buffer=True); imgs[kind] = im
        for m in MATS.values():
            n = m.node_tree.nodes.new('ShaderNodeTexImage'); n.image = im; n.select = True; m.node_tree.nodes.active = n
        if kind == 'DIFFUSE': bpy.ops.object.bake(type='DIFFUSE', pass_filter={'COLOR'}, margin=10, margin_type='EXTEND', use_clear=True)
        else: bpy.ops.object.bake(type='AO', margin=10, margin_type='EXTEND', use_clear=True)
        for m in MATS.values():
            for n in [n for n in m.node_tree.nodes if n.type == 'TEX_IMAGE']: m.node_tree.nodes.remove(n)
    import numpy as np
    a = np.empty(size * size * 4, dtype=np.float32); b = np.empty(size * size * 4, dtype=np.float32); imgs['DIFFUSE'].pixels.foreach_get(a); imgs['AO'].pixels.foreach_get(b)
    a = a.reshape(-1, 4); b = b.reshape(-1, 4)
    # Sample the interior of coat faces in the colour-only bake. This measures
    # material variation independently of the AO pass and avoids island edges.
    uv = body.data.uv_layers.active.data; coat_samples = []; coat_indices = []
    coat_slots = {i for i, slot in enumerate(body.material_slots) if slot.material and slot.material.name == 'coat'}
    for poly in body.data.polygons:
        if poly.material_index not in coat_slots: continue
        coords = [uv[i].uv for i in poly.loop_indices]
        u = sum(p.x for p in coords) / len(coords); v = sum(p.y for p in coords) / len(coords)
        ix, iy = min(size - 1, max(0, int(u * size))), min(size - 1, max(0, int(v * size)))
        colour = a[iy * size + ix, 1]
        if colour > 0.02: coat_samples.append(colour); coat_indices.append(iy * size + ix)
    if len(coat_samples) < 100: raise ValueError('too few coat texture samples')
    spread = float(np.percentile(coat_samples, 90) - np.percentile(coat_samples, 10))
    print('REPORT coat diffuse spread %.6f from %d samples' % (spread, len(coat_samples)))
    ao = np.clip(b[:, :1], 0, 1) ** 1.25; lin = np.clip(a[:, :3] * (0.50 + 0.50 * ao), 0, 1); a[:, :3] = np.where(lin <= 0.0031308, lin * 12.92, 1.055 * np.power(lin, 1 / 2.4) - 0.055); a[:, 3] = 1.0      # (the image holds display values: the linear colour is encoded here)
    final = bpy.data.images.new('adventurer_tex', size, size, alpha=False); final.colorspace_settings.name = 'sRGB'; final.pixels.foreach_set(a.reshape(-1))
    os.makedirs(outdir, exist_ok=True); path = os.path.join(outdir, 'adventurer_tex.jpg'); final.filepath_raw = path; final.file_format = 'JPEG'; scene.render.image_settings.quality = 93; final.save()
    img = bpy.data.images.load(path); img.name = 'adventurer_tex_file'; img.pack()
    encoded = np.empty(size * size * 4, dtype=np.float32); img.pixels.foreach_get(encoded)
    values = encoded.reshape(-1, 4)[coat_indices, 1]
    print('REPORT coat JPEG spread %.6f' % float(np.percentile(values, 90) - np.percentile(values, 10)))
    nm = bpy.data.materials.new('adventurer'); nm.use_nodes = True; bs = nm.node_tree.nodes['Principled BSDF']; bs.inputs['Roughness'].default_value = 1.0; bs.inputs['Metallic'].default_value = 0.0
    tn = nm.node_tree.nodes.new('ShaderNodeTexImage'); tn.image = img; nm.node_tree.links.new(tn.outputs['Color'], bs.inputs['Base Color']); nm.use_nodes = True
    body.data.materials.clear(); body.data.materials.append(nm)
    for poly in body.data.polygons: poly.material_index = 0
    for k in ('bake_DIFFUSE', 'bake_AO'): bpy.data.images.remove(bpy.data.images[k])
    print('REPORT texture', size, 'x', size, os.path.getsize(path), 'bytes')
    scene.render.engine = 'BLENDER_WORKBENCH'

if TEXTURE: bake_texture(body, TEXSIZE, os.path.join(os.path.dirname(os.path.abspath(OUT)), 'tex'))

# ---- the armature
ad = bpy.data.armatures.new('Adventurer'); arm = bpy.data.objects.new('Adventurer', ad); scene.collection.objects.link(arm)
bpy.context.view_layer.objects.active = arm; arm.select_set(True); bpy.ops.object.mode_set(mode='EDIT')
for name, (h, t, par) in B.items():
    eb = ad.edit_bones.new(name); eb.head = h; eb.tail = t
for name, (h, t, par) in B.items():
    if par: ad.edit_bones[name].parent = ad.edit_bones[par]
bpy.ops.object.mode_set(mode='OBJECT')
body.parent = arm; m = body.modifiers.new('Armature', 'ARMATURE'); m.object = arm
for c in list(col.objects): pass
bpy.data.collections.remove(col)
scene.collection.objects.link(body) if body.name not in scene.collection.objects else None

# ---- posing helpers
pb = arm.pose.bones
for b in pb: b.rotation_mode = 'XYZ'
def world(point_rest, bone):      # a point given in the rest pose, carried by the bone
    skin = pb[bone].matrix @ ad.bones[bone].matrix_local.inverted(); return arm.matrix_world @ (skin @ point_rest)
def reset():
    for b in pb: b.rotation_euler = (0, 0, 0); b.location = (0, 0, 0)
def update(): bpy.context.view_layer.update()
# which sign of a rotation about the bone's X moves its tip forward (-Y)?
FWD = {}
for name in B:
    reset(); pb[name].rotation_euler = (0.3, 0, 0); update(); tip = world(B[name][1], name); FWD[name] = 1.0 if tip.y < B[name][1].y - 1e-4 else -1.0
reset(); update()
# a twist about the bone's own Y (along its length) is a turn about the vertical for the vertical bones
HEEL, TOE = V(0.10, 0.045, 0.012), V(0.10, -0.195, 0.012)      # the sole under the boot, in the rest pose (left foot; the right is mirrored)
def sole(foot, s):
    x = 1 if s == 'L' else -1; return [world(Vector((p.x * x, p.y, p.z)), foot) for p in (HEEL, TOE)]

def pose_at(kind, phi, A):      # forward-positive angles in degrees
    d = {}
    def rot(b, fwd=0.0, twist=0.0, side=0.0): d[b] = (math.radians(fwd) * FWD[b], math.radians(twist), math.radians(side))
    if kind == 'idle':
        s, c = math.sin(phi), math.cos(phi)
        rot('spine', 1.0 * s); rot('chest', 1.4 * s, 1.2 * math.sin(phi)); rot('neck', -0.8 * s); rot('head', 1.0 * math.sin(phi + 0.8), 2.0 * math.sin(phi + 0.3))
        rot('pelvis', 0, 1.5 * math.sin(phi), 0.8 * math.sin(phi))
        for sd, k in (('L', 1), ('R', -1)):
            rot('upper_arm.' + sd, 2.0 * math.sin(phi + (0.3 if k > 0 else 0.9)), 0, 0); rot('forearm.' + sd, 4 + 1.5 * s); rot('thigh.' + sd, 0.5 * s * k); rot('shin.' + sd, 0)
        return d
    run = kind == 'run'; K = 45 if run else 34; lean = 9 if run else 2
    arm_sw = 0.8 * A if run else 0.9 * A; elbow = 85 if run else 12
    rot('pelvis', 0, 5.0 * math.sin(phi) * (1.6 if run else 1.0), 2.0 * math.sin(2 * phi))
    rot('spine', lean + (1.5 if run else 0.5) * math.cos(2 * phi), -2.5 * math.sin(phi) * (1.6 if run else 1.0)); rot('chest', 1.0 * math.cos(2 * phi), -3.0 * math.sin(phi) * (1.6 if run else 1.0)); rot('neck', -lean * 0.7); rot('head', -lean * 0.3, 1.5 * math.sin(phi))
    for sd, k in (('L', 1), ('R', -1)):
        ph = phi if k > 0 else phi + math.pi
        hip = A * math.sin(ph); flex = K * max(0.0, math.cos(ph + 0.35)) ** (1.4 if run else 1.0)
        knee_stance = (8 + 35 * max(0.0, -math.sin(ph))) if run else (4 * max(0.0, math.sin(ph)) + 10 * max(0.0, -math.sin(ph)))      # a run kicks the trailing leg back; a walk bends the knee a little as the leg trails
        rot('thigh.' + sd, hip, 0, 0); rot('shin.' + sd, -(flex + knee_stance))
        rot('foot.' + sd, -(hip - flex - knee_stance) * 0.55 + (10 * max(0.0, -math.sin(ph)) if not run else 14 * max(0.0, -math.sin(ph))))
        rot('upper_arm.' + sd, -arm_sw * math.sin(ph), 0, 0); rot('forearm.' + sd, elbow + (14 if run else 6) * (1 - math.sin(ph)) * 0.5)
    return d

def apply(kind, phi, A, lift=0.0):
    # lift is retained by the preview caller; the locomotion trajectory now owns flight height.
    reset()
    for b, (rx, ry, rz) in pose_at(kind, phi, A).items():
        if kind != 'idle' and (b == 'pelvis' or b.startswith(('thigh.', 'shin.', 'foot.'))): continue
        pb[b].rotation_euler = (rx, ry, rz)
    if kind == 'idle':
        update()
        low = min(min(p.z for p in sole('foot.L', 'L')), min(p.z for p in sole('foot.R', 'R')))
        pb['pelvis'].location = (0, -low, 0); update(); return
    period, speed, stance, height = (1.0, WALK_SPEED, 0.50, 0.10) if kind == 'walk' else (RUN_DURATION, RUN_SPEED, 0.27, 0.14)
    reach = speed * period * stance * 0.5; targets = []
    for side, shift in (('L', 0.0), ('R', 0.5)):
        u = (phi / (2 * math.pi) + shift) % 1.0
        if u < stance: y = -reach + speed * period * u; z = 0.078; pitch = 0.0
        else:
            v = (u - stance) / (1 - stance)
            tangent = speed * period * (1 - stance)
            y = (2*v**3 - 3*v*v + 1)*reach + (v**3 - 2*v*v + v)*tangent + (-2*v**3 + 3*v*v)*(-reach) + (v**3 - v*v)*tangent
            z = 0.078 + height * math.sin(math.pi * v)
            pitch = math.radians(15) * math.sin(math.pi * v)
        if '--gait-mutant' in args: y *= 0.3  # test: a stride that does not match the configured speed
        targets.append((side, y, z, pitch))
    # Keep a small bend at each knee, lowering the pelvis only as far as the
    # most extended leg needs; this avoids holding a deep crouch all cycle.
    drop = max(0.015, *(0.93 - z - math.sqrt(max(0.0, 0.83**2 - y*y)) for _, y, z, _ in targets))
    pb['pelvis'].location = (0, -drop, 0)
    for side, y, z, pitch in targets:
        forward, down = -y, 0.93 - drop - z
        l1, l2 = 0.43, 0.41
        c = max(-1.0, min(1.0, (forward*forward + down*down - l1*l1 - l2*l2) / (2*l1*l2)))
        knee = math.acos(c)
        hip = math.atan2(forward, down) + math.atan2(l2 * math.sin(knee), l1 + l2 * math.cos(knee))
        pb['thigh.' + side].rotation_euler.x = hip * FWD['thigh.' + side]
        pb['shin.' + side].rotation_euler.x = -knee * FWD['shin.' + side]
        pb['foot.' + side].rotation_euler.x = (-(hip-knee) + pitch) * FWD['foot.' + side]
    update()
A_WALK, A_RUN = 25.0, 39.0
print('REPORT walk run use planted-foot trajectory')

# ---- the clips: every frame is posed first (with animation evaluation off, or an earlier clip would re-pose the rig at every update), then the keys are written straight into the action
arm.animation_data_create(); arm.animation_data.action = None; arm.animation_data.use_nla = False
def clip(name, kind, T, A, flight=0.0):
    n = int(round(T * FPS)); frames = []
    for f in range(n + 1):
        apply(kind, 2 * math.pi * (f % n) / n, A, flight * math.sin(2 * math.pi * (f % n) / n) ** 2 if flight else 0.0)
        frames.append({b.name: (tuple(b.rotation_euler), tuple(b.location)) for b in pb})
    act = bpy.data.actions.new(name); act.use_fake_user = True
    for b in pb:
        for i in range(3):
            fc = act.fcurves.new('pose.bones["%s"].rotation_euler' % b.name, index=i, action_group=b.name)
            for f in range(n + 1): fc.keyframe_points.insert(1 + f, frames[f][b.name][0][i], options={'FAST'})
        if b.name == 'pelvis':
            for i in range(3):
                fc = act.fcurves.new('pose.bones["%s"].location' % b.name, index=i, action_group=b.name)
                for f in range(n + 1): fc.keyframe_points.insert(1 + f, frames[f][b.name][1][i], options={'FAST'})
    tr = arm.animation_data.nla_tracks.new(); tr.name = name; st = tr.strips.new(name, 1, act); st.name = name
clip('idle', 'idle', 3.0, 0.0); clip('walk', 'walk', 1.0, A_WALK); clip('run', 'run', RUN_DURATION, A_RUN, 0.040)
reset(); update(); arm.animation_data.use_nla = True

if '--gait-metrics' in args:
    # In model space the character faces -Y. Subtract forward travel from
    # a planted sole point to measure its movement in world space.
    arm.animation_data.use_nla = False
    for kind, period, amplitude, speed, flight in (('walk', 1.0, A_WALK, WALK_SPEED, 0.0), ('run', RUN_DURATION, A_RUN, RUN_SPEED, 0.040)):
        samples = 240; previous = None; slip = []
        for i in range(samples + 1):
            t = period * i / samples; phi = 2 * math.pi * (i % samples) / samples
            apply(kind, phi, amplitude, flight * math.sin(phi) ** 2)
            points = [(p.z, side, k, p.y - speed * t) for side in ('L', 'R') for k, p in enumerate(sole('foot.' + side, side))]
            z, side, k, y = min(points)
            if z < 0.006 and previous and previous[0] == side and previous[1] == k:
                slip.append(abs(y - previous[2]) * samples / period)
            previous = (side, k, y) if z < 0.006 else None
        slip.sort()
        if not slip: raise ValueError('no stance samples for ' + kind)
        print('REPORT %s ground slip median %.3f p90 %.3f m/s (%d samples)' % (kind, slip[len(slip)//2], slip[int(len(slip)*0.9)], len(slip)))
    arm.animation_data.use_nla = True
    reset(); update()

# ---- export
os.makedirs(os.path.dirname(os.path.abspath(OUT)), exist_ok=True)
bpy.ops.object.select_all(action='DESELECT'); arm.select_set(True); body.select_set(True)
bpy.ops.export_scene.gltf(filepath=OUT, export_format='GLB', use_selection=True, export_animations=True, export_animation_mode='NLA_TRACKS', export_skins=True, export_def_bones=False,
                          export_yup=True, export_apply=False, export_attributes=True, export_materials='EXPORT', export_image_format='JPEG' if TEXTURE else 'NONE', export_cameras=False, export_lights=False, export_optimize_animation_size=False, export_force_sampling=True)
print('REPORT exported', OUT, os.path.getsize(OUT), 'bytes')
run_made_for = RUN_SPEED
with open(OUT + '.txt', 'w', newline='\n') as f:      # the loader's keys: the eyes decide the size (the camera is 1.71 m above the feet), the speeds the clips were made for
    f.write('eye_joint=eye\nspeed=%.2f\nrun_speed=%.2f\n' % (WALK_SPEED, run_made_for))
print('REPORT wrote', OUT + '.txt', 'run_speed=%.2f' % run_made_for)

# ---- previews (the game is not needed to see what was made)
if PREVIEW:
    arm.animation_data.use_nla = False      # (the exported clips would override the poses of the previews)
    os.makedirs(PREVIEW, exist_ok=True)
    scene.render.engine = 'BLENDER_WORKBENCH'; sh = scene.display.shading; sh.light = 'STUDIO'; sh.color_type = 'TEXTURE' if TEXTURE else 'MATERIAL'; sh.show_cavity = False; sh.show_object_outline = False; sh.show_shadows = False
    scene.render.film_transparent = False; scene.world = bpy.data.worlds.new('w'); scene.world.color = (0.62, 0.78, 0.92); scene.render.resolution_x, scene.render.resolution_y = 640, 880
    cam_d = bpy.data.cameras.new('cam'); cam_d.lens = 70; cam = bpy.data.objects.new('cam', cam_d); scene.collection.objects.link(cam); scene.camera = cam
    def shoot(name, kind, phi, A, az, dist=5.6, lift=0.0, tz=0.93, lens=70, tx=0.0):
        cam_d.lens = lens; apply(kind, phi, A, lift); a = math.radians(az); cam.location = Vector((tx + math.sin(a) * dist, -math.cos(a) * dist, tz + 0.12)); d = Vector((tx, 0, tz)) - cam.location
        cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler(); scene.render.filepath = os.path.join(PREVIEW, name + '.png'); bpy.ops.render.render(write_still=True)
    shoot('face_front', 'idle', 0.0, 0, 0, 0.78, 0.0, 1.665, 90); shoot('face_threequarter', 'idle', 0.0, 0, 38, 0.78, 0.0, 1.665, 90)
    shoot('face', 'idle', 0.0, 0, 20, 1.5, 0.0, 1.66, 90); shoot('face_side', 'idle', 0.0, 0, 75, 1.5, 0.0, 1.66, 90)
    shoot('hand', 'idle', 0.0, 0, 80, 1.0, 0.0, 0.86, 90, 0.265)
    shoot('front', 'idle', 0.0, 0, 0); shoot('three_quarter', 'idle', 0.0, 0, 38); shoot('back', 'idle', 0.0, 0, 180)
    shoot('walk_side', 'walk', 0.9, A_WALK, 90); shoot('walk_34', 'walk', 0.9, A_WALK, 40); shoot('run_side', 'run', 1.0, A_RUN, 90, 6.2, 0.040); shoot('run_34', 'run', 1.0, A_RUN, 40, 6.2, 0.040)
    for i in range(8): shoot('run_phase_%d' % i, 'run', 2 * math.pi * i / 8, A_RUN, 90, 6.2, 0.040)
    print('REPORT previews in', PREVIEW)
