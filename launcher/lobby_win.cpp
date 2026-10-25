// See lobby_win.h.
#include "lobby_win.h"
#include <mmsystem.h>
#include <ws2tcpip.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lw {
namespace {
bool g_wsa;
void Wsa() { if (!g_wsa) { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); g_wsa = true; } }
uint32_t HostByName(const std::string& h) {
    unsigned a, b, c, d; if (sscanf(h.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) == 4 && a < 256 && b < 256 && c < 256 && d < 256) return a << 24 | b << 16 | c << 8 | d;
    addrinfo hints = {}; hints.ai_family = AF_INET; addrinfo* res = nullptr; if (getaddrinfo(h.c_str(), nullptr, &hints, &res) != 0 || !res) return 0;
    uint32_t ip = ntohl(((sockaddr_in*)res->ai_addr)->sin_addr.s_addr); freeaddrinfo(res); return ip;
}
uint32_t LocalIp() {      // this PC's address on the local network: a private-range one first
    char name[256]; if (gethostname(name, sizeof name) != 0) return 0x7F000001; addrinfo hints = {}; hints.ai_family = AF_INET; addrinfo* res = nullptr; if (getaddrinfo(name, nullptr, &hints, &res) != 0 || !res) return 0x7F000001;
    uint32_t best = 0x7F000001; int bestScore = -1;
    for (addrinfo* p = res; p; p = p->ai_next) { uint32_t ip = ntohl(((sockaddr_in*)p->ai_addr)->sin_addr.s_addr); int score = (ip >> 24) == 192 && ((ip >> 16) & 255) == 168 ? 4 : (ip >> 24) == 10 ? 3 : (ip >> 24) == 172 ? 2 : (ip >> 24) == 127 ? 0 : 1; if (score > bestScore) { bestScore = score; best = ip; } }
    freeaddrinfo(res); return best;
}
std::string Token() { static const char* k = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; unsigned s = (unsigned)GetTickCount64() ^ (unsigned)GetCurrentProcessId() * 2654435761u; std::string t; for (int i = 0; i < 6; ++i) { s = s * 1664525u + 1013904223u; t += k[(s >> 16) % 32]; } return t; }
}  // namespace

