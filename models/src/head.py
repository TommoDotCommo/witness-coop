"""The adventurer's head, sculpted as signed distance fields (sdf.py): the skull, the face's planes, real eyeballs in
sockets under thick lids, the nose, the mouth, the ears and the neck. The measurements come from character_spec.py.

Coordinates: metres, z up, the character faces -y, its left is +x.
"""
import numpy as np
import sdf
from character_spec import FACE as F

def eye_centre(side):
    """The eyeball's centre (side +1 left, -1 right)."""
    return np.array([side * F['eye_x'], F['eye_y'], F['eye_z']])

def aperture(side, p):
    """How far a point is outside the lid opening, seen from the front, as an elliptical measure (<1 inside).
    The opening is an almond: a higher, rounder upper arc, a flatter lower arc, the outer corner lifted a little."""
    c = eye_centre(side); dx = (p[:, 0] - c[0]) * side; dz = p[:, 2] - c[2] - F['corner_lift'] * np.clip(dx / F['aperture_w'], -1, 1)
    up = dz > 0
    hz = np.where(up, F['aperture_up'], F['aperture_down'])
    skew = 1 + F['aperture_skew'] * np.clip(dx / F['aperture_w'], -1, 1)      # the opening is taller toward the inner corner
    return np.sqrt((dx / F['aperture_w']) ** 2 + (dz / (hz * skew)) ** 2)

def lid_shell(side, upper_only=None):
    """The lids: a shell round the eyeball with the almond opening cut out. upper_only: True the upper lid (the part
    that blinks), False the rest (the lower lid and the lid fold), None both."""
    c = eye_centre(side); r = F['eye_r']
    outer = sdf.sphere(c, r + F['lid_thick']); inner = sdf.sphere(c, r + F['lid_gap'])
    def f(p):
        d = np.maximum(outer(p), -inner(p))
        window = (1.0 - aperture(side, p)) * F['aperture_w'] * 0.6      # positive inside the opening: removed
        d = np.maximum(d, window)
        d = np.maximum(d, (p[:, 1] - c[1] - 0.35 * r))                     # the front of the ball and round its sides
        if upper_only is not None:
            lid_line = p[:, 2] - c[2]                                       # above the centre: the moving lid
            d = np.maximum(d, -lid_line if upper_only else lid_line)
        return d
    return f

