# The adventurer: a stylized traveller with a tailored teal coat, soft marigold scarf, felt hat and handmade leather gear.
# Built entirely by this script (shapes lofted from rings, a skeleton, weights by distance, idle / walk / run clips), exported as a binary glTF. Original work: CC0 1.0.
# usage: blender -b -P adventurer.py -- <out.glb> [--preview <folder>]
# Walk and run use a two-link leg solution and a planted-foot trajectory at the reference speeds written to the model config.
# Swing, contact and pelvis reach are authored in model space; exported interpolation is checked separately by tools/gait_probe.cpp.
import bpy, bmesh, math, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from appearance import material_group, parse_appearance, tint_colour
from mathutils import Vector, Matrix

args = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
STYLE = args[args.index('--style') + 1] if '--style' in args else 'witness'      # witness: painterly colour fields (docs/CHARACTER.md); classic: the first version's grain and grime
TEXTURE = '--no-texture' not in args; FLAT_MATERIALS = '--flat-materials' in args; TEXSIZE = int(args[args.index('--texsize') + 1]) if '--texsize' in args else 4096
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
COLOURS = dict(panel='#203a57', panel_line='#f3e7c8', nail='#bd967f', coat='#2f857d', coat_dark='#1d5753', trousers='#a8946b', boots='#563826', scarf='#df9d34', hat='#cfb58a', band='#a3442e', skin='#ddb397', hair='#4a3227',
               leather='#8a5634', brass='#aa8a4b', mouth_dark='#6d3930', skin_dark='#ae775e', vest='#725138', shirt='#c9bda5', sole='#211914', iris='#8c6a3a', iris_ring='#33241a', lash='#2a1f19', lips='#b87868', boots_cuff='#584031', scarf_dark='#b8742a', white='#f0e9d9', mouth='#8e5549', bedroll='#a99b7c', feather='#e48aa2', strap='#49301f', metal='#b99c68', eye='#17191a', thread='#a99b78', stitching='#b49a70')
COLOURS.update(hand_skin=COLOURS['skin'],lid_skin=COLOURS['skin'],glove_leather='#7b5238')      # (the glove: a quieter, deeper tan than the bags, so the hand reads as skin against it)
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
}
# eye.L/R (the eyeball centres: the iris, pupil and glint turn about them), lid.L/R (the upper eyelid: a blink stretches it down over the eye),
# scarf_a.1-3 and scarf_b.1-2 (the scarf tails swing), canteen and satchel (hanging gear) are added below, where the surfaces they belong to are made.
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

SIGMA = 0.075      # the distance weighting's width (metres), used below and by the coat's own weighting
parts = []      # (object, allowed bones | single rigid bone [, weights]): weights(vertex index) -> {bone: weight} replaces the distance weighting
def part(ob, bones, weights=None): parts.append((ob, bones, weights))
TORSO = ['pelvis', 'spine', 'chest']
# -- layered travelling clothes: a shaped waistcoat, shirt and an open tailored coat
part(loft('hips', [(0.84, 0, 0.005, 0.178, 0.122), (0.94, 0, 0, 0.185, 0.126), (1.02, 0, 0, 0.174, 0.120)], 'trousers', 20), ['pelvis', 'thigh.L', 'thigh.R'])
part(loft('vest', [(0.98, 0, 0, 0.176, 0.122), (1.10, 0, -0.002, 0.164, 0.115), (1.24, 0, 0, 0.188, 0.128), (1.36, 0, 0.005, 0.211, 0.137), (1.43, 0, 0.010, 0.190, 0.116)], 'vest', 24, folds=0.012), TORSO)
part(loft('shirt_collar', [(1.42, 0, 0.005, 0.112, 0.096), (1.50, 0, 0.008, 0.092, 0.082)], 'shirt', 20), ['chest', 'neck'])
COAT = [(0.57, 0, 0.018, 0.265, 0.183, 0.23), (0.70, 0, 0.012, 0.250, 0.175, 0.18), (0.87, 0, 0.004, 0.225, 0.155, 0.12), (1.04, 0, 0, 0.188, 0.135, 0.055), (1.18, 0, 0, 0.207, 0.142, 0.18),
        (1.31, 0, 0.003, 0.239, 0.152, 0.45), (1.40, 0, 0.008, 0.237, 0.146, 0.62), (1.455, 0, 0.012, 0.200, 0.122, 0.74)]
def coat_drape(co):
    p = Vector(co); angle = math.atan2(p.x, -(p.y-0.010))
    hang = max(0.0, min(1.0, (1.04-p.z)/0.47))
    # Four broad hanging folds, growing from the belt. No all-over sinusoidal crumpling.
    fold = 0.006 * hang**0.8 * math.cos(4*angle) + 0.003 * hang * math.cos(6*angle)
    p.x += math.sin(angle)*fold; p.y -= math.cos(angle)*fold
    p.z += hang**2 * 0.004 * math.cos(2*angle)
    p.z -= 0.026 * max(0.0,min(1.0,(p.z-1.34)/0.115))**2 * math.sin(angle)**2
    return p
coat_body = loft('coat', COAT, 'coat', 48, 2, False, 0.009, 0.0)
for v in coat_body.data.vertices: v.co = coat_drape(v.co)
# The coat's skirt hangs from three hem bones at the waist (front left, front right, back). The proxy swings each with the leg on its side
# (so a leg never comes through the cloth) and lets it lag and sway; in the clips themselves they stay still. Above the waist the coat
# follows the torso and the thighs by distance as before; the hem's share grows from nothing at the waist to all of it 25 cm lower.
B['hem_front.L'] = (V(0.11, -0.125, 1.00), V(0.15, -0.19, 0.57), 'pelvis'); B['hem_front.R'] = (V(-0.11, -0.125, 1.00), V(-0.15, -0.19, 0.57), 'pelvis'); B['hem_back'] = (V(0, 0.135, 1.00), V(0, 0.19, 0.57), 'pelvis')
HEM_BONES = ['hem_front.L', 'hem_front.R', 'hem_back']
def coat_weights(ob, base):      # base: the torso/thigh bones with their multipliers, as the distance weighting takes them
    cos = [v.co.copy() for v in ob.data.vertices]; mult = {(b[0] if isinstance(b, tuple) else b): (b[1] if isinstance(b, tuple) else 1.0) for b in base}
    def smooth(t): t = max(0.0, min(1.0, t)); return t * t * (3 - 2 * t)
    def seg(p, a, b):
        ab = b - a; t = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.length_squared, 1e-9))); return (p - (a + ab * t)).length
    def w(i):
        p = cos[i]; h = smooth((1.0 - p.z) / 0.25); angle = math.atan2(p.x, -(p.y - 0.01))
        back = smooth((abs(angle) - 1.5) / 0.9); left = smooth((angle + 0.12) / 0.24)
        out = {'hem_front.L': h * (1 - back) * left, 'hem_front.R': h * (1 - back) * (1 - left), 'hem_back': h * back}
        raw = [(b, mult[b] * math.exp(-(seg(p, B[b][0], B[b][1]) / SIGMA) ** 2)) for b in mult]; raw.sort(key=lambda r: -r[1]); raw = raw[:4]; tot = sum(x for _, x in raw)
        if tot > 1e-6:
            for b, x in raw: out[b] = out.get(b, 0.0) + (1 - h) * x / tot
        return {b: x for b, x in out.items() if x > 0.01}
    return w
COAT_BASE = ['pelvis', 'spine', 'chest', ('thigh.L', 0.55), ('thigh.R', 0.55)]
part(coat_body, COAT_BASE + HEM_BONES, coat_weights(coat_body, COAT_BASE))
coat_facing = loft('coat_facing', [(0.571, 0, 0.018, 0.265, 0.185, 0.23), (0.600, 0, 0.017, 0.263, 0.184, 0.22)], 'coat_dark', 36, 0, False, 0.014)
for v in coat_facing.data.vertices: v.co = coat_drape(v.co)
part(coat_facing, COAT_BASE + HEM_BONES, coat_weights(coat_facing, COAT_BASE))
coat_collar = loft('collar', [(1.43, 0, 0.016, 0.112, 0.095, 0.52), (1.48, 0, 0.018, 0.098, 0.083, 0.58), (1.535, 0, 0.020, 0.070, 0.060, 0.64)], 'coat_dark', 28, 1, False, 0.010)
part(coat_collar, ['chest', 'neck'])
for sd, x in (('L', 1), ('R', -1)):
    flap = box('pocket.' + sd, (0.125 * x, -0.174, 0.805), (0.105, 0.018, 0.064), 'coat_dark', 0.005)
    flap.data.transform(Matrix.Translation((0.125*x, -0.174, 0.805)) @ Matrix.Rotation(x * 0.08, 4, 'Y') @ Matrix.Translation((-0.125*x, 0.174, -0.805)))
    part(flap, ['pelvis'] + HEM_BONES, coat_weights(flap, ['pelvis']))
    btn = ellipsoid('pocket_btn.' + sd, (0.125 * x, -0.187, 0.810), (0.008, 0.004, 0.008), 'brass', 10, 6); part(btn, ['pelvis'] + HEM_BONES, coat_weights(btn, ['pelvis']))
for z in (1.11, 1.22, 1.33):
    part(ellipsoid('vest_button', (0, -0.127 if z < 1.2 else -0.137, z), (0.008, 0.0045, 0.008), 'brass', 10, 6), TORSO)
part(loft('belt', [(1.015, 0, 0, 0.205, 0.147), (1.072, 0, 0, 0.205, 0.147)], 'strap', 32, 0, False, 0.005), ['pelvis', 'spine'])
part(box('buckle', (0, -0.158, 1.044), (0.064, 0.015, 0.054), 'brass', 0.005), ['pelvis', 'spine'])
part(box('buckle_inset', (0, -0.168, 1.044), (0.043, 0.006, 0.034), 'strap', 0.003), ['pelvis', 'spine'])
part(box('buckle_tongue', (0, -0.174, 1.044), (0.035, 0.006, 0.007), 'brass', 0.002), ['pelvis', 'spine'])
# -- articulated sleeves, fingerless gloves and layered leather wrist guards
for s, x in (('L', 1), ('R', -1)):
    arms = ['chest', 'upper_arm.' + s, 'forearm.' + s]
    sleeve = loft('sleeve.'+s, [(1.035, 0.263*x, 0.002, 0.046, 0.050), (1.085, 0.260*x, 0.002, 0.052, 0.056), (1.17, 0.256*x, -0.002, 0.058, 0.064), (1.23, 0.249*x, 0.006, 0.065, 0.072), (1.32, 0.236*x, 0.002, 0.071, 0.076), (1.415, 0.214*x, 0.006, 0.074, 0.077), (1.455, 0.204*x, 0.008, 0.050, 0.059)], 'coat', 28, folds=0.0)
    for v in sleeve.data.vertices:
        p = v.co; cx = x*(0.263 - 0.049*max(0.0, min(1.0, (p.z-1.035)/0.380)))
        angle = math.atan2(p.x-cx, -p.y); elbow = math.exp(-((p.z-1.16)/0.055)**2)
        # Compression lives at the elbow, and is measured about the sleeve's axis.
        radial = 1 + 0.028*elbow*math.cos(65*(p.z-1.16)+1.2*math.sin(angle))
        p.x = cx + (p.x-cx)*radial; p.y *= radial
    part(sleeve, arms)
    part(loft('cuff.'+s, [(1.024, 0.263*x, 0.002, 0.049, 0.054), (1.074, 0.261*x, 0.002, 0.052, 0.058)], 'coat_dark', 28, 0, False, 0.005), ['forearm.'+s, 'upper_arm.'+s])
    part(loft('forearm.' + s, [(0.94, 0.265*x, 0, 0.033, 0.037), (0.96, 0.265*x, 0, 0.038, 0.043), (1.04, 0.263*x, 0, 0.043, 0.049), (1.12, 0.259*x, 0, 0.048, 0.052)], 'skin', 18), ['upper_arm.' + s, 'forearm.' + s, 'hand.' + s])
    part(loft('bracer.' + s, [(0.928, 0.265*x, 0, 0.044, 0.049), (0.970, 0.264*x, 0, 0.047, 0.052), (1.035, 0.263*x, 0, 0.050, 0.055)], 'leather', 24, 0, False, 0.004), ['forearm.' + s])
    for z in (0.955, 1.012):
        part(loft('bracer_strap.' + s, [(z-0.010, 0.265*x, 0, 0.049, 0.055), (z+0.010, 0.265*x, 0, 0.049, 0.055)], 'strap', 20, 0, False, 0.003), ['forearm.' + s])
        part(box('bracer_buckle.' + s, (0.314*x, -0.005, z), (0.008, 0.025, 0.023), 'brass', 0.002), ['forearm.' + s])
