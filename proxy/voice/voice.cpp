// See voice.h.
#include "voice.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace wvoice {
static const int kStep[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int kIdx[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
static int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void Encode(const int16_t* pcm, Adpcm* st, uint8_t* out) {
    out[0] = uint8_t(st->pred & 0xFF); out[1] = uint8_t((st->pred >> 8) & 0xFF); out[2] = uint8_t(st->index);      // the state at the start of this frame
    int pred = st->pred, index = st->index;
    for (int i = 0; i < kFrame; ++i) {
        int step = kStep[index], diff = pcm[i] - pred, code = 0;
        if (diff < 0) { code = 8; diff = -diff; }
        int d = step >> 3; if (diff >= step) { code |= 4; diff -= step; d += step; } step >>= 1; if (diff >= step) { code |= 2; diff -= step; d += step; } step >>= 1; if (diff >= step) { code |= 1; d += step; }
        pred = Clamp(code & 8 ? pred - d : pred + d, -32768, 32767); index = Clamp(index + kIdx[code], 0, 88);
        if (i & 1) out[3 + i / 2] |= uint8_t(code << 4); else out[3 + i / 2] = uint8_t(code);
    }
    st->pred = pred; st->index = index;
}
void Decode(const uint8_t* in, int16_t* pcm) {
    int pred = int16_t(in[0] | in[1] << 8), index = Clamp(in[2], 0, 88);
#ifdef WV_MUT_CODEC_STATELESS
    pred = 0; index = 0;
#endif
    for (int i = 0; i < kFrame; ++i) {
        int code = (i & 1) ? (in[3 + i / 2] >> 4) : (in[3 + i / 2] & 15), step = kStep[index], d = step >> 3;
        if (code & 4) d += step; if (code & 2) d += step >> 1; if (code & 1) d += step >> 2;
        pred = Clamp(code & 8 ? pred - d : pred + d, -32768, 32767); index = Clamp(index + kIdx[code], 0, 88); pcm[i] = int16_t(pred);
    }
}

static uint32_t Fnv(const uint8_t* d, size_t n) { uint32_t h = 2166136261u; for (size_t i = 0; i < n; ++i) { h ^= d[i]; h *= 16777619u; } return h; }
std::vector<uint8_t> MakePacket(const Packet& p) {
    std::vector<uint8_t> b = {'W', 'T', 'O', '5'}; auto put = [&](unsigned v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); };
    put(p.from); put(p.seq); b.push_back(p.flags); b.push_back(p.channel); b.push_back(p.to); b.push_back(kPayload); b.insert(b.end(), p.payload, p.payload + kPayload); uint32_t h = Fnv(b.data(), b.size()); put(h); return b;
}
bool ParsePacket(const uint8_t* d, size_t n, Packet* p) {
    if (n != 4 + 4 + 4 + 1 + 1 + 1 + 1 + kPayload + 4 || memcmp(d, "WTO5", 4)) return false;
    auto get = [&](size_t at) { return unsigned(d[at]) | unsigned(d[at + 1]) << 8 | unsigned(d[at + 2]) << 16 | unsigned(d[at + 3]) << 24; };
#ifndef WV_MUT_NO_CHECKSUM
    if (Fnv(d, n - 4) != get(n - 4)) return false;
#endif
    if (d[15] != kPayload || d[13] > kDirect) return false;
    p->from = get(4); p->seq = get(8); p->flags = d[12]; p->channel = d[13]; p->to = d[14]; memcpy(p->payload, d + 16, kPayload);
#ifdef WV_MUT_CH_NOCHANNEL
    p->channel = kProximity; p->to = 0;
#endif
    return true;
}

// ---- proximity and radio
static float Smooth(float x) { x = x < 0 ? 0 : (x > 1 ? 1 : x); return x * x * (3 - 2 * x); }
const char* ChannelName(Channel c) { return c == kRadio ? "radio" : c == kGlobal ? "global" : c == kDirect ? "direct" : "proximity"; }
bool ParseChannel(const char* t, Channel* c, unsigned* to) {
    if (!strcmp(t, "proximity") || !strcmp(t, "prox")) { *c = kProximity; return true; }
    if (!strcmp(t, "radio")) { *c = kRadio; return true; }
    if (!strcmp(t, "global") || !strcmp(t, "open")) { *c = kGlobal; return true; }
    unsigned id = 0; if (sscanf(t, "direct:%u", &id) == 1 && id > 0 && id < 256) { *c = kDirect; *to = id; return true; }
    return false;
}
Mix ComputeMix(float dist, float bearing, const Settings& s, Channel ch) {
#ifdef WV_MUT_CH_IGNORED
    ch = kProximity;
#endif
    if (ch == kRadio || ch == kDirect) {      // over the radio at any distance, from the middle
        Mix r; r.pan = 0; r.cutoff = 8000.0f;
#ifdef WV_MUT_CH_RADIO_CLEAN
        r.prox = s.volume; return r;
#endif
        r.radio = 0.85f * s.volume; return r;
    }
    if (ch == kGlobal) {      // as it is, whatever the distance
#ifndef WV_MUT_CH_GLOBAL_DIST
        Mix g; g.prox = s.volume; g.pan = 0; g.cutoff = 8000.0f; return g;
#endif
    }
    Mix m; const float near = 2.5f, R = std::max(s.range, 3.0f), fade = std::max(6.0f, 0.3f * R);
    float inv = dist <= near ? 1.0f : near / dist;                                    // inverse distance: 6 dB per doubling
    float taper = 1.0f - Smooth((dist - 0.75f * R) / (R + fade - 0.75f * R));          // gone by the end of the fade
#ifdef WV_MUT_NO_TAPER
    taper = 1.0f;
#endif
    m.prox = inv * taper * s.volume;
    float r = Smooth((dist - R) / fade);                                               // 0 inside the range .. 1 once the fade is over
#ifdef WV_MUT_RADIO_HARD
    r = dist > R ? 1.0f : 0.0f;
#endif
    m.radio = s.radio ? r * 0.85f * s.volume : 0.0f;
    if (s.radio && s.radioRange > 0 && dist > s.radioRange) m.radio *= 1.0f - Smooth((dist - s.radioRange) / (0.1f * s.radioRange + 1.0f));
    m.pan = std::sin(bearing) * 0.9f;                                                  // ahead and behind: the middle; to the right: right
#ifdef WV_MUT_NO_PAN
    m.pan = 0;
#endif
    m.cutoff = 8000.0f - 5500.0f * Smooth(dist / R);
    return m;
}

// ---- one voice
static void Butter(float fc, bool high, float* b0, float* b1, float* b2, float* a1, float* a2) {
    const float pi = 3.14159265f, w = std::tan(pi * fc / kRate), q = 0.70710678f, n = 1.0f / (1.0f + w / q + w * w);
    if (high) { *b0 = n; *b1 = -2 * n; *b2 = n; } else { *b0 = w * w * n; *b1 = 2 * w * w * n; *b2 = w * w * n; }
    *a1 = 2 * (w * w - 1) * n; *a2 = (1 - w / q + w * w) * n;
}
void Mixer::Push(const Packet& p) {
#ifndef WV_MUT_CH_DIRECT_ALL
    if (p.channel == kDirect && s_.self && p.to != s_.self) return;      // said to somebody else
#endif
    Voice& v = v_[p.from]; v.stat.id = p.from; v.stat.packets++;
    if (v.started && int(p.seq - v.next) < 0) { v.stat.dropped++; return; }      // too late: its place has been played
    if (!v.started) { v.next = p.seq; v.started = true; v.fill = 0; }
    v.queue[p.seq] = p;
    if (v.queue.size() > 12) { v.next = v.queue.begin()->first; }               // a burst: skip ahead rather than grow a delay
}
void Mixer::Frame(Voice& v, int16_t* pcm) {      // the next 20 ms of this speaker: from the queue, or concealed, or silence
    if (v.talking == false && v.queue.empty()) { memset(pcm, 0, kFrame * 2); return; }
    if (!v.talking) {      // wait for 3 frames (60 ms) before starting a transmission: a little room for jitter
        if (v.queue.size() < 3 && !(v.queue.size() && (v.queue.begin()->second.flags & kEnd))) { memset(pcm, 0, kFrame * 2); return; }
        v.talking = true; v.next = v.queue.begin()->first; v.squelch = 960; v.squelchStart = true;
    }
    auto it = v.queue.find(v.next);
    if (it != v.queue.end()) {
        Decode(it->second.payload, pcm); memcpy(v.last, pcm, sizeof v.last); v.silentFrames = 0; v.channel = Channel(it->second.channel);
        if (it->second.flags & kEnd) { v.talking = false; v.squelch = 1280; v.squelchStart = false; }
        v.queue.erase(it); ++v.next;
#ifdef WV_MUT_NO_ORDER
        v.queue.clear();
#endif
        return;
    }
    if (!v.queue.empty() && v.queue.rbegin()->first > v.next) {      // a hole with later frames behind it: lost, repeat the last frame fading out
        for (int i = 0; i < kFrame; ++i) pcm[i] = int16_t(v.last[i] * 0.5f * (1.0f - float(i) / kFrame)); memset(v.last, 0, sizeof v.last); v.stat.concealed++; ++v.next; return;
    }
    memset(pcm, 0, kFrame * 2);      // nothing yet: wait; after 160 ms of nothing the transmission is over
    if (++v.silentFrames > 8) { v.talking = false; v.silentFrames = 0; v.squelch = 1280; v.squelchStart = false; v.started = false; v.queue.clear(); }
}
void Mixer::Render(const std::vector<Speaker>& where, float* stereo) {
    for (int i = 0; i < kFrame * 2; ++i) stereo[i] = 0;
    for (auto& kv : v_) {
        Voice& v = kv.second; int16_t pcm[kFrame]; Frame(v, pcm);
        const Speaker* sp = nullptr; for (const auto& w : where) if (w.id == kv.first) sp = &w;
        bool active = v.talking || v.squelch > 0; v.stat.talking = v.talking;
        if (!active) { v.stat.rms = 0; v.stat.prox = v.stat.radio = 0; continue; }
        Mix m = sp ? ComputeMix(sp->distance, sp->bearing, s_, v.channel) : (v.channel == kProximity ? Mix() : ComputeMix(1000.0f, 0, s_, v.channel));
        // the radio filter: band 350..3000 Hz, a little drive, noise; set up once
        static float hb[5], lb[5]; static bool init; if (!init) { Butter(350, true, &hb[0], &hb[1], &hb[2], &hb[3], &hb[4]); Butter(3000, false, &lb[0], &lb[1], &lb[2], &lb[3], &lb[4]); init = true; }
        float a = 1.0f - std::exp(-6.2831853f * m.cutoff / kRate);
        float mono[kFrame]; double e = 0;
        for (int i = 0; i < kFrame; ++i) {
            float x = pcm[i] / 32768.0f, t = float(i) / kFrame;
            v.lp += a * (x - v.lp); float prox = v.lp;
            float rad = x; for (int k = 0; k < 2; ++k) { Biquad& h = v.hp[k]; h.b0 = hb[0]; h.b1 = hb[1]; h.b2 = hb[2]; h.a1 = hb[3]; h.a2 = hb[4]; rad = h.step(rad); }
            for (int k = 0; k < 2; ++k) { Biquad& l = v.lpf[k]; l.b0 = lb[0]; l.b1 = lb[1]; l.b2 = lb[2]; l.a1 = lb[3]; l.a2 = lb[4]; rad = l.step(rad); }
#ifdef WV_MUT_NO_RADIO_FILTER
            rad = x;
#endif
            rad = std::tanh(rad * 2.2f) * 0.6f;
            v.noise = v.noise * 1664525u + 1013904223u; float nz = (float(v.noise >> 8) / 8388608.0f - 1.0f);
            float radioGain = v.prevRadio + (m.radio - v.prevRadio) * t, proxGain = v.prevProx + (m.prox - v.prevProx) * t;      // gains move smoothly across the frame: no clicks
            float hiss = radioGain > 0.01f ? nz * 0.012f * std::min(1.0f, radioGain * 2) : 0;
            float sq = 0; if (v.squelch > 0) { float env = float(v.squelch) / 1280.0f; sq = nz * 0.08f * std::min(1.0f, env) * std::min(1.0f, radioGain * 3); --v.squelch; }
            mono[i] = prox * proxGain + (rad + hiss) * radioGain + sq; e += mono[i] * mono[i];
        }
        v.prevProx = m.prox; v.prevRadio = m.radio;
        v.stat.prox = m.prox; v.stat.radio = m.radio; v.stat.pan = m.pan; v.stat.channel = v.channel; v.stat.rms = float(std::sqrt(e / kFrame));
        // tones: a running measure of three frequencies in what is heard (Goertzel on this frame, smoothed)
        for (int k = 0; k < 3; ++k) { const float w = 6.2831853f * kToneHz[k] / kRate, c = 2 * std::cos(w); float s1 = 0, s2 = 0; for (int i = 0; i < kFrame; ++i) { float s0 = mono[i] + c * s1 - s2; s2 = s1; s1 = s0; }
            float mag = std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) * 2.0f / kFrame; v.stat.tone[k] = 0.7f * v.stat.tone[k] + 0.3f * mag; }
        float p = (m.pan + 1.0f) * 0.78539816f, gl = std::cos(p), gr = std::sin(p);      // equal power
        float rpan = (m.radio > m.prox) ? 0.0f : m.pan; (void)rpan;      // (the radio voice sits in the middle: the share of it is panned by the same law below)
        float rshare = (m.prox + m.radio) > 1e-6f ? m.radio / (m.prox + m.radio) : 0; float mid = 0.70710678f;
        float L = gl * (1 - rshare) + mid * rshare, R = gr * (1 - rshare) + mid * rshare;
        for (int i = 0; i < kFrame; ++i) { stereo[2 * i] += mono[i] * L; stereo[2 * i + 1] += mono[i] * R; }
    }
    for (int i = 0; i < kFrame * 2; ++i) stereo[i] = std::max(-1.0f, std::min(1.0f, stereo[i]));
}
std::vector<SpeakerStat> Mixer::Stats() const { std::vector<SpeakerStat> r; for (const auto& kv : v_) r.push_back(kv.second.stat); return r; }

