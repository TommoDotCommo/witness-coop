#pragma once
// Per-session logs, shared by the proxy (the game) and the launcher, so a report from a machine nobody can touch is enough.
//
//  - Every run writes its own file, <logs>\<UTC start>[_launcher]_p<pid>.log, opened for appending and never replaced.
//    A line is "<n> <utc ms> <chain> <text>"; chain is FNV-1a/64 of "<n> <utc ms> <text>" continued from the previous
//    line's chain, so a missing, cut or edited line breaks every chain value after it (tools/diag_report.py and
//    dist/diag.ps1 check). That shows accidents, not forgery. "session end: clean exit" closes a run that ended
//    normally; <session>.crash marks one that crashed and keeps it (and its dumps) from being pruned.
//  - The last kRing lines are also kept in memory, for the flight recorder's dump.
//  - Prune keeps the folder under a size cap: the oldest runs go first; a crashed run and the running one never.
// Header-only on purpose: the launcher and the proxy are separate programs. Game-free test: common/diag_test.cpp.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace diag {

constexpr uint64_t kBasis = 0xcbf29ce484222325ULL, kPrime = 0x100000001b3ULL;
constexpr int kLineMax = 1000, kRing = 256, kRingText = 200;

inline uint64_t Fnv(uint64_t h, const char* s, size_t n) {
    for (size_t i = 0; i < n; i++) {
#ifndef DIAG_MUT_WEAK_CHAIN
        h ^= (unsigned char)s[i];
#else
        h ^= (unsigned char)(s[i] & 0x1f);      // mutant: a case change or a changed digit is not noticed
#endif
        h *= kPrime;
    }
    return h;
}

inline uint64_t UtcMs() {
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    return (u.QuadPart - 116444736000000000ULL) / 10000ULL;
}

// One log line as written, with the chain advanced. Line breaks inside a message become spaces.
inline std::string FormatLine(uint64_t* chain, unsigned n, uint64_t utcMs, const char* text) {
    std::string head = std::to_string(n) + " " + std::to_string((unsigned long long)utcMs) + " ";
    std::string body;
    for (size_t i = 0; text[i] && body.size() < (size_t)kLineMax; i++) body += (text[i] == '\r' || text[i] == '\n') ? ' ' : text[i];
    std::string payload = head + body;
#ifndef DIAG_MUT_NO_CHAIN
    *chain = Fnv(*chain, payload.data(), payload.size());
#endif
    char hex[20]; snprintf(hex, sizeof hex, "%08lx%08lx", (unsigned long)(*chain >> 32), (unsigned long)*chain);
    return head + hex + " " + body + "\n";
}

// "2026-10-26T10-00-00Z_..." : a file this library wrote
inline bool IsSessionFile(const wchar_t* n) {
#ifdef DIAG_MUT_PRUNE_ALL
    (void)n; return true;
#else
    static const char* pat = "dddd-dd-ddTdd-dd-ddZ_";
    for (int i = 0; pat[i]; i++) {
        if (!n[i]) return false;
        if (pat[i] == 'd' ? !(n[i] >= L'0' && n[i] <= L'9') : n[i] != (wchar_t)pat[i]) return false;
    }
    return true;
#endif
}

struct FileEntry { std::wstring name; unsigned long long size; };

inline std::wstring KeyOf(const std::wstring& name) { return name.substr(0, name.find(L'.')); }

// Keep the folder under capBytes. Oldest session (by name: the key starts with its UTC time) first; never the running one, never
// one with a .crash marker (its log, flight recordings and dump all share the key). Returns the number of files deleted.
inline int Prune(const std::wstring& dir, unsigned long long capBytes, const std::wstring& currentKey) {
    std::vector<FileEntry> files; unsigned long long total = 0;
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!IsSessionFile(fd.cFileName)) continue;      // only our own files: a logs folder shared with something else (the game's own logs) is left alone
        unsigned long long sz = ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        files.push_back({fd.cFileName, sz}); total += sz;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    int deleted = 0;
    while (total > capBytes) {
        int best = -1;
        for (size_t i = 0; i < files.size(); i++) {
            if (files[i].name.empty()) continue;
            std::wstring key = KeyOf(files[i].name);
            if (key == currentKey) continue;
#ifndef DIAG_MUT_PRUNE_CRASHED
            bool crashed = false;
            for (const auto& o : files) if (o.name == key + L".crash") { crashed = true; break; }
            if (crashed) continue;
#endif
            if (best < 0 || files[i].name < files[best].name) best = (int)i;
        }
        if (best < 0) break;
        if (DeleteFileW((dir + L"\\" + files[best].name).c_str())) deleted++;
        total -= files[best].size; files[best].name.clear();
    }
    return deleted;
}

