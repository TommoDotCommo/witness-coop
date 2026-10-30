#pragma once
// Footsteps of the other players, game-free: a tracker turns a figure's gait phase into foot plants, and a small Foley synthesizer turns each
// plant into a short burst (a soft thud under a crisp scuff) placed in the world like a proximity voice: quieter with distance, from the side
// the walker is on, gone beyond the voice range. The render thread reports steps (footsteps.h knows nothing of threads); the voice playback
// thread renders them into its 20 ms stereo frames beside the voices (voice_audio.cpp). Mutants (-DWV_MUT_FS_...) break one thing each.
#include "voice.h"
#include <cmath>
#include <cstdint>
#include <vector>

namespace wvoice {

// Turns the walk or run cycle's phase (0..1, two steps a cycle: the left foot lands at 0, the right at 0.5) into foot plants, forwards or in reverse.
struct FootstepTracker {
    float last = -1; bool moving = false;
    // Returns the number of plants crossed since the previous call (0, 1, or 2 after a long frame); *foot: 0 left, 1 right (the last one crossed).
    int Step(float phase, bool isMoving, int* foot) {
        int n = 0;
#ifdef WV_MUT_FS_NO_STEPS
        (void)phase; (void)isMoving; (void)foot; return 0;
#endif
        phase -= std::floor(phase);
        if (!isMoving) { moving = false; last = -1; return 0; }
        if (!moving || last < 0) { moving = true; last = phase; return 0; }      // a walk begins: the first plant comes with the first crossing, not at once
        float d = phase - last; if (d > 0.5f) d -= 1.0f; if (d < -0.5f) d += 1.0f;      // the shortest way round: forward or reverse
        float a = last, b = last + d;
        for (float mark : {0.0f, 0.5f}) {
            for (int k = -2; k <= 2; k++) {
                float m = mark + k;
                if ((a < m && m <= b) || (b <= m && m < a)) { n++; if (foot) *foot = mark > 0.25f ? 1 : 0; }
            }
        }
        last = phase; return n > 2 ? 2 : n;
    }
};

// One footfall being played: a burst of filtered noise with a fast attack and an exponential decay, placed by distance and bearing.
struct Footfall { float gain = 0, pan = 0, cutoff = 3000, decay = 0.985f, t = 0; float env = 0, lp = 0, bp = 0; uint32_t rng = 1; int left = 0; bool run = false; };

struct Foley {
    std::vector<Footfall> active; unsigned played = 0, dropped = 0; float volume = 1.0f;
    // A plant by a player `dist` metres away at `bearing` (radians clockwise from straight ahead), at `speed` m/s. `s` is the voice settings (range, volume).
    void Plant(float dist, float bearing, float speed, bool run, const Settings& s) {
        Mix m = ComputeMix(dist, bearing, s, kProximity);
#ifdef WV_MUT_FS_NO_DISTANCE
        m.prox = s.volume;
#endif
#ifdef WV_MUT_FS_NO_PAN
        m.pan = 0;
#endif
        if (m.prox <= 0.002f) { dropped++; return; }
        if (active.size() >= 12) { dropped++; return; }
        Footfall f; f.run = run; f.pan = m.pan;
        float loud = run ? 1.0f : 0.55f + 0.25f * std::min(1.0f, speed / 2.0f);      // a run lands harder
        f.gain = 0.22f * loud * m.prox * volume;
        f.cutoff = std::min(m.cutoff, run ? 3600.0f : 2600.0f);
        f.decay = run ? 0.998f : 0.9975f;       // per sample at 16 kHz: the thud falls 60 dB in about 170 ms (walk) to 215 ms (run)
#ifdef WV_MUT_FS_NO_DECAY
        f.decay = 1.0f;
#endif
        f.left = run ? 3600 : 3000;             // samples of life
        f.rng = 2463534242u ^ (uint32_t)(played * 2654435761u + 7); if (!f.rng) f.rng = 1;
        active.push_back(f); played++;
    }
    // Adds `frames` stereo samples (interleaved, -1..1) into `stereo`, which already holds the voices.
    void Render(float* stereo, int frames) {
        for (auto& f : active) {
            float p = (f.pan + 1.0f) * 0.78539816f, gl = std::cos(p), gr = std::sin(p);
            float a = 1.0f - std::exp(-6.2831853f * f.cutoff / (float)kRate);
            for (int i = 0; i < frames && f.left > 0; i++, f.left--) {
                f.rng ^= f.rng << 13; f.rng ^= f.rng >> 17; f.rng ^= f.rng << 5; float n = ((f.rng & 0xFFFF) / 32768.0f) - 1.0f;
                // the envelope: 2 ms attack to 1, then the decay; the thud is the low-passed noise, the scuff a touch of the raw noise early on
                if (f.t < 32) f.env = f.t / 32.0f; else f.env *= f.decay;
                f.t += 1;
                f.lp += a * (n - f.lp);
                float scuff = f.t < 400 ? n * 0.25f * (1.0f - f.t / 400.0f) : 0.0f;
                float x = (f.lp * 1.6f + scuff) * f.env * f.gain;
                stereo[i * 2] += x * gl; stereo[i * 2 + 1] += x * gr;
            }
        }
        for (size_t i = 0; i < active.size();) { if (active[i].left <= 0 || active[i].env < 1e-4f) active.erase(active.begin() + i); else i++; }
        for (int i = 0; i < frames * 2; i++) { if (stereo[i] > 1.0f) stereo[i] = 1.0f; if (stereo[i] < -1.0f) stereo[i] = -1.0f; }      // the voices and the steps never clip the output
    }
};

}  // namespace wvoice
