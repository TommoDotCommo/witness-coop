// The game's own perspective for the current frame, so everything the mod draws in the world lines up with what the game draws.
// The game keeps its projection matrix in the image (build 11370412); its x and y scale terms are cot(half vertical fov) / aspect and
// cot(half vertical fov). The vertical fov is the player's saved option (default 54 degrees), so it must never be a constant here.
#pragma once
#include <cmath>

struct GameProj {
    float vfovDeg;      // vertical field of view
    float matrixAspect; // width / height the game's own matrix was built for (0 when unknown)
    bool live;          // read from the game this frame (false: the fallback)
};

constexpr float kGameFallbackVFovDeg = 54.0f;     // the game's default option; only used when its matrix cannot be read

// Pure part (game-free tests): the field of view and aspect that a projection with these x and y scale terms has. Signs are ignored.
inline bool GameProjFromScales(float sx, float sy, GameProj* out) {
    sx = std::fabs(sx); sy = std::fabs(sy);
    if (!(sy > 0.4f && sy < 12.0f) || !(sx > 0.2f && sx < 12.0f)) return false;      // 10 to 140 degrees vertical, a plausible window shape
    out->vfovDeg = 2.0f * std::atan(1.0f / sy) * 57.2957795f; out->matrixAspect = sy / sx; out->live = true; return true;
}

GameProj GameProjection();      // gamecam.cpp: read this frame's projection from the running game