def skull_and_face():
    s = sdf
    m = lambda f: s.mirror_x(f)
    base = s.ellipsoid((0, 0.010, F['crown_z'] - 0.096), (0.097, 0.105, 0.096))            # the cranium
    strokes = [
        (s.ellipsoid((0, -0.030, 1.655), (0.077, 0.070, 0.080)), 0.032, 'add'),             # the face's mass: widest at the cheekbones
        (s.ellipsoid((0, -0.040, 1.600), (0.058, 0.060, 0.050)), 0.030, 'add'),             # tapering to the jaw
        (m(s.ellipsoid((0.040, -0.072, 1.631), (0.026, 0.020, 0.020))), 0.025, 'add'),       # round cheeks under the eyes
        (s.ellipsoid((0, -0.052, F['chin_z'] + 0.030), (0.039, 0.044, 0.031)), 0.028, 'add'),  # jaw and chin
        (m(s.ellipsoid((0.050, 0.004, 1.618), (0.015, 0.028, 0.022))), 0.026, 'add'),        # jaw angles, high and back
        (m(s.capsule((0.010, -0.094, 1.704), (0.054, -0.082, 1.708), 0.0090)), 0.020, 'add'),  # brow ridge
        (m(s.ellipsoid((0.088, -0.050, 1.705), (0.020, 0.030, 0.026))), 0.024, 'cut'),       # temples: a plane, not a bulge
        (s.ellipsoid((0, -0.086, 1.601), (0.027, 0.024, 0.026)), 0.032, 'add'),              # the muzzle
        (s.sphere((0, -0.091, F['chin_z'] + 0.017), 0.0155), 0.016, 'add'),                  # the chin pad, forward: a chin that does not recede
        (s.capsule((0, 0.028, 1.440), (0, 0.012, 1.600), 0.047), 0.035, 'add'),              # the neck, into the jaw's underside
    ]
    face = s.blend(base, strokes)
    # eye sockets: a bowl round each eyeball
    for side in (1, -1):
        c = eye_centre(side)      # front half only: the ball's sides stay inside the skull
        bowl = s.intersect(s.sphere(c, F['eye_r'] + F['socket_gap']), s.plane((0, 1, 0), -(c[1] + 0.2 * F['eye_r'])))
        face = s.subtract(face, bowl, 0.006)
        def tunnel(p, side=side, c=c):      # everything in front of the ball inside the lid opening: the face never covers the eye
            inside = (aperture(side, p) - 1.0) * F['aperture_w'] * 0.6
            return np.maximum(inside, p[:, 1] - (c[1] - 0.2 * F['eye_r']))
        face = s.subtract(face, tunnel, 0.0025)
    # the nose: bridge, tip, wings, nostrils
    nose = s.union(0.009,
                   s.capsule((0, -0.097, 1.690), (0, -0.113, F['nose_tip_z'] + 0.007), 0.0085, 0.0100),
                   s.sphere((0, -0.116, F['nose_tip_z']), 0.0115),
                   m(s.sphere((0.0115, -0.104, F['nose_tip_z'] - 0.0055), 0.0076)))
    face = s.blend(face, [(nose, 0.010, 'add')])
    face = s.blend(face, [(m(s.ellipsoid((0.0080, -0.110, F['nose_tip_z'] - 0.0105), (0.0032, 0.0045, 0.0019))), 0.003, 'cut')])
    # the lips: an upper lip with a soft bow, a fuller lower lip, the line between them, upturned corners
    mz = F['mouth_z']; mw = F['mouth_w'] * 0.5
    lips = s.union(0.004,
                   m(s.capsule((0.0, -0.1098, mz + 0.0042), (mw * 0.86, -0.1010, mz + 0.0010), 0.0050, 0.0024)),
                   m(s.capsule((0.0, -0.1076, mz - 0.0056), (mw * 0.80, -0.1008, mz - 0.0016), 0.0066, 0.0026)))
    face = s.blend(face, [(lips, 0.0045, 'add')])
    global LIPS; LIPS = lips
    slit = s.round_box((0, -0.112, mz), (mw, 0.014, 0.0011), 0.0010)
    global SLIT; SLIT = slit
    face = s.blend(face, [(slit, 0.0012, 'cut'),
                          (m(s.sphere((mw + 0.0015, -0.100, mz + 0.0015), 0.0028)), 0.003, 'cut'),       # the corners, a touch of a smile
                          (s.ellipsoid((0, -0.106, mz + 0.0135), (0.0045, 0.003, 0.0060)), 0.003, 'cut')])  # the philtrum
    # the lids belong to the face (the upper lid's moving part is meshed apart: lids() below)
    for side in (1, -1):
        face = s.blend(face, [(lid_shell(side, upper_only=False), 0.0028, 'add')])
    # the lid crease: a shallow groove over each upper lid, following the opening's arc
    for side in (1, -1):
        c = eye_centre(side)
        def crease(p, side=side, c=c):
            a = aperture(side, p - np.array([0, 0, F['crease_up']]))
            band = np.abs(a - 1.0) * F['aperture_w'] - 0.0011
            front = np.linalg.norm(p - c, axis=1) - (F['eye_r'] + F['lid_thick'] + 0.0035)
            return np.maximum(np.maximum(band, front), c[2] - p[:, 2] + 0.002)
        face = s.blend(face, [(crease, 0.0016, 'cut')])
    # the ears: a rim and a bowl, set back and tilted with the jaw line
    ear = s.subtract(s.ellipsoid((0.090, 0.014, 1.664), (0.010, 0.018, 0.030), s.rot_x(-0.22)),
                     s.ellipsoid((0.097, 0.012, 1.662), (0.0060, 0.012, 0.020), s.rot_x(-0.22)), 0.003)
    face = s.blend(face, [(m(ear), 0.007, 'add')])
    return face

LIPS = None
SLIT = None
def mouth_mask(p):
    """True on the walls of the mouth's line (its inside: dark)."""
    return SLIT(p) < 0.0006
def lip_mask(p):
    """True on the lips' red (the lips' own field, a little outside its surface)."""
    u = np.clip(np.abs(p[:, 0]) / (F['mouth_w'] * 0.5), 0, 1.5); taper = np.clip(1 - u * u, 0, 1) ** 0.6
    dz = p[:, 2] - F['mouth_z']
    bow = 0.0007 * np.exp(-((np.abs(p[:, 0]) - 0.0065) / 0.0035) ** 2)      # the upper lip's two peaks
    upper = (dz >= 0) & (dz < (0.0040 + bow) * taper)
    lower = (dz < 0) & (dz > -0.0058 * taper)
    return (upper | lower) & (u < 1) & (p[:, 1] < -0.096)

