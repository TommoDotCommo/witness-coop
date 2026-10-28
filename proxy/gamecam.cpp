#include "gamecam.h"
#include "config.h"
#include <windows.h>

// Module offsets, build 11370412: the world projection matrix scale terms (m00, m11) and the option the game builds them from.
static const uintptr_t kProjX = 0x649910, kProjY = 0x649924, kFovOption = 0x6208BC;

GameProj GameProjection() {
    GameProj g = {kGameFallbackVFovDeg, 0, false};
    if (GetCfg().oldProjection) { g.vfovDeg = 60.0f; return g; }      // twin: the old hardcoded value
    uintptr_t base = (uintptr_t)GetModuleHandleA(nullptr);
    const float* sx = (const float*)(base + kProjX); const float* sy = (const float*)(base + kProjY);
    if (!IsBadReadPtr(sx, 4) && !IsBadReadPtr(sy, 4) && GameProjFromScales(*sx, *sy, &g)) return g;
    const float* opt = (const float*)(base + kFovOption);
    if (!IsBadReadPtr(opt, 4) && *opt > 20.0f && *opt < 120.0f) g.vfovDeg = *opt;
    return g;
}
