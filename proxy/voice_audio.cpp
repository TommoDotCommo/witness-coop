// Voice chat on Windows: the microphone (WinMM) is encoded and sent to the other players, their voices are mixed (voice/voice.h) and played; the game's own volume is set on its
// audio session. See voice/voice.h for what the listener hears. Settings: voice_mode, key_voice, voice_volume, mic_volume, voice_range, voice_radio_range, voice_radio, game_volume.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "voice/voice.h"
#include "voice/gesture.h"
#include "radio/radio.h"
#include "coop.h"
#include "config.h"
#include "social.h"

void Log(const char* msg);

namespace {
CRITICAL_SECTION g_lk; bool g_lkInit;
struct Lock { Lock() { EnterCriticalSection(&g_lk); } ~Lock() { LeaveCriticalSection(&g_lk); } };
wvoice::Mixer g_mixer; volatile LONG g_proximityGesture = 0, g_ptt = 0, g_transmitting = 0, g_started = 0; volatile LONG g_sent = 0, g_captured = 0, g_received = 0, g_played = 0, g_micLevel = 0;
volatile LONG g_radioHeld = 0, g_txChannel = 0;
std::map<unsigned, float> g_playerVolume;
wvoice::Channel g_channel = wvoice::kProximity; unsigned g_to = 0;      // what this player says goes where (the cfg sets it, the channel key and the voicechannel command change it)
unsigned g_seq; float g_gameVolume = -1, g_gameVolumeRead = -1;
const float kPi = 3.14159265f;

bool ListenerPose(float pos[3], float* yaw) {
    const uintptr_t base = (uintptr_t)GetModuleHandleA(nullptr); const float* c = (const float*)(base + 0x6304B0 + 0x10); const float* a = (const float*)(base + 0x6303EC);
    if (IsBadReadPtr(c, 12) || IsBadReadPtr(a, 8)) return false; memcpy(pos, c, 12); *yaw = a[0]; return true;
}
std::vector<wvoice::Speaker> Speakers() {
    std::vector<wvoice::Speaker> r; float pos[3], yaw = 0; bool have = ListenerPose(pos, &yaw); const Cfg& cfg = GetCfg();
    for (const auto& rp : CoopGetRemotes()) {
        wvoice::Speaker s; s.id = rp.id; s.distance = 1000.0f; s.bearing = 0;
        if (have && rp.valid && rp.ageMs < 5000) {
            float dx = rp.pos[0] - pos[0], dy = rp.pos[1] - pos[1], dz = rp.pos[2] - pos[2]; s.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            float fx = std::cos(yaw), fy = std::sin(yaw), rx = std::sin(yaw), ry = -std::cos(yaw); s.bearing = std::atan2(dx * rx + dy * ry, dx * fx + dy * fy);
        }
        if (cfg.voiceNoPos) { s.distance = 0; s.bearing = 0; }
        auto volume = g_playerVolume.find(s.id); if (volume != g_playerVolume.end()) s.volume = volume->second;
        r.push_back(s);
    }
    return r;
}
wvoice::Settings MixSettings() { const Cfg& c = GetCfg(); wvoice::Settings s; s.range = c.voiceRange; s.radioRange = c.voiceRadioRange; s.radio = c.voiceRadio; s.volume = c.voiceVolume / 100.0f; s.self = c.voiceNoSelf ? 0 : CoopMyId(); return s; }

// ---- the microphone: a WinMM capture (or the test tones), 20 ms at a time
void Transmit(const int16_t* pcm, bool want, wvoice::Adpcm* enc) {
    static bool on; static uint8_t lastChannel = 0, lastTo = 0; const Cfg& cfg = GetCfg(); InterlockedIncrement(&g_captured);
    float lvl = 0; for (int i = 0; i < wvoice::kFrame; ++i) lvl = std::max(lvl, std::fabs(pcm[i] / 32768.0f)); InterlockedExchange(&g_micLevel, (LONG)(lvl * 1000));
    wvoice::Packet p; p.from = CoopMyId(); p.seq = g_seq; { Lock l; if (!cfg.noVoiceChannel) { p.channel = g_channel; p.to = uint8_t(g_to); } }
    bool radio = cfg.radioEnabled && g_radioHeld && !GamePaused() && !SocialCapturing();
    if (radio && want && !cfg.noVoiceChannel) { p.channel = wvoice::kRadio; p.to = 0; }
    static wvoice::GestureActivity activity;
    float energy=0; for(int i=0;i<wvoice::kFrame;++i) energy+=float(pcm[i])*float(pcm[i]);
    InterlockedExchange(&g_proximityGesture, activity.Feed(std::sqrt(energy/wvoice::kFrame),want,p.channel,.02f) ? 1 : 0);
    if (!want && !on) return;
    if (!want) { p.channel = lastChannel; p.to = lastTo; }
    if (want && on && p.channel != lastChannel) { // close the previous stream before switching channels
        wvoice::Packet end = p; end.channel = lastChannel; end.to = lastTo; end.flags = wvoice::kEnd;
        wvoice::Encode(pcm, enc, end.payload); auto bytes = wvoice::MakePacket(end);
        if (!cfg.noVoiceSend) { if (end.channel == wvoice::kDirect) CoopSendRawTo(end.to,bytes.data(),bytes.size());
        else for (const auto& rp : CoopGetRemotes()) CoopSendRawTo(rp.id, bytes.data(), bytes.size()); }
        p.seq = ++g_seq; on = false;
    }
    lastChannel = p.channel; lastTo = p.to; InterlockedExchange(&g_txChannel, p.channel);
    p.flags = (want && !on ? wvoice::kStart : 0) | (cfg.voiceMode == 2 ? wvoice::kPtt : 0) | (!want ? wvoice::kEnd : 0);
    wvoice::Encode(pcm, enc, p.payload); ++g_seq; on = want; InterlockedExchange(&g_transmitting, want ? 1 : 0);
    if (cfg.noVoiceSend) return;
    auto bytes = wvoice::MakePacket(p);
    if (p.channel == wvoice::kDirect) CoopSendRawTo(p.to, bytes.data(), bytes.size());      // one on one: nobody else is sent a thing
    else for (const auto& rp : CoopGetRemotes()) CoopSendRawTo(rp.id, bytes.data(), bytes.size());
    InterlockedIncrement(&g_sent);
}
DWORD WINAPI CaptureThread(LPVOID) {
    const Cfg& cfg = GetCfg(); wvoice::Adpcm enc; wvoice::Vad vad; uint64_t sample = 0;
    std::vector<float> tones; { size_t at = 0; const std::string& t = cfg.voiceTestTone; while (at < t.size()) { size_t e = t.find(',', at); if (e == std::string::npos) e = t.size(); tones.push_back((float)atof(t.substr(at, e - at).c_str())); at = e + 1; } }
    timeBeginPeriod(1);
    auto wanted = [&](const int16_t* pcm) { if (cfg.voiceMode == 0) return false; if (GamePaused() || SocialCapturing()) return false; if (cfg.radioEnabled && g_radioHeld) return true; if (cfg.voiceMode == 2) return g_ptt != 0; return tones.empty() ? vad.Feed(pcm) : true; };
    if (!tones.empty()) {      // no microphone: the test voice, paced by the clock
        ULONGLONG next = GetTickCount64();
        for (;;) { int16_t pcm[wvoice::kFrame]; wvoice::Tone(tones.data(), (int)tones.size(), 0.9f * cfg.micVolume / 100.0f, &sample, pcm); Transmit(pcm, wanted(pcm), &enc); next += 20; ULONGLONG now = GetTickCount64(); if (next > now) Sleep((DWORD)(next - now)); else next = now; }
    }
    HWAVEIN in = nullptr; WAVEFORMATEX f = {WAVE_FORMAT_PCM, 1, wvoice::kRate, wvoice::kRate * 2, 2, 16, 0}; HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (waveInOpen(&in, WAVE_MAPPER, &f, (DWORD_PTR)ev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) { Log("voice: no microphone could be opened"); return 0; }
    static int16_t bufs[6][wvoice::kFrame]; WAVEHDR hdr[6] = {};
    for (int i = 0; i < 6; ++i) { hdr[i].lpData = (LPSTR)bufs[i]; hdr[i].dwBufferLength = sizeof bufs[i]; waveInPrepareHeader(in, &hdr[i], sizeof hdr[i]); waveInAddBuffer(in, &hdr[i], sizeof hdr[i]); }
    waveInStart(in); Log("voice: microphone open");
    for (;;) {
        WaitForSingleObject(ev, 100);
        for (int i = 0; i < 6; ++i) if (hdr[i].dwFlags & WHDR_DONE) {
            int16_t pcm[wvoice::kFrame]; float g = cfg.micVolume / 100.0f; for (int k = 0; k < wvoice::kFrame; ++k) pcm[k] = (int16_t)std::max(-32768.0f, std::min(32767.0f, bufs[i][k] * g));
            Transmit(pcm, wanted(pcm), &enc); hdr[i].dwFlags &= ~WHDR_DONE; waveInAddBuffer(in, &hdr[i], sizeof hdr[i]);
        }
    }
}

// ---- the speakers: 20 ms of the mix at a time, paced by the device (or the clock when muted)
DWORD WINAPI PlayThread(LPVOID) {
    const Cfg& cfg = GetCfg(); HWAVEOUT out = nullptr; WAVEFORMATEX f = {WAVE_FORMAT_PCM, 2, wvoice::kRate, wvoice::kRate * 4, 4, 16, 0}; static int16_t bufs[6][wvoice::kFrame * 2]; WAVEHDR hdr[6] = {};
    timeBeginPeriod(1);
    bool device = !cfg.voiceMuteOutput && waveOutOpen(&out, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR;
    if (device) for (int i = 0; i < 6; ++i) { hdr[i].lpData = (LPSTR)bufs[i]; hdr[i].dwBufferLength = sizeof bufs[i]; waveOutPrepareHeader(out, &hdr[i], sizeof hdr[i]); hdr[i].dwFlags |= WHDR_DONE; }
    else if (!cfg.voiceMuteOutput) Log("voice: no playback device could be opened");
    ULONGLONG next = GetTickCount64(); int cur = 0;
    for (;;) {
        float st[wvoice::kFrame * 2]; { Lock l; g_mixer.Configure(MixSettings()); g_mixer.Render(Speakers(), st); }
        if (device) {
            while (!(hdr[cur].dwFlags & WHDR_DONE)) Sleep(2);
            for (int i = 0; i < wvoice::kFrame * 2; ++i) bufs[cur][i] = (int16_t)(st[i] * 32767.0f);
            hdr[cur].dwFlags &= ~WHDR_DONE; waveOutWrite(out, &hdr[cur], sizeof hdr[cur]); cur = (cur + 1) % 6;
        } else { next += 20; ULONGLONG now = GetTickCount64(); if (next > now) Sleep((DWORD)(next - now)); else next = now; }
        InterlockedIncrement(&g_played);
    }
}

// ---- the game's own volume: the Windows volume mixer's entry for this process
void ApplyGameVolume() {
    const Cfg& cfg = GetCfg(); if (cfg.noGameVolume || cfg.gameVolume >= 100) return;
    IMMDeviceEnumerator* en = nullptr; IMMDevice* dev = nullptr; IAudioSessionManager2* mgr = nullptr; IAudioSessionEnumerator* se = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en))) return;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) && SUCCEEDED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)&mgr)) && SUCCEEDED(mgr->GetSessionEnumerator(&se))) {
        int n = 0; se->GetCount(&n);
        for (int i = 0; i < n; ++i) {
            IAudioSessionControl* c = nullptr; if (FAILED(se->GetSession(i, &c))) continue; IAudioSessionControl2* c2 = nullptr; DWORD pid = 0;
            if (SUCCEEDED(c->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&c2))) { c2->GetProcessId(&pid); c2->Release(); }
            if (pid == GetCurrentProcessId()) { ISimpleAudioVolume* v = nullptr; if (SUCCEEDED(c->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&v))) { v->SetMasterVolume(cfg.gameVolume / 100.0f, nullptr); float rb = -1; v->GetMasterVolume(&rb); g_gameVolumeRead = rb; v->Release(); } }
            c->Release();
        }
    }
    if (se) se->Release(); if (mgr) mgr->Release(); if (dev) dev->Release(); en->Release();
}
DWORD WINAPI VolumeThread(LPVOID) { CoInitializeEx(nullptr, COINIT_MULTITHREADED); for (;;) { ApplyGameVolume(); Sleep(3000); } }
}  // namespace

