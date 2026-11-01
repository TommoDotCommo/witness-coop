#pragma once
// The crash handler shared by the proxy and the launcher (test: common/crash_test.cpp, which crashes a child process on purpose).
//
// An UNHANDLED-exception filter, not a vectored handler. A vectored handler runs for every first-chance exception, including the ones the
// game catches itself (IsBadReadPtr and guarded code raise and handle access violations all the time): a marker and a dump from there would
// be written for runs that carry on. The filter runs only when nothing handled the exception. The game could replace the filter, so
// Recheck() puts ours back (and logs who replaced it). The dump is written from a fresh thread, so a stack overflow can still produce one.
// Not seen by any filter: __fastfail / STACK_BUFFER_OVERRUN and TerminateProcess; such a session has no "clean exit" line and no marker.
//
// On a crash: "crash:" lines in the session log (module+offset, registers, return addresses found on the stack), <session>.crash, the caller's
// onCrash (the flight recorder), and <session>.dmp: a small minidump with the data segments of the module named in Options::dataModule only.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstring>
#include "diag_log.h"

namespace diag {

struct CrashOptions {
    Session* session = nullptr;
    HMODULE dataModule = nullptr;          // the one module whose data segments go into the dump (the proxy / the launcher itself)
    bool enabled = true;                   // false: the filter is installed but does nothing (the test mutant)
    bool firstChance = false;              // test mutant: FirstChance() writes the crash from a first-chance handler
    void (*log)(const char* line) = nullptr;           // where "crash:" lines go (also the session log)
    void (*onCrash)() = nullptr;           // before the dump: the flight recorder
};

namespace crash_detail {
inline CrashOptions g_opt;
inline LPTOP_LEVEL_EXCEPTION_FILTER g_prev;
inline volatile LONG g_crashing;
typedef BOOL(WINAPI* WriteDumpFn)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
inline WriteDumpFn g_writeDump;

inline void Say(const char* s) { if (g_opt.log) g_opt.log(s); else if (g_opt.session) g_opt.session->Line(s); }

inline void Where(const void* a, char* out, size_t n) {
    HMODULE mod = nullptr; char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)a, &mod) && mod) {
        GetModuleFileNameA(mod, name, MAX_PATH); const char* sl = strrchr(name, '\\');
        snprintf(out, n, "%s+0x%llx", sl ? sl + 1 : name, (unsigned long long)((uintptr_t)a - (uintptr_t)mod));
    } else snprintf(out, n, "%p", a);
}

inline BOOL CALLBACK DumpCallback(PVOID, const PMINIDUMP_CALLBACK_INPUT in, PMINIDUMP_CALLBACK_OUTPUT out) {
    // the data segments of this module only: the game's and the system's are large
    if (in->CallbackType == ModuleCallback && in->Module.BaseOfImage != (ULONG64)(uintptr_t)g_opt.dataModule) out->ModuleWriteFlags &= ~(ULONG)ModuleWriteDataSeg;
    return TRUE;
}