def upper_lids():
    """The two moving upper lids (each rotates about its eyeball's centre to blink)."""
    return {side: sdf.blend(lid_shell(side, upper_only=True), []) for side in (1, -1)}

def lash(side, p):
    """True where a point of the upper lid is on its lash line (the dark rim along the opening's upper edge)."""
    c = eye_centre(side); a = aperture(side, p)
    return (p[:, 2] > c[2] - 0.001) & (a < 1.0 + F['lash_band']) & (a > 0.85)

def brows():
    """Soft brows on the brow ridge (hair): thick at the inner end, arched, tapering out. One field per side."""
    out = {}
    for side in (1, -1):
        pts = [np.array([side * x, y, z]) for x, y, z in ((0.013, -0.1000, 1.7050), (0.030, -0.0985, 1.7115), (0.047, -0.0935, 1.7125), (0.061, -0.0855, 1.7075))]
        radii = (0.0036, 0.0033, 0.0026, 0.0015)
        segs = [sdf.capsule(pts[i], pts[i + 1], radii[i], radii[i + 1]) for i in range(3)]
        flat = lambda p, f=sdf.union(0.004, *segs): f(p * np.array([1, 1, 0.75]) + np.array([0, 0, 0]))      # flatter than round
        out[side] = sdf.union(0.004, *segs)
    return out

# ---- hair: a shell over the skull with a designed hairline, thin under the hat, chunky locks along its edge, clear of the ears
HAIRLINE = ((0, 1.748), (45, 1.726), (72, 1.690), (84, 1.652), (96, 1.648), (106, 1.680), (128, 1.640), (155, 1.606), (180, 1.596))   # degrees from the front, z
def _hairline(theta):
    t = np.abs(np.degrees(theta)); a = np.array([h[0] for h in HAIRLINE], float); z = np.array([h[1] for h in HAIRLINE], float)
    return np.interp(t, a, z)
def hair(skull):
    s = sdf
    def shell(p):
        theta = np.arctan2(p[:, 0], -(p[:, 1] - 0.010)); hz = _hairline(theta)
        under_hat = np.clip((p[:, 2] - 1.700) / 0.03, 0, 1)                                  # thin where the hat covers it
        thick = 0.0105 - 0.0055 * under_hat + 0.004 * np.clip((np.abs(np.degrees(theta)) - 90) / 90, 0, 1)
        d = skull(p); out = d - thick; inn = -(d + 0.0015)
        edge = hz - p[:, 2]
        return -sdf.smin(-np.maximum(out, inn), -edge, 0.004)
    f = shell
    # clumps along the edge: fat and soft with rounded ends, lying on the skull and sweeping down and back; together they make
    # a scalloped edge instead of a cut line
    def on_skull(q, lift):      # a point moved onto the skull's surface, then lifted off it
        for _ in range(4):
            g = sdf.gradient(skull, q[None])[0]; q = q - skull(q[None])[0] * g
        return q + sdf.gradient(skull, q[None])[0] * lift
    locks = []
    for deg in list(range(-170, 181, 21)):
        if abs(abs(deg) - 95) < 14: continue      # (the ears)
        th = np.radians(deg); hz = _hairline(np.array([th]))[0]; d = np.array([np.sin(th), -np.cos(th), 0.0])
        sweep = 0.012 * np.sign(deg) * (abs(deg) < 150)      # toward the back
        top = on_skull(np.array([0, 0.010, hz + 0.034]) + d * 0.09, 0.0055)
        tip = on_skull(np.array([0, 0.010, hz - 0.002]) + d * 0.09 + np.array([0, sweep * 0, 0]) + np.array([np.cos(th), np.sin(th), 0]) * sweep * 0, 0.0050)
        locks.append(s.capsule(top, tip, 0.0105, 0.0072))
    for side in (1, -1):      # sideburns in front of the ears
        th = np.radians(84 * side); d = np.array([np.sin(th), -np.cos(th), 0.0])
        locks.append(s.capsule(on_skull(np.array([0, 0.0, 1.700]) + d * 0.09, 0.004), on_skull(np.array([0, -0.004, 1.652]) + d * 0.09, 0.003), 0.0065, 0.0042))
    f = s.blend(f, [(l, 0.006, 'add') for l in locks])
    # never over the ears, never in front of the face's skin
    for side in (1, -1):
        f = s.subtract(f, s.ellipsoid((side * 0.094, 0.012, 1.664), (0.030, 0.026, 0.036)), 0.003)
    return f
