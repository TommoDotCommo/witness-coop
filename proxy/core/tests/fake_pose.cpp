#include "../remote_pose.h"
#include <algorithm>
#include <cstdio>

int main() {
    RemotePose p;
    std::fill_n(p.name, sizeof p.name, 'N');
    p.pingMs = 123; p.emote = 7; p.emoteSeq = 8; p.typing = p.speaking = p.proximitySpeaking = true;
    const float pos[3] = {1.0f, -2.0f, 3.0f};
    FillFakeRemote(p, pos, 0.75f, 42);
    bool safe = p.valid && p.id == 99 && p.seq == 42 && p.ageMs == 0 &&
                p.pos[0] == 1.0f && p.pos[1] == -2.0f && p.pos[2] == 3.0f &&
                p.ang[0] == 0.75f && p.ang[1] == 0 && p.pingMs == -1 &&
                p.emote == 0 && p.emoteSeq == 0 && !p.typing && !p.speaking && !p.proximitySpeaking &&
                std::all_of(p.name, p.name + sizeof p.name, [](char c) { return c == 0; });
    std::printf("fake pose metadata: %s\n", safe ? "ok" : "FAILED");
    return safe ? 0 : 1;
}