# -- legs: breeches, a knee strap, tall boots with a turned cuff, a sole and a heel
for s, x in (('L', 1), ('R', -1)):
    part(loft('trousers.' + s, [(0.46, 0.10 * x, 0, 0.084, 0.086), (0.525, 0.10 * x, 0, 0.100, 0.102), (0.60, 0.10 * x, 0, 0.097, 0.099), (0.72, 0.10 * x, 0, 0.106, 0.108), (0.88, 0.10 * x, 0, 0.086, 0.098), (0.98, 0.10 * x, 0, 0.074, 0.094)], 'trousers', 24, folds=0.025), ['pelvis', 'thigh.' + s, 'shin.' + s])      # bloused over the boot tops
    part(loft('knee_strap.' + s, [(0.50, 0.10 * x, 0, 0.078, 0.080), (0.535, 0.10 * x, 0, 0.078, 0.080)], 'strap', 12, 0), ['thigh.' + s, 'shin.' + s])
    part(loft('boot_shaft.' + s, [(0.07, 0.10 * x, 0.004, 0.066, 0.075), (0.13, 0.10 * x, 0.004, 0.057, 0.065), (0.22, 0.10 * x, 0.006, 0.066, 0.072), (0.32, 0.10 * x, 0.008, 0.077, 0.081), (0.42, 0.10 * x, 0.004, 0.082, 0.084), (0.470, 0.10 * x, 0.004, 0.088, 0.088)], 'boots', 18), ['shin.' + s, 'foot.' + s])      # a shaped boot: a slim ankle, a full calf
    part(loft('boot_cuff.' + s, [(0.44, 0.10 * x, 0.004, 0.093, 0.093), (0.505, 0.10 * x, 0.004, 0.106, 0.106)], 'boots_cuff', 14, 0), ['shin.' + s])
    part(ellipsoid('boot_toe.' + s, (0.10 * x, -0.056, 0.064), (0.080, 0.174, 0.071), 'boots', 14, 9), ['foot.' + s])
    part(box('boot_sole.' + s, (0.10 * x, -0.066, 0.020), (0.140, 0.300, 0.034), 'sole', 0.016), ['foot.' + s])
    part(box('boot_heel.' + s, (0.10 * x, 0.048, 0.033), (0.112, 0.080, 0.050), 'sole', 0.010), ['foot.' + s, 'shin.' + s])
    part(box('boot_lace.' + s, (0.10 * x, -0.095, 0.112), (0.034, 0.100, 0.014), 'boots_cuff', 0.004, Matrix.Rotation(-0.55, 3, 'X')), ['foot.' + s])
# -- the head: sculpted as signed distance fields (head.py; the measurements are character_spec.py's): the skull, the face's
# planes, real eyeballs in sockets under thick lids, the nose, the lips, the ears and the neck, as one surface. It is meshed with
# surface nets, decimated to its budget and shaded with the field's own normals. docs/CHARACTER.md says why it looks the way it does.
import numpy as np
import sdf, head as headmod
from character_spec import FACE, TARGETS
HEAD_CELL = 0.0018
def field_part(name, f, lo, hi, cell, material, tris, relax=2, symmetric=False):
    if symmetric:      # x = 0 on a grid line: the mesh is its own mirror image (and so is the decimation)
        k = math.ceil(max(abs(lo[0]), abs(hi[0])) / cell); lo = (-k * cell, lo[1], lo[2]); hi = (k * cell, hi[1], hi[2])
    V, Q, _ = sdf.mesh(f, lo, hi, cell)
    if relax: V, _ = sdf.relax(f, V, Q, relax)
    bm = bmesh.new(); vs = [bm.verts.new(v) for v in V.tolist()]
    for q in Q.tolist():
        try: bm.faces.new([vs[i] for i in q])
        except ValueError: pass      # (a duplicate face at a grid pinch)
    ob = finish(name, bm, material); me = ob.data
    if tris and len(me.polygons) * 2 > tris:      # down to the budget; the shape is kept by collapsing the flattest edges first
        m = ob.modifiers.new('D', 'DECIMATE'); m.ratio = tris / (2.0 * len(me.polygons))
        if symmetric: m.use_symmetry = True; m.symmetry_axis = 'X'      # the same cuts on both sides of a face
        dg = bpy.context.evaluated_depsgraph_get(); ev = ob.evaluated_get(dg); me2 = bpy.data.meshes.new_from_object(ev); ob.modifiers.clear(); old = ob.data; ob.data = me2; bpy.data.meshes.remove(old); me = ob.data
    bm = bmesh.new(); bm.from_mesh(me); bmesh.ops.dissolve_degenerate(bm, dist=1e-5, edges=bm.edges[:]); bm.to_mesh(me); bm.free()      # (slivers left by the decimation sample unbaked texels)
    # The decimation folds a few thin triangles over their neighbours (they become specks after the bake): relax every vertex toward
    # its neighbours' mean along the surface and put it back on the field, three times.
    nb = [[] for _ in me.vertices]
    for e in me.edges: a_, b_ = e.vertices; nb[a_].append(b_); nb[b_].append(a_)
    P = np.array([v.co[:] for v in me.vertices]); nbs = [np.array(x, int) for x in nb]
    for _ in range(3):
        avg = np.array([P[x].mean(axis=0) if len(x) else P[i] for i, x in enumerate(nbs)]); P = P + 0.5 * (avg - P)
        for _ in range(2): g = sdf.gradient(f, P); P = P - f(P)[:, None] * g
    me.vertices.foreach_set('co', P.reshape(-1)); me.update()
    P = np.array([v.co[:] for v in me.vertices]); me.normals_split_custom_set_from_vertices(sdf.gradient(f, P).tolist())
    FIELDS[ob.name] = f
    print('REPORT %s: %d triangles from the field' % (name, sum(len(p.vertices) - 2 for p in me.polygons)))
    return ob
FIELDS = {}
def paint(ob, mask, material):      # faces whose centre the mask selects take the material and its appearance label, on vertices of their own
    me = ob.data; me.materials.append(mat(material)); k = len(me.materials) - 1
    cs = np.array([p.center[:] for p in me.polygons]); sel = mask(cs) if len(cs) else np.zeros(0, bool)
    for p, s_ in zip(me.polygons, sel):
        if s_: p.material_index = k
    bm = bmesh.new(); bm.from_mesh(me)
    seam = [e for e in bm.edges if len(e.link_faces) == 2 and (e.link_faces[0].material_index == k) != (e.link_faces[1].material_index == k)]
    bmesh.ops.split_edges(bm, edges=seam); bm.to_mesh(me); bm.free()
    label = me.attributes['_APPEARANCE']; g = material_group(material)
    for p in me.polygons:
        if p.material_index == k:
            for vi in p.vertices: label.data[vi].value = g
    if ob.name in FIELDS:      # the split vertices need the field's normals again
        P = np.array([v.co[:] for v in me.vertices]); me.normals_split_custom_set_from_vertices(sdf.gradient(FIELDS[ob.name], P).tolist())
    return int(np.sum(sel))
head_field = sdf.intersect(headmod.skull_and_face(), sdf.plane((0, 0, -1), 1.425))      # closed under the scarf
head_surface = field_part('head', head_field, (-0.13, -0.16, 1.40), (0.13, 0.13, 1.84), HEAD_CELL, 'skin', 26000, 2, True)
mz = FACE['mouth_z']
# Lips are painted in object space; polygon material borders made a jagged mouth.
pass      # (the mouth's line is painted on the skin along the smile: paint_materials)
# the lower lip and the skin under it follow the lip bone (the proxy drops it while the player speaks): a smooth falloff from the lip to the chin and the corners
B['lip_lower'] = (V(0, -0.104, mz - 0.004), V(0, -0.104, mz - 0.014), 'head')
def head_weights(i, co=None):
    p = head_surface.data.vertices[i].co
    if 'HEAD_SCALE' in globals() and HEAD_SCALE != 1.0: p = HEAD_PIVOT + (p - HEAD_PIVOT) / HEAD_SCALE      # (weights are designed on the sculpt's own scale)
    dx = abs(p.x) / (FACE['mouth_w'] * 0.62); dz = (mz - 0.0006 - p.z) / 0.016
    lip = 0.0
    if p.y < -0.085 and p.z < mz - 0.0006 and dx < 1 and dz < 1:      # the lower lip moves whole; the skin below it and toward the corners follows less and less
        a_ = max(0.0, (dx - 0.68) / 0.32); b_ = max(0.0, (dz - 0.42) / 0.58); lip = (1 - a_ * a_) ** 2 * (1 - b_ * b_) ** 2
    raw = {b: math.exp(-(seg_dist(p, B[b][0], B[b][1]) / SIGMA) ** 2) * mult for b, mult in (('head', 1.0), ('neck', 0.7), ('chest', 0.6))}
    tot = sum(raw.values()) or 1.0; ws = {b: w / tot * (1 - lip) for b, w in raw.items()}
    if p.z > 1.58: ws = {'head': 1 - lip}      # the face itself is rigid on the skull
    ws['lip_lower'] = lip; return ws
part(head_surface, ['head', 'neck', 'chest', 'lip_lower'], head_weights)
from mathutils.bvhtree import BVHTree
head_bvh = BVHTree.FromObject(head_surface, bpy.context.evaluated_depsgraph_get())
def face_y(x, z):
    hit, _, _, _ = head_bvh.ray_cast(Vector((x, -0.4, z)), Vector((0, 1, 0)))
    if hit is None: raise ValueError('facial detail misses head at %.4f, %.4f' % (x, z))
    return hit.y
def face_finish(name, bm, material, smooth=True, subsurf=0, bevel=0.0, solid=0.0):
    return finish(name, bm, material, smooth, subsurf, bevel, solid, front_facing=True)
def cap(name, centre, r, angle, material, bones, segs=48, rings=10):
    """A spherical cap of radius r round the forward (-y) axis, `angle` radians from its middle to its rim: the eyeball's parts."""
    bm = bmesh.new(); c = Vector(centre); pole = bm.verts.new(c + V(0, -r, 0)); prev = None
    for j in range(1, rings + 1):
        a = angle * j / rings
        row = [bm.verts.new(c + V(r * math.sin(a) * math.cos(2 * math.pi * i / segs), -r * math.cos(a), r * math.sin(a) * math.sin(2 * math.pi * i / segs))) for i in range(segs)]
        for i in range(segs):
            if prev is None: bm.faces.new((pole, row[(i + 1) % segs], row[i]))
            else: bm.faces.new((prev[i], prev[(i + 1) % segs], row[(i + 1) % segs], row[i]))
        prev = row
    part(face_finish(name, bm, material), bones)
# A clean smile drawn onto the actual face surface. Submillimetre offset avoids both a floating strip
# and the triangle-sized material boundaries of the old painted lips.
def smile_ribbon(name, half_height, z_offset, material, bone):
    bm = bmesh.new(); rows = []
    for i in range(49):
        u = -1 + 2*i/48; x = u*FACE['mouth_w']*0.47
        z = FACE['mouth_z'] + FACE['smile']*u*u + z_offset
        h = half_height * max(0.08, 1-u*u)**0.65
        row = []
        for dz in (-h, h):
            row.append(bm.verts.new((x, face_y(x, z+dz)-0.00065, z+dz)))
        rows.append(row)
    for a,b in zip(rows, rows[1:]): bm.faces.new((a[0],b[0],b[1],a[1]))
    part(face_finish(name,bm,material),[bone])
smile_ribbon('lower_lip_colour',0.0023,-0.0022,'lips','lip_lower')
smile_ribbon('upper_lip_colour',0.0011,0.0010,'lips','head')
smile_ribbon('smile',0.00085,0.0,'mouth_dark','head')

