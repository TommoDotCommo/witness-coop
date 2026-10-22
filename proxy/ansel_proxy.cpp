// Proxy for AnselSDK64.dll. The original is renamed AnselSDK64_real.dll; every export is a stub that
// loads it by full path on first call and jumps to the real function. (Export forwarders did not work:
// the loader reported AnselSDK64_real.dll "not found" although it sits next to the exe.)
// Nothing in DllMain blocks or does I/O (the loader lock): logging runs on a thread.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>

static const char* kNames[] = {
    "getConfiguration", "getConfigurationSize", "getSessionConfigurationSize", "getVersion", "setConfiguration",
    "setStopSessionCallback", "setUpdateCameraFunc", "stopSession", "updateCamera",
};
static HMODULE g_self, g_real;
HMODULE g_selfModule() { return g_self; }
static FARPROC g_fn[9];

void Log(const char* msg) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(g_self, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) *(slash + 1) = 0;
    wcscat_s(path, L"witness_proxy.log");
    HANDLE f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    char line[512];
    int n = snprintf(line, sizeof line, "%s\r\n", msg);
    DWORD wrote;
    WriteFile(f, line, n, &wrote, nullptr);
    CloseHandle(f);
}

extern "C" uintptr_t ResolveThunk(int i) {
    if (!g_fn[i]) {
        if (!g_real) {
            wchar_t path[MAX_PATH];
            GetModuleFileNameW(g_self, path, MAX_PATH);
            wchar_t* slash = wcsrchr(path, L'\\');
            if (slash) *(slash + 1) = 0;
            wcscat_s(path, L"AnselSDK64_real.dll");
            g_real = LoadLibraryW(path);
            char m[120];
            snprintf(m, sizeof m, "real Ansel DLL %s", g_real ? "loaded" : "FAILED to load");
            Log(m);
        }
        g_fn[i] = g_real ? GetProcAddress(g_real, kNames[i]) : nullptr;
    }
    return (uintptr_t)g_fn[i];
}

// Keep the argument registers while resolving, then tail-jump to the real function.
#define THUNK(idx, name) \
    extern "C" __attribute__((naked)) void name() { \
        __asm__ volatile( \
            "push %rcx\n push %rdx\n push %r8\n push %r9\n sub $40, %rsp\n" \
            "mov $" #idx ", %ecx\n call ResolveThunk\n" \
            "add $40, %rsp\n pop %r9\n pop %r8\n pop %rdx\n pop %rcx\n jmp *%rax\n"); \
    }
THUNK(0, getConfiguration)
THUNK(1, getConfigurationSize)
THUNK(2, getSessionConfigurationSize)
THUNK(3, getVersion)
THUNK(4, setConfiguration)
THUNK(5, setStopSessionCallback)
THUNK(6, setUpdateCameraFunc)
THUNK(7, stopSession)

// updateCamera(const Camera&) is called by the game with its camera. Keep a copy of the argument
// block for the overlay and logging, then pass the call through unchanged.
static volatile LONG g_camCalls;
static unsigned char g_camBytes[160];
typedef uintptr_t (*Fn4)(void*, void*, void*, void*);
extern "C" uintptr_t updateCamera(void* a, void* b, void* c, void* d) {
    if (a && !IsBadReadPtr(a, sizeof g_camBytes)) memcpy(g_camBytes, a, sizeof g_camBytes);
    InterlockedIncrement(&g_camCalls);
    Fn4 fn = (Fn4)ResolveThunk(8);
    return fn ? fn(a, b, c, d) : 0;
}

void OverlayInstall();
void CoopStart();
void GameStart();
void StateStart();
static DWORD WINAPI Init(LPVOID) {
    char msg[300], exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    snprintf(msg, sizeof msg, "proxy loaded: pid=%lu host=%s tick=%lu", GetCurrentProcessId(), exe, GetTickCount());
    Log(msg);
    OverlayInstall();
    CoopStart();
    GameStart();
    StateStart();
    LONG last = -1; int logged = 0;
    for (int tick = 0; tick < 400 && logged < 4; tick++) {
        Sleep(1000);
        LONG c = g_camCalls;
        if (c == 0 || (c == last)) { last = c; continue; }
        if (logged == 0 || tick % 5 == 0) {
            char l[900]; int n = snprintf(l, sizeof l, "updateCamera calls=%ld floats:", (long)c);
            for (int i = 0; i < 40; i++) { float f; memcpy(&f, g_camBytes + i*4, 4); n += snprintf(l + n, sizeof l - n, " %.6g", f); }
            Log(l); logged++;
        }
        last = c;
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);
        HANDLE t = CreateThread(nullptr, 0, Init, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
