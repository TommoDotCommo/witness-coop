// Prints how loud a rendered footstep is at several distances next to speech through the same distance curve (a measurement, not a test):
// make -C proxy/voice levels. Speech reference: a talker at -20 dBFS RMS (0.10) with a 12 dB crest factor, a typical normalised microphone.
#include "footsteps.h"
#include <cmath>
#include <cstdio>
static double Db(double x) { return 20.0 * std::log10(std::max(x, 1e-9)); }
int main() {
    wvoice::Settings s; s.range = 25; s.volume = 1;
    printf("%-6s %-5s %9s %9s %12s %12s %10s\n", "dist", "gait", "peak", "peak dBFS", "20ms-rms dB", "speech rms dB", "step-speech");
    for (float d : {1.0f, 3.0f, 6.0f, 12.0f, 20.0f}) for (bool run : {false, true}) {
        wvoice::Foley f; f.Plant(d, 0, run ? 3.0f : 1.8f, run, s);
        float pk = 0, best = 0;
        for (int k = 0; k < 20; k++) { float st[wvoice::kFrame * 2] = {}; f.Render(st, wvoice::kFrame); double e = 0; for (int i = 0; i < wvoice::kFrame; i++) { e += st[i * 2] * st[i * 2]; pk = std::max(pk, std::fabs(st[i * 2])); } best = std::max(best, (float)std::sqrt(e / wvoice::kFrame)); }
        wvoice::Mix m = wvoice::ComputeMix(d, 0, s); double speech = 0.10 * m.prox;
        printf("%-6.0f %-5s %9.4f %9.1f %12.1f %12.1f %+10.1f\n", d, run ? "run" : "walk", pk, Db(pk), Db(best), Db(speech), Db(best) - Db(speech));
        if (f.played == 0) printf("   (dropped: silent)\n");
    }
}