Session::Session() {}
Session::~Session() { Stop(); }
bool Session::Open(uint16_t port, std::string* err) {
    Wsa(); sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); if (sock_ == INVALID_SOCKET) { *err = "no socket"; return false; }
    sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons(port);
    if (bind(sock_, (sockaddr*)&a, sizeof a) != 0) { *err = port ? "Port " + std::to_string(port) + " is in use (is another lobby open?)" : "could not bind"; closesocket(sock_); sock_ = INVALID_SOCKET; return false; }
    u_long nb = 1; ioctlsocket(sock_, FIONBIO, &nb); return true;
}
bool Session::Host(const std::string& name, uint16_t gamePort, uint16_t lobbyPort, const std::string& advertise, std::string* err) {
    Stop(); if (!Open(lobbyPort, err)) return false;
    std::string tok = Token(); uint32_t ip = advertise.empty() ? LocalIp() : HostByName(advertise); if (!ip) { *err = "Cannot find the address \"" + advertise + "\""; closesocket(sock_); sock_ = INVALID_SOCKET; return false; }
    SOCKET s = sock_;
    lobby_ = std::make_unique<wlobby::Lobby>(true, name, gamePort, tok, [s](const wlobby::Addr& to, const wlobby::Bytes& b) { sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(to.ip); a.sin_port = htons(to.port); sendto(s, (const char*)b.data(), (int)b.size(), 0, (sockaddr*)&a, sizeof a); });
    lobby_->SetSession((uint32_t)GetTickCount64() ^ 0x5bd1e995u);
    lobby_->onVoice = [this](const wvoice::Packet& p) { if (sink_) sink_(p); };
    char b[80]; snprintf(b, sizeof b, "%s:%u/%s", wlobby::IpText(ip).c_str(), lobbyPort, tok.c_str()); invite_ = b; host_ = true; hostIp_ = ip; stop_ = false; active_ = true; th_ = CreateThread(nullptr, 0, Run, this, 0, nullptr); return true;
}
bool Session::Join(const std::string& invite, const std::string& name, uint16_t gamePort, std::string* err) {
    Stop(); std::string inv; for (char c : invite) if (c != ' ' && c != '\r' && c != '\n' && c != '\t') inv += c;
    size_t colon = inv.rfind(':'), slash = inv.find('/', colon == std::string::npos ? 0 : colon); if (colon == std::string::npos || slash == std::string::npos || slash <= colon + 1) { *err = "The invite looks like 192.168.1.20:47100/ABC123"; return false; }
    std::string host = inv.substr(0, colon), tok = inv.substr(slash + 1); unsigned port = (unsigned)atoi(inv.substr(colon + 1, slash - colon - 1).c_str());
    if (host.empty() || port < 1 || port > 65535 || tok.empty()) { *err = "The invite looks like 192.168.1.20:47100/ABC123"; return false; }
    Wsa(); uint32_t ip = HostByName(host); if (!ip) { *err = "Cannot find the address \"" + host + "\""; return false; }
    if (!Open(0, err)) return false; SOCKET s = sock_;
    lobby_ = std::make_unique<wlobby::Lobby>(false, name, gamePort, tok, [s](const wlobby::Addr& to, const wlobby::Bytes& b) { sockaddr_in a = {}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(to.ip); a.sin_port = htons(to.port); sendto(s, (const char*)b.data(), (int)b.size(), 0, (sockaddr*)&a, sizeof a); });
    lobby_->onVoice = [this](const wvoice::Packet& p) { if (sink_) sink_(p); };
    lobby_->Join({ip, (uint16_t)port}); invite_ = inv; host_ = false; hostIp_ = ip; stop_ = false; active_ = true; th_ = CreateThread(nullptr, 0, Run, this, 0, nullptr); return true;
}
void Session::Stop() {
    if (th_) { stop_ = true; if (!host_ && lobby_) { Guard l(mu_); lobby_->Leave(); } WaitForSingleObject(th_, 3000); CloseHandle(th_); th_ = nullptr; }
    Guard l(mu_); if (sock_ != INVALID_SOCKET) { closesocket(sock_); sock_ = INVALID_SOCKET; } lobby_.reset(); active_ = false; invite_.clear();
}
void Session::Loop() {
    uint8_t buf[2048];
    while (!stop_) {
        { Guard l(mu_);
          if (!lobby_) break; sockaddr_in from; int fl = sizeof from; int n; uint64_t now = GetTickCount64();
          while ((n = recvfrom(sock_, (char*)buf, sizeof buf, 0, (sockaddr*)&from, &fl)) > 0) { wlobby::Addr a{ntohl(from.sin_addr.s_addr), ntohs(from.sin_port)}; lobby_->OnPacket(a, buf, (size_t)n, now); fl = sizeof from; }
          lobby_->Tick(now); }
        Sleep(5);
    }
}
#define LOCKED(expr, def) Guard l(mu_); if (!lobby_) return def; return expr
wlobby::State Session::State() { LOCKED(lobby_->state(), wlobby::State()); }
unsigned Session::MyId() { LOCKED(lobby_->myId(), 0); }
bool Session::Joined() { LOCKED(lobby_->joined(), false); }
wlobby::JoinResult Session::Refused() { LOCKED(lobby_->refused(), wlobby::kPending); }
std::vector<wlobby::ChatLine> Session::TakeChat() { LOCKED(lobby_->TakeChat(), std::vector<wlobby::ChatLine>()); }
bool Session::TakeLaunch(wlobby::Launch* out) { LOCKED(lobby_->TakeLaunch(GetTickCount64(), out), false); }
bool Session::Start() { LOCKED(lobby_->Start(GetTickCount64()), false); }
void Session::SetReady(bool r) { Guard l(mu_); if (lobby_) lobby_->SetReady(r); }
void Session::SendChat(const std::string& t) { Guard l(mu_); if (lobby_) lobby_->SendChat(t); }
void Session::SetSettings(const wlobby::Settings& s) { Guard l(mu_); if (lobby_) lobby_->SetSettings(s); }
void Session::SetInGame(bool b) { Guard l(mu_); if (lobby_) lobby_->SetInGame(b); }
void Session::SetResult(const std::string& t) { Guard l(mu_); if (lobby_) lobby_->SetResult(t); }
void Session::SendVoice(const wvoice::Packet& p) { Guard l(mu_); if (lobby_) lobby_->SendVoice(p); }
void Session::SetSpeaking(bool b) { Guard l(mu_); if (lobby_) lobby_->SetSpeaking(b); }
void Session::SetVoiceSink(std::function<void(const wvoice::Packet&)> f) { Guard l(mu_); sink_ = std::move(f); }

