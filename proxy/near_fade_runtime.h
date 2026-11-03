#pragma once
#include <map>
#include "near_fade.h"
#include "config.h"
#include "gamecam.h"
inline float NearFadeAlpha(unsigned id, const float* cam, const float* ang, const float* eye, float aspect) {
    static std::map<unsigned,wnear::State> states;
    const auto& c=GetCfg();
    return states[id].step(cam,ang[0],ang[1],GameProjection().vfovDeg,aspect,eye,c.nearFadeStart,c.nearFadeEnd,c.nearFade);
}