struct DumpJob { EXCEPTION_POINTERS* ep; DWORD thread; };
inline DWORD WINAPI DumpThread(LPVOID p) {
    DumpJob* j = (DumpJob*)p;
    if (!g_writeDump) return 1;
    HANDLE h = CreateFileW((g_opt.session->Base() + L".dmp").c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 1;
    MINIDUMP_EXCEPTION_INFORMATION ei = {j->thread, j->ep, FALSE};
    MINIDUMP_CALLBACK_INFORMATION cb = {DumpCallback, nullptr};
    BOOL ok = g_writeDump(GetCurrentProcess(), GetCurrentProcessId(), h, (MINIDUMP_TYPE)(MiniDumpNormal | MiniDumpWithDataSegs | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules), &ei, nullptr, &cb);
    CloseHandle(h);
    return ok ? 0 : 2;
}

inline void Write(EXCEPTION_POINTERS* ep, const char* how) {
    EXCEPTION_RECORD* r = ep->ExceptionRecord; char w[200], m[500];
    Where(r->ExceptionAddress, w, sizeof w);
    snprintf(m, sizeof m, "crash: %s exception 0x%08lx at %s (thread %lu, %s %p)", how, (unsigned long)r->ExceptionCode, w, GetCurrentThreadId(),
             r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2 ? (r->ExceptionInformation[0] == 1 ? "writing" : r->ExceptionInformation[0] == 8 ? "executing" : "reading") : "info", r->NumberParameters >= 2 ? (void*)r->ExceptionInformation[1] : nullptr);
    Say(m);
    CONTEXT* c = ep->ContextRecord; char line[1200];
    snprintf(line, sizeof line, "crash: registers rip=%llx rsp=%llx rbp=%llx rax=%llx rbx=%llx rcx=%llx rdx=%llx rsi=%llx rdi=%llx", (unsigned long long)c->Rip, (unsigned long long)c->Rsp, (unsigned long long)c->Rbp, (unsigned long long)c->Rax, (unsigned long long)c->Rbx, (unsigned long long)c->Rcx, (unsigned long long)c->Rdx, (unsigned long long)c->Rsi, (unsigned long long)c->Rdi);
    Say(line);
    snprintf(line, sizeof line, "crash: trace"); size_t at = strlen(line); int depth = 0;
    for (uintptr_t* sp = (uintptr_t*)c->Rsp; depth < 24 && (uintptr_t)sp < c->Rsp + 8192; sp++) {      // raw stack words that point into executable code: return addresses to look at, not an unwound stack
        if (IsBadReadPtr(sp, 8)) break;
        uintptr_t v = *sp; HMODULE mod = nullptr;
        if (v > 0x10000 && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)v, &mod) && mod) {
            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery((void*)v, &mbi, sizeof mbi) && (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) { Where((void*)v, w, sizeof w); at += snprintf(line + at, sizeof line - at, " %s", w); depth++; if (at > sizeof line - 120) break; }
        }
    }
    Say(line);
    g_opt.session->MarkCrashed();
    if (g_opt.onCrash) g_opt.onCrash();
    DumpJob job = {ep, GetCurrentThreadId()};
    HANDLE t = CreateThread(nullptr, 0, DumpThread, &job, 0, nullptr);      // a fresh thread has a stack even after a stack overflow
    if (t) { DWORD wr = WaitForSingleObject(t, 15000); DWORD code = 0; GetExitCodeThread(t, &code); CloseHandle(t); snprintf(m, sizeof m, "crash: minidump %s", wr != WAIT_OBJECT_0 ? "timed out" : code == 0 ? "written" : "FAILED"); Say(m); }
}

inline LONG WINAPI Filter(EXCEPTION_POINTERS* ep) {
    if (g_opt.enabled && !InterlockedExchange(&g_crashing, 1)) { Write(ep, "unhandled"); Sleep(100); }
    if (g_prev && g_prev != Filter) return g_prev(ep);      // a crash reporter that was there before us still gets its turn
    return EXCEPTION_CONTINUE_SEARCH;
}
}  // namespace crash_detail

inline void InstallCrashHandler(const CrashOptions& o) {
    using namespace crash_detail;
    g_opt = o;
    char sys[MAX_PATH]; UINT n = GetSystemDirectoryA(sys, MAX_PATH);      // the system's dbghelp, never one from the game folder
    if (n && n < MAX_PATH - 16) { strcat(sys, "\\dbghelp.dll"); if (HMODULE d = LoadLibraryA(sys)) g_writeDump = (WriteDumpFn)(void*)GetProcAddress(d, "MiniDumpWriteDump"); }
    g_prev = SetUnhandledExceptionFilter(Filter);
    char m[300], w[200]; Where((void*)g_prev, w, sizeof w);
    snprintf(m, sizeof m, "diag: crash handler %s, dump writer %s, previous filter %s", o.enabled ? "on" : "OFF", g_writeDump ? "ready" : "MISSING", g_prev ? w : "none");
    Say(m);
}

// Called about once a second: the game could have replaced the filter. Puts ours back and says who replaced it.
inline void RecheckCrashHandler() {
    using namespace crash_detail;
    LPTOP_LEVEL_EXCEPTION_FILTER prev = SetUnhandledExceptionFilter(Filter);
    if (prev != Filter && prev != nullptr && prev != g_prev) {
        char w[200], m[300]; Where((void*)prev, w, sizeof w); snprintf(m, sizeof m, "crash: the unhandled-exception filter was replaced by %s; ours is back", w); Say(m); g_prev = prev;
    }
}

// What a naive first-chance (vectored) handler would do. Only the test mutant calls it with firstChance set.
inline void FirstChanceMutant(EXCEPTION_POINTERS* ep) {
    using namespace crash_detail;
    if (g_opt.firstChance && g_opt.enabled && !InterlockedExchange(&g_crashing, 1)) Write(ep, "first-chance");
}

}  // namespace diag
