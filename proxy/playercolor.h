#pragma once
// One colour per player id, the same everywhere a player is shown (lines, name tags): eight distinct hues that repeat for ids above eight.
inline void PlayerColor(unsigned id, float rgb[3]) {
    static const float kPalette[8][3] = {{1.00f, 0.35f, 0.35f}, {0.30f, 0.80f, 1.00f}, {0.40f, 1.00f, 0.40f}, {1.00f, 0.80f, 0.20f}, {0.80f, 0.50f, 1.00f}, {1.00f, 0.60f, 0.20f}, {0.20f, 1.00f, 0.80f}, {1.00f, 0.50f, 0.80f}};
    const float* c = kPalette[(id ? id - 1 : 0) % 8]; rgb[0] = c[0]; rgb[1] = c[1]; rgb[2] = c[2];
}