# Eyes: a ball in each socket. The eye bone turns the iris, pupil and their ring about the ball's centre (the white is a sphere: turning it changes nothing).
# Lids: the upper lid is a shell round the ball (head.upper_lids) on a bone at the ball's centre; a blink turns it down over the ball (lid_rotate in the
# .glb.txt; the proxy's life layer turns the bone about its local x). Its rim is the lash line.
LID_ROTATE = 0.98 if '--lid-sign-mutant' in args else -0.98      # (negative: the bone's local x points to the character's right in the exported rig; tools/check_character.py proves the lid closes)
for s, x in (('L', 1), ('R', -1)):
    c = headmod.eye_centre(x); r = FACE['eye_r']
    if '--eye-mutant' in args: c = c + np.array([0, -0.006, 0])      # test: an eyeball standing out of its socket
    B['eye.' + s] = (V(*c), V(*(c + np.array([0, -0.012, 0]))), 'head')
    B['lid.' + s] = (V(*c), V(*(c + np.array([0, -0.020, 0]))), 'head')
    cap('eye_white.' + s, c, r, math.radians(88), 'white', ['head'])
    ia = math.asin(min(0.95, (FACE['iris_r'] * (1.6 if '--iris-mutant' in args else 1.0)) / r))
    cap('iris_ring.' + s, c, r + 0.00020, ia, 'iris_ring', ['eye.' + s])
    cap('iris.' + s, c, r + 0.00035, ia * 0.86, 'iris', ['eye.' + s])
    cap('pupil.' + s, c, r + 0.00050, math.asin(FACE['pupil_r'] * 1.12 / r), 'eye', ['eye.' + s])
    glint = c + np.array([-0.0028 * x * 0 - 0.0030, 0, 0.0036])
    cap('eye_glint.' + s, glint, r + 0.0008, 0.13, 'white', ['head'], 12, 3)      # a big catchlight, high and to one side
    lid = field_part('lid.' + s, headmod.upper_lids()[x], c - 0.026, c + 0.026, HEAD_CELL * 0.4, 'lid_skin', 1600, 1)
    # The upper rim is shaded by its thickness; no triangle-sized lash flecks.
    part(lid, ['lid.' + s])
    bm=bmesh.new(); rows=[]
    for i in range(49):
        t=math.pi*i/48;row=[]
        for scale in (1.005,1.065):
            dx=FACE['aperture_w']*math.cos(t)*scale
            dz=(FACE['aperture_up']*math.sin(t)*(1+FACE['aperture_skew']*math.cos(t))+FACE['corner_lift']*math.cos(t))*scale
            yy=c[1]-math.sqrt(max(1e-8,(r+FACE['lid_thick'])**2-dx*dx-dz*dz))-0.0003
            row.append(bm.verts.new((c[0]+x*dx,yy,c[2]+dz)))
        rows.append(row)
    for a,b in zip(rows,rows[1:]):bm.faces.new((a[0],b[0],b[1],a[1]))
    part(face_finish('upper_lash.'+s,bm,'lash'),['lid.'+s])

    brow = field_part('brow.' + s, headmod.brows()[x], c + np.array([0, -0.02, 0.032]) - 0.04, c + np.array([0, -0.02, 0.032]) + 0.04, HEAD_CELL * 0.5, 'hair', 1000, 1)
    b0, b1 = V(x * 0.013, -0.1000, 1.7050), V(x * 0.061, -0.0855, 1.7075)
    B['brow.' + s] = ((b0 + b1) * 0.5, (b0 + b1) * 0.5 + (b1 - b0) * 0.5, 'head')
    part(brow, ['brow.' + s])
# -- the hands: sculpted (hands.py): a solid palm, tapered fingers in a relaxed curl, a thumb with mass, the fingerless glove over
# the palm and the knuckles. The wrist blends into the forearm over 3 cm.
import hands as handmod
for s, x in (('L', 1), ('R', -1)):
    lo, hi = handmod.bounds(x)
    for kind, field, material, budget in (('hand', handmod.skin(x), 'hand_skin', 4200), ('glove', handmod.glove(x), 'glove_leather', 2600)):
        ob = field_part(kind + '.' + s, field, lo, hi, 0.0012, material, budget, 1)
        def hand_weights(i, ob=ob, s=s):
            z = ob.data.vertices[i].co.z; w = max(0.0, min(1.0, (z - (handmod.WRIST[2] - 0.012)) / 0.030))
            return {'hand.' + s: 1 - w, 'forearm.' + s: w}
        part(ob, ['hand.' + s, 'forearm.' + s], hand_weights)
# -- hair: a sculpted crop (head.hair): a smooth cap, a broad swept forelock and a quiet nape, clear of the ears
hair_ob = field_part('hair', headmod.hair(headmod.skull_and_face()), (-0.13, -0.16, 1.52), (0.13, 0.15, 1.84), HEAD_CELL * 1.2, 'hair', 7000, 1, False)
part(hair_ob, ['head'])
# Storybook proportions: the whole head (skull, face, eyes, lids, brows, hair, their bones and the face's paint) is HEAD_SCALE times the
# sculpt, about the base of the neck. The hat is fitted to it below; the proxy's eye-height fit absorbs the rest.
HEAD_SCALE = 1.0 if '--life-size-head' in args else 1.10; HEAD_PIVOT = V(0, 0.012, 1.50)
def head_scaled(p): return HEAD_PIVOT + (Vector(p) - HEAD_PIVOT) * HEAD_SCALE
if HEAD_SCALE != 1.0:
    hx = Matrix.Translation(HEAD_PIVOT) @ Matrix.Scale(HEAD_SCALE, 4) @ Matrix.Translation(-HEAD_PIVOT)
    for o, _, _ in parts:
        if o.name.split('.')[0] in ('head', 'eye_white', 'iris_ring', 'iris', 'pupil', 'eye_glint', 'lid', 'brow', 'hair', 'lower_lip_colour', 'upper_lip_colour', 'smile', 'upper_lash'): o.data.transform(hx)
    for bn in ('eye.L', 'eye.R', 'lid.L', 'lid.R', 'brow.L', 'brow.R', 'lip_lower'):
        h, t, par = B[bn]; B[bn] = (head_scaled(h), head_scaled(t), par)
    head_bvh = BVHTree.FromObject(head_surface, bpy.context.evaluated_depsgraph_get())
    print('REPORT head scaled %.2f about the neck' % HEAD_SCALE)
HAT0 = len(parts)
# -- shaped felt hat, with an upswept brim, a pinched crown and a folded feather vane
# The opening is an oval the size of the head at the brow line (plus the hair), so the hat sits on the head instead of above it
# (the check below measures the result).
HAT_OPEN = (0.128, 0.128) if '--hat-mutant' in args else (0.110, 0.122)      # (the mutant: the earlier round opening, which floated above the head)
brim = loft('hat_brim', [(1.768, 0, 0.006, *HAT_OPEN), (1.758, 0, 0.004, 0.176, 0.178), (1.748, 0, 0, 0.222, 0.216), (1.750, 0, -0.004, 0.252, 0.244), (1.762, 0, -0.006, 0.264, 0.254)], 'hat', 64, 2, False, 0.012)
for v in brim.data.vertices:
    v.co.z += 0.034 * (abs(v.co.x) / 0.264) ** 3 + 0.006 * math.sin(v.co.y * 12)
part(brim, ['head'])
crown = loft('hat_crown', [(1.756, 0, 0.006, HAT_OPEN[0] - 0.001, HAT_OPEN[1] - 0.001), (1.768, 0, 0.006, *HAT_OPEN),      # (a support ring: the wall stays straight down under the brim)
     (1.806, 0, 0.006, 0.115, 0.127), (1.844, 0.004, 0.008, 0.115, 0.125), (1.877, 0.006, 0.010, 0.106, 0.114), (1.895, 0.004, 0.012, 0.078, 0.094), (1.899, 0, 0.014, 0.038, 0.046)], 'hat', 32, 1)
for v in crown.data.vertices:
    if v.co.z > 1.85:
        v.co.z -= 0.013 * math.exp(-((v.co.x / 0.045) ** 2 + ((v.co.y - 0.01) / 0.065) ** 2))
part(crown, ['head'])
part(loft('hat_band', [(1.776, 0, 0.006, 0.116, 0.128), (1.803, 0, 0.006, 0.119, 0.131)], 'band', 40, 0, False, 0.002), ['head'])
part(ellipsoid('hat_knot', (0.095, -0.078, 1.790), (0.015, 0.012, 0.013), 'band', 14, 8), ['head'])
f0,f1=V(0.097,-0.060,1.798),V(0.160,0.047,1.973)
bm=bmesh.new();rows=[];shaft=[]
for i in range(25):
    t=i/24;c=f0.lerp(f1,t)+V(0.013*math.sin(math.pi*t),-0.007*math.sin(math.pi*t),0)
    w=0.026*math.sin(math.pi*t)**0.85;shaft.append(c)
    rows.append([bm.verts.new(c+V(-w,0,0)),bm.verts.new(c+V(0,-0.004,0)),bm.verts.new(c+V(w*0.8,0,0))])
for a,b in zip(rows,rows[1:]):
    for k in range(2):bm.faces.new((a[k],b[k],b[k+1],a[k+1]))
part(face_finish('feather_vane',bm,'feather',True,0,0,0.0015),['head'])
for a,b in zip(shaft,shaft[1:]):part(tube('feather_shaft',a,b,0.0015,'bedroll',8),['head'])
def tilt(ob, rx, pivot=(0, -0.02, 1.74)):
    p = Vector(pivot); ob.data.transform(Matrix.Translation(p) @ Matrix.Rotation(rx, 4, 'X') @ Matrix.Translation(-p))
HAT_DROP = 0.010 if '--hat-mutant' in args else 0.026      # the opening sits above the brows: the face stays open to a camera at eye height
# The hat is made for an opening of HAT_OPEN; it is scaled (in plan) to the head that was actually built: the head plus 4 mm of hair plus
# 3 mm of ease, measured where the opening lands.
def head_reach(direction, z):
    d = V(direction[0], direction[1], 0).normalized(); hit = head_bvh.ray_cast(V(0, 0.006, z) + d * 0.3, -d)[0]
    return (hit - V(0, 0.006, z)).length if hit is not None else None
z_open = 1.768 - HAT_DROP
hat_rise = (HEAD_SCALE - 1.0) * (z_open - HEAD_PIVOT.z); z_open += hat_rise      # the hat rides on the scaled head
for o, _, _ in parts[HAT0:]: o.data.transform(Matrix.Translation((0, 0, hat_rise)))
want = [head_reach(d, z_open) for d in ((1, 0), (-1, 0), (0, 1), (0, -1))]
if '--hat-mutant' not in args and None not in want:
    hsx = (max(want[0], want[1]) + 0.0105) / HAT_OPEN[0]; hsy = (max(want[2], want[3]) + 0.0105) / HAT_OPEN[1]
    print('REPORT hat scaled to the head: x %.3f y %.3f' % (hsx, hsy))
    for o, _, _ in parts[HAT0:]: o.data.transform(Matrix.Translation((0, 0.006, 0)) @ Matrix.Diagonal((hsx, hsy, 1, 1)) @ Matrix.Translation((0, -0.006, 0)))
for o, _, _ in parts[HAT0:]: tilt(o, -0.12); o.data.transform(Matrix.Translation((0, 0, -HAT_DROP)))      # tipped back a little
# Fit check: the rim of the hat's opening (the crown's lowest vertex in each direction) must sit just outside the head and hair there.
rim = {}; low = {}
def _bin(co): return int(((math.atan2(co.y - 0.006, co.x) + 2 * math.pi) % (2 * math.pi)) / (2 * math.pi) * 36) % 36
for v in crown.data.vertices: k = _bin(v.co); low[k] = min(low.get(k, 9.0), v.co.z)
for v in crown.data.vertices:      # the wall at the opening: the widest vertex within 8 mm of the lowest in each direction (subdivision rounds the cap's edge)
    k = _bin(v.co)
    if v.co.z <= low[k] + 0.008 and (k not in rim or V(v.co.x, v.co.y - 0.006, 0).length > V(rim[k].x, rim[k].y - 0.006, 0).length): rim[k] = v.co.copy()
gaps = []
for k, p in rim.items():
    r = V(p.x, p.y - 0.006, 0); d = r.length; r.normalize(); hh = head_bvh.ray_cast(V(0, 0.006, p.z) + r * 0.2, -r)[0]
    if hh is not None: gaps.append((d - (hh - V(0, 0.006, p.z)).length - 0.004, k * 10))      # 4 mm of hair on the scalp
print('REPORT hat fit: rim to hair min %.1f mm (%d deg) max %.1f mm (%d deg), %d directions' % (min(gaps)[0] * 1000, min(gaps)[1], max(gaps)[0] * 1000, max(gaps)[1], len(gaps)))
if (len(gaps) < 30 or min(gaps)[0] < -0.002 or max(gaps)[0] > 0.012): raise ValueError('the hat does not fit the head: %s %s' % (min(gaps), max(gaps)))
# -- a softer scarf and well-used travelling gear
scarf_wrap = loft('scarf', [(1.425,0,0.008,0.126,0.112),(1.442,0,0.006,0.125,0.111),
    (1.463,0.003,0.004,0.121,0.107),(1.488,0.002,0.004,0.114,0.101),
    (1.513,-0.003,0.005,0.105,0.094),(1.535,-0.004,0.006,0.096,0.085),
    (1.547,-0.003,0.007,0.080,0.072),(1.559,-0.002,0.008,0.067,0.061)],'scarf',48,2,False,0.008)
for v in scarf_wrap.data.vertices:
    p=v.co;angle=math.atan2(p.x,-(p.y-0.006));u=max(0,min(1,(p.z-1.425)/0.134))
    fold=0.003*math.sin(math.pi*u)*math.sin(4*math.pi*u+0.9*math.sin(angle))
    p.x+=math.sin(angle)*fold;p.y-=math.cos(angle)*fold
    p.z+=0.005*math.sin(angle)*u*u
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
# The scarf tails hang from chains of bones (the proxy swings them as pendulums when the player moves or the wind blows); the satchel and the
# canteen hang from one bone each. A chain bone starts at a ring's centre and ends at the next ring's.
TAIL_A = [(1.45, 0.064, -0.145, 0.027, 0.006), (1.36, 0.072, -0.151, 0.031, 0.007), (1.25, 0.077, -0.158, 0.033, 0.007), (1.16, 0.085, -0.161, 0.027, 0.006)]
TAIL_B = [(1.44, 0.015, -0.143, 0.024, 0.006), (1.37, 0.017, -0.146, 0.028, 0.007), (1.30, 0.024, -0.151, 0.026, 0.006)]
for tail, rings in (('a', TAIL_A), ('b', TAIL_B)):
    for k, (r0, r1) in enumerate(zip(rings, rings[1:])):
        B['scarf_%s.%d' % (tail, k + 1)] = (V(r0[1], r0[2], r0[0]), V(r1[1], r1[2], r1[0]), 'chest' if k == 0 else 'scarf_%s.%d' % (tail, k))