// ---- the voice of the lobby: the same codec, packet and mixer as in the game, every voice heard as it is (the global channel)
Voice::Voice() {}
Voice::~Voice() { Stop(); }
void Voice::Start(Session* s, const VoiceCfg& c) {
    if (running_) return; s_ = s; c_ = c; stop_ = false; seq_ = 0; on_ = false; enc_ = wvoice::Adpcm(); running_ = true;
    if (c_.mode != 0 || !c_.testTone.empty()) threads_[0] = CreateThread(nullptr, 0, CaptureThread, this, 0, nullptr);
    threads_[1] = CreateThread(nullptr, 0, PlayThread, this, 0, nullptr);
}
void Voice::Stop() {
    if (!running_) return; stop_ = true; for (HANDLE& t : threads_) if (t) { WaitForSingleObject(t, 3000); CloseHandle(t); t = nullptr; } running_ = false; if (s_) s_->SetSpeaking(false); speaking_ = false;
}
void Voice::Push(const wvoice::Packet& p) { if (deaf_) return; ++received_; Guard l(mu_); mixer_.Push(p); }
VoiceStats Voice::Stats() { VoiceStats v; v.sent = sent_; v.received = received_; v.level = level_; v.speaking = speaking_; Guard l(mu_); v.speakers = mixer_.Stats(); return v; }
DWORD WINAPI Voice::CaptureThread(LPVOID p) { ((Voice*)p)->Capture(); return 0; }
DWORD WINAPI Voice::PlayThread(LPVOID p) { ((Voice*)p)->Play(); return 0; }

