// Game-free (T0) test of the voice code: the codec survives loss, the mix is smooth and goes over to the radio, the radio sounds like one, packets are checked, order and loss are handled.
#include "voice.h"
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace wvoice;
static int fails = 0, checks = 0;
static void check(const char* what, bool ok) { ++checks; if (!ok) { ++fails; printf("FAILED: %s\n", what); } }

static void Voice(int frame, int16_t* pcm) {      // something speech-like: three partials with a slow envelope
    for (int i = 0; i < kFrame; ++i) { double t = double(frame * kFrame + i) / kRate, env = 0.6 + 0.4 * std::sin(6.2831853 * 3.0 * t);
        pcm[i] = int16_t(9000 * env * (std::sin(6.2831853 * 220 * t) + 0.6 * std::sin(6.2831853 * 710 * t) + 0.3 * std::sin(6.2831853 * 1830 * t)) / 1.9 * 1.9 / 1.9); }
}
static double Snr(const int16_t* a, const int16_t* b, int n) { double s = 0, e = 0; for (int i = 0; i < n; ++i) { s += double(a[i]) * a[i]; double d = double(a[i]) - b[i]; e += d * d; } return 10 * std::log10(s / (e + 1e-9)); }
static Packet Pk(unsigned from, unsigned seq, int frame, uint8_t flags, Adpcm* st) { Packet p; p.from = from; p.seq = seq; p.flags = flags; int16_t pcm[kFrame]; Voice(frame, pcm); Encode(pcm, st, p.payload); return p; }
static float Rms(const float* x, int n) { double e = 0; for (int i = 0; i < n; ++i) e += x[i] * x[i]; return float(std::sqrt(e / n)); }
static void ToneFrames(Mixer& m, unsigned from, int first, int count, Adpcm* st, uint64_t* sample, uint8_t firstFlags = kStart) {
    const float hz[3] = {120, 1000, 5500};
    for (int k = 0; k < count; ++k) { int16_t pcm[kFrame]; Tone(hz, 3, 0.9f, sample, pcm); Packet p; p.from = from; p.seq = first + k; p.flags = k == 0 ? firstFlags : 0; Encode(pcm, st, p.payload); m.Push(p); }
}