part(loft('scarf_tail_a', TAIL_A, 'scarf', 16, folds=0.06), ['scarf_a.1', 'scarf_a.2', 'scarf_a.3'])
part(loft('scarf_tail_b', TAIL_B, 'scarf_dark', 16, folds=0.05), ['scarf_b.1', 'scarf_b.2'])
B['satchel'] = (V(-0.312, 0.040, 0.985), V(-0.312, 0.040, 0.80), 'spine')
satchel = box('satchel', (-0.312, 0.040, 0.86), (0.092, 0.270, 0.230), 'leather', 0.020); part(satchel, ['satchel'])
part(box('satchel_gusset', (-0.316, 0.040, 0.850), (0.098, 0.238, 0.196), 'strap', 0.016), ['satchel'])
part(box('satchel_flap', (-0.315, 0.040, 0.950), (0.102, 0.282, 0.100), 'leather', 0.014), ['satchel'])
part(box('satchel_clasp', (-0.370, 0.040, 0.915), (0.009, 0.034, 0.044), 'brass', 0.003), ['satchel'])
part(box('satchel_clasp_inset', (-0.376, 0.040, 0.915), (0.004, 0.020, 0.027), 'strap', 0.002), ['satchel'])
SATCHEL_MOTIF = True      # (made below, once the decal helper exists)
from mathutils.bvhtree import BVHTree as SurfaceBVH
strap_surfaces = [SurfaceBVH.FromObject(ob, bpy.context.evaluated_depsgraph_get()) for ob, _, _ in parts if ob.name == 'vest']
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
rucksack = pack_drape(box('rucksack', (0, 0.205, 1.06), (0.250, 0.115, 0.310), 'leather', 0.028, None, 1)); part(rucksack, ['spine', 'pelvis', 'chest'])
bm=bmesh.new();rows=[]
for j,(y,z) in enumerate(((0.154,1.212),(0.167,1.230),(0.251,1.235),(0.275,1.207),(0.277,1.150))):
    row=[]
    for i in range(25):
        u=-1+2*i/24;x=0.123*u
        zz=z+(0.027*u*u if j==4 else 0)
        row.append(bm.verts.new((x,y+0.003*(1-u*u),zz)))
    rows.append(row)
for a,b in zip(rows,rows[1:]):
    for i in range(24):bm.faces.new((a[i],a[i+1],b[i+1],b[i]))
part(finish('rucksack_flap',bm,'strap',True,1,0,0.005),['spine','chest'])
for x in (-0.075, 0.075):
    part(box('pack_strap', (x, 0.275, 1.065), (0.025, 0.012, 0.235), 'strap', 0.004), ['spine', 'pelvis', 'chest'])
    part(box('pack_keeper', (x, 0.283, 1.13), (0.034, 0.008, 0.030), 'brass', 0.003), ['spine', 'chest'])
    part(box('pack_keeper_inset', (x, 0.288, 1.13), (0.019, 0.005, 0.015), 'strap', 0.001), ['spine', 'chest'])
# -- the island's sign: a puzzle line (a start circle, a path through a grid, a rounded end), embroidered in cream on the rucksack and
# worked as a little panel on the satchel. Ribbons and discs are ray-cast onto the surface they sit on and raised off it.
MOTIF_PATH = [(0.0, 0.0), (0.0, 0.5), (0.5, 0.5), (0.5, 0.25), (1.0, 0.25), (1.0, 1.0), (1.0, 1.12)]      # a 4 x 4 grid, unit square
def decal(name, target, origin, u_axis, v_axis, size, width, material, bones, raise_=0.0018, path=MOTIF_PATH, start=2.3, panel=None):
    tb = BVHTree.FromObject(target, bpy.context.evaluated_depsgraph_get()); O = V(*origin); U = V(*u_axis).normalized(); W = V(*v_axis).normalized(); Nn = U.cross(W).normalized()
    def onto(u, v, lift):
        p = O + U * (u * size) + W * (v * size); hit, nrm, _, _ = tb.ray_cast(p + Nn * 0.06, -Nn)
        if hit is None: raise ValueError('%s misses its surface at %.2f %.2f' % (name, u, v))
        return hit + nrm * lift, nrm
    bm = bmesh.new()
    def disc(u, v, r, lift):
        c, n_ = onto(u, v, lift); ring = []
        for i in range(20):
            a = 2 * math.pi * i / 20; q, _ = onto(u + r * math.cos(a) / size, v + r * math.sin(a) / size, lift); ring.append(bm.verts.new(q))
        mid = bm.verts.new(c)
        for i in range(20): bm.faces.new((mid, ring[i], ring[(i + 1) % 20]))
    def ribbon(a, b, w, lift):
        L = math.hypot(b[0] - a[0], b[1] - a[1]) * size; n = max(2, int(L / 0.004)); du, dv = (b[0] - a[0]) / (L / size), (b[1] - a[1]) / (L / size); pu, pv = -dv, du
        rows = []
        for k in range(n + 1):
            t = k / n; u, v = a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t
            rows.append([bm.verts.new(onto(u + pu * w / size * sgn, v + pv * w / size * sgn, lift)[0]) for sgn in (-1, 1)])
        for r0, r1 in zip(rows, rows[1:]): bm.faces.new((r0[0], r1[0], r1[1], r0[1]))
    if panel:      # the panel behind the line: a rounded square a little larger than the grid
        m_ = panel / size; grid = 8; rows = []
        for j in range(grid + 1):
            rows.append([bm.verts.new(onto(-m_ + (1 + 2 * m_) * i / grid, -m_ + (1 + 2 * m_) * j / grid, raise_ * 0.5)[0]) for i in range(grid + 1)])
        for j in range(grid):
            for i in range(grid): bm.faces.new((rows[j][i], rows[j][i + 1], rows[j + 1][i + 1], rows[j + 1][i]))
        ob = finish(name + '_panel', bm, 'panel', True, 0, 0.006); part(ob, bones); bm = bmesh.new()
    disc(path[0][0], path[0][1], width * start, raise_)
    for a_, b_ in zip(path, path[1:]): ribbon(a_, b_, width, raise_); disc(b_[0], b_[1], width, raise_)
    part(finish(name, bm, material, True), bones)
decal('motif_satchel', satchel, (-0.40, 0.080, 0.790), (0, -1, 0), (0, 0, 1), 0.080, 0.0030, 'panel_line', ['satchel'], 0.0022, panel=0.012)
decal('motif_rucksack', rucksack, (0.050, 0.30, 0.960), (-1, 0, 0), (0, 0, 1), 0.100, 0.0042, 'panel_line', ['spine', 'pelvis', 'chest'])
part(tube('bedroll', (-0.165, 0.205, 1.305), (0.165, 0.205, 1.305), 0.058, 'bedroll', 24), ['chest', 'spine'])
for bx in (-0.09, 0.09): part(loft('bedroll_strap', [(1.244, bx, 0.205, 0.014, 0.065), (1.366, bx, 0.205, 0.014, 0.065)], 'strap', 16, 0), ['chest', 'spine'])
B['canteen'] = (V(0.185, 0.215, 1.125), V(0.185, 0.215, 0.94), 'spine')
part(ellipsoid('canteen', (0.185, 0.215, 1.02), (0.052, 0.034, 0.073), 'leather', 24, 14), ['canteen'])
part(tube('canteen_cap', (0.185, 0.215, 1.092), (0.185, 0.215, 1.120), 0.015, 'leather', 16), ['canteen'])

# -- construction details: folded lapels, topstitching, shoulder seams and real boot laces
def cord(name, points, radius, material, bones, seg=6):
    for a, b in zip(points, points[1:]): part(tube(name, a, b, radius, material, seg), bones)
for sx in (-1, 1):
    spiral = []
    for i in range(31):
        t = i/30; angle = t*math.pi*5.4; radius = 0.004+0.050*t
        spiral.append(V(sx*0.166, 0.205+radius*math.cos(angle), 1.305+radius*math.sin(angle)))
    cord('bedroll_spiral', spiral, 0.0014, 'trousers', ['chest', 'spine'], 4)
def stitches(name, a, b, count, bones, material='stitching', radius=0.0011, on_coat=False):
    a, b = Vector(a), Vector(b)
    for i in range(count):
        p = a.lerp(b, (i + 0.18) / count); q = a.lerp(b, (i + 0.68) / count); t = tube(name, p, q, radius, material, 5)
        if on_coat: part(t, bones + HEM_BONES, coat_weights(t, bones))
        else: part(t, bones)
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
    stitches('pocket_stitch.'+sd, (sx*0.080, -0.187, 0.827), (sx*0.170, -0.187, 0.827), 9, ['pelvis'], on_coat=True)
    for i in range(5):
        z = 0.155 + i*0.047
        for side in (-1, 1):
            part(ellipsoid('boot_eyelet.'+sd, (sx*0.10+side*0.025, -0.073, z), (0.004, 0.0025, 0.004), 'brass', 8, 6), ['shin.'+sd])
        if i < 4:
            cord('boot_lacing.'+sd, [(sx*0.10-0.022, -0.078, z), (sx*0.10+0.022, -0.078, z+0.047)], 0.0020, 'boots_cuff', ['shin.'+sd])
            cord('boot_lacing.'+sd, [(sx*0.10+0.022, -0.079, z), (sx*0.10-0.022, -0.079, z+0.047)], 0.0020, 'boots_cuff', ['shin.'+sd])
    welt = [V(sx*0.10+0.069*math.cos(a), -0.052+0.134*math.sin(a), 0.039) for a in [2*math.pi*i/24 for i in range(25)]]
    cord('boot_welt.'+sd, welt, 0.0018, 'boots_cuff', ['foot.'+sd], 4)
# Project the edge trim onto the finished cloth, including subdivision and drape.
coat_bvh = SurfaceBVH.FromObject(coat_body, bpy.context.evaluated_depsgraph_get())
def cloth_point(z, angle, lift=0.0028):
    origin = V(0,0.010,z); direction=V(math.sin(angle),-math.cos(angle),0)
    hit, normal, _, _ = coat_bvh.ray_cast(origin+direction*0.5,-direction)
    return hit + normal*lift if hit is not None else coat_point(z,angle,lift)
for sd,sx in (('L',1),('R',-1)):
    points=[]
    for i in range(49):
        z=0.583+(1.44-0.583)*i/48; gap=coat_ring(z)[4]
        points.append(cloth_point(z,sx*(gap+0.024)))
    for a,b in zip(points,points[1:]):
        ob=tube('coat_edge.'+sd,a,b,0.0034,'coat_dark',8)
        part(ob,COAT_BASE+HEM_BONES,coat_weights(ob,COAT_BASE))
for i in range(72):
    a0,a1=0.26+i*(2*math.pi-0.52)/72,0.26+(i+1)*(2*math.pi-0.52)/72
    ob=tube('hem_binding',cloth_point(0.589,a0),cloth_point(0.589,a1),0.003,'coat_dark',8)
    part(ob,COAT_BASE+HEM_BONES,coat_weights(ob,COAT_BASE))
cord('back_seam', [coat_point(z, math.pi, 0.004) for z in (0.60, 0.75, 0.90, 1.05, 1.20, 1.35, 1.43)], 0.0018, 'coat_dark', TORSO)
stitches('satchel_stitch', (-0.371, -0.085, 0.775), (-0.371, -0.085, 0.945), 15, ['pelvis', 'spine'])
stitches('satchel_stitch', (-0.371, 0.165, 0.775), (-0.371, 0.165, 0.945), 15, ['pelvis', 'spine'])

for sd,sx in (('L',1),('R',-1)):
    pivot=V(sx*0.10,0,0.09)
    xform=Matrix.Translation(pivot)@Matrix.Rotation(sx*math.radians(7),4,'Z')@Matrix.Translation(-pivot)
    for ob,bones,_ in parts:
        if ob.name.startswith(('boot_toe.'+sd,'boot_sole.'+sd,'boot_heel.'+sd,'boot_lace.'+sd,'boot_welt.'+sd)):
            ob.data.transform(xform)

