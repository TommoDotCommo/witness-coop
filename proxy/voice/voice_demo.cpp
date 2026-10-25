// Writes WAV files of what the mixer makes of one voice at different distances, for listening: voice_demo <folder>
// The voice is a synthetic one (a vowel-like buzz with a moving pitch and syllable pauses), run through the codec and the real mixer.
#include "voice.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
using namespace wvoice;
static void Wav(const std::string& path, const std::vector<int16_t>& s) {
    FILE* f = fopen(path.c_str(), "wb"); if (!f) return; uint32_t n = (uint32_t)s.size() * 2, rate = kRate, br = kRate * 4; uint16_t ch = 2, bits = 16, ba = 4, fmt = 1; uint32_t sz = 16, tot = 36 + n;
    fwrite("RIFF", 1, 4, f); fwrite(&tot, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&sz, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&n, 4, 1, f); fwrite(s.data(), 2, s.size(), f); fclose(f);
}
static std::vector<int16_t> g_speech;      // a recording given on the command line: the voice is that, repeated
static void Voice(uint64_t* t, int16_t* pcm) {
    if (!g_speech.empty()) { for (int i = 0; i < kFrame; ++i, ++*t) pcm[i] = g_speech[*t % g_speech.size()]; return; }      // a buzz with formants, syllables of 0.3 s with short pauses, pitch gliding between 110 and 160 Hz
    for (int i = 0; i < kFrame; ++i, ++*t) {
        double sec = double(*t) / kRate, syl = std::fmod(sec, 0.45), env = syl < 0.32 ? std::sin(3.14159 * syl / 0.32) : 0.0, f0 = 135 + 25 * std::sin(sec * 3.1), ph = 6.2831853 * f0 * sec, x = 0;
        for (int h = 1; h <= 30; ++h) { double fr = f0 * h, g = std::exp(-std::pow((fr - 700) / 300, 2)) + 0.7 * std::exp(-std::pow((fr - 1220) / 350, 2)) + 0.3 * std::exp(-std::pow((fr - 2600) / 500, 2)); x += g * std::sin(ph * h) / h; }
        pcm[i] = int16_t(std::max(-30000.0, std::min(30000.0, x * env * 9000)));
    }
}
static void Render(const std::string& folder, const char* name, float dist, float bearing, int seconds, bool endWithRelease) {
    Mixer m; Settings s; m.Configure(s); Adpcm enc; uint64_t t = 0; unsigned seq = 0; std::vector<int16_t> out; int frames = seconds * 50;
    for (int f = 0; f < frames; ++f) {
        int16_t pcm[kFrame]; Voice(&t, pcm); Packet p; p.from = 2; p.seq = seq++; p.flags = f == 0 ? kStart : (f == frames - 1 && endWithRelease ? kEnd : 0); Encode(pcm, &enc, p.payload); m.Push(p);
        float st[kFrame * 2]; m.Render({{2, dist, bearing}}, st); for (int i = 0; i < kFrame * 2; ++i) out.push_back(int16_t(st[i] * 30000));
    }
    for (int f = 0; f < 20; ++f) { float st[kFrame * 2]; m.Render({{2, dist, bearing}}, st); for (int i = 0; i < kFrame * 2; ++i) out.push_back(int16_t(st[i] * 30000)); }      // the tail: the squelch
    Wav(folder + "/" + (g_speech.empty() ? "" : "speech_") + name + ".wav", out); printf("%s\n", name);
}
static void Walk(const std::string& folder) {      // someone walks away from the listener along a line: 3 m to 70 m over 12 s, from the right: close, fading, radio
    Mixer m; Settings s; m.Configure(s); Adpcm enc; uint64_t t = 0; std::vector<int16_t> out; int frames = 12 * 50;
    for (int f = 0; f < frames; ++f) {
        int16_t pcm[kFrame]; Voice(&t, pcm); Packet p; p.from = 2; p.seq = f; p.flags = f == 0 ? kStart : (f == frames - 1 ? kEnd : 0); Encode(pcm, &enc, p.payload); m.Push(p);
        float d = 3.0f + 67.0f * float(f) / frames, st[kFrame * 2]; m.Render({{2, d, 1.2f}}, st); for (int i = 0; i < kFrame * 2; ++i) out.push_back(int16_t(st[i] * 30000));
    }
    for (int f = 0; f < 20; ++f) { float st[kFrame * 2]; m.Render({{2, 70.0f, 1.2f}}, st); for (int i = 0; i < kFrame * 2; ++i) out.push_back(int16_t(st[i] * 30000)); }
    Wav(folder + "/" + (g_speech.empty() ? "" : "speech_") + "voice_walks_away.wav", out); printf("voice_walks_away\n");
}
int main(int argc, char** argv) {      // voice_demo <folder> [speech.wav]
    std::string folder = argc > 1 ? argv[1] : ".";
    if (argc > 2 && !LoadWav(argv[2], &g_speech)) { printf("cannot read %s\n", argv[2]); return 1; }
    std::string tag = g_speech.empty() ? "" : "speech_"; (void)tag;
    Render(folder, "voice_3m_in_front", 3, 0, 4, true); Render(folder, "voice_10m_to_the_right", 10, 1.5708f, 4, true); Render(folder, "voice_22m_to_the_left", 22, -1.5708f, 4, true);
    Render(folder, "voice_28m_the_fade", 28, 0.5f, 4, true); Render(folder, "voice_80m_radio", 80, 0.5f, 4, true); Walk(folder); return 0;
}