int main() {
    // codec
    { Adpcm st; int16_t src[40][kFrame], dec[40][kFrame]; uint8_t out[40][kPayload]; for (int f = 0; f < 40; ++f) { Voice(f, src[f]); Encode(src[f], &st, out[f]); Decode(out[f], dec[f]); }
      double worst = 1e9, worstOdd = 1e9; for (int f = 4; f < 40; ++f) worst = std::min(worst, Snr(src[f], dec[f], kFrame));
      int16_t one[kFrame]; for (int f = 5; f < 40; f += 2) { Decode(out[f], one); worstOdd = std::min(worstOdd, Snr(src[f], one, kFrame)); }      // only every other frame arrives
      check("codec: a voice comes back at better than 20 dB signal-to-noise", worst > 20);
      check("codec: a frame decodes on its own (the one before it lost) just as well", worstOdd > 20); check("codec: 20 ms is 163 bytes, a quarter of 16-bit samples", kPayload == 163); }
    // packets
    { Packet p; p.from = 3; p.seq = 77; p.flags = kStart | kPtt; for (int i = 0; i < kPayload; ++i) p.payload[i] = uint8_t(i * 7); auto b = MakePacket(p); Packet q;
      check("packet: round trip", ParsePacket(b.data(), b.size(), &q) && q.from == 3 && q.seq == 77 && q.flags == (kStart | kPtt) && !memcmp(q.payload, p.payload, kPayload));
      auto c = b; c[40] ^= 0x10; check("packet: a flipped bit is refused", !ParsePacket(c.data(), c.size(), &q));
      check("packet: another size or magic is refused", !ParsePacket(b.data(), b.size() - 1, &q) && [&] { auto d = b; d[0] = 'X'; return !ParsePacket(d.data(), d.size(), &q); }()); }
    // the mix
    { Settings s; s.range = 25; Mix a = ComputeMix(1, 0, s), b = ComputeMix(2.5f, 0, s), c = ComputeMix(5, 0, s), d = ComputeMix(10, 0, s);
      check("mix: full volume up close, half at twice the distance, a quarter at four times", std::fabs(a.prox - 1) < 1e-4 && std::fabs(b.prox - 1) < 1e-4 && std::fabs(c.prox - 0.5f) < 0.01f && std::fabs(d.prox - 0.25f) < 0.01f);
      check("mix: no radio within the range, the full radio far outside it", ComputeMix(20, 0, s).radio == 0 && ComputeMix(25, 0, s).radio == 0 && ComputeMix(100, 0, s).radio > 0.8f && ComputeMix(100, 0, s).prox == 0);
      float maxStepP = 0, maxStepR = 0, lastP = 1, lastR = 0; bool monoR = true, monoP = true;
      for (float x = 0.5f; x < 60; x += 0.1f) { Mix m = ComputeMix(x, 0, s); if (x > 0.6f) { maxStepP = std::max(maxStepP, std::fabs(m.prox - lastP)); maxStepR = std::max(maxStepR, std::fabs(m.radio - lastR)); monoR = monoR && m.radio >= lastR - 1e-6f; monoP = monoP && m.prox <= lastP + 1e-6f; } lastP = m.prox; lastR = m.radio; }
      check("mix: the proximity voice and the radio change smoothly with distance (no step above 0.05 per 10 cm)", maxStepP < 0.05f && maxStepR < 0.05f);
      check("mix: the proximity voice only ever gets quieter and the radio only louder with distance", monoP && monoR);
      check("mix: the proximity voice is gone when the radio has fully taken over", ComputeMix(s.range * 1.3f + 1, 0, s).prox == 0);
      check("mix: to the right is louder in the right ear, to the left in the left, ahead and behind in the middle", ComputeMix(5, 1.5708f, s).pan > 0.8f && ComputeMix(5, -1.5708f, s).pan < -0.8f && std::fabs(ComputeMix(5, 0, s).pan) < 0.01f && std::fabs(ComputeMix(5, 3.14159f, s).pan) < 0.01f);
      check("mix: the proximity voice gets duller with distance", ComputeMix(2, 0, s).cutoff > ComputeMix(20, 0, s).cutoff + 2000);
      Settings r = s; r.radioRange = 200; check("mix: the radio has a range of its own, fading out at its end", ComputeMix(150, 0, r).radio > 0.8f && ComputeMix(300, 0, r).radio == 0);
      Settings off = s; off.radio = false; check("mix: with the radio off a far voice is simply not heard", ComputeMix(100, 0, off).radio == 0 && ComputeMix(100, 0, off).prox == 0);
      Settings loud = s; loud.volume = 0.5f; check("mix: the volume scales both voices", std::fabs(ComputeMix(5, 0, loud).prox - 0.25f) < 0.01f && std::fabs(ComputeMix(100, 0, loud).radio - 0.425f) < 0.01f); }
    // the mixer: a voice of three tones, close and to the right
    { Mixer m; Settings s; m.Configure(s); Adpcm st; uint64_t smp = 0; ToneFrames(m, 7, 100, 60, &st, &smp); std::vector<Speaker> where = {{7, 5.0f, 1.5708f}}; float st2[kFrame * 2]; float rl = 0, rr = 0; bool talked = false;
      for (int f = 0; f < 50; ++f) { m.Render(where, st2); if (f >= 10) { float l[kFrame], r[kFrame]; for (int i = 0; i < kFrame; ++i) { l[i] = st2[2 * i]; r[i] = st2[2 * i + 1]; } rl += Rms(l, kFrame); rr += Rms(r, kFrame); } auto ss = m.Stats(); talked = talked || (ss.size() && ss[0].talking); }
      auto ss = m.Stats();
      check("mixer: a voice 5 m to the right is heard, louder in the right ear", talked && rr > 3 * rl && rr > 1.0f);
      check("mixer: close by, the whole voice is there (120 Hz, 1 kHz and 5.5 kHz)", ss[0].tone[0] > 0.02f && ss[0].tone[1] > 0.02f && ss[0].tone[2] > 0.5f * ss[0].tone[1]); }
    // far away: the radio
    { Mixer m; Settings s; m.Configure(s); Adpcm st; uint64_t smp = 0; ToneFrames(m, 7, 0, 60, &st, &smp); std::vector<Speaker> where = {{7, 80.0f, 1.5708f}}; float out[kFrame * 2]; float rl = 0, rr = 0;
      for (int f = 0; f < 50; ++f) { m.Render(where, out); if (f >= 10) { float l[kFrame], r[kFrame]; for (int i = 0; i < kFrame; ++i) { l[i] = out[2 * i]; r[i] = out[2 * i + 1]; } rl += Rms(l, kFrame); rr += Rms(r, kFrame); } }
      auto ss = m.Stats(); float low = ss[0].tone[0], mid = ss[0].tone[1], high = ss[0].tone[2];
      check("radio: a voice 80 m away is still heard", mid > 0.05f && ss[0].radio > 0.8f && ss[0].prox == 0);
      check("radio: it sounds like a walkie-talkie: 120 Hz and 5.5 kHz at least 12 dB below 1 kHz", low < 0.25f * mid && high < 0.25f * mid);
      check("radio: it comes from the middle, whatever side the player is on", std::fabs(rl - rr) < 0.1f * (rl + rr)); }
    // transmissions: start, end, squelch
    { Mixer m; Settings s; m.Configure(s); Adpcm st; uint64_t smp = 0; ToneFrames(m, 7, 0, 10, &st, &smp);
      { Packet e; e.from = 7; e.seq = 10; e.flags = kEnd; int16_t z[kFrame] = {}; Encode(z, &st, e.payload); m.Push(e); }
      std::vector<Speaker> where = {{7, 80.0f, 0}}; float out[kFrame * 2]; int talkingFrames = 0, afterEnd = 0; float tailEnergy = 0;
      for (int f = 0; f < 40; ++f) { m.Render(where, out); auto ss = m.Stats(); if (ss[0].talking) ++talkingFrames; else { ++afterEnd; if (afterEnd <= 4) tailEnergy += Rms(out, kFrame * 2); } }
      check("transmission: it is over after the end frame, not before", talkingFrames >= 9 && talkingFrames <= 12);
      check("transmission: the end of a radio transmission has a short squelch of noise, then silence", tailEnergy > 0.002f && [&] { m.Render(where, out); m.Render(where, out); return Rms(out, kFrame * 2) < 1e-6f; }()); }
    // order and loss
    { Adpcm st; uint64_t smp = 0; const float hz[1] = {440}; Packet pk[30]; for (int k = 0; k < 30; ++k) { int16_t pcm[kFrame]; Tone(hz, 1, 0.5f, &smp, pcm); pk[k].from = 7; pk[k].seq = k; pk[k].flags = k == 0 ? kStart : 0; Encode(pcm, &st, pk[k].payload); }
      auto play = [&](const std::vector<int>& order, std::vector<float>* rms) { Mixer m; Settings s; m.Configure(s); for (int i : order) m.Push(pk[i]); std::vector<Speaker> w = {{7, 1.0f, 0}}; float out[kFrame * 2]; for (int f = 0; f < 30; ++f) { m.Render(w, out); rms->push_back(Rms(out, kFrame * 2)); } return m.Stats()[0]; };
      std::vector<int> inOrder, swapped, lossy; for (int k = 0; k < 30; ++k) { inOrder.push_back(k); swapped.push_back(k); if (k != 10) lossy.push_back(k); } std::swap(swapped[1], swapped[2]);
      std::vector<float> a, b, c; play(inOrder, &a); play(swapped, &b); auto sl = play(lossy, &c); int loud = 0; for (float x : a) loud += x > 0.01f;
      check("jitter: all 30 frames of a transmission are played", loud >= 28);
      check("jitter: two frames that arrive swapped sound exactly like the ordered ones", a == b);
      check("jitter: a lost frame is concealed (counted, quieter than its neighbours, not silent)", sl.concealed == 1 && c[10 + 3] < c[8 + 3] && c[10 + 3] > 0.0f);
      check("jitter: a frame that comes after its place was played is dropped", [&] { Mixer m; Settings s; m.Configure(s); for (int i = 0; i < 12; ++i) m.Push(pk[i]); std::vector<Speaker> w = {{7, 1.0f, 0}}; float out[kFrame * 2]; for (int f = 0; f < 8; ++f) m.Render(w, out); m.Push(pk[1]); return m.Stats()[0].dropped == 1; }()); }
    // the microphone gate
    { Vad v; int16_t quiet[kFrame] = {}, loud[kFrame]; for (int i = 0; i < kFrame; ++i) loud[i] = int16_t(8000 * std::sin(i * 0.3)); for (int i = 0; i < kFrame; i += 2) quiet[i] = 20;
      check("vad: silence is not speech", !v.Feed(quiet)); check("vad: a loud frame is", v.Feed(loud));
      int held = 0; for (int i = 0; i < 40; ++i) if (v.Feed(quiet)) ++held; check("vad: speech is held for about 300 ms after it ends", held >= 13 && held <= 15); }
    printf("%d checks, %d failed\n", checks, fails); return fails ? 1 : 0;
}
