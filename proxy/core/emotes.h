#pragma once
// Stable wire IDs: the first eight retain their original meanings.
#include <cmath>
#include <cstdint>
namespace witness {
struct EmoteDef { const char* name; float duration; bool loop, mobile; };
inline constexpr EmoteDef kEmotes[] = {
    {"",0,false,false}, {"Wave",2.6f,false,true}, {"Dance",2.6f,false,false},
    {"Cheer",2.6f,false,false}, {"Yes",2.6f,false,true}, {"No",2.6f,false,true},
    {"Bow",2.6f,false,false}, {"Spin",2.6f,false,false}, {"Shrug",2.6f,false,true},
    {"Point",3.2f,false,true}, {"Clap",3.6f,false,true}, {"Facepalm",3.6f,false,false},
    {"Thumbs up",3.2f,false,true}, {"Thinking",5.f,true,false}, {"Sit",6.f,true,false},
    {"Stretch",4.8f,false,false}, {"Salute",3.2f,false,true}, {"Beckon",3.6f,false,true},
    {"Celebrate",4.8f,false,false}, {"Sad",4.8f,false,false}, {"Laugh",4.f,false,false},
    {"Panic",4.f,false,false}, {"Sway",4.f,true,false}, {"Puzzle spark",4.f,false,false},
    {"Horizon",4.8f,false,false}
};
inline constexpr int kEmoteCount = sizeof(kEmotes)/sizeof(kEmotes[0])-1;
inline bool ValidEmote(int id) { return id >= 0 && id <= kEmoteCount; }
inline bool EmoteActive(int id, float t) {
    return id > 0 && ValidEmote(id) && std::isfinite(t) && t >= 0 && (
#ifndef WE_MUT_LOOP
        kEmotes[id].loop ||
#endif
        t < kEmotes[id].duration);
}
inline float Ease(float t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return t*t*t*(t*(6*t-15)+10); }
inline float EmoteEnvelope(int id, float t) {
    if (!EmoteActive(id,t)) return 0;
#ifdef WE_MUT_ENVELOPE
    return 1;
#endif
    return Ease(t/(id==14 || id==15 ? .8f : .35f)) * (kEmotes[id].loop ? 1 : Ease((kEmotes[id].duration-t)/.45f));
}
// Shared wheel hit test: three pages, eight large targets, centre dead zone.
inline int EmoteSlot(float x, float y, int page) {
    if (!std::isfinite(x) || !std::isfinite(y) || x*x+y*y < 900 || page < 0 || page > 2) return 0;
    float a = std::atan2(y,x)+1.5707963268f; if(a<0) a+=6.283185307f;
    return 1+page*8+int(a/.7853981634f+.5f)%8;
}
// Per-peer token spacing. Zero cancels immediately; repeated state is free.
struct EmoteGate {
    bool seen = false; uint64_t at = 0;
    bool Accept(uint64_t now, int id, bool changed) {
        if (!ValidEmote(id)) return false;
#ifndef WE_MUT_RATE
        if (id && changed && seen && now-at < 400) return false;
#endif
        if (id && changed) { seen=true; at=now; }
        return true;
    }
};
} // namespace witness
