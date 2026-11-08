#pragma once
// The voice display in the game's lower left (owner, 2026-11-08: no permanent "hold B to talk"): only what is happening, nothing at all otherwise.
//   my microphone, while it is live: open mic shows "Open mic" for as long as that is the mode (the microphone is on), lit while my voice goes out;
//                                    push to talk shows "Mic on" only while I talk; no row with voice off
//   a row for each player I can hear right now: talking, and their voice reaches me (proximity or radio above a whisper, not muted by me), in their colour
// Pure: the drawing is proxy/social.cpp; game-free test proxy/ui/voice_hud_test.cpp (make -C proxy/ui test).
#include <string>
#include <vector>

namespace wvhud {
struct Heard { unsigned id = 0; std::string name; bool talking = false; float prox = 0, radio = 0, playerVolume = 1; };
struct Row { bool me = false; unsigned id = 0; std::string text; bool lit = false; };
constexpr float kAudible = 0.02f;      // a mixer gain below this is not heard (the fade at the edge of the range)
// mode: 0 voice off, 1 open mic, 2 push to talk (VoiceModeNow); channel: what I talk on ("Proximity" is the usual one and is not named)
inline std::vector<Row> Rows(int mode, bool transmitting, const std::string& channel, const std::vector<Heard>& heard) {
    std::vector<Row> r;
#ifdef VH_MUT_ALWAYS_HINT
    if (mode == 2 && !transmitting) r.push_back({true, 0, "Mic: hold to talk", false});      // (mutant: the old permanent hint)
#endif
    if (mode == 1) r.push_back({true, 0, "Open mic", transmitting});
    else if (mode == 2 && transmitting) r.push_back({true, 0, "Mic on", true});
    if (!r.empty() && r.back().me && !channel.empty() && channel != "Proximity") r.back().text += "  -  " + channel;
    for (const auto& h : heard) {
        if (!h.talking || h.playerVolume <= 0) continue;
#ifndef VH_MUT_HEAR_ALL
        if (h.prox < kAudible && h.radio < kAudible) continue;      // talking, but too far to reach me
#endif
        r.push_back({false, h.id, h.radio > h.prox ? h.name + "  (radio)" : h.name, true});
    }
    return r;
}
}  // namespace wvhud
