// The co-op proxy, built as dsound.dll: the game imports DSOUND (by ordinal) but does not ship it, so Windows loads this file from the
// game folder and the game's own files stay untouched. Every export is a stub that loads the system's dsound.dll by full path
// (GetSystemDirectory, never by bare name: that name is this file) on first call and jumps to the real function.
// A dsound.dll that was in the game folder before the mod (another mod, an ASI loader) is kept as dsound.chain.dll and loaded
// after this one for its own start-up work; the exports still go to the system copy (README, Install: "Another mod named dsound.dll").
// Nothing in DllMain blocks or does I/O (the loader lock): logging and the chain load run on a thread.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../common/steam_ids.h"
#include <string>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cctype>
#include "../common/buildid.h"
#include "diag.h"

static const char* kNames[] = {
    "DirectSoundCreate", "DirectSoundEnumerateA", "DirectSoundEnumerateW", "DllCanUnloadNow", "DllGetClassObject",
    "DirectSoundCaptureCreate", "DirectSoundCaptureEnumerateA", "DirectSoundCaptureEnumerateW", "GetDeviceID",
    "DirectSoundFullDuplexCreate", "DirectSoundCreate8", "DirectSoundCaptureCreate8",
};
static const int kCount = sizeof kNames / sizeof kNames[0];
static HMODULE g_self, g_real;
HMODULE g_selfModule() { return g_self; }
static FARPROC g_fn[12];
// Release tools and Setup recognise a dsound.dll of this mod by this text (any version); another file of that name is chained.
extern "C" __attribute__((used)) const char kCoopProxyMarker[] = "witness-coop:proxy:dsound";

void Log(const char* msg) {
    DiagLine(msg);      // this run's own session log (and the rotation of witness_proxy.log on the first line)
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

// The folder the real dsound.dll is loaded from: the system folder. Test builds (tools/test_build_gate.sh) use a folder beside
// the proxy instead, so a fake "system" DLL can prove the forwarding.
static void SystemDsoundPath(wchar_t* path, size_t n) {
#ifdef COOP_TEST_SYSTEM_SUBDIR
    GetModuleFileNameW(g_self, path, (DWORD)n);
    wchar_t* slash = wcsrchr(path, L'\\'); if (slash) slash[1] = 0;
    wcscat_s(path, n, COOP_TEST_SYSTEM_SUBDIR);
#else
    UINT len = GetSystemDirectoryW(path, (UINT)n);
    if (!len || len >= n) path[0] = 0;
#endif
    wcscat_s(path, n, L"\\dsound.dll");
}
// If the system copy is missing (a broken Windows) the game gets "failed" from every call instead of a jump to address 0.
static long __stdcall NoDsound() { return (long)0x80004005; }     // E_FAIL

extern "C" uintptr_t ResolveThunk(int i) {
    if (!g_fn[i]) {
        if (!g_real) {
            wchar_t path[MAX_PATH];
            SystemDsoundPath(path, MAX_PATH);
#ifdef COOP_MUT_BARE_NAME
            g_real = LoadLibraryW(L"dsound.dll");      // mutant: loads by bare name, which is this file itself
#else
            g_real = LoadLibraryW(path);
#endif
            char m[120];
            snprintf(m, sizeof m, "system dsound.dll %s", g_real ? "loaded" : "FAILED to load: the game gets no DirectSound");
            Log(m);
        }
        FARPROC f = (g_real && g_real != g_self) ? GetProcAddress(g_real, kNames[i]) : nullptr;
        g_fn[i] = f ? f : (FARPROC)NoDsound;
    }
    return (uintptr_t)g_fn[i];
}

// Keep the argument registers while resolving, then tail-jump to the real function (stack arguments stay where they are).
#define THUNK(idx, name) \
    extern "C" __attribute__((naked)) void name() { \
        __asm__ volatile( \
            "push %rcx\n push %rdx\n push %r8\n push %r9\n sub $40, %rsp\n" \
            "mov $" #idx ", %ecx\n call ResolveThunk\n" \
            "add $40, %rsp\n pop %r9\n pop %r8\n pop %rdx\n pop %rcx\n jmp *%rax\n"); \
    }
THUNK(0, DirectSoundCreate)
THUNK(1, DirectSoundEnumerateA)
THUNK(2, DirectSoundEnumerateW)
THUNK(3, DllCanUnloadNow)
THUNK(4, DllGetClassObject)
THUNK(5, DirectSoundCaptureCreate)
THUNK(6, DirectSoundCaptureEnumerateA)
THUNK(7, DirectSoundCaptureEnumerateW)
THUNK(8, GetDeviceID)
THUNK(9, DirectSoundFullDuplexCreate)
THUNK(10, DirectSoundCreate8)
THUNK(11, DirectSoundCaptureCreate8)

// ---- chain load: a dsound.dll that was there before the mod is kept by Setup as dsound.chain.dll and loaded here, after this
// DLL, for whatever it does when it loads (an ASI loader loads its plugins). Its exports are not used: the game's DirectSound
// calls go to the system copy. Loaded once, from the start-up thread, whatever the game build (it is someone else's mod).
static void ChainLoad() {
#ifdef COOP_MUT_NO_CHAIN
    return;                    // mutant: the chain file is ignored
#endif
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(g_self, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\'); if (slash) slash[1] = 0;
    wcscat_s(path, L"dsound.chain.dll");
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fa)) { Log("chain: no dsound.chain.dll"); return; }
    HMODULE m = LoadLibraryW(path);
    char line[200];
    if (m) snprintf(line, sizeof line, "chain: loaded dsound.chain.dll (%lu bytes)", (unsigned long)fa.nFileSizeLow);
    else snprintf(line, sizeof line, "chain: dsound.chain.dll (%lu bytes) FAILED to load (error %lu)", (unsigned long)fa.nFileSizeLow, GetLastError());
    Log(line);
}

