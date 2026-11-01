// Game-free test of common/crash_handler.h: a child process (this program, run again) crashes on purpose, and the parent looks at what was left.
//   crash-test.exe                      the parent: runs the cases below
//   crash-test.exe child <mode> <dir>   the child
// Cases: an access violation leaves the crash marker, the crash lines and a minidump; a stack overflow does too; an exception the program handles
// itself (IsBadReadPtr, which a vectored handler sees first) leaves none, and the run ends with "session end: clean exit"; with the handler disabled
// a crash leaves nothing but the log. Mutants (CRASH_MUT_*) change the child and the test must then fail.
#include "crash_handler.h"
#include <shellapi.h>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>

static void SayToSession(const char* s);
static diag::Session* g_s;
static void SayToSession(const char* s) { g_s->Line(s); }
static LONG CALLBACK Vectored(EXCEPTION_POINTERS* ep) {      // like the proxy's: it sees every first-chance exception
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) diag::FirstChanceMutant(ep);
    return EXCEPTION_CONTINUE_SEARCH;
}
static int Recurse(int n) { volatile char pad[2048]; pad[0] = (char)n; return pad[0] + Recurse(n + 1); }

static int Child(const std::string& mode, const std::wstring& dir) {
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    diag::Session s; if (!s.Open(dir, L"p", 0)) return 3; g_s = &s;
    diag::CrashOptions o; o.session = &s; o.dataModule = GetModuleHandleW(nullptr); o.log = SayToSession; o.enabled = true;
#ifdef CRASH_MUT_DISABLED
    o.enabled = false;                       // mutant: the handler does nothing
#endif
#ifdef CRASH_MUT_FIRSTCHANCE
    o.firstChance = true;                    // mutant: the dump is written from the first-chance handler
#endif
    diag::InstallCrashHandler(o);
    AddVectoredExceptionHandler(1, Vectored);
    s.Line("child started");
    if (mode == "handled") { BOOL bad = IsBadReadPtr(nullptr, 4); s.Line(bad ? "handled access violation done" : "no exception?"); s.Close(); return 0; }
    if (mode == "av") { volatile int* p = nullptr; *p = 1; }
    if (mode == "stack") Recurse(0);
    s.Close(); return 0;
}

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)
static std::string Slurp(const std::wstring& p) { std::ifstream f(diag::Session::Narrow(p), std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
static std::wstring Find(const std::wstring& dir, const wchar_t* ext) {
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd); std::wstring out;
    if (h != INVALID_HANDLE_VALUE) { do { std::wstring n = fd.cFileName; if (n.size() > wcslen(ext) && n.compare(n.size() - wcslen(ext), std::wstring::npos, ext) == 0) out = dir + L"\\" + n; } while (FindNextFileW(h, &fd)); FindClose(h); }
    return out;
}
static DWORD RunChild(const std::wstring& exe, const std::wstring& mode, const std::wstring& dir) {
    std::wstring cmd = L"\"" + exe + L"\" child " + mode + L" \"" + dir + L"\"";
    STARTUPINFOW si; memset(&si, 0, sizeof si); si.cb = sizeof si; PROCESS_INFORMATION pi; std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return 9999;
    WaitForSingleObject(pi.hProcess, 60000); DWORD code = 7777; GetExitCodeProcess(pi.hProcess, &code); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return code;
}

int main(int argc, char** argv) {
    if (argc >= 4 && !strcmp(argv[1], "child")) { int n; wchar_t* wa = CommandLineToArgvW(GetCommandLineW(), &n)[3]; return Child(argv[2], wa); }
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring root = (argc > 1 ? std::wstring(argv[1], argv[1] + strlen(argv[1])) : L"crash-test-out"); CreateDirectoryW(root.c_str(), nullptr);
    const char* cases[] = {"av", "stack", "handled"};
    for (const char* mode : cases) {
        std::wstring dir = root + L"\\" + std::wstring(mode, mode + strlen(mode)); CreateDirectoryW(dir.c_str(), nullptr);
        DeleteFileW((dir + L"\\x").c_str());
        DWORD code = RunChild(exe, std::wstring(mode, mode + strlen(mode)), dir);
        std::wstring log = Find(dir, L".log"), dmp = Find(dir, L".dmp"), marker = Find(dir, L".crash"); std::string text = Slurp(log);
        printf("case %s: exit 0x%lx, log %d bytes, marker %d, dump %d bytes\n", mode, (unsigned long)code, (int)text.size(), !marker.empty(), (int)Slurp(dmp).size());
        if (!strcmp(mode, "handled")) {
            CHECK(code == 0, "exit %lx", (unsigned long)code);
            CHECK(marker.empty() && dmp.empty(), "a handled exception left a crash marker or a dump");
            CHECK(text.find("session end: clean exit") != std::string::npos, "no clean exit line");
            CHECK(text.find("handled access violation done") != std::string::npos, "the child did not get there");
            CHECK(text.find("crash:") == std::string::npos || text.find("crash: the unhandled") != std::string::npos || text.find("diag: crash handler") != std::string::npos, "crash lines in a run that did not crash");
            continue;
        }
        CHECK(code != 0, "the child did not die");
        CHECK(!marker.empty(), "no crash marker");
        CHECK(text.find("crash: unhandled exception 0x") != std::string::npos, "no crash line: %s", text.c_str());
        CHECK(text.find(!strcmp(mode, "av") ? "0xc0000005" : "0xc00000fd") != std::string::npos, "wrong exception code");
        CHECK(text.find("crash-test.exe+0x") != std::string::npos, "no module+offset");
        CHECK(text.find("session end: clean exit") == std::string::npos, "a crashed run claims a clean exit");
        std::string d = Slurp(dmp);
        CHECK(d.size() > 1000 && d.compare(0, 4, "MDMP") == 0, "no valid minidump (%d bytes)", (int)d.size());
        CHECK(d.size() < 20u * 1024 * 1024, "dump too big: %d", (int)d.size());
        CHECK(text.find("crash: minidump written") != std::string::npos, "dump not reported");
    }
    printf(g_fail ? "crash_test: %d FAILED\n" : "crash_test: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
