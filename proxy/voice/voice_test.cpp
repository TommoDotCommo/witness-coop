// Game-free (T0) test of the voice code: the codec survives loss, the mix is smooth and goes over to the radio, the radio sounds like one, packets are checked, order and loss are handled.
#include "voice.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
    // channels: who hears a transmission and how
    { Packet p; p.from = 3; p.seq = 1; p.flags = kStart; p.channel = kDirect; p.to = 9; auto b = MakePacket(p); Packet q;
      check("channel: the channel and the addressee travel in the packet", ParsePacket(b.data(), b.size(), &q) && q.channel == kDirect && q.to == 9);
      auto c = b; c[13] = 9; Packet z; check("channel: a channel that does not exist is refused", !ParsePacket(c.data(), c.size(), &z) || z.channel <= kDirect);
      Channel ch; unsigned to = 0; check("channel: names parse (proximity, radio, global, direct:N)", ParseChannel("proximity", &ch, &to) && ch == kProximity && ParseChannel("radio", &ch, &to) && ch == kRadio && ParseChannel("global", &ch, &to) && ch == kGlobal && ParseChannel("direct:4", &ch, &to) && ch == kDirect && to == 4 && !ParseChannel("direct:0", &ch, &to) && !ParseChannel("loud", &ch, &to)); }
    { Settings s; s.range = 25; s.volume = 0.8f;
      Mix rn = ComputeMix(2, 1.0f, s, kRadio), rf = ComputeMix(900, 1.0f, s, kRadio), gn = ComputeMix(2, 1.0f, s, kGlobal), gf = ComputeMix(900, -1.0f, s, kGlobal), dn = ComputeMix(2, 0, s, kDirect), df = ComputeMix(900, 0, s, kDirect);
      check("channel radio: as loud next to you as 900 m away, over the radio only, from the middle", rn.radio > 0.6f && rn.prox == 0 && std::fabs(rn.radio - rf.radio) < 1e-5f && rf.prox == 0 && rn.pan == 0 && rf.pan == 0);
      check("channel global: as loud next to you as 900 m away, as it is (no radio), from the middle", gn.prox > 0.79f && gn.radio == 0 && std::fabs(gn.prox - gf.prox) < 1e-5f && gf.pan == 0 && gf.cutoff > 7000);
      check("channel direct: sounds like the radio at any distance", dn.radio > 0.6f && dn.prox == 0 && df.radio == dn.radio);
      Settings off = s; off.radio = false; check("channel radio: still heard when the proximity radio is off (it is the channel the speaker chose)", ComputeMix(900, 0, off, kRadio).radio > 0.6f);
      check("channel proximity: the default is unchanged (6 dB per doubling, radio far away)", std::fabs(ComputeMix(5, 0, s).prox - 0.4f) < 0.01f && ComputeMix(100, 0, s).radio > 0.6f); }
    { auto heard = [&](Channel ch, unsigned to, unsigned self, float dist, SpeakerStat* out) { Mixer m; Settings s; s.self = self; m.Configure(s); Adpcm st; uint64_t smp = 0; const float hz[3] = {120, 1000, 5500};
        for (int k = 0; k < 60; ++k) { int16_t pcm[kFrame]; Tone(hz, 3, 0.9f, &smp, pcm); Packet p; p.from = 7; p.seq = k; p.flags = k == 0 ? kStart : 0; p.channel = ch; p.to = uint8_t(to); Encode(pcm, &st, p.payload); m.Push(p); }
        std::vector<Speaker> where = {{7, dist, 1.5708f}}; float o[kFrame * 2]; float rl = 0, rr = 0; for (int f = 0; f < 50; ++f) { m.Render(where, o); if (f >= 10) { float l[kFrame], r[kFrame]; for (int i = 0; i < kFrame; ++i) { l[i] = o[2 * i]; r[i] = o[2 * i + 1]; } rl += Rms(l, kFrame); rr += Rms(r, kFrame); } }
        auto ss = m.Stats(); *out = ss.empty() ? SpeakerStat() : ss[0]; return rl + rr; };
      SpeakerStat a, b, c, d, e; float lp = heard(kProximity, 0, 2, 500.0f, &a), lr = heard(kRadio, 0, 2, 500.0f, &b), lg = heard(kGlobal, 0, 2, 500.0f, &c), ld = heard(kDirect, 2, 2, 500.0f, &d), lx = heard(kDirect, 5, 2, 500.0f, &e);
      check("channel mixer: a proximity voice 500 m away is on the radio too (the old behaviour)", lp > 0.5f && a.radio > 0.8f);
      check("channel mixer: a radio-channel voice at 500 m is heard over the radio: 120 Hz and 5.5 kHz 12 dB below 1 kHz", lr > 0.5f && b.channel == kRadio && b.radio > 0.8f && b.tone[0] < 0.25f * b.tone[1] && b.tone[2] < 0.25f * b.tone[1]);
      check("channel mixer: a global voice at 500 m is whole: all three tones there, no radio", lg > 1.0f && c.channel == kGlobal && c.radio == 0 && c.prox > 0.9f && c.tone[0] > 0.02f && c.tone[2] > 0.5f * c.tone[1]);
      check("channel mixer: a direct voice to me at 500 m is heard over the radio", ld > 0.5f && d.channel == kDirect && d.tone[0] < 0.25f * d.tone[1]);
      check("channel mixer: a direct voice to somebody else is not heard at all", lx == 0.0f && e.packets == 0);
      SpeakerStat f; check("channel mixer: a listener who does not know its own id hears direct voices (no filter without an id)", heard(kDirect, 5, 0, 500.0f, &f) > 0.5f); }
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
    // real speech (when SPEECH_WAV names a recording: tools/make_speech.sh makes one with the Windows voice): it survives the codec, and what is heard keeps its rhythm near and far
    if (const char* wav = getenv("SPEECH_WAV")) {
        std::vector<int16_t> sp; check("speech: the recording loads", LoadWav(wav, &sp) && sp.size() > kFrame * 100);
        if (sp.size() > kFrame * 100) {
            int frames = int(sp.size() / kFrame); Adpcm enc; std::vector<float> envIn, envNear, envFar; double snrSum = 0; int snrN = 0;
            Mixer near_, far_; Settings s0; near_.Configure(s0); far_.Configure(s0);
            for (int f = 0; f < frames; ++f) {
                Packet p; p.from = 3; p.seq = f; p.flags = f == 0 ? kStart : 0; Encode(&sp[f * kFrame], &enc, p.payload); near_.Push(p); far_.Push(p);
                int16_t dec[kFrame]; Decode(p.payload, dec); double sa = 0; for (int i = 0; i < kFrame; ++i) sa += double(sp[f * kFrame + i]) * sp[f * kFrame + i]; if (sa / kFrame > 1e5) { snrSum += Snr(&sp[f * kFrame], dec, kFrame); ++snrN; }
                float e = 0; for (int i = 0; i < kFrame; ++i) e += float(sp[f * kFrame + i]) * sp[f * kFrame + i]; envIn.push_back(std::sqrt(e / kFrame) / 32768.0f);
            }
            for (int f = 0; f < frames + 6; ++f) { float o[kFrame * 2]; near_.Render({{3, 5.0f, 0}}, o); envNear.push_back(Rms(o, kFrame * 2)); far_.Render({{3, 80.0f, 0}}, o); envFar.push_back(Rms(o, kFrame * 2)); }
            auto corr = [&](const std::vector<float>& a, int lag) { double ma = 0, mb = 0; int n = frames - 8; for (int i = 0; i < n; ++i) { ma += envIn[i]; mb += a[i + lag]; } ma /= n; mb /= n; double sab = 0, saa = 0, sbb = 0; for (int i = 0; i < n; ++i) { double x = envIn[i] - ma, y = a[i + lag] - mb; sab += x * y; saa += x * x; sbb += y * y; } return sab / std::sqrt(saa * sbb + 1e-12); };
            double bestN = -1, bestF = -1; for (int lag = 0; lag <= 5; ++lag) { bestN = std::max(bestN, corr(envNear, lag)); bestF = std::max(bestF, corr(envFar, lag)); }
            printf("  speech: %d s, codec %.1f dB, loudness rhythm kept: near %.3f, radio %.3f\n", frames / 50, snrN ? snrSum / snrN : 0.0, bestN, bestF);
            check("speech: the codec keeps it above 15 dB signal-to-noise", snrN > 20 && snrSum / snrN > 15);
            check("speech: heard 5 m away it keeps its rhythm (loudness correlates above 0.95 with what was said)", bestN > 0.95);
            check("speech: heard over the radio it keeps its rhythm too (above 0.85)", bestF > 0.85);
        }
    }
    printf("%d checks, %d failed\n", checks, fails); return fails ? 1 : 0;
}