// ---- the old layout beside this one: up to v0.1.0-alpha the mod was the game's AnselSDK64.dll. If that old proxy is in this process
// too (an alpha install with the new files copied over it, or a test copy prepared by older tools), two copies of the mod would hook the
// same game: this one stays passive (forwarding only) and the old one runs co-op alone. Decided in DllMain from the images the loader has
// already mapped (all static imports are mapped before any DllMain runs): no file I/O, only a scan of that module's readable pages.
static bool g_otherCopy;
static bool OldProxyMapped() {
#ifdef COOP_MUT_NO_OTHER_COPY
    return false;              // mutant: the old proxy is not looked for
#endif
    HMODULE m = GetModuleHandleW(L"AnselSDK64.dll");
    if (!m || m == g_self) return false;
    const uint8_t* base = (const uint8_t*)m;
    size_t size = buildid::MappedSize(base);
    static const char kOld[] = "real Ansel DLL";      // a text only the old co-op proxy carries
    for (size_t at = 0; at < size;) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(base + at, &mbi, sizeof mbi)) break;
        size_t end = (size_t)((const uint8_t*)mbi.BaseAddress + mbi.RegionSize - base); if (end > size) end = size;
        bool readable = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
                        (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
        if (readable)
            for (size_t i = at; i + sizeof kOld - 1 <= end; ++i)
                if (base[i] == 'r' && !memcmp(base + i, kOld, sizeof kOld - 1)) return true;
        if (end <= at) break;
        at = end;
    }
    return false;
}

// ---- which build of the game is this? Decided in DllMain from the exe already in memory (no I/O under the loader lock).
// Any build but the one the mod's addresses fit is left strictly alone: no hook, no thread of ours that touches the game,
// every export still forwards to the system dsound.dll, and the player is told once that co-op is off.
#ifdef COOP_TEST_SPEC      // test builds only (tools/test_build_gate.sh): the build to treat as the known one
static buildid::Spec KnownSpec() { return buildid::Spec{ COOP_TEST_SPEC }; }
#else
static buildid::Spec KnownSpec() { return buildid::Witness(); }
#endif
static bool g_buildKnown;
static buildid::Info g_build;
static void CheckBuild() {
    const uint8_t* base = (const uint8_t*)GetModuleHandleW(nullptr);
    size_t size = base ? buildid::MappedSize(base) : 0;
    if (size) g_build = buildid::Inspect(base, size, true, KnownSpec());
    g_buildKnown = g_build.verdict == buildid::kKnown;
#ifdef COOP_MUT_ALWAYS_KNOWN
    g_buildKnown = true;       // mutant: the check is ignored, an unknown build is hooked
#endif
}
static void UnsupportedBuild() {
    char what[200], line[400];
    buildid::Describe(g_build, what, sizeof what);
    snprintf(line, sizeof line, "BUILD: not %s (%s). Co-op is OFF; nothing of the game is patched, the sound calls pass through.", WITNESS_BUILD_NAME, what);
    Log(line);
    char off[8];
    if (GetEnvironmentVariableA("WITNESS_COOP_NO_DIALOG", off, sizeof off)) { Log("BUILD: message box suppressed (WITNESS_COOP_NO_DIALOG)"); return; }
    Log("BUILD: message box shown");
    char text[1600];
    snprintf(text, sizeof text, "%s\n\n%s\n\nFound: %s", WITNESS_UNSUPPORTED_SHORT, WITNESS_UNSUPPORTED_HELP, what);
    MessageBoxA(nullptr, text, "The Witness Co-op", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST);
}