// ---- the channel: who hears me and how
static std::string RemoteName(unsigned id) { for (const auto& rp : CoopGetRemotes()) if (rp.id == id && rp.name[0]) return rp.name; char b[24]; snprintf(b, sizeof b, "player %u", id); return b; }
void VoiceSetChannel(const char* text) {
    wvoice::Channel ch; unsigned to = 0; char t[40]; snprintf(t, sizeof t, "%s", text); for (char* q = t; *q; ++q) if (*q == '\r' || *q == '\n') *q = 0;
    if (!g_lkInit) { InitializeCriticalSection(&g_lk); g_lkInit = true; }
    if (!wvoice::ParseChannel(t, &ch, &to)) return; { Lock l; g_channel = ch; g_to = ch == wvoice::kDirect ? to : 0; }
    char m[120]; if (ch == wvoice::kDirect) snprintf(m, sizeof m, "voice: channel direct to %u", to); else snprintf(m, sizeof m, "voice: channel %s", wvoice::ChannelName(ch)); Log(m);
}
bool VoiceCycleChannel() {
    std::vector<std::string> all = {"proximity", "radio", "global"}; for (const auto& rp : CoopGetRemotes()) all.push_back("direct:" + std::to_string(rp.id));
    char cur[40]; { Lock l; if (g_channel == wvoice::kDirect) snprintf(cur, sizeof cur, "direct:%u", g_to); else snprintf(cur, sizeof cur, "%s", wvoice::ChannelName(g_channel)); }
    size_t i = 0; for (; i < all.size(); ++i) if (all[i] == cur) break;
    VoiceSetChannel(all[(i + 1) % all.size()].c_str()); return true;
}
void VoiceChannelText(char* out, size_t n) {
    if (g_radioHeld && GetCfg().radioEnabled) { snprintf(out, n, "Radio"); return; }
    wvoice::Channel ch; unsigned to; { Lock l; ch = g_channel; to = g_to; }
    if (ch == wvoice::kDirect) snprintf(out, n, "Private: %s", RemoteName(to).c_str()); else snprintf(out, n, "%s", ch == wvoice::kRadio ? "Radio" : ch == wvoice::kGlobal ? "Open line" : "Proximity");
}