void Voice::Transmit(const int16_t* pcm, bool want) {
    float lvl = 0; for (int i = 0; i < wvoice::kFrame; ++i) lvl = std::max(lvl, std::fabs(pcm[i] / 32768.0f)); level_ = (unsigned)(lvl * 1000);
    if (muted_) want = false;
    if (!want && !on_) return;
    wvoice::Packet p; p.from = s_->MyId(); p.seq = seq_++; p.channel = wvoice::kGlobal; p.flags = (want && !on_ ? wvoice::kStart : 0) | (c_.mode == 2 ? wvoice::kPtt : 0) | (!want ? wvoice::kEnd : 0);
    wvoice::Encode(pcm, &enc_, p.payload); on_ = want; speaking_ = want; s_->SetSpeaking(want); s_->SendVoice(p); ++sent_;
}
void Voice::Capture() {
    wvoice::Vad vad; uint64_t sample = 0; std::vector<float> tones; { size_t at = 0; const std::string& t = c_.testTone; while (at < t.size()) { size_t e = t.find(',', at); if (e == std::string::npos) e = t.size(); tones.push_back((float)atof(t.substr(at, e - at).c_str())); at = e + 1; } }
    auto wanted = [&](const int16_t* pcm) { if (c_.mode == 0 && tones.empty()) return false; if (c_.mode == 2 && tones.empty()) return (GetAsyncKeyState((int)c_.key) & 0x8000) != 0; return tones.empty() ? vad.Feed(pcm) : true; };
    timeBeginPeriod(1);
    if (!tones.empty()) {      // no microphone: a test voice, paced by the clock
        ULONGLONG next = GetTickCount64();
        while (!stop_) { int16_t pcm[wvoice::kFrame]; wvoice::Tone(tones.data(), (int)tones.size(), 0.9f * c_.micVolume / 100.0f, &sample, pcm); Transmit(pcm, wanted(pcm)); next += 20; ULONGLONG now = GetTickCount64(); if (next > now) Sleep((DWORD)(next - now)); else next = now; }
        timeEndPeriod(1); return;
    }
    HWAVEIN in = nullptr; WAVEFORMATEX f = {WAVE_FORMAT_PCM, 1, wvoice::kRate, wvoice::kRate * 2, 2, 16, 0}; HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (waveInOpen(&in, WAVE_MAPPER, &f, (DWORD_PTR)ev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) { CloseHandle(ev); timeEndPeriod(1); return; }
    static int16_t bufs[6][wvoice::kFrame]; WAVEHDR hdr[6] = {};
    for (int i = 0; i < 6; ++i) { hdr[i].lpData = (LPSTR)bufs[i]; hdr[i].dwBufferLength = sizeof bufs[i]; waveInPrepareHeader(in, &hdr[i], sizeof hdr[i]); waveInAddBuffer(in, &hdr[i], sizeof hdr[i]); }
    waveInStart(in);
    while (!stop_) {
        WaitForSingleObject(ev, 100);
        for (int i = 0; i < 6; ++i) if (hdr[i].dwFlags & WHDR_DONE) {
            int16_t pcm[wvoice::kFrame]; float g = c_.micVolume / 100.0f; for (int k = 0; k < wvoice::kFrame; ++k) pcm[k] = (int16_t)std::max(-32768.0f, std::min(32767.0f, bufs[i][k] * g));
            Transmit(pcm, wanted(pcm)); hdr[i].dwFlags &= ~WHDR_DONE; waveInAddBuffer(in, &hdr[i], sizeof hdr[i]);
        }
    }
    waveInStop(in); waveInReset(in); for (int i = 0; i < 6; ++i) waveInUnprepareHeader(in, &hdr[i], sizeof hdr[i]); waveInClose(in); CloseHandle(ev); timeEndPeriod(1);
}
void Voice::Play() {
    HWAVEOUT out = nullptr; WAVEFORMATEX f = {WAVE_FORMAT_PCM, 2, wvoice::kRate, wvoice::kRate * 4, 4, 16, 0}; static int16_t bufs[6][wvoice::kFrame * 2]; WAVEHDR hdr[6] = {}; timeBeginPeriod(1);
    bool device = !c_.muteOutput && waveOutOpen(&out, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR;
    if (device) for (int i = 0; i < 6; ++i) { hdr[i].lpData = (LPSTR)bufs[i]; hdr[i].dwBufferLength = sizeof bufs[i]; waveOutPrepareHeader(out, &hdr[i], sizeof hdr[i]); hdr[i].dwFlags |= WHDR_DONE; }
    ULONGLONG next = GetTickCount64(); int cur = 0;
    while (!stop_) {
        float st[wvoice::kFrame * 2]; { Guard l(mu_); wvoice::Settings ms; ms.volume = c_.outVolume / 100.0f; ms.self = s_ ? s_->MyId() : 0; mixer_.Configure(ms); mixer_.Render({}, st); }
        if (device) { while (!(hdr[cur].dwFlags & WHDR_DONE) && !stop_) Sleep(2); if (stop_) break; for (int i = 0; i < wvoice::kFrame * 2; ++i) bufs[cur][i] = (int16_t)(st[i] * 32767.0f); hdr[cur].dwFlags &= ~WHDR_DONE; waveOutWrite(out, &hdr[cur], sizeof hdr[cur]); cur = (cur + 1) % 6; }
        else { next += 20; ULONGLONG now = GetTickCount64(); if (next > now) Sleep((DWORD)(next - now)); else next = now; }
    }
    if (device) { waveOutReset(out); for (int i = 0; i < 6; ++i) waveOutUnprepareHeader(out, &hdr[i], sizeof hdr[i]); waveOutClose(out); }
    timeEndPeriod(1);
}
}  // namespace lw
