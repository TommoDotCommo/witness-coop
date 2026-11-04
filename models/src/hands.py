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
FINGERS = ((-0.0255, (0.028, 0.020, 0.017), 0.0089), (-0.0085, (0.031, 0.022, 0.018), 0.0093),
           (0.0085, (0.029, 0.020, 0.017), 0.0088), (0.0245, (0.023, 0.016, 0.015), 0.0077))
CURL = (0.22, 0.38, 0.30)                    # radians per joint, toward the palm: relaxed, more at the middle joint

def _finger(y, lengths, r0, spread):
    """A finger as three capsules from its knuckle, each joint curling toward the palm (-x) and fanning by `spread`."""
    p = np.array([0.2662, y, KNUCKLE_Z]); a = 0.0; parts = []; r = r0
    for k, L in enumerate(lengths):
        a += CURL[k]
        d = np.array([-np.sin(a), spread, -np.cos(a)]); d /= np.linalg.norm(d)
        q = p + d * L; r1 = r * (0.90 if k < 2 else 0.80)
        parts.append(sdf.capsule(p, q, r, r1)); p, r = q, r1
    tip = p
    return sdf.union(0.0035, *parts), tip

def skin(side=1):
    """The hand's skin: palm, fingers, thumb (left hand; side -1 mirrors)."""
    s = sdf
    palm = s.union(0.010,
                   s.round_box((0.2665, 0.0000, 0.8580), (0.0125, 0.0345, 0.0335), 0.0100),          # the palm's block
                   s.ellipsoid((0.2600, -0.0180, 0.8700), (0.0110, 0.0190, 0.0240)),                  # the thumb's ball
                   s.capsule((0.2665, -0.026, KNUCKLE_Z + 0.004), (0.2665, 0.025, KNUCKLE_Z + 0.004), 0.0095))  # the knuckle row
    fingers = []
    for i, (y, lengths, r0) in enumerate(FINGERS):
        f, _ = _finger(y, lengths, r0, (i - 1.5) * 0.035); fingers.append(f)
    # the thumb: from the heel of the palm, forward and down, curled in toward the index finger
    t0 = np.array([0.2600, -0.0300, 0.8800]); t1 = t0 + np.array([-0.0040, -0.0165, -0.0270]); t2 = t1 + np.array([-0.0070, -0.0060, -0.0200])
    global THUMB_TIP; THUMB_TIP = t2
    thumb = s.union(0.004, s.capsule(t0, t1, 0.0102, 0.0090), s.capsule(t1, t2, 0.0090, 0.0072))
    wrist = s.capsule(WRIST + np.array([0, 0, 0.030]), WRIST + np.array([0, 0, -0.022]), 0.0225, 0.0230)  # into the forearm
    hand = s.union(0.006, palm, wrist, thumb)
    hand = s.blend(hand, [(f, 0.0055, 'add') for f in fingers])
    hand = s.blend(hand, [(s.ellipsoid((0.2525, 0.0, 0.852), (0.0035, 0.022, 0.020)), 0.010, 'cut')])  # a soft hollow in the palm (the palm faces inward, -x)
    return hand if side > 0 else _mirror(hand)

THUMB_TIP = np.zeros(3)
def glove(side=1):
    """The fingerless glove: the palm's surface lifted 1.6 mm, from the wrist to just past the knuckles, the fingers bare."""
    base = skin(1)
    def f(p):
        d = base(p) - 0.0016
        d = np.maximum(d, (KNUCKLE_Z - 0.013) - p[:, 2])        # over the knuckles, the fingers bare past them
        d = np.maximum(d, 0.024 - np.linalg.norm(p - THUMB_TIP, axis=1))      # the thumb bare past its base
        d = np.maximum(d, p[:, 2] - (WRIST[2] + 0.020))           # and at the cuff
        return d
    return f if side > 0 else _mirror(f)

def _mirror(f):
    def g(p):
        q = p.copy(); q[:, 0] = -q[:, 0]; return f(q)
    return g

def bounds(side=1):
    lo = np.array([0.215, -0.075, 0.735]); hi = np.array([0.315, 0.060, 0.945])
    return (lo, hi) if side > 0 else (np.array([-hi[0], lo[1], lo[2]]), np.array([-lo[0], hi[1], hi[2]]))
