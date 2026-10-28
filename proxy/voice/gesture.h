#pragma once
#include <algorithm>
#include <cmath>
namespace wvoice {
// 20 ms capture cadence, RMS in PCM units. Schmitt thresholds and a 300 ms
// release hold prevent syllable gaps from repeatedly lowering the hand.
class GestureActivity {
    float attack_ = 0, hold_ = 0; bool on_ = false;
public:
    bool Feed(float rms, bool transmitting, unsigned channel, float dt) {
        dt = std::max(0.f, std::min(.1f, dt));
#ifndef WE_MUT_CHANNEL
        if (channel != 0 || !transmitting) { on_=false; attack_=hold_=0; return false; }
#endif
        bool loud = std::isfinite(rms) && rms >= (on_ ? 400.f : 700.f);
        if (loud) { attack_ += dt; hold_ = .3f; }
        else { attack_=0; hold_ = std::max(0.f,hold_-dt); }
#ifndef WE_MUT_HYSTERESIS
        if (!on_ && attack_ >= .06f) on_=true;
        if (on_ && hold_ <= 0) on_=false;
#else
        on_=loud;
#endif
        return on_;
    }
};
}
