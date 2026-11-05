"""The adventurer's hands, sculpted as signed distance fields (sdf.py): a solid palm, four tapered fingers in a relaxed,
progressive curl, a thumb with real mass, and the fingerless leather glove as a shell over the palm. Stylized: a little
large and chunky, so they read at a distance and match the head.

Coordinates: metres, z up, the character faces -y. The arms hang in the bind pose with the hands turned sideways: the palm
faces the thigh (inward), the knuckles run front (index, -y) to back (little finger, +y), the fingers point down. Fields are
built for the left hand (x > 0); hand(-1) mirrors to the right.
"""
import numpy as np
import sdf

WRIST = np.array([0.265, 0.0, 0.910])        # the hand bone's head (adventurer.py's skeleton)
KNUCKLE_Z = 0.826
# per finger: y at the knuckle, segment lengths (proximal, middle, distal), radius at the knuckle
FINGERS = ((-0.0285, (0.026, 0.019, 0.016), 0.0097), (-0.0095, (0.029, 0.021, 0.017), 0.0101),
           (0.0095, (0.027, 0.019, 0.016), 0.0096), (0.0275, (0.021, 0.015, 0.014), 0.0085))      # chunky: short, thick, tapering; spaced so the fingers read apart
HAND_SCALE = 1.12                            # the whole hand a little large, as the storybook head asks (about the wrist)
CURL = (0.22, 0.38, 0.30)                    # radians per joint, toward the palm: relaxed, more at the middle joint

def _finger(y, lengths, r0, spread):
    """A finger as three capsules from its knuckle, each joint curling toward the palm (-x) and fanning by `spread`."""
    p = np.array([0.2662, y, KNUCKLE_Z]); a = 0.0; parts = []; r = r0
    for k, L in enumerate(lengths):
        a += CURL[k]
        d = np.array([-np.sin(a), spread, -np.cos(a)]); d /= np.linalg.norm(d)
        q = p + d * L; r1 = r * (0.93 if k < 2 else 0.84)
        parts.append(sdf.capsule(p, q, r, r1)); p, r = q, r1
    tip = p
    return sdf.union(0.0045, *parts), tip

def _scaled(f):
    """f grown by HAND_SCALE about the wrist (distances stay metric)."""
    c = WRIST.copy(); k = HAND_SCALE
    return lambda p: f(c + (p - c) / k) * k

def skin(side=1):
    f = _scaled(_skin_raw())
    return f if side > 0 else _mirror(f)

def _skin_raw():
    """The hand's skin: palm, fingers, thumb (left hand)."""
    s = sdf
    palm = s.union(0.010,
                   s.round_box((0.2665, 0.0000, 0.8580), (0.0125, 0.0345, 0.0335), 0.0100),          # the palm's block
                   s.ellipsoid((0.2600, -0.0180, 0.8700), (0.0110, 0.0190, 0.0240)),                  # the thumb's ball
                   s.capsule((0.2665, -0.026, KNUCKLE_Z + 0.004), (0.2665, 0.025, KNUCKLE_Z + 0.004), 0.0095))  # the knuckle row
    fingers = []
    for i, (y, lengths, r0) in enumerate(FINGERS):
        f, _ = _finger(y, lengths, r0, (i - 1.5) * 0.050); fingers.append(f)
    PARTS['palm'] = palm; PARTS['fingers'] = sdf.union(0.0045, *fingers)
    # the thumb: from the heel of the palm, forward and down, curled in toward the index finger
    t0 = np.array([0.2600, -0.0300, 0.8800]); t1 = t0 + np.array([-0.0040, -0.0165, -0.0270]); t2 = t1 + np.array([-0.0070, -0.0060, -0.0200])
    global THUMB_TIP; THUMB_TIP = t2
    thumb = s.union(0.004, s.capsule(t0, t1, 0.0112, 0.0100), s.capsule(t1, t2, 0.0100, 0.0082))
    PARTS['thumb'] = thumb; PARTS['thumb_line'] = (t0, t2)
    wrist = s.capsule(WRIST + np.array([0, 0, 0.030]), WRIST + np.array([0, 0, -0.022]), 0.0225, 0.0230)  # into the forearm
    hand = s.union(0.006, palm, wrist, thumb)
    hand = s.blend(hand, [(f, 0.0055, 'add') for f in fingers])
    hand = s.blend(hand, [(s.ellipsoid((0.2525, 0.0, 0.852), (0.0035, 0.022, 0.020)), 0.010, 'cut')])  # a soft hollow in the palm (the palm faces inward, -x)
    return hand