bool Vad::Feed(const int16_t* pcm) {
    double e = 0; for (int i = 0; i < kFrame; ++i) e += double(pcm[i]) * pcm[i]; float rms = float(std::sqrt(e / kFrame));
    if (rms >= threshold) hang_ = 15; else if (hang_ > 0) --hang_;
    return hang_ > 0;
}
void Tone(const float* hz, int n, float amp, uint64_t* sample, int16_t* out) {
    for (int i = 0; i < kFrame; ++i, ++*sample) { float x = 0; for (int k = 0; k < n; ++k) x += std::sin(6.2831853f * hz[k] * float(*sample) / kRate); out[i] = int16_t(std::max(-32767.0f, std::min(32767.0f, x / n * amp * 32767.0f))); }
}
bool LoadWav(const char* path, std::vector<int16_t>* out) {
    FILE* f = fopen(path, "rb"); if (!f) return false; std::vector<uint8_t> d; uint8_t b[4096]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) d.insert(d.end(), b, b + n); fclose(f);
    if (d.size() < 44 || memcmp(d.data(), "RIFF", 4) || memcmp(&d[8], "WAVE", 4)) return false;
    auto u32 = [&](size_t i) { return unsigned(d[i]) | unsigned(d[i + 1]) << 8 | unsigned(d[i + 2]) << 16 | unsigned(d[i + 3]) << 24; };
    unsigned rate = 0, ch = 0, bits = 0; size_t at = 12, dataAt = 0, dataLen = 0;
    while (at + 8 <= d.size()) { unsigned len = u32(at + 4); if (!memcmp(&d[at], "fmt ", 4) && at + 24 <= d.size()) { ch = d[at + 10] | d[at + 11] << 8; rate = u32(at + 12); bits = d[at + 22] | d[at + 23] << 8; } else if (!memcmp(&d[at], "data", 4)) { dataAt = at + 8; dataLen = std::min<size_t>(len, d.size() - dataAt); break; } at += 8 + len + (len & 1); }
    if (!rate || bits != 16 || (ch != 1 && ch != 2) || !dataAt) return false;
    size_t frames = dataLen / (2 * ch); std::vector<float> mono(frames); for (size_t i = 0; i < frames; ++i) { float s = 0; for (unsigned c = 0; c < ch; ++c) s += int16_t(d[dataAt + (i * ch + c) * 2] | d[dataAt + (i * ch + c) * 2 + 1] << 8); mono[i] = s / ch; }
    size_t outN = size_t(double(frames) * kRate / rate); out->assign(outN, 0);
    for (size_t i = 0; i < outN; ++i) { double p = double(i) * rate / kRate; size_t a = size_t(p); double fr = p - a; float x0 = mono[std::min(a, frames - 1)], x1 = mono[std::min(a + 1, frames - 1)]; (*out)[i] = int16_t(x0 + (x1 - x0) * fr); }      // (linear resampling: a voice, not music)
    return true;
}
constexpr float Mixer::kToneHz[3];
}  // namespace wvoice
