// Proxy for AnselSDK64.dll. The original is renamed AnselSDK64_real.dll; every export is a stub that
// loads it by full path on first call and jumps to the real function. (Export forwarders did not work:
// the loader reported AnselSDK64_real.dll "not found" although it sits next to the exe.)
// Nothing in DllMain blocks or does I/O (the loader lock): logging runs on a thread.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cctype>

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
void InstallHooks();
void CoopStart();
void GameStart();
void StateStart();
// A crash is logged before the game dies: the faulting address and the return addresses above it, each as module+offset, so a fault in a
// driver call can be traced to the call in this DLL that made it (map an AnselSDK64.dll offset with objdump or nm on the build).
static LONG WINAPI CrashLogger(EXCEPTION_POINTERS* ep) {
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_PRIV_INSTRUCTION) return EXCEPTION_CONTINUE_SEARCH;
    {   // a probe like IsBadReadPtr raises and catches its own access violation (vectored handlers see it first): not a crash
        HMODULE mod = nullptr; char name[MAX_PATH] = "";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)ep->ExceptionRecord->ExceptionAddress, &mod) && mod) GetModuleFileNameA(mod, name, MAX_PATH);
        for (char* c = name; *c; ++c) *c = (char)tolower((unsigned char)*c);
        if (strstr(name, "kernel32.dll") || strstr(name, "kernelbase.dll") || strstr(name, "ntdll.dll")) return EXCEPTION_CONTINUE_SEARCH;
    }
    static volatile LONG once; if (InterlockedExchange(&once, 1)) return EXCEPTION_CONTINUE_SEARCH;
    auto where = [](const void* a, char* out, size_t n) {
        HMODULE mod = nullptr; char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)a, &mod) && mod) { GetModuleFileNameA(mod, name, MAX_PATH); const char* sl = strrchr(name, '\\'); snprintf(out, n, "%s+0x%llx", sl ? sl + 1 : name, (unsigned long long)((uintptr_t)a - (uintptr_t)mod)); }
        else snprintf(out, n, "%p", a);
    };
    char m[400], w[200]; where(ep->ExceptionRecord->ExceptionAddress, w, sizeof w);
    snprintf(m, sizeof m, "CRASH: exception 0x%lx at %s (thread %lu, access %llu of %p)", code, w, GetCurrentThreadId(), code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2 ? (unsigned long long)ep->ExceptionRecord->ExceptionInformation[0] : 0ull, code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2 ? (void*)ep->ExceptionRecord->ExceptionInformation[1] : nullptr); Log(m);
    void* frames[32]; USHORT n = RtlCaptureStackBackTrace(0, 32, frames, nullptr);
    for (USHORT i = 0; i < n; i++) { where(frames[i], w, sizeof w); snprintf(m, sizeof m, "CRASH: stack %2u %s", i, w); Log(m); }
    // the frames above are the handler's own; the faulting thread's stack is walked from its context
    CONTEXT* c = ep->ContextRecord; int depth = 0; char line[1200] = "CRASH: trace"; size_t at = strlen(line);
    for (uintptr_t* sp = (uintptr_t*)c->Rsp; depth < 24 && (uintptr_t)sp < c->Rsp + 4096; sp++) {
        if (IsBadReadPtr(sp, 8)) break; uintptr_t v = *sp; HMODULE mod = nullptr;
        if (v > 0x10000 && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)v, &mod) && mod) {
            MEMORY_BASIC_INFORMATION mbi; if (VirtualQuery((void*)v, &mbi, sizeof mbi) && (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) { where((void*)v, w, sizeof w); at += snprintf(line + at, sizeof line - at, " %s", w); depth++; if (at > sizeof line - 120) break; }
        }
    }
    Log(line); Sleep(300);      // the log thread gets a moment to write
    return EXCEPTION_CONTINUE_SEARCH;
}
static DWORD WINAPI Init(LPVOID) {
    char msg[300], exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    snprintf(msg, sizeof msg, "proxy loaded: pid=%lu host=%s tick=%lu", GetCurrentProcessId(), exe, GetTickCount());
    Log(msg);
    AddVectoredExceptionHandler(1, CrashLogger);
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
        InstallHooks();      // memory patching only: no I/O under the loader lock
        DisableThreadLibraryCalls(inst);
        HANDLE t = CreateThread(nullptr, 0, Init, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