THUMB_TIP = np.zeros(3)
PARTS = {}

def _raw(p, side):      # a built point (world, left or right hand) back to the raw left-hand sculpt's coordinates
    q = np.array(p, float).reshape(-1, 3).copy()
    if side < 0: q[:, 0] = -q[:, 0]
    return WRIST + (q - WRIST) / HAND_SCALE

def bone_points(side=1):
    """Head and tail of the finger and thumb bones, scaled and mirrored like the hand: the four fingers curl as one about the knuckle
    row, the thumb about its base (fists, a thumbs up, pointing)."""
    if not PARTS: _skin_raw()
    def built(p):
        q = WRIST + (np.asarray(p, float) - WRIST) * HAND_SCALE
        return q if side > 0 else q * np.array([-1.0, 1.0, 1.0])
    kn = np.array([0.2662, 0.0, KNUCKLE_Z]); d = np.array([-np.sin(0.45), 0.0, -np.cos(0.45)])
    t0, t2 = PARTS['thumb_line']
    return {'fingers': (built(kn), built(kn + d * 0.050)), 'thumb': (built(t0), built(t2))}

def part_weights(points, side=1):
    """(finger, thumb) weights for points of the built hand or glove, from the sculpt's own parts: a point nearer a finger than the
    palm follows the fingers (fading in across the knuckles), one on the thumb beyond its base follows the thumb."""
    if not PARTS: _skin_raw()
    q = _raw(points, side)
    dp, df, dt = PARTS['palm'](q), PARTS['fingers'](q), PARTS['thumb'](q)
    wf = np.clip((dp - df) / 0.006 + 0.5, 0, 1) * np.clip((KNUCKLE_Z + 0.006 - q[:, 2]) / 0.012, 0, 1)
    t0, t2 = PARTS['thumb_line']; ax = t2 - t0; u = ((q - t0) @ ax) / (ax @ ax)
    wt = np.clip((np.minimum(dp, df) - dt) / 0.006 + 0.5, 0, 1) * np.clip((u - 0.05) / 0.30, 0, 1)
    wt = np.minimum(wt, 1 - wf)
    return wf, wt
def glove(side=1):
    """The fingerless glove: the hand's surface lifted 2.2 mm, from a rolled cuff at the wrist to just past the knuckles, with
    smooth rounded edges (no raw cut planes), the fingers and the thumb's tip bare."""
    base = _skin_raw(); s = sdf; k = HAND_SCALE; c = WRIST
    cuff = s.capsule(c + np.array([0, 0, 0.004]), c + np.array([0, 0, 0.040]), 0.0300, 0.0310)      # the wrist's rolled hem, an inch of leather round the wrist
    def f(p):
        q = c + (p - c) / k                                                                  # raw hand coordinates
        d = s.smin(base(q) - 0.0022 / k, cuff(q), 0.006)
        d = -s.smin(-d, q[:, 2] - (KNUCKLE_Z - 0.0125), 0.0035)                              # past the knuckles, a rounded edge
        d = -s.smin(-d, np.linalg.norm(q - THUMB_TIP, axis=1) - 0.024, 0.0035)             # the thumb bare past its base
        d = -s.smin(-d, (c[2] + 0.034) - q[:, 2], 0.0030)                                    # the top of the cuff, tucked under the bracer
        return d * k
    return f if side > 0 else _mirror(f)

def _mirror(f):
    def g(p):
        q = p.copy(); q[:, 0] = -q[:, 0]; return f(q)
    return g

def bounds(side=1):
    lo = np.array([0.205, -0.085, 0.715]); hi = np.array([0.325, 0.070, 0.955])
    return (lo, hi) if side > 0 else (np.array([-hi[0], lo[1], lo[2]]), np.array([-lo[0], hi[1], hi[2]]))
