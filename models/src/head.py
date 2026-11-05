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
    # Three continuous masses: a broad upper face, a tapered lower face and a slim neck.
    # Small additive cheek/jaw strokes made separate islands of curvature in three-quarter view.
    strokes = [
        (s.ellipsoid((0, -0.022, 1.659), (0.076, 0.077, 0.086)), 0.040, 'add'),
        (s.ellipsoid((0, -0.028, 1.600), (0.050, 0.060, 0.039)), 0.036, 'add'),
        (s.ellipsoid((0, -0.048, F['chin_z'] + 0.021), (0.031, 0.037, 0.025)), 0.026, 'add'),
        (s.capsule((0, 0.025, 1.440), (0, 0.018, 1.602), 0.034), 0.030, 'add'),
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
    # A clean, small nose with one bridge-to-tip transition. Nostrils are omitted at this scale.
    nose = s.union(0.012,
                   s.capsule((0, -0.095, 1.692), (0, -0.114, F['nose_tip_z'] + 0.007), 0.0070, 0.0092),
                   s.ellipsoid((0, -0.115, F['nose_tip_z'] + 0.002), (0.011, 0.010, 0.009)))
    face = s.blend(face, [(nose, 0.014, 'add')])
    # A low, continuous lip cushion. The smile is painted continuously rather than cut into tiny cavities.
    mz = F['mouth_z']; mw = F['mouth_w'] * 0.5
    lips = s.ellipsoid((0, -0.089, mz - 0.002), (mw * 0.94, 0.006, 0.005))
    face = s.blend(face, [(lips, 0.008, 'add')])
    global LIPS, SLIT
    LIPS = lips
    SLIT = s.round_box((0, -0.104, mz), (mw, 0.010, 0.0008), 0.0005)
    # the lids belong to the face (the upper lid's moving part is meshed apart: lids() below)
    for side in (1, -1):
        face = s.blend(face, [(lid_shell(side, upper_only=False), 0.0028, 'add')])
    # the ears: a rim and a bowl, set back and tilted with the jaw line
    ear = s.subtract(s.ellipsoid((0.085, 0.018, 1.664), (0.012, 0.016, 0.025), s.rot_x(-0.22)),
                     s.ellipsoid((0.094, 0.014, 1.665), (0.0060, 0.010, 0.016), s.rot_x(-0.22)), 0.003)
    face = s.blend(face, [(m(ear), 0.012, 'add')])
    return face

LIPS = None
SLIT = None
def mouth_mask(p):
    """True on the walls of the mouth's line (its inside: dark)."""
    return SLIT(p) < 0.0006
def lip_mask(p):
    """True on the lips' red (the lips' own field, a little outside its surface)."""
    u = np.clip(np.abs(p[:, 0]) / (F['mouth_w'] * 0.5), 0, 1.5); taper = np.clip(1 - u * u, 0, 1) ** 0.6
    dz = p[:, 2] - F['mouth_z'] - F['smile'] * np.clip(np.abs(p[:, 0]) / (F['mouth_w'] * 0.5), 0, 1) ** 2
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
        a = np.array([side*0.012, -0.099, 1.709])
        b = np.array([side*0.032, -0.102, 1.724])
        c = np.array([side*0.061, -0.086, 1.709])
        ts = np.linspace(0, 1, 10)
        pts = [(1-t)**2*a + 2*t*(1-t)*b + t*t*c for t in ts]
        radii = [0.0038*(1-t) + 0.0012*t for t in ts]
        segs = [sdf.capsule(pts[i], pts[i+1], radii[i], radii[i+1]) for i in range(len(pts)-1)]
        out[side] = sdf.union(0.002, *segs)
    return out

# ---- hair: a shell over the skull with a designed hairline, thin under the hat, chunky locks along its edge, clear of the ears
HAIRLINE = ((0, 1.751), (35, 1.741), (60, 1.717), (77, 1.683), (88, 1.675), (105, 1.685), (125, 1.659), (155, 1.642), (180, 1.638))
def _hairline(theta):
    t = np.abs(np.degrees(theta)); a = np.array([h[0] for h in HAIRLINE], float); z = np.array([h[1] for h in HAIRLINE], float)
    return np.interp(t, a, z)
def hair(skull):
    """A swept crop: one smooth cap, a shaped nape, two broad temple locks. No strand noise."""
    cap = sdf.ellipsoid((0, 0.016, 1.715), (0.103, 0.111, 0.104))
    def f(p):
        theta = np.arctan2(p[:, 0], -(p[:, 1] - 0.010))
        # A very broad sweep in the front hairline; the back ends on one calm curve.
        line = _hairline(theta) + 0.003 * np.sin(theta * 2) - 0.012*np.exp(-((theta-0.60)/0.40)**2)
        d = np.maximum(cap(p), line - p[:, 2])
        return d
    locks = []
    for side in (1, -1):
        locks.append(sdf.capsule((side*0.084, -0.037, 1.720), (side*0.085, -0.024, 1.675), 0.013, 0.007))
    # One broad forelock sweeps across the forehead; its end clears the outer brow.
    forelock=sdf.union(0.010,
        sdf.capsule((-0.027,-0.083,1.754),(0.021,-0.097,1.744),0.015,0.013),
        sdf.capsule((0.021,-0.097,1.744),(0.065,-0.077,1.724),0.013,0.0045))
    forelock=sdf.intersect(forelock,sdf.ellipsoid((0,0.016,1.715),(0.106,0.114,0.107)),0.003)
    out = sdf.union(0.007, f, forelock, *locks)
    for side in (1, -1):
        out = sdf.subtract(out, sdf.ellipsoid((side * 0.094, 0.016, 1.663), (0.024, 0.022, 0.030)), 0.005)
    return out

# ---- the island figure: the same skull and living eyes, the face carved in big simple planes (a prototype: headlab --style island)
def island_face():
    s = sdf; m = lambda f: s.mirror_x(f)
    base = s.ellipsoid((0, 0.010, F['crown_z'] - 0.096), (0.097, 0.105, 0.096))
    face = s.blend(base, [
        (s.ellipsoid((0, -0.034, 1.650), (0.075, 0.072, 0.086)), 0.040, 'add'),          # one egg-shaped face mass
        (s.ellipsoid((0, -0.052, F['chin_z'] + 0.030), (0.040, 0.044, 0.030)), 0.040, 'add'),  # a soft, small chin
        (m(s.capsule((0.012, -0.092, 1.704), (0.054, -0.080, 1.708), 0.0085)), 0.030, 'add'),  # the brow, one soft shelf
        (s.capsule((0, 0.028, 1.440), (0, 0.012, 1.600), 0.045), 0.040, 'add'),          # the neck
    ])
    for side in (1, -1):
        c = eye_centre(side)
        bowl = s.intersect(s.sphere(c, F['eye_r'] + F['socket_gap']), s.plane((0, 1, 0), -(c[1] + 0.2 * F['eye_r'])))
        face = s.subtract(face, bowl, 0.008)
        def tunnel(p, side=side, c=c):
            return np.maximum((aperture(side, p) - 1.0) * F['aperture_w'] * 0.6, p[:, 1] - (c[1] - 0.2 * F['eye_r']))
        face = s.subtract(face, tunnel, 0.003)
        face = s.blend(face, [(lid_shell(side, upper_only=False), 0.004, 'add')])
    # the nose: one carved wedge, no nostrils; the mouth: a single soft line over a small swell
    face = s.blend(face, [(s.union(0.012, s.capsule((0, -0.097, 1.690), (0, -0.111, 1.636), 0.0072, 0.0090), s.sphere((0, -0.112, 1.632), 0.0092)), 0.014, 'add')])
    mz = F['mouth_z']
    face = s.blend(face, [(s.ellipsoid((0, -0.096, mz - 0.004), (0.018, 0.010, 0.008)), 0.012, 'add'),
                          (s.round_box((0, -0.115, mz), (0.0150, 0.016, 0.0006), 0.0005), 0.0022, 'cut')])
    # the ears: smooth discs, set back
    face = s.blend(face, [(m(s.ellipsoid((0.090, 0.014, 1.664), (0.009, 0.017, 0.028), s.rot_x(-0.22))), 0.012, 'add')])
    return face
