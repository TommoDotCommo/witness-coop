#pragma once
// Voice chat, game-free (T0 test: voice_test.cpp): the codec, the packet, a jitter buffer per speaker, and the mixer that turns "who is talking, how far, in which direction"
// into what the listener hears. Near players are heard in the world: louder when close, quieter and duller with distance, from the side they are on. Beyond the proximity
// range a voice turns into a walkie-talkie (band-limited, a little distorted, noise floor, squelch at the start and end of each transmission) and comes from nowhere in
// particular, fading over from the proximity voice, so far-away players stay reachable. The windows audio (capture and playback) is voice_audio.cpp.
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace wvoice {
constexpr int kRate = 16000, kFrame = 320;           // 16 kHz mono, 20 ms per frame
constexpr int kPayload = 3 + kFrame / 2;             // IMA ADPCM: predictor (2 bytes), step index (1), two samples per byte

// ---- codec: every frame carries its own predictor and step index, so a lost frame never damages the next one
struct Adpcm { int pred = 0, index = 0; };
void Encode(const int16_t* pcm, Adpcm* st, uint8_t* out);        // kFrame samples -> kPayload bytes
void Decode(const uint8_t* in, int16_t* pcm);                   // kPayload bytes -> kFrame samples

// ---- who hears a transmission and how (chosen by the speaker, carried in every packet)
//   kProximity  heard in the world near the speaker, a walkie-talkie beyond the range (the default)
//   kRadio      everybody hears it over the radio, at any distance (band-limited, noise, squelch), coming from the middle
//   kGlobal     everybody hears it as it is, as loud at 500 m as next to them, from the middle: an open line
//   kDirect     only the player named in `to` hears it, over the radio, at any distance: one on one
enum Channel : uint8_t { kProximity = 0, kRadio = 1, kGlobal = 2, kDirect = 3 };
const char* ChannelName(Channel c);                                              // "proximity", "radio", "global", "direct"
bool ParseChannel(const char* text, Channel* c, unsigned* to);                  // "proximity", "radio", "global", "direct:3" (to = 3)

// ---- the packet ("WTO5", from, seq, flags, channel, to, payload, checksum): sent once, never repeated
enum : uint8_t { kStart = 1, kEnd = 2, kPtt = 4 };
struct Packet { unsigned from = 0, seq = 0; uint8_t flags = 0, channel = kProximity, to = 0; uint8_t payload[kPayload] = {}; };
std::vector<uint8_t> MakePacket(const Packet& p);
bool ParsePacket(const uint8_t* data, size_t n, Packet* p);

// ---- what the listener hears
struct Settings {
    float range = 25.0f;           // metres within which players are heard in the world
    float radioRange = 0.0f;       // metres beyond which even the radio is silent (0: no limit)
    bool radio = true;             // far voices come over the radio (off: they are simply not heard)
    float volume = 1.0f;           // 0..1 on everything
    unsigned self = 0;             // this listener's player id: a direct transmission to another player is not heard (0: not known, every direct transmission is heard)
};
struct Mix { float prox = 0, radio = 0, pan = 0, cutoff = 8000; };      // prox, radio: gains of the two voices; pan -1 left .. 1 right; cutoff: low-pass of the proximity voice in Hz
Mix ComputeMix(float distance, float bearing, const Settings& s, Channel ch = kProximity);       // bearing: radians clockwise from straight ahead of the listener

struct Biquad { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; float step(float x) { float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
// Pure streaming DSP: explicit state, one input/output sample, no buffering or I/O.
struct RadioDsp { Biquad hp[2], lp[2]; uint32_t noise = 1; };
struct RadioSample { float voice, noise, squelch; };
RadioSample ProcessRadio(RadioDsp* state, float input, int squelchRemaining = 0, float gain = 1);
struct Speaker { unsigned id = 0; float distance = 0, bearing = 0; float volume = 1; bool muted = false; };
struct SpeakerStat { unsigned id = 0; uint8_t channel = 0; bool talking = false; float prox = 0, radio = 0, pan = 0, rms = 0; unsigned packets = 0, concealed = 0, dropped = 0; float tone[3] = {}; };
class Mixer {
public:
    void Configure(const Settings& s) { s_ = s; }
    void Push(const Packet& p);                                   // a packet arrived
    void Render(const std::vector<Speaker>& where, float* stereo);  // one 20 ms frame of stereo (kFrame * 2 floats), -1..1
    std::vector<SpeakerStat> Stats() const;
    static constexpr float kToneHz[3] = {120.0f, 1000.0f, 5500.0f};      // what SpeakerStat::tone measures in the speaker's mixed voice
private:
    struct Voice {
        std::map<unsigned, Packet> queue; Channel channel = kProximity; unsigned next = 0; bool started = false; int fill = 0; int16_t last[kFrame] = {}; int silentFrames = 0; bool talking = false;
        float lp = 0; RadioDsp dsp; int squelch = 0; bool squelchStart = false; float prevProx = 0, prevRadio = 0, prevPan = 0;
        SpeakerStat stat;
    };
    Settings s_; std::map<unsigned, Voice> v_;
    void Frame(Voice& v, int16_t* pcm);
};

// ---- the microphone side
class Vad {      // voice activity: loud enough for a moment, held for 300 ms after
public:
    bool Feed(const int16_t* pcm);
    float threshold = 600.0f;
private:
    int hang_ = 0;
};
// A speech recording (a .wav of 16-bit mono or stereo PCM at any rate) as kRate mono samples, for the test voice and the demo. false: not such a file.
bool LoadWav(const char* path, std::vector<int16_t>* out);
void Tone(const float* hz, int n, float amp, uint64_t* sample, int16_t* out);      // a test voice: the sum of n sine waves, kFrame samples, continuing from `sample`
}  // namespace wvoice
