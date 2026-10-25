// What Windows says every program is playing, seen from outside the game: for each audio session on the default output device, the process id, the session's volume and mute, and the
// loudest peak (0..1) its meter showed over the given time. A game that mutes itself, or whose sound Windows stops when its window is not active, shows a peak of 0 here while
// another program (or the voice mixer inside the game) is writing sound. usage: sessions.exe [seconds]      output: "session pid=<n> volume=<v> mute=<0|1> state=<0 inactive|1 active|2 expired> peak=<p>"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <cstdio>
#include <cstdlib>
#include <map>
struct IPeakMeter : IUnknown { virtual HRESULT STDMETHODCALLTYPE GetPeakValue(float* peak) = 0; };      // (the MinGW headers only forward-declare it)
static const GUID kIidPeakMeter = {0xC02216F6, 0x8C67, 0x4B5B, {0x9D, 0x00, 0xD0, 0x08, 0xE7, 0x3E, 0x00, 0x64}};
int main(int argc, char** argv) {
    double secs = argc > 1 ? atof(argv[1]) : 1.0; CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* en = nullptr; IMMDevice* dev = nullptr; IAudioSessionManager2* mgr = nullptr; IAudioSessionEnumerator* se = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en)) || FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) ||
        FAILED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)&mgr)) || FAILED(mgr->GetSessionEnumerator(&se))) { printf("no audio device\n"); return 2; }
    std::map<DWORD, float> peak; std::map<DWORD, float> vol; std::map<DWORD, int> mute, state; ULONGLONG end = GetTickCount64() + (ULONGLONG)(secs * 1000);
    do {
        int n = 0; se->GetCount(&n);
        for (int i = 0; i < n; ++i) {
            IAudioSessionControl* c = nullptr; if (FAILED(se->GetSession(i, &c))) continue; IAudioSessionControl2* c2 = nullptr; DWORD pid = 0;
            if (SUCCEEDED(c->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&c2))) { c2->GetProcessId(&pid); c2->Release(); }
            ISimpleAudioVolume* sv = nullptr; IPeakMeter* mi = nullptr; AudioSessionState st = AudioSessionStateInactive; c->GetState(&st); state[pid] = (int)st;
            if (SUCCEEDED(c->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&sv))) { float f = 0; BOOL mu = FALSE; sv->GetMasterVolume(&f); sv->GetMute(&mu); vol[pid] = f; mute[pid] = mu ? 1 : 0; sv->Release(); }
            if (SUCCEEDED(c->QueryInterface(kIidPeakMeter, (void**)&mi))) { float pk = 0; mi->GetPeakValue(&pk); if (pk > peak[pid]) peak[pid] = pk; mi->Release(); }
            c->Release();
        }
        Sleep(30);
    } while (GetTickCount64() < end);
    for (auto& kv : peak) printf("session pid=%lu volume=%.2f mute=%d state=%d peak=%.4f\n", kv.first, vol[kv.first], mute[kv.first], state[kv.first], kv.second);
    return 0;
}