void VoiceStart() {
    if (InterlockedExchange(&g_started, 1)) return;
    if (!g_lkInit) { InitializeCriticalSection(&g_lk); g_lkInit = true; }
    { const Cfg& c0 = GetCfg(); VoiceSetChannel(c0.voiceChannel.c_str()); }
    const Cfg& c = GetCfg();
    if (c.voiceMode != 0 || !c.voiceTestTone.empty()) { HANDLE t = CreateThread(nullptr, 0, CaptureThread, nullptr, 0, nullptr); if (t) CloseHandle(t); }
    { HANDLE t = CreateThread(nullptr, 0, PlayThread, nullptr, 0, nullptr); if (t) CloseHandle(t); }
    if (c.gameVolume < 100 && !c.noGameVolume) { HANDLE t = CreateThread(nullptr, 0, VolumeThread, nullptr, 0, nullptr); if (t) CloseHandle(t); }
}
void VoiceOnPacket(const uint8_t* data, size_t n) {      // network thread
    if (!g_lkInit) return; wvoice::Packet p; if (!wvoice::ParsePacket(data, n, &p)) return; InterlockedIncrement(&g_received);
    if (GetCfg().noVoicePlay) return; Lock l; g_mixer.Push(p);
}
bool VoiceKey(unsigned vk, bool down) { if (vk != GetCfg().keyVoice) return false; InterlockedExchange(&g_ptt, down ? 1 : 0); return false; }
bool VoiceProximitySpeaking() { return g_proximityGesture != 0 && !GetCfg().noSpeakingFlag; }
bool VoiceRadioKey(bool down, bool blocked) {
    wradio::Hold h; h.Key(down, GetCfg().radioEnabled, blocked);
    InterlockedExchange(&g_radioHeld, h.down ? 1 : 0); return GetCfg().radioEnabled;
}
unsigned VoiceChannel() { if (!g_lkInit) return wvoice::kProximity; if (g_transmitting) return (unsigned)g_txChannel; Lock l; return g_channel; }
bool VoiceRadioTx() { return g_transmitting && g_txChannel == wvoice::kRadio && GetCfg().radioEnabled; }
float VoiceGetPlayerVolume(unsigned id) { if (!g_lkInit) return 1; Lock l; auto it=g_playerVolume.find(id); return it==g_playerVolume.end()?1:it->second; }
void VoicePlayerVolume(unsigned id, float volume) { if (!g_lkInit) return; Lock l; g_playerVolume[id] = std::max(0.0f, std::min(1.0f, volume)); }
bool VoiceSpeaking() { return g_transmitting != 0 && !GetCfg().noSpeakingFlag; }
int VoiceMicLevel() { return (int)g_micLevel; }
void VoiceStat() {
    char m[300]; const Cfg& c = GetCfg();
    snprintf(m, sizeof m, "voicestat: mode %d ptt %ld transmitting %ld sent %ld captured %ld received %ld played %ld game_volume %.2f hud_speakers %u hud_mic %d channel %s to %u", c.voiceMode, g_ptt, g_transmitting, g_sent, g_captured, g_received, g_played, g_gameVolumeRead, SocialSpeakDrawn(), SocialMicHudDrawn() ? 1 : 0, wvoice::ChannelName(g_channel), g_to); Log(m);
    if (!g_lkInit) return; Lock l;
    for (const auto& s : g_mixer.Stats()) { snprintf(m, sizeof m, "voicestat: speaker %u talking %d prox %.3f radio %.3f pan %.3f rms %.4f tone %.4f %.4f %.4f packets %u concealed %u dropped %u channel %u", s.id, s.talking ? 1 : 0, s.prox, s.radio, s.pan, s.rms, s.tone[0], s.tone[1], s.tone[2], s.packets, s.concealed, s.dropped, (unsigned)s.channel); Log(m); }
}

std::vector<wvoice::SpeakerStat> VoiceSnapshot() { if (!g_lkInit) return {}; Lock l; return g_mixer.Stats(); }
