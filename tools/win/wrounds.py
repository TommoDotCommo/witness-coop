"""The screen orientations wdraw tries in turn when it looks for a panel's start circle (pure, so it can be tested without a game).

A round is (ax, sx, sy) or (ax, sx, sy, roll in degrees): the panel's width/height ratio on the screen, the flips of its two axes, and a turn in
the panel's plane. Roll 0 with the four flips and roll 90 with the four flips are the eight ways a square grid can be shown; roll 45 is the diamond."""
ROLL_CANDS = [(w2, dy2) for dy2 in (0, 12, -12, 24, -24) for w2 in (1.88, 1.75, 2.0, 1.6, 2.15, 1.45, 1.3)]      # (a diamond is seen at about 1.4 times the size)
QUARTER_CANDS = [(w2, dy2) for dy2 in (0, 12, -12, 24, -24) for w2 in (0.567, 0.645, 0.76, 0.88, 0.70, 0.50)]    # (a quarter turn keeps the size)
QUARTER = ((1.0, 1, 1, 90), (1.0, -1, -1, 90), (1.0, 1, -1, 90), (1.0, -1, 1, 90))


def base_rounds():
    return ([(1.0, 1, 1), (1.0, 1, 1)] + list(QUARTER[:2]) + [(1.4, -1, -1), (1.0, 1, 1, 45), (1.0, -1, -1, 45)]
            + list(QUARTER[2:])
            + [(ax_, sx_, sy_) for ax_ in (1.0, 1.45, 0.7, 2.0, 0.5) for (sx_, sy_) in ((-1, -1), (1, 1), (1, -1), (-1, 1)) if (ax_, sx_, sy_) != (1.0, 1, 1)])


def round_key(r_, roll0):
    return (r_[0], r_[1], r_[2], r_[3] if len(r_) > 3 else roll0)


def orientation_rounds(bad, roll0):
    """The rounds in order. bad: orientations [ax, sx, sy, roll] a first edge refuted earlier (not tried again; their mirror images come first).
    roll0: the panel's own roll in degrees (0 unless found or forced)."""
    rounds = base_rounds()
    bad = [tuple(b) for b in bad]
    if bad:
        mirrors = [m_ if m_[3] != roll0 else m_[:3] for ax_, sx_, sy_, rl_ in bad for m_ in ((ax_, sx_, -sy_, rl_), (ax_, -sx_, sy_, rl_)) if m_ not in bad]
        rounds = list(dict.fromkeys(mirrors)) + [r_ for r_ in rounds if round_key(r_, roll0) not in bad]
    return rounds


def candidates(rd_, attempt, std_cands, forced=False):
    """The (W, dy) pairs one round scans, or None for wdraw's own default list (rounds 2 and later of a plain orientation)."""
    if len(rd_) <= 3: return std_cands if (attempt < 2 or forced) else None
    return ROLL_CANDS if rd_[3] % 90 else QUARTER_CANDS