# Relax the bind pose without changing the animation's angular authoring. The
# satchel-side arm comes forward slightly to keep the hand clear of the bag.
for s, sx in (('L', 1), ('R', -1)):
    pivot = V(0.215*sx, 0, 1.42)
    if s == 'L': rotation = Matrix.Rotation(math.radians(-8), 4, 'Y')
    else: rotation = Matrix.Rotation(math.radians(-9), 4, 'X') @ Matrix.Rotation(math.radians(3), 4, 'Y')
    xform = Matrix.Translation(pivot) @ rotation @ Matrix.Translation(-pivot)
    arm_bones = {'upper_arm.'+s, 'forearm.'+s, 'hand.'+s}
    for ob, bones, _ in parts:
        names = {b[0] if isinstance(b, tuple) else b for b in bones}
        if names & arm_bones: ob.data.transform(xform)
    for name in arm_bones:
        h, t, parent = B[name]; B[name] = (xform @ h, xform @ t, parent)

# ---- weights: by distance to the bones a part may follow, blended near the joints (at most four per vertex)
def seg_dist(p, a, b):
    ab = b - a; t = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.length_squared, 1e-9))); return (p - (a + ab * t)).length
for ob, bones, custom in parts:
    mult = {(b[0] if isinstance(b, tuple) else b): (b[1] if isinstance(b, tuple) else 1.0) for b in bones}; allowed = [b for b in mult if b in B]
    groups = {b: ob.vertex_groups.new(name=b) for b in allowed}
    for v in ob.data.vertices:
        if custom: ws = [(b, w) for b, w in custom(v.index).items() if w > 0]
        elif len(allowed) == 1: ws = [(allowed[0], 1.0)]
        else:
            raw = [(b, mult[b] * math.exp(-(seg_dist(v.co, B[b][0], B[b][1]) / SIGMA) ** 2)) for b in allowed]; raw.sort(key=lambda r: -r[1]); raw = raw[:4]; tot = sum(w for _, w in raw)
            ws = [(b, w / tot) for b, w in raw] if tot > 1e-6 else [(min(allowed, key=lambda b: seg_dist(v.co, B[b][0], B[b][1])), 1.0)]
        for b, w in ws:
            if w > 0.01: groups[b].add([v.index], w, 'REPLACE')
head_chart = head_surface.data.attributes.new('_HEAD_CHART', 'INT', 'POINT')
for value in head_chart.data: value.value = 1
bpy.ops.object.select_all(action='DESELECT')
for ob, _, _ in parts: ob.select_set(True)
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
        hv = node('ShaderNodeValue', (-1200, -1500)); hv.outputs[0].default_value = 0.0; height = hv.outputs[0]      # the bump height: the same noises add up here (amount x factor)
        def add_height(fac_out, amount):
            nonlocal height
            if not amount: return
            mu = node('ShaderNodeMath', (-400, -1500), operation='MULTIPLY_ADD'); mu.inputs[1].default_value = amount; nt.links.new(fac_out, mu.inputs[0]); nt.links.new(height, mu.inputs[2]); height = mu.outputs[0]
        def noise_mul(scale, amt, stretch=(1, 1, 1), seed=0.0, bump=0.0, detail=4.0):
            nonlocal out
            mp = node('ShaderNodeMapping', (-1200, -100 - 200 * seed)); mp.inputs['Scale'].default_value = (scale * stretch[0], scale * stretch[1], scale * stretch[2]); mp.inputs['Location'].default_value = (seed * 7.1, seed * 3.3, seed * 5.9)
            nt.links.new(tc.outputs['Object'], mp.inputs['Vector'])
            nz = node('ShaderNodeTexNoise', (-1000, -100 - 200 * seed)); nz.inputs['Scale'].default_value = 1.0; nz.inputs['Detail'].default_value = detail; nz.inputs['Roughness'].default_value = 0.5; nt.links.new(mp.outputs[0], nz.inputs['Vector'])
            rp = node('ShaderNodeValToRGB', (-800, -100 - 200 * seed)); rp.color_ramp.elements[0].color = (1 - amt, 1 - amt, 1 - amt, 1); rp.color_ramp.elements[1].color = (1, 1, 1, 1); nt.links.new(nz.outputs['Fac'], rp.inputs['Fac'])
            mx = node('ShaderNodeMixRGB', (-600, 300 - 200 * seed), blend_type='MULTIPLY'); mx.inputs['Fac'].default_value = 1.0; nt.links.new(out, mx.inputs['Color1']); nt.links.new(rp.outputs['Color'], mx.inputs['Color2']); out = mx.outputs['Color']
            add_height(nz.outputs['Fac'], bump)
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
        def grad(z0, z1, colour, strength):      # a soft, clean shift toward colour from object height z0 (full) to z1 (none): painted light and shade
            sep = node('ShaderNodeSeparateXYZ', (-1000, 900)); nt.links.new(tc.outputs['Object'], sep.inputs[0])
            mr = node('ShaderNodeMapRange', (-800, 900)); mr.inputs['From Min'].default_value = z0; mr.inputs['From Max'].default_value = z1; mr.inputs['To Min'].default_value = 1.0; mr.inputs['To Max'].default_value = 0.0
            mr.interpolation_type = 'SMOOTHSTEP'; nt.links.new(sep.outputs['Z'], mr.inputs['Value']); blend_to(colour, mr.outputs[0], strength)
        def spot(centre, radius, colour, strength, squash=(1, 1, 1)):      # a soft round patch of colour in object space (a blush, a nose tip)
            nonlocal out
            if name in ('skin', 'lips', 'hair') and centre[2] > 1.45: centre = tuple(head_scaled(centre)); radius *= HEAD_SCALE; squash = squash      # (the face's paint follows the head's scale)
            vm = node('ShaderNodeVectorMath', (-1000, -700), operation='SUBTRACT'); vm.inputs[1].default_value = centre; nt.links.new(tc.outputs['Object'], vm.inputs[0])
            sc = node('ShaderNodeVectorMath', (-900, -700), operation='MULTIPLY'); sc.inputs[1].default_value = squash; nt.links.new(vm.outputs[0], sc.inputs[0])
            ln = node('ShaderNodeVectorMath', (-800, -700), operation='LENGTH'); nt.links.new(sc.outputs[0], ln.inputs[0])
            mr = node('ShaderNodeMapRange', (-600, -700)); mr.inputs['From Min'].default_value = 0.0; mr.inputs['From Max'].default_value = radius; mr.inputs['To Min'].default_value = 1.0; mr.inputs['To Max'].default_value = 0.0; mr.clamp = True; nt.links.new(ln.outputs['Value'], mr.inputs['Value'])
            blend_to(colour, mr.outputs[0], strength)
        def weave(scale, amount, bump=0.0):
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
            add_height(cross.outputs[0], bump)
        def rib(scale, amount, bump=0.0):      # clean horizontal rows: one soft sine of object height (no noise), a darker trough between rows
            nonlocal out
            sep = node('ShaderNodeSeparateXYZ', (-1100, -1300)); nt.links.new(tc.outputs['Object'], sep.inputs[0])
            fq = node('ShaderNodeMath', (-900, -1300), operation='MULTIPLY'); fq.inputs[1].default_value = scale; nt.links.new(sep.outputs['Z'], fq.inputs[0])
            sn = node('ShaderNodeMath', (-750, -1300), operation='SINE'); nt.links.new(fq.outputs[0], sn.inputs[0])
            hf = node('ShaderNodeMath', (-600, -1300), operation='MULTIPLY_ADD'); hf.inputs[1].default_value = 0.5; hf.inputs[2].default_value = 0.5; nt.links.new(sn.outputs[0], hf.inputs[0])
            ramp = node('ShaderNodeValToRGB', (-300, -1300)); ramp.color_ramp.elements[0].color = (1-amount, 1-amount, 1-amount, 1); nt.links.new(hf.outputs[0], ramp.inputs[0])
            mix = node('ShaderNodeMixRGB', (-100, 200), blend_type='MULTIPLY'); mix.inputs['Fac'].default_value = 1.0; nt.links.new(out, mix.inputs['Color1']); nt.links.new(ramp.outputs[0], mix.inputs['Color2']); out = mix.outputs[0]
            add_height(hf.outputs[0], bump)
        if STYLE == 'witness' and name not in ('skin',):
            # The island's look: large clean colour fields, light painted in (sunlit above, cooler in the folds below), almost no grain.
            c = base; cool = tuple(max(0.0, c[k] * f) for k, f in enumerate((0.62, 0.70, 0.86))); warm = tuple(min(1.0, c[k] * f) for k, f in enumerate((1.22, 1.16, 1.04)))
            noise_mul(2.4, 0.07, (1, 1, 1), 0, 0.0, 1)      # a painter's unevenness, very broad
            if name in ('coat', 'coat_dark'): grad(0.55, 1.05, cool, 0.42); grad(1.48, 1.28, warm, 0.28); noise_mul(9, 0.0, (1, 1, 3), 1, 0.025, 1)
            elif name in ('trousers', 'vest', 'shirt', 'bedroll'): grad(0.45, 0.95, cool, 0.35); noise_mul(9, 0.0, (1, 1, 1), 1, 0.2, 1)
            elif name in ('hat', 'band'): grad(1.66, 1.86, cool, 0.26); grad(2.00, 1.80, warm, 0.20)      # felt: two broad smooth ramps, no relief noise
            elif name in ('boots', 'boots_cuff', 'leather', 'strap', 'sole'): grad(0.0, 0.25, cool, 0.30); noise_mul(14, 0.015, (1, 1, 1), 1, 0.045, 2)
            elif name in ('scarf', 'scarf_dark'): grad(1.20, 1.50, cool, 0.30); rib(430, 0.11, 0.06)      # a broad, clean knit rib (about 1.5 cm per row): a pattern you read at a distance, not noise
            elif name == 'hair': grad(1.58, 1.72, cool, 0.35); noise_mul(8, 0.025, (1, 1, 2), 2, 0.015, 1)      # strand direction, soft
        elif name in ('coat', 'coat_dark'): noise_mul(45, 0.09, (1, 1, 4), 0, 0.0, 2); noise_mul(7, 0.28, (1, 1, 1.6), 1, 1.0); weave(260, 0.07); noise_mul(22, 0.0, (1, 1, 2.5), 3, 0.2, 2); blend_to((0.02, 0.05, 0.04), z_mask(0.45, 0.85), 0.36)
        elif name in ('trousers', 'vest', 'bedroll', 'shirt'): noise_mul(40, 0.08, (4, 1, 1), 0, 0.0, 2); noise_mul(7, 0.24, (1, 1, 1), 1, 1.0); weave(240, 0.06); noise_mul(20, 0.0, (1, 1, 1), 3, 0.15, 2); blend_to((0.05, 0.035, 0.02), z_mask(0.50, 1.10), 0.35)
        elif name in ('hat', 'band'): noise_mul(40, 0.08, (1, 1, 1), 0, 0.0, 2); noise_mul(6, 0.24, (1, 1, 1), 1, 0.8); weave(220, 0.05); noise_mul(24, 0.0, (1, 1, 1), 3, 0.12, 2)
        elif name in ('boots', 'boots_cuff', 'leather', 'strap', 'sole'): noise_mul(60, 0.06, (1, 1, 1), 0, 0.0, 2); noise_mul(6, 0.28, (1, 1, 1), 1, 0.7); noise_mul(36, 0.0, (1, 1, 1), 3, 0.3, 2); blend_to((0.04, 0.03, 0.02), z_mask(0.0, 0.40), 0.50)
        elif name in ('scarf', 'scarf_dark'): noise_mul(45, 0.09, (1, 4, 1), 0, 0.0, 2); noise_mul(8, 0.22, (1, 1, 1), 1, 0.9); weave(180, 0.10); noise_mul(40, 0.0, (1, 4, 1), 3, 0.3, 2)
        elif name == 'skin' and STYLE == 'witness': pass      # the continuous head chart is painted analytically below
        elif name == 'skin':
            noise_mul(30, 0.03 if STYLE != 'witness' else 0.012, (1, 1, 1), 0, 0.0, 2)
            for sx in (1, -1): spot((0.044*sx, -0.094, 1.630), 0.028, (0.66, 0.26, 0.19), 0.16)      # cheeks: warm, soft
            spot((0, -0.127, 1.630), 0.013, (0.66, 0.26, 0.19), 0.20)      # the nose tip
            for sx in (1, -1): spot((0.093*sx, 0.012, 1.664), 0.022, (0.62, 0.22, 0.16), 0.18)      # the ears (light through them)
            for sx in (1, -1): spot((0.0365*sx, -0.088, 1.683), 0.020, (0.30, 0.16, 0.14), 0.16, (1.2, 1, 0.6))      # the lids: a little darker and warmer than the cheek
            spot((0, -0.075, 1.575), 0.070, (0.075, 0.045, 0.028), 0.04, (1.25, 0.65, 0.80))      # the jaw's shadow
            spot((0, -0.111, 1.611), 0.010, (0.065, 0.038, 0.024), 0.03, (1, 0.25, 2.4))      # the philtrum
            mw_ = FACE['mouth_w'] * 0.5
            for i in range(25):
                u = -1 + 2 * i / 24; x = u * mw_ * 0.94; z = FACE['mouth_z'] + FACE['smile'] * u * u
                # A warm lip field and one confident, softly tapered smile line.
                spot((x, -0.098, z - 0.0014), 0.0042, (0.49, 0.22, 0.17), 0.18*(1-u**4), (1, 0.12, 1.4))
                spot((x, -0.098, z), 0.0020, (0.20, 0.065, 0.045), 0.30*(1-0.8*u**4), (1, 0.06, 1.7))
            if STYLE == 'witness' and '--freckles' in args:      # (off: at the atlas's density a freckle is a texel or two and reads as dirt)      # freckles over the nose and the cheeks: someone who lives outdoors
                import random; rnd = random.Random(7)
                for _ in range(34):
                    u = rnd.uniform(-1, 1); x = 0.052 * u; z = 1.640 + 0.008 * (1 - u * u) + rnd.uniform(-0.007, 0.007)
                    if abs(x) < 0.006 and z > 1.646: continue
                    spot((x, -0.118 + 0.040 * abs(u) ** 1.6, z), rnd.uniform(0.0012, 0.0022), (0.36, 0.17, 0.10), rnd.uniform(0.35, 0.6))
            for sx in (1, -1): spot((0.084*sx, -0.025, 1.630), 0.042, (0.070, 0.042, 0.026), 0.05, (0.75, 1.0, 1.4))
            if STYLE != 'witness': noise_mul(70, 0.035, (1, 1, 1), 2, 0.0, 2)
            noise_mul(16, 0.0, (1, 1, 1), 3, 0.0, 2)
        elif name == 'hair': noise_mul(60, 0.18, (1, 1, 5), 0, 0.3, 3); noise_mul(12, 0.15, (1, 1, 1), 1, 0.5); noise_mul(90, 0.08, (1, 1, 8), 2, 0.0, 2); noise_mul(30, 0.0, (1, 1, 6), 3, 0.4, 2)
        else: noise_mul(30, 0.05, (1, 1, 1), 0, 0.1, 2)
        nt.links.new(out, bsdf.inputs['Base Color'])
        # surface relief (baked to the tangent-space normal map) and roughness / metalness (baked to the material map); bump distances in metres
        bp = node('ShaderNodeBump', (-100, -1500)); bp.inputs['Strength'].default_value = BUMP.get(name, 0.5); bp.inputs['Distance'].default_value = 0.0100; nt.links.new(height, bp.inputs['Height']); nt.links.new(bp.outputs['Normal'], bsdf.inputs['Normal'])
        rough, metal = SURFACE.get(name, (0.85, 0.0))
        rv = node('ShaderNodeMath', (-300, -1800), operation='MULTIPLY_ADD'); rv.inputs[1].default_value = 0.05; rv.inputs[2].default_value = rough - 0.025; nt.links.new(height if name not in ('eye', 'white', 'iris') else hv.outputs[0], rv.inputs[0])
        cl = node('ShaderNodeClamp', (-100, -1800)); nt.links.new(rv.outputs[0], cl.inputs['Value']); nt.links.new(cl.outputs[0], bsdf.inputs['Roughness'])
        bsdf.inputs['Metallic'].default_value = metal