class Session {
public:
    // dir: created if missing. kind: "p" for the game, "launcher_p" for the launcher. capBytes: Prune's cap (0: no pruning).
    bool Open(const std::wstring& dir, const wchar_t* kind, unsigned long long capBytes) {
        if (!init_) { InitializeCriticalSection(&cs_); init_ = true; }
        dir_ = dir;
        CreateDirectoryW(dir.c_str(), nullptr);
        SYSTEMTIME st; GetSystemTime(&st);
        wchar_t k[96]; swprintf(k, 96, L"%04d-%02d-%02dT%02d-%02d-%02dZ_%ls%lu", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, kind, GetCurrentProcessId());
        key_ = k; base_ = dir + L"\\" + key_;
        if (capBytes) Prune(dir, capBytes, key_);
        // appending only; others may read it while the process runs, nobody can delete it from under us
        file_ = CreateFileW((base_ + L".log").c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        return file_ != INVALID_HANDLE_VALUE;
    }
    bool Ready() const { return init_ && file_ != INVALID_HANDLE_VALUE; }
    const std::wstring& Key() const { return key_; }
    const std::wstring& Base() const { return base_; }
    const std::wstring& Dir() const { return dir_; }
    std::string KeyUtf8() const { return Narrow(key_); }

    void Line(const char* text) { Write(text, true); }

    void Close() {
        if (!init_) return;
        Write("session end: clean exit", true);
        EnterCriticalSection(&cs_);
        if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
        LeaveCriticalSection(&cs_);
    }

    // The crash marker (<key>.crash). Safe from an exception filter: takes no lock.
    void MarkCrashed() {
        if (!init_) return;
        HANDLE h = CreateFileW((base_ + L".crash").c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }

    // The last lines, oldest first, as "L <utc> <text>" for the flight recorder. In a crash handler another thread may hold the lock: then the ring is read as it is.
    std::string RingText() {
        bool locked = init_ && TryEnterCriticalSection(&cs_);
        std::string out;
        unsigned count = ringN_ < (unsigned)kRing ? ringN_ : (unsigned)kRing, from = ringN_ - count;
        for (unsigned i = from; i < ringN_; i++) {
            const Slot& s = ring_[i % kRing]; char head[40]; snprintf(head, sizeof head, "L %llu ", (unsigned long long)s.utc);
            out += head; out += s.text; out += "\n";
        }
        if (locked) LeaveCriticalSection(&cs_);
        return out;
    }

    static std::string Narrow(const std::wstring& w) {
        if (w.empty()) return "";
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr); return s;
    }

private:
    struct Slot { uint64_t utc; char text[kRingText]; };
    void Write(const char* text, bool ring) {
        if (!init_) return;
        EnterCriticalSection(&cs_);
        uint64_t now = UtcMs();
        if (ring) { Slot& s = ring_[ringN_++ % kRing]; s.utc = now; snprintf(s.text, kRingText, "%s", text); for (char* c = s.text; *c; ++c) if (*c == '\r' || *c == '\n') *c = ' '; }
        if (file_ != INVALID_HANDLE_VALUE) {
            std::string line = FormatLine(&chain_, lineNo_, now, text); DWORD w;
            WriteFile(file_, line.data(), (DWORD)line.size(), &w, nullptr);
        }
        lineNo_++;
        LeaveCriticalSection(&cs_);
    }
    CRITICAL_SECTION cs_; bool init_ = false;
    HANDLE file_ = INVALID_HANDLE_VALUE;
    uint64_t chain_ = kBasis; unsigned lineNo_ = 0;
    std::wstring dir_, key_, base_;
    Slot ring_[kRing]; unsigned ringN_ = 0;
};

// %LOCALAPPDATA%\witness-coop\logs
inline std::wstring DefaultLogsDir() {
    wchar_t p[MAX_PATH]; DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", p, MAX_PATH);
    if (!n || n >= MAX_PATH - 40) return L"";
    std::wstring d = p; d += L"\\witness-coop"; CreateDirectoryW(d.c_str(), nullptr); return d + L"\\logs";
}

}  // namespace diag