void OverlayInstall();
void InstallHooks();
void CoopStart();
void GameStart();
void StateStart();
// A crash is logged before the game dies: the faulting address and the return addresses above it, each as module+offset, so a fault in a
// driver call can be traced to the call in this DLL that made it (map a dsound.dll offset with objdump or nm on the build).
// A stack overflow leaves almost no stack for the handler: its trace is written with static buffers and a file opened beforehand
// (witness_crash_stack.txt): each return address as module+offset, so a recursion between two hooks (the mod's and another's) shows.
static HANDLE g_overflowFile = INVALID_HANDLE_VALUE; static void* g_overflowFrames[96]; static char g_overflowLine[160]; static char g_overflowName[MAX_PATH];
static void OverflowTrace() {
    if (g_overflowFile == INVALID_HANDLE_VALUE) return;
    DWORD w; USHORT n = RtlCaptureStackBackTrace(0, 96, g_overflowFrames, nullptr);      // the innermost 96 frames
    for (USHORT i = 0; i < n; ++i) {
        HMODULE mod = nullptr; g_overflowName[0] = 0; uintptr_t off = (uintptr_t)g_overflowFrames[i];
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)g_overflowFrames[i], &mod) && mod) { GetModuleFileNameA(mod, g_overflowName, MAX_PATH); off -= (uintptr_t)mod; }
        const char* b = strrchr(g_overflowName, '\\'); b = b ? b + 1 : g_overflowName;
        int k = 0; for (const char* c = b; *c && k < 100; ++c) g_overflowLine[k++] = *c; g_overflowLine[k++] = '+'; g_overflowLine[k++] = '0'; g_overflowLine[k++] = 'x';
        for (int sh = 28; sh >= 0; sh -= 4) g_overflowLine[k++] = "0123456789abcdef"[(off >> sh) & 15]; g_overflowLine[k++] = '\r'; g_overflowLine[k++] = '\n';
        WriteFile(g_overflowFile, g_overflowLine, (DWORD)k, &w, nullptr);
    }
    FlushFileBuffers(g_overflowFile);
}
// It is a first-chance view: it also sees exceptions the game catches itself, so its lines are "first-chance:" and it writes no crash marker or dump (diag.cpp's unhandled-exception filter does, for a crash that really ends the game).
static LONG WINAPI CrashLogger(EXCEPTION_POINTERS* ep) {
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_STACK_OVERFLOW) { static volatile LONG once; if (InterlockedExchange(&once, 1) == 0) OverflowTrace(); return EXCEPTION_CONTINUE_SEARCH; }
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_PRIV_INSTRUCTION) return EXCEPTION_CONTINUE_SEARCH;
    DiagFirstChance(ep);      // test mutant only (diag_first_chance=1): a naive dump from here
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
    snprintf(m, sizeof m, "first-chance: exception 0x%lx at %s (thread %lu, access %llu of %p)", code, w, GetCurrentThreadId(), code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2 ? (unsigned long long)ep->ExceptionRecord->ExceptionInformation[0] : 0ull, code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2 ? (void*)ep->ExceptionRecord->ExceptionInformation[1] : nullptr); Log(m);
    void* frames[32]; USHORT n = RtlCaptureStackBackTrace(0, 32, frames, nullptr);
    for (USHORT i = 0; i < n; i++) { where(frames[i], w, sizeof w); snprintf(m, sizeof m, "first-chance: stack %2u %s", i, w); Log(m); }
    // the frames above are the handler's own; the faulting thread's stack is walked from its context
    CONTEXT* c = ep->ContextRecord; int depth = 0; char line[1200] = "first-chance: trace"; size_t at = strlen(line);
    for (uintptr_t* sp = (uintptr_t*)c->Rsp; depth < 24 && (uintptr_t)sp < c->Rsp + 4096; sp++) {
        if (IsBadReadPtr(sp, 8)) break; uintptr_t v = *sp; HMODULE mod = nullptr;
        if (v > 0x10000 && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)v, &mod) && mod) {
            MEMORY_BASIC_INFORMATION mbi; if (VirtualQuery((void*)v, &mbi, sizeof mbi) && (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) { where((void*)v, w, sizeof w); at += snprintf(line + at, sizeof line - at, " %s", w); depth++; if (at > sizeof line - 120) break; }
        }
    }
    Log(line); Sleep(300);      // the log thread gets a moment to write
    return EXCEPTION_CONTINUE_SEARCH;
}
// A friend accepted a Steam invite (or chose Join Game) while The Witness was not running: Steam starts the game itself with "+connect_lobby <id>".
// The lobby lives in the launcher, so the launcher is started with the same words (it inherits Steam's environment) and this game closes before it opens a window.
static bool HandOffSteamJoin() {
    uint64_t lobby = 0; if (!steamids::ParseConnect(GetCommandLineA(), &lobby)) return false;
    wchar_t dir[MAX_PATH]; GetModuleFileNameW(nullptr, dir, MAX_PATH); wchar_t* sl = wcsrchr(dir, L'\\'); if (sl) *sl = 0;
    std::wstring launcher = std::wstring(dir) + L"\\witness_launcher.exe";
    if (GetFileAttributesW(launcher.c_str()) == INVALID_FILE_ATTRIBUTES) { Log("steam: +connect_lobby on the command line, but no witness_launcher.exe beside the game"); return false; }
    std::wstring cmd = L"\"" + launcher + L"\" +connect_lobby " + std::to_wstring((unsigned long long)lobby);
    STARTUPINFOW si = {sizeof si}; PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, dir, &si, &pi)) { Log("steam: could not start the launcher for the Steam invite"); return false; }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); Log("steam: a Steam invite started the game: the launcher joins the lobby, this game closes"); Sleep(200);
    return true;
}
static DWORD WINAPI Init(LPVOID) {
    ChainLoad();
    if (g_otherCopy) {
        Log("OTHER COPY: the co-op mod's old AnselSDK64.dll (v0.1.0-alpha layout) is loaded too. This dsound.dll stays passive (sound calls pass through); "
            "co-op runs from the old copy. Run Setup to finish the move to dsound.dll (it puts the game's own AnselSDK64.dll back).");
        return 0;
    }
    if (!g_buildKnown) { UnsupportedBuild(); return 0; }
    char msg[300], exe[MAX_PATH];
    DiagStart();
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    snprintf(msg, sizeof msg, "proxy loaded: pid=%lu host=%s tick=%lu", GetCurrentProcessId(), exe, GetTickCount());
    Log(msg);
    if (HandOffSteamJoin()) TerminateProcess(GetCurrentProcess(), 0);
    { wchar_t p[MAX_PATH]; GetModuleFileNameW(g_selfModule(), p, MAX_PATH); wchar_t* sl = wcsrchr(p, L'\\'); if (sl) sl[1] = 0; wcscat_s(p, L"witness_crash_stack.txt");
      g_overflowFile = CreateFileW(p, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr); }
    AddVectoredExceptionHandler(1, CrashLogger);
    OverlayInstall();
    CoopStart();
    GameStart();
    StateStart();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_DETACH && reserved) DiagStop();      // the process is ending normally: a session log without this line ended in a crash or a kill
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        g_otherCopy = OldProxyMapped();      // memory only: no I/O under the loader lock
        CheckBuild();        // reads the exe's own header and code in memory: no I/O under the loader lock
        if (g_buildKnown && !g_otherCopy) InstallHooks();      // memory patching only: no I/O under the loader lock
        DisableThreadLibraryCalls(inst);
        HANDLE t = CreateThread(nullptr, 0, Init, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