# roughness and metalness per material (the material map), and how strong each one's relief is (the normal map)
SURFACE = dict(hand_skin=(0.64,0.0),lid_skin=(0.64,0.0),glove_leather=(0.55,0.0),skin=(0.62, 0.0), nail=(0.45, 0.0), lips=(0.50, 0.0), skin_dark=(0.65, 0.0), mouth=(0.45, 0.0), mouth_dark=(0.5, 0.0), hair=(0.55, 0.0), eye=(0.12, 0.0), white=(0.18, 0.0), iris=(0.14, 0.0), iris_ring=(0.2, 0.0), lash=(0.6, 0.0), panel=(0.55, 0.0), panel_line=(0.6, 0.0),
               coat=(0.92, 0.0), coat_dark=(0.90, 0.0), vest=(0.88, 0.0), shirt=(0.80, 0.0), trousers=(0.90, 0.0), scarf=(0.95, 0.0), scarf_dark=(0.95, 0.0), hat=(0.93, 0.0), band=(0.65, 0.0), feather=(0.55, 0.0), bedroll=(0.92, 0.0), thread=(0.8, 0.0), stitching=(0.8, 0.0),
               boots=(0.42, 0.0), boots_cuff=(0.50, 0.0), sole=(0.70, 0.0), leather=(0.46, 0.0), strap=(0.50, 0.0), brass=(0.38, 0.65), metal=(0.38, 1.0))
