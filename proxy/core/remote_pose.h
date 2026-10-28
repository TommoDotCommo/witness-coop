#pragma once
#include <cstdint>
#include "appearance.h"
#include "emotes.h"

// Complete snapshot consumed by rendering and social UI. Empty metadata is
// represented by terminated empty strings and an unknown ping.
struct RemotePose {
    witness::Appearance appearance;
    bool valid = false;
    float pos[3] = {}, ang[2] = {};
    uint32_t ageMs = 0;
    unsigned seq = 0, id = 0;
    int pingMs = -1;
    char name[25] = {};
    unsigned char emote = 0, emoteSeq = 0;
    bool typing = false, speaking = false, proximitySpeaking = false, radioEquipped = false, radioTx = false; uint8_t voiceChannel = 0;
};
inline void FillFakeRemote(RemotePose& out, const float pos[3], float yaw, unsigned seq) {
#ifndef WM_MUT_FAKE_METADATA
    out = RemotePose{};
#endif
    out.valid = true;
    for (int i = 0; i < 3; ++i) out.pos[i] = pos[i];
    out.ang[0] = yaw; out.ang[1] = 0;
    out.ageMs = 0; out.seq = seq; out.id = 99;
}
