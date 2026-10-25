#pragma once
// The lobby on Windows: the UDP socket and its thread around wlobby::Lobby (lobby.h), and the voice of the lobby (microphone in, everybody's voice out, the same codec and mixer as in the game).
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <atomic>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "lobby.h"

namespace lw {
class Mutex { CRITICAL_SECTION cs_; public: Mutex() { InitializeCriticalSection(&cs_); } ~Mutex() { DeleteCriticalSection(&cs_); } void lock() { EnterCriticalSection(&cs_); } void unlock() { LeaveCriticalSection(&cs_); } };
struct Guard { Mutex& m; explicit Guard(Mutex& mm) : m(mm) { m.lock(); } ~Guard() { m.unlock(); } };
struct VoiceCfg { int mode = 2; unsigned key = 'N'; int micVolume = 100, outVolume = 100; std::string testTone; bool muteOutput = false; };       // mode: 0 off, 1 open microphone, 2 push to talk
struct VoiceStats { unsigned sent = 0, received = 0, level = 0; bool speaking = false; std::vector<wvoice::SpeakerStat> speakers; };

class Voice;
class Session {
public:
    Session(); ~Session();
    bool Host(const std::string& name, uint16_t gamePort, uint16_t lobbyPort, const std::string& advertise, std::string* err);       // advertise: the address friends reach this PC at (empty: this PC's address on the local network)
    bool Join(const std::string& invite, const std::string& name, uint16_t gamePort, std::string* err);                              // invite: address:port/CODE
    void Stop();
    bool Active() const { return active_; }
    bool IsHost() const { return host_; }
    std::string Invite() const { return invite_; }
    uint32_t HostIp() const { return hostIp_; }                       // the address the host was reached at (the host's own game address, for the others)
    // the lobby (every call is under the lock)
    wlobby::State State(); unsigned MyId(); bool Joined(); wlobby::JoinResult Refused();
    void SetReady(bool r); void SendChat(const std::string& t); void SetSettings(const wlobby::Settings& s); bool Start(); void SetInGame(bool b); void SetResult(const std::string& t);
    std::vector<wlobby::ChatLine> TakeChat(); bool TakeLaunch(wlobby::Launch* l);
    void SendVoice(const wvoice::Packet& p); void SetSpeaking(bool b);
    void SetVoiceSink(std::function<void(const wvoice::Packet&)> f);
private:
    bool Open(uint16_t port, std::string* err); void Loop();
    Mutex mu_; std::unique_ptr<wlobby::Lobby> lobby_; SOCKET sock_ = INVALID_SOCKET; HANDLE th_ = nullptr; static DWORD WINAPI Run(LPVOID p) { ((Session*)p)->Loop(); return 0; } std::atomic<bool> active_{false}, stop_{false}; bool host_ = false; std::string invite_; uint32_t hostIp_ = 0;
    std::function<void(const wvoice::Packet&)> sink_;
};

class Voice {
public:
    Voice(); ~Voice();
    void Start(Session* s, const VoiceCfg& c);       // opens the microphone and the speakers
    void Stop();                                      // closes them (the game has the microphone while it runs)
    bool Running() const { return running_; }
    void Push(const wvoice::Packet& p);               // a voice from the lobby
    void SetMuted(bool m) { muted_ = m; } void SetDeaf(bool d) { deaf_ = d; }
    VoiceStats Stats();
private:
    static DWORD WINAPI CaptureThread(LPVOID); static DWORD WINAPI PlayThread(LPVOID);
    void Capture(); void Play(); void Transmit(const int16_t* pcm, bool want);
    Session* s_ = nullptr; VoiceCfg c_; std::atomic<bool> running_{false}, stop_{false}, muted_{false}, deaf_{false}; Mutex mu_; wvoice::Mixer mixer_; HANDLE threads_[2] = {nullptr, nullptr};
    std::atomic<unsigned> sent_{0}, received_{0}, level_{0}; std::atomic<bool> speaking_{false}; unsigned seq_ = 0; bool on_ = false; wvoice::Adpcm enc_;
};
}  // namespace lw