BUMP = dict(white=0.0, iris=0.0, iris_ring=0.0, eye=0.0, lash=0.0, panel=0.1, panel_line=0.3, coat=0.55, coat_dark=0.55, vest=0.5, shirt=0.4, trousers=0.5, scarf=0.8, scarf_dark=0.8, hat=0.45, band=0.3, boots=0.5, boots_cuff=0.5, leather=0.6, strap=0.5, sole=0.4, skin=0.05, hair=0.5, bedroll=0.5, brass=0.1, metal=0.15, nail=0.05, lips=0.05, skin_dark=0.05)
def bake_texture(body, size, outdir):
    if not FLAT_MATERIALS: paint_materials()
    bpy.ops.object.select_all(action='DESELECT'); body.select_set(True); bpy.context.view_layer.objects.active = body
    detail_names=('white','iris','iris_ring','eye','lips','mouth_dark','lash','hair','hand_skin','lid_skin','glove_leather','band','feather','thread','stitching','panel_line','brass')
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(70),island_margin=0.0065);bpy.ops.object.mode_set(mode='OBJECT')
    # One cylindrical chart for the continuous head, with its seam at the nape. Smart projection
    # split the sculpt's decimated slivers into subpixel islands (black specks, broken painted lips).
    chart=body.data.attributes['_HEAD_CHART'];uv=body.data.uv_layers.active.data;head_faces=[]
    for poly in body.data.polygons:
        if not all(chart.data[vi].value for vi in poly.vertices): continue
        head_faces.append(poly.index)
        coords=[]
        for vi in poly.vertices:
            p=body.data.vertices[vi].co
            coords.append([math.atan2(p.x,-(p.y-0.012))*0.10,p.z])
        if max(c[0] for c in coords)-min(c[0] for c in coords)>math.pi*0.10:
            for c in coords:
                if c[0]<0:c[0]+=2*math.pi*0.10
        for li,c in zip(poly.loop_indices,coords):uv[li].uv=c
    body.data.attributes.remove(chart)
    # Spend the texture density on the face; hands, eyes and hair use stable colour swatches.
    names = [m_.name for m_ in body.data.materials]
    if '--even-texels' not in args:
        FOCUS = {'skin', 'lips', 'mouth', 'mouth_dark', 'lash', 'white', 'iris', 'iris_ring', 'eye', 'hair', 'nail'}
        uvl = body.data.uv_layers.active.data; k = 0
        for poly in body.data.polygons:
            if names[poly.material_index] in FOCUS:
                for li in poly.loop_indices: uvl[li].uv = uvl[li].uv * 2.2
                k += 1
        print('REPORT face at 2.2x texel density: %d faces' % k)
    scene.tool_settings.use_uv_select_sync = True
    for poly in body.data.polygons:poly.select=names[poly.material_index] not in detail_names
    bpy.ops.object.mode_set(mode='EDIT');bpy.ops.uv.pack_islands(rotate=True,scale=True,margin=0.0065);bpy.ops.object.mode_set(mode='OBJECT')
    for poly in body.data.polygons:poly.select=True
    # Small graphic features each sample one deliberately painted, mip-safe colour swatch. They
    # still use the shared atlas and material; their colour never depends on a subpixel UV triangle.
    names=[m.name for m in body.data.materials];uv=body.data.uv_layers.active.data
    for poly in body.data.polygons:
        name=names[poly.material_index]
        for li in poly.loop_indices:
            if name in detail_names:uv[li].uv=((detail_names.index(name)+0.5)/len(detail_names),0.020)
            else:uv[li].uv.y=0.05+0.95*uv[li].uv.y
    scene.render.engine = 'CYCLES'; scene.cycles.device = 'CPU'; scene.cycles.samples = 24; scene.cycles.use_denoising = False
    if scene.world is None: scene.world = bpy.data.worlds.new('w')
    scene.world.light_settings.distance = 0.09
    imgs = {}
    for kind in ('DIFFUSE', 'AO', 'NORMAL', 'ROUGHNESS', 'METAL'):
        im = bpy.data.images.new('bake_' + kind, size, size, alpha=False, float_buffer=True); imgs[kind] = im
        if kind != 'DIFFUSE': im.colorspace_settings.name = 'Non-Color'
        for m in MATS.values():
            n = m.node_tree.nodes.new('ShaderNodeTexImage'); n.image = im; n.select = True; m.node_tree.nodes.active = n
        if kind == 'DIFFUSE': bpy.ops.object.bake(type='DIFFUSE', pass_filter={'COLOR'}, margin=10, margin_type='EXTEND', use_clear=True)
        elif kind == 'AO': scene.cycles.samples = 160; bpy.ops.object.bake(type='AO', margin=10, margin_type='EXTEND', use_clear=True); scene.cycles.samples = 24      # occlusion is the one Monte Carlo pass: at 24 samples its noise was baked into every texel as a speckle
        elif kind == 'NORMAL': bpy.ops.object.bake(type='NORMAL', normal_space='TANGENT', margin=10, margin_type='EXTEND', use_clear=True)
        elif kind == 'ROUGHNESS': bpy.ops.object.bake(type='ROUGHNESS', margin=10, margin_type='EXTEND', use_clear=True)
        else:      # metalness: no bake pass of its own; emit it as the colour of an emission pass
            for m in MATS.values():
                bs = m.node_tree.nodes['Principled BSDF']; v = bs.inputs['Metallic'].default_value; bs.inputs['Emission Color'].default_value = (v, v, v, 1); bs.inputs['Emission Strength'].default_value = 1.0
            bpy.ops.object.bake(type='EMIT', margin=10, margin_type='EXTEND', use_clear=True)
            for m in MATS.values(): m.node_tree.nodes['Principled BSDF'].inputs['Emission Strength'].default_value = 0.0
        for m in MATS.values():
            for n in [n for n in m.node_tree.nodes if n.type == 'TEX_IMAGE']: m.node_tree.nodes.remove(n)
    import numpy as np
    a = np.empty(size * size * 4, dtype=np.float32); b = np.empty(size * size * 4, dtype=np.float32); imgs['DIFFUSE'].pixels.foreach_get(a); imgs['AO'].pixels.foreach_get(b)
    # A collapsed UV sliver can miss every bake sample and export as a black triangle.
    # Extend valid neighbouring samples into those holes before any filtering/compression.
    raw_colour = a.reshape(size,size,4); valid = raw_colour[:,:,:3].max(axis=2)>1e-6
    maps = [raw_colour,b.reshape(size,size,4)]
    other_maps = {}
    for kind in ('NORMAL','ROUGHNESS','METAL'):
        pixels=np.empty(size*size*4,dtype=np.float32);imgs[kind].pixels.foreach_get(pixels)
        other_maps[kind]=pixels.reshape(size,size,4);maps.append(other_maps[kind])
    for k,name in enumerate(detail_names):
        x0=int(size*k/len(detail_names));x1=int(size*(k+1)/len(detail_names));y1=int(size*0.04)
        raw_colour[:y1,x0:x1,:3]=srgb(COLOURS[name],name)[:3];raw_colour[:y1,x0:x1,3]=1
        maps[1][:y1,x0:x1,:]=1
        if '--eye-ao-mutant' in args and name in ('white','iris','iris_ring','eye'):
            raw_colour[:y1,x0:x1,:3]*=0.25;maps[1][:y1,x0:x1,:3]=0      # deliberately bake deep socket occlusion into the eye swatches
        other_maps['NORMAL'][:y1,x0:x1,:]=(0.5,0.5,1,1)
        rough,metal=SURFACE.get(name,(0.68,0.0))
        other_maps['ROUGHNESS'][:y1,x0:x1,:]=(rough,rough,rough,1)
        other_maps['METAL'][:y1,x0:x1,:]=(metal,metal,metal,1)
        valid[:y1,x0:x1]=True
    # Paint the head directly on its continuous chart. In concave sockets, several layers of skin
    # can share a ray; a Cycles normal/AO bake there produced muddy eye rings. Keep those maps neutral
    # and let the actual sculpt's normals and the island light describe its planes.
    skin_base=np.array(srgb(COLOURS['skin'],'skin')[:3]);blush=np.array(srgb('#d28f7b','skin')[:3])
    headmask=np.zeros((size,size),bool)
    for pi in head_faces:
        poly=body.data.polygons[pi];loops=list(poly.loop_indices)
        for k in range(1,len(loops)-1):
            lis=[loops[0],loops[k],loops[k+1]]
            tri=np.array([uv[li].uv[:] for li in lis])*size
            p3=np.array([body.data.vertices[body.data.loops[li].vertex_index].co[:] for li in lis])
            x0=max(0,int(tri[:,0].min())-1);x1=min(size,int(tri[:,0].max())+2)
            y0=max(0,int(tri[:,1].min())-1);y1=min(size,int(tri[:,1].max())+2)
            if x0>=x1 or y0>=y1:continue
            xx,yy=np.meshgrid(np.arange(x0,x1)+0.5,np.arange(y0,y1)+0.5)
            aa,bb,cc=tri;den=(bb[1]-cc[1])*(aa[0]-cc[0])+(cc[0]-bb[0])*(aa[1]-cc[1])
            if abs(den)<1e-8:continue
            wa=((bb[1]-cc[1])*(xx-cc[0])+(cc[0]-bb[0])*(yy-cc[1]))/den
            wb=((cc[1]-aa[1])*(xx-cc[0])+(aa[0]-cc[0])*(yy-cc[1]))/den;wc=1-wa-wb
            hit=(wa>=-0.03)&(wb>=-0.03)&(wc>=-0.03)
            if not hit.any():continue
            pos=wa[:,:,None]*p3[0]+wb[:,:,None]*p3[1]+wc[:,:,None]*p3[2]
            pos=(pos-np.array(HEAD_PIVOT))/HEAD_SCALE+np.array(HEAD_PIVOT)
            warm=np.zeros_like(xx)
            for sx in (-1,1):
                warm+=0.20*np.exp(-(((pos-np.array([sx*0.045,-0.085,1.642]))/np.array([0.031,0.044,0.026]))**2).sum(axis=2))
            warm+=0.20*np.exp(-(((pos-np.array([0,-0.123,1.632]))/np.array([0.015,0.025,0.018]))**2).sum(axis=2))
            colour=skin_base+(blush-skin_base)*warm[:,:,None]
            raw_colour[y0:y1,x0:x1,:3][hit]=colour[hit];maps[1][y0:y1,x0:x1,:][hit]=1
            other_maps['NORMAL'][y0:y1,x0:x1,:][hit]=(0.5,0.5,1,1)
            other_maps['ROUGHNESS'][y0:y1,x0:x1,:][hit]=(0.64,0.64,0.64,1)
            other_maps['METAL'][y0:y1,x0:x1,:][hit]=(0,0,0,1)
            valid[y0:y1,x0:x1][hit]=True;headmask[y0:y1,x0:x1][hit]=True
    print('REPORT clean head paint: %d texels' % int(headmask.sum()))
    repaired=0
    for _ in range(12):
        counts=np.zeros((size,size),np.float32)
        sums=[np.zeros_like(q) for q in maps]
        for dy,dx in ((-1,0),(1,0),(0,-1),(0,1)):
            dst=(slice(max(0,dy),size+min(0,dy)),slice(max(0,dx),size+min(0,dx)))
            src=(slice(max(0,-dy),size+min(0,-dy)),slice(max(0,-dx),size+min(0,-dx)))
            weights=valid[src].astype(np.float32); counts[dst]+=weights
            for out_,map_ in zip(sums,maps): out_[dst]+=map_[src]*weights[:,:,None]
        fill=(~valid)&(counts>0);repaired+=int(fill.sum())
        for map_,sum_ in zip(maps,sums): map_[fill]=sum_[fill]/counts[fill,None]
        valid|=fill
    for kind,pixels in other_maps.items(): imgs[kind].pixels.foreach_set(pixels.reshape(-1))
    print('REPORT bake sliver padding: %d texels' % repaired)
    # The eyes are not occluded: a ball in a socket bakes nearly black ambient occlusion, which greys the whites and lets the iris swallow the eye.
    # Their texels (rasterized from their UV triangles) keep full light.
    EYE_MATS = {'white', 'iris', 'iris_ring', 'eye'}
    if '--eye-ao-mutant' not in args:
        uvl = body.data.uv_layers.active.data; eyemask = np.zeros((size, size), bool); names = [m_.name for m_ in body.data.materials]
        for poly in body.data.polygons:
            if names[poly.material_index] not in EYE_MATS: continue
            uv = [uvl[li].uv for li in poly.loop_indices]
            for t in range(1, len(uv) - 1):
                tri = [(uv[0].x * size, uv[0].y * size), (uv[t].x * size, uv[t].y * size), (uv[t + 1].x * size, uv[t + 1].y * size)]
                x0 = max(0, int(min(q[0] for q in tri)) - 2); x1 = min(size - 1, int(max(q[0] for q in tri)) + 2); y0 = max(0, int(min(q[1] for q in tri)) - 2); y1 = min(size - 1, int(max(q[1] for q in tri)) + 2)
                eyemask[y0:y1 + 1, x0:x1 + 1] = True      # (the triangle's box, with the bake margin: eye islands are tiny)
        b4 = b.reshape(size * size, 4); b4[eyemask.reshape(-1), :3] = 1.0
        print('REPORT eyes kept out of the occlusion: %d texels' % int(eyemask.sum()))
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
    # the remaining sampling noise is blurred away (occlusion is smooth by nature): a 5 x 5 box over the image, then the occlusion is applied
    ao_img = np.clip(b[:, 0], 0, 1).reshape(size, size); acc = np.zeros_like(ao_img); cnt = np.zeros_like(ao_img)
    for dy in range(-2, 3):
        for dx in range(-2, 3):
            acc[max(0, dy):size + min(0, dy), max(0, dx):size + min(0, dx)] += ao_img[max(0, -dy):size + min(0, -dy), max(0, -dx):size + min(0, -dx)]; cnt[max(0, dy):size + min(0, dy), max(0, dx):size + min(0, dx)] += 1
    ao_img = acc / cnt; b[:, 0] = ao_img.reshape(-1)
    ao = np.clip(b[:, :1], 0, 1) ** 1.25; ao_mix = 0.20 if STYLE == 'witness' else 0.50      # (painterly: occlusion lighter, so deep creases do not go black)
    lin = np.clip(a[:, :3] * ((1 - ao_mix) + ao_mix * ao), 0, 1); a[:, :3] = np.where(lin <= 0.0031308, lin * 12.92, 1.055 * np.power(lin, 1 / 2.4) - 0.055); a[:, 3] = 1.0      # (the image holds display values: the linear colour is encoded here)
    final = bpy.data.images.new('adventurer_tex', size, size, alpha=False); final.colorspace_settings.name = 'sRGB'; final.pixels.foreach_set(a.reshape(-1))
    os.makedirs(outdir, exist_ok=True); path = os.path.join(outdir, 'adventurer_tex.png'); final.filepath_raw = path; final.file_format = 'PNG'; final.save()      # lossless: JPEG blocks stepped the smooth gradients (the hat crown)
    img = bpy.data.images.load(path); img.name = 'adventurer_tex_file'; img.pack()
    # the normal map (tangent space, +Y up as glTF wants) and the material map, both at half the atlas size (their detail is low frequency next to the colour's)
    def shrink(px4, factor):      # box filter: size x size x 4 -> (size/f) x (size/f) x 4
        q = px4.reshape(size // factor, factor, size // factor, factor, 4); return q.mean(axis=(1, 3))
    n4 = np.empty(size * size * 4, dtype=np.float32); imgs['NORMAL'].pixels.foreach_get(n4); n4 = n4.reshape(size, size, 4)
    n4[n4[:, :, :3].max(axis=2) < 0.01] = (0.5, 0.5, 1.0, 1.0)      # unbaked texels decode to a wrong normal and light as black specks: make them flat
    nm = shrink(n4, 2); v = nm[:, :, :3] * 2 - 1; l = np.maximum(1e-6, np.sqrt((v * v).sum(axis=2, keepdims=True))); nm[:, :, :3] = (v / l) * 0.5 + 0.5; nm[:, :, 3] = 1
    flat = np.abs(nm[:, :, :2] - 0.5).mean(); print('REPORT normal map mean tilt %.4f (0 = flat)' % flat)
    # Test authored cloth interiors, not the unused pixels between UV islands: a global mean depends on atlas occupancy.
    ci = np.array(coat_indices, dtype=int); coat_n = nm[ci // size // 2, (ci % size) // 2, :2]
    cloth_tilt = float(np.abs(coat_n - 0.5).mean())
    print('REPORT cloth normal tilt %.6f from %d UV interiors (map mean %.4f)' % (cloth_tilt, len(ci), flat))
    if cloth_tilt < (0.0003 if STYLE == 'witness' else 0.001): raise ValueError('the cloth normal map is flat: the bump relief was not baked')
    r4 = np.empty(size * size * 4, dtype=np.float32); imgs['ROUGHNESS'].pixels.foreach_get(r4); r4 = shrink(r4.reshape(size, size, 4), 2)
    m4 = np.empty(size * size * 4, dtype=np.float32); imgs['METAL'].pixels.foreach_get(m4); m4 = shrink(m4.reshape(size, size, 4), 2)
    orm = np.empty_like(r4); orm[:, :, 0] = shrink(np.clip(b[:, :1], 0, 1).reshape(size, size, 1).repeat(4, axis=2), 2)[:, :, 0]; orm[:, :, 1] = np.clip(r4[:, :, 0], 0, 1); orm[:, :, 2] = np.clip(m4[:, :, 0], 0, 1); orm[:, :, 3] = 1
    empty = (orm[:, :, 1] < 0.02) & (orm[:, :, 2] < 0.02)      # texels between the islands (the bake leaves them at zero): neutral cloth, not mirror-smooth
    orm[:, :, 1] = np.where(empty, 0.85, orm[:, :, 1]); orm[:, :, 0] = np.where(empty, 1.0, orm[:, :, 0])
    print('REPORT material map roughness %.2f..%.2f metal fraction %.3f' % (float(orm[:, :, 1].min()), float(orm[:, :, 1].max()), float((orm[:, :, 2] > 0.5).mean())))
    half = size // 2; paths = {}
    for key, arr, fmt in (('nrm', nm, 'PNG'), ('orm', orm, 'PNG')):
        im = bpy.data.images.new('adventurer_' + key, half, half, alpha=False); im.colorspace_settings.name = 'Non-Color'; im.pixels.foreach_set(arr.reshape(-1).astype(np.float32))
        pth = os.path.join(outdir, 'adventurer_%s.png' % key); im.filepath_raw = pth; im.file_format = fmt; scene.render.image_settings.color_mode = 'RGB'; im.save(); paths[key] = pth
        print('REPORT texture %s %d x %d %d bytes' % (key, half, half, os.path.getsize(pth)))
    img_n = bpy.data.images.load(paths['nrm']); img_n.name = 'adventurer_nrm_file'; img_n.colorspace_settings.name = 'Non-Color'; img_n.pack()
    img_m = bpy.data.images.load(paths['orm']); img_m.name = 'adventurer_orm_file'; img_m.colorspace_settings.name = 'Non-Color'; img_m.pack()
    encoded = np.empty(size * size * 4, dtype=np.float32); img.pixels.foreach_get(encoded)
    values = encoded.reshape(-1, 4)[coat_indices, 1]
    print('REPORT coat atlas spread %.6f' % float(np.percentile(values, 90) - np.percentile(values, 10)))
    nm = bpy.data.materials.new('adventurer'); nm.use_nodes = True; bs = nm.node_tree.nodes['Principled BSDF']; bs.inputs['Roughness'].default_value = 1.0; bs.inputs['Metallic'].default_value = 0.0
    tn = nm.node_tree.nodes.new('ShaderNodeTexImage'); tn.image = img; nm.node_tree.links.new(tn.outputs['Color'], bs.inputs['Base Color']); nm.use_nodes = True
    tnn = nm.node_tree.nodes.new('ShaderNodeTexImage'); tnn.image = img_n; nmap = nm.node_tree.nodes.new('ShaderNodeNormalMap'); nm.node_tree.links.new(tnn.outputs['Color'], nmap.inputs['Color']); nm.node_tree.links.new(nmap.outputs['Normal'], bs.inputs['Normal'])
    tnm = nm.node_tree.nodes.new('ShaderNodeTexImage'); tnm.image = img_m; sep = nm.node_tree.nodes.new('ShaderNodeSeparateColor'); nm.node_tree.links.new(tnm.outputs['Color'], sep.inputs['Color'])
    nm.node_tree.links.new(sep.outputs['Green'], bs.inputs['Roughness']); nm.node_tree.links.new(sep.outputs['Blue'], bs.inputs['Metallic'])
    body.data.materials.clear();body.data.materials.append(nm)
    for poly in body.data.polygons:poly.material_index=0
    for k in ('bake_DIFFUSE', 'bake_AO', 'bake_NORMAL', 'bake_ROUGHNESS', 'bake_METAL'): bpy.data.images.remove(bpy.data.images[k])
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
SIDE = {}
for name in B:
    reset(); pb[name].rotation_euler = (0, 0, 0.3); update(); tip = world(B[name][1], name); SIDE[name] = 1.0 if tip.x > B[name][1].x + 1e-4 else -1.0
reset(); update()
# a twist about the bone's own Y (along its length) is a turn about the vertical for the vertical bones
HEEL, TOE = V(0.10, 0.045, 0.012), V(0.10, -0.195, 0.012)      # the sole under the boot, in the rest pose (left foot; the right is mirrored)
def sole(foot, s):
    x = 1 if s == 'L' else -1; return [world(Vector((p.x * x, p.y, p.z)), foot) for p in (HEEL, TOE)]

def ease(u): u = max(0.0, min(1.0, u)); return u * u * (3 - 2 * u)
def bump(u, a, b, c, d):      # 0 before a, eases up to 1 by b, holds, eases back to 0 from c to d
    if u <= a or u >= d: return 0.0
    if u < b: return ease((u - a) / (b - a))
    if u > c: return 1 - ease((u - c) / (d - c))
    return 1.0
HAT_TOUCH = {}      # filled by solve_hat_touch(): the right arm's angles that bring the hand to the hat's brim
def pose_at(kind, phi, A):      # forward-positive angles in degrees
    d = {}
    def rot(b, fwd=0.0, twist=0.0, side=0.0): d[b] = (math.radians(fwd) * FWD[b], math.radians(twist), math.radians(side) * SIDE[b])
    if kind == 'fidget_shift':      # the weight goes onto the left leg: the pelvis slides left, the right knee relaxes and its heel lifts, the torso counters
        u = phi / (2 * math.pi); w = bump(u, 0.04, 0.30, 0.64, 0.96); shift = 0.035 * w; lean = math.degrees(math.atan2(shift, 0.83))
        d['_pelvis_x'] = shift
        rot('pelvis', 0, 2.0 * w, -3.0 * w); rot('spine', 1.0 * w, 2.0 * w, 2.5 * w); rot('chest', 0, 3.0 * w, 1.5 * w); rot('neck', 0, -2.0 * w); rot('head', 1.0 * w, -6.0 * w, -2.0 * w)
        for sd in ('L', 'R'): rot('thigh.' + sd, (6.0 * w if sd == 'R' else 0.5 * w), 0, -lean); rot('shin.' + sd, -(12.0 * w if sd == 'R' else 0)); rot('foot.' + sd, 0)
        rot('upper_arm.L', 2.0 * w, 0, 1.5 * w); rot('forearm.L', 4 + 3.0 * w); rot('upper_arm.R', 3.0 * w, 0, -1.0 * w); rot('forearm.R', 4 + 6.0 * w)
        return d
    if kind == 'fidget_look':       # a look round: left, then right, the shoulders following a little
        u = phi / (2 * math.pi); left = bump(u, 0.04, 0.24, 0.36, 0.50); right = bump(u, 0.50, 0.64, 0.78, 0.96); turn = 32 * left - 26 * right
        rot('head', -3.0 * (left + right), turn * 0.7, 0); rot('neck', 0, turn * 0.3); rot('chest', 0, turn * 0.12); rot('spine', 0, turn * 0.05)
        for sd in ('L', 'R'): rot('forearm.' + sd, 4 + 2.0 * (left + right))
        return d
    if kind == 'fidget_neck':       # the right hand rubs the back of the neck, the head bows and rolls a little
        u = phi / (2 * math.pi); w = bump(u, 0.05, 0.34, 0.66, 0.96); rub = math.sin(6 * math.pi * max(0.0, min(1.0, (u - 0.3) / 0.4))) * w
        fwd, side, elbow, wrist = HAT_TOUCH.get('neck', (60.0, -30.0, 130.0, 0.0))
        rot('upper_arm.R', fwd * w, 0, side * w + 3.0 * rub); rot('forearm.R', 4 + (elbow - 4) * w + 4.0 * rub); rot('hand.R', -20.0 * w)
        rot('head', 10.0 * w, 0, 4.0 * w); rot('neck', 5.0 * w, 2.0 * rub); rot('chest', 2.0 * w, -3.0 * w); rot('upper_arm.L', 2.0 * w); rot('forearm.L', 4 + 3.0 * w)
        return d
    if kind == 'fidget_hat':        # the right hand comes up to the hat's brim, settles it, and drops again
        u = phi / (2 * math.pi); w = bump(u, 0.04, 0.36, 0.62, 0.96); fwd, side, elbow, wrist = HAT_TOUCH.get('angles', (150.0, -20.0, 120.0, 0.0))
        rot('upper_arm.R', fwd * w, 0, side * w); rot('forearm.R', 4 + (elbow - 4) * w); rot('hand.R', wrist * w)
        rot('head', -4.0 * w, 0, 3.0 * w); rot('neck', -2.0 * w); rot('chest', -1.5 * w, 2.0 * w); rot('upper_arm.L', 1.5 * w); rot('forearm.L', 4 + 2.0 * w)
        return d
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
    reset(); standing = kind == 'idle' or kind.startswith('fidget'); posed = pose_at(kind, phi, A)
    for b, v in posed.items():
        if b.startswith('_'): continue
        if not standing and (b == 'pelvis' or b.startswith(('thigh.', 'shin.', 'foot.'))): continue
        pb[b].rotation_euler = v
    if standing:
        update()
        low = min(min(p.z for p in sole('foot.L', 'L')), min(p.z for p in sole('foot.R', 'R')))
        pb['pelvis'].location = (posed.get('_pelvis_x', 0.0), -low, 0); update(); return
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
def solve_hat_touch(target=V(-0.165, -0.085, 1.735), key='angles', what='the brim', clearance=0.115):      # the brim, in front of the right ear (the hat sits 4 cm lower than it was built, and tilted)
    head_centre = V(0, 0.003, 1.690); best = None
    def hand_at(fwd, side, elbow):
        reset(); pb['upper_arm.R'].rotation_euler = (math.radians(fwd) * FWD['upper_arm.R'], 0, math.radians(side) * SIDE['upper_arm.R']); pb['forearm.R'].rotation_euler = (math.radians(elbow) * FWD['forearm.R'], 0, 0); update()
        return world(B['hand.R'][1], 'hand.R')
    for fwd in range(100, 171, 10):
        for side in range(-50, 31, 10):
            for elbow in range(60, 141, 10):
                h = hand_at(fwd, side, elbow); dist = (h - target).length
                if (best is None or dist < best[0]) and (h - head_centre).length > clearance: best = (dist, fwd, side, elbow)
    _, fwd, side, elbow = best
    for step in (5, 2):      # refine
        for dfwd in (-step, 0, step):
            for dside in (-step, 0, step):
                for delbow in (-step, 0, step):
                    h = hand_at(fwd + dfwd, side + dside, elbow + delbow); dist = (h - target).length
                    if dist < best[0] and (h - head_centre).length > clearance: best = (dist, fwd + dfwd, side + dside, elbow + delbow)
        _, fwd, side, elbow = best
    reset(); update()
    HAT_TOUCH[key] = (float(fwd), float(side), float(elbow), -10.0)
    print('REPORT hat touch: hand %.1f cm from %s at fwd %d side %d elbow %d' % (best[0] * 100, what, fwd, side, elbow))
    if best[0] > 0.05: raise ValueError('the hand cannot reach %s (%.3f m)' % (what, best[0]))
solve_hat_touch()
solve_hat_touch(V(-0.06, 0.075, 1.50), 'neck', 'the back of the neck', 0.09)

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
clip('fidget_shift', 'fidget_shift', 4.8, 0.0); clip('fidget_look', 'fidget_look', 4.4, 0.0); clip('fidget_hat', 'fidget_hat', 3.6, 0.0); clip('fidget_neck', 'fidget_neck', 4.0, 0.0)
# every fidget starts and ends in the idle's rest frame (the proxy cross-fades them in and out of the idle loop): compare the first frame's keys
for name in ('fidget_shift', 'fidget_look', 'fidget_hat', 'fidget_neck'):
    act = bpy.data.actions[name]; worst = 0.0
    for fc in act.fcurves:
        k = fc.keyframe_points; worst = max(worst, abs(k[0].co[1] - k[-1].co[1]))
    if worst > 1e-4: raise ValueError('%s does not return to its first frame (%.5f)' % (name, worst))
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
                          export_yup=True, export_apply=False, export_attributes=True, export_materials='EXPORT', export_image_format='AUTO' if TEXTURE else 'NONE', export_cameras=False, export_lights=False, export_optimize_animation_size=False, export_force_sampling=True)
print('REPORT exported', OUT, os.path.getsize(OUT), 'bytes')
run_made_for = RUN_SPEED
with open(OUT + '.txt', 'w', newline='\n') as f:      # the loader's keys: the eyes decide the size (the camera is 1.71 m above the feet), the speeds the clips were made for
    f.write('eye_joint=eye\nspeed=%.2f\nrun_speed=%.2f\nlid_rotate=%.3f\n' % (WALK_SPEED, run_made_for, LID_ROTATE))
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
    shoot('fidget_shift', 'fidget_shift', math.pi, 0, 20); shoot('fidget_look', 'fidget_look', 2 * math.pi * 0.3, 0, 20); shoot('fidget_hat', 'fidget_hat', math.pi, 0, 35)
    shoot('fidget_hat_face', 'fidget_hat', math.pi, 0, 60, 1.6, 0.0, 1.66, 90)
    # what the proxy's life layer does to the face: a blink (the lid bones stretched), and a look to the side (the eye bones turned)
    def face_shot(name, az, pose):
        apply('idle', 0.0, 0); pose(); update(); cam_d.lens = 90; a = math.radians(az); cam.location = Vector((math.sin(a) * 0.78, -math.cos(a) * 0.78, 1.665 + 0.12)); d = Vector((0, 0, 1.665)) - cam.location
        cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler(); scene.render.filepath = os.path.join(PREVIEW, name + '.png'); bpy.ops.render.render(write_still=True)
    def blink():
        for sd in ('L', 'R'): pb['lid.' + sd].rotation_euler = (LID_ROTATE, 0, 0)
    def half_blink():
        for sd in ('L', 'R'): pb['lid.' + sd].rotation_euler = (LID_ROTATE * 0.5, 0, 0)
    def look():
        for sd in ('L', 'R'): pb['eye.' + sd].rotation_euler = (math.radians(-6), math.radians(14), 0)
    face_shot('blink_front', 0, blink); face_shot('blink_threequarter', 38, blink); face_shot('halfblink_front', 0, half_blink); face_shot('look_front', 0, look)
    for sd in ('L', 'R'): pb['lid.' + sd].rotation_euler = (0, 0, 0); pb['eye.' + sd].rotation_euler = (0, 0, 0)
    def brows(lift, tilt):      # lift: metres up; tilt: radians, inner end up when positive (the bone points outward: a roll about the face normal)
        for sd, x in (('L', 1), ('R', -1)):
            pb['brow.' + sd].location = (0, lift, 0); pb['brow.' + sd].rotation_euler = (0, 0, -tilt * SIDE['brow.' + sd] * x)      # (the proxy's own sign comes from a probe at load time; this is only the preview)
    def hem_demo():
        pb['thigh.L'].rotation_euler = (math.radians(25) * FWD['thigh.L'], 0, 0); pb['thigh.R'].rotation_euler = (math.radians(-20) * FWD['thigh.R'], 0, 0); pb['shin.R'].rotation_euler = (math.radians(-30) * FWD['shin.R'], 0, 0)
        pb['hem_front.L'].rotation_euler = (math.radians(24) * FWD['hem_front.L'], 0, 0); pb['hem_back'].rotation_euler = (math.radians(-19) * FWD['hem_back'], 0, 0)
    apply('idle', 0.0, 0); hem_demo(); update(); cam_d.lens = 70; a = math.radians(60); cam.location = Vector((math.sin(a) * 4.0, -math.cos(a) * 4.0, 0.95)); cam.rotation_euler = (Vector((0, 0, 0.85)) - cam.location).to_track_quat('-Z', 'Y').to_euler()
    scene.render.filepath = os.path.join(PREVIEW, 'hem_step.png'); bpy.ops.render.render(write_still=True)
    for nm_ in ('thigh.L', 'thigh.R', 'shin.R', 'hem_front.L', 'hem_back'): pb[nm_].rotation_euler = (0, 0, 0)
    def mouth_open():
        pb['lip_lower'].location = (0, -0.0045, 0)
    face_shot('mouth_open', 0, mouth_open); face_shot('mouth_open_threequarter', 38, mouth_open); pb['lip_lower'].location = (0, 0, 0)
    shoot('fidget_neck', 'fidget_neck', math.pi, 0, 40); shoot('fidget_neck_face', 'fidget_neck', math.pi, 0, 120, 1.6, 0.0, 1.6, 90)
    face_shot('brows_sad', 0, lambda: brows(0.002, 0.30)); face_shot('brows_raised', 0, lambda: brows(0.006, 0.0)); face_shot('brows_frown', 0, lambda: brows(-0.003, -0.22))
    for sd in ('L', 'R'): pb['brow.' + sd].location = (0, 0, 0); pb['brow.' + sd].rotation_euler = (0, 0, 0)
    print('REPORT previews in', PREVIEW)
