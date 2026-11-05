"""The adventurer's design as numbers: the generator builds from them and tools/check_character.py measures the built
model against them. docs/CHARACTER.md explains the direction and every rule. Metres; z up, the character faces -y.

Direction: stylized appeal (storybook adventurer). Simplified, confident planes; eyes a little larger and more open than
life with real depth (eyeballs under thick lids); a short lower face; hands slightly large with clear fingers; no thin
realistic detail that the rest of the model cannot match. The eye line stays where the camera is (the proxy fits the
model so the eyes are at the player's eye height).
"""

FACE = dict(
    eye_z=1.6766,          # the eye line (fixed: the proxy scales the model to put it at the camera)
    eye_x=0.0370,          # eyeball centres either side of the midline
    eye_y=-0.0720,         # eyeball centre depth
    eye_r=0.0180,         # (stylized: larger than life's 12 mm, so the open almond is all eye)          # eyeball radius
    socket_gap=0.0016,     # the socket's bowl clears the ball by this
    lid_gap=0.0004,        # the lids ride this far off the ball
    lid_thick=0.0045,      # and are this thick at the rim
    aperture_w=0.0185,     # half the lid opening's width
    aperture_up=0.0080,    # the opening above the centre
    aperture_down=0.0075,  # and below it
    aperture_skew=-0.10,   # taller toward the inner corner
    corner_lift=0.0018,    # the outer corner sits this much higher
    iris_r=0.0082,         # iris radius on the ball (about 40 % of the opening's width)
    pupil_r=0.0033,
    nose_tip_z=1.6290,
    mouth_z=1.5960, mouth_w=0.053,
    smile=0.0034,          # the mouth's corners sit this much above its middle: a friendly resting face
    chin_z=1.5620,         # the underside of the chin
    crown_z=1.8130,        # the top of the skull (under the hat)
    lash_band=0.16,        # the upper lid's lash line: this share of the opening's size above its edge is dark
    crease_up=0.0042,      # the lid crease runs this far above the opening
)

# Targets the gate holds the built model to: (low, high). Ratios are of head height (chin to crown) unless named.
TARGETS = dict(
    heads_tall=(6.0, 6.9),               # standing height / head height: storybook (the owner's choice, 2026-11-05; life is about 7.5)
    eye_line=(0.45, 0.53),               # eye line above the chin / head height
    aperture_aspect=(0.36, 0.60),        # visible eye, height / width, seen from the front: open and readable (the old squint: 0.26)
    eye_spacing=(0.95, 1.40),            # gap between the two openings / one opening's width (about one eye apart)
    iris_fraction=(0.38, 0.52),          # visible iris width / opening width: enough white to read the gaze, never a doll's eye
    nose_length=(0.14, 0.22),            # eye line to nose tip / head height
    face_symmetry_mm=(0.0, 0.8),         # 99th percentile mirror mismatch of the head's skin (the face is built symmetric)
    hand_to_face=(0.75, 1.0),            # hand length (wrist to fingertip) / chin-to-brow height: slightly large hands read
    head_tris=(8000, 30000),             # the head's skin, triangles: a smooth silhouette and lids, still a game asset
    blink_left=(0.0, 0.08),
    eye_white=(0.70, 1.0),               # mean brightness of the visible eye white (texture, 0..1): grey whites let the iris swallow the eye              # visible eye after a full blink through the proxy's life layer / visible eye open
    degenerate=(0, 400),                 # zero-area triangles in the whole model
)
