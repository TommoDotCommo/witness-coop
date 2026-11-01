// Game-free test of common/diag_log.h (runs on Windows or under WSL interop; mutants in the Makefile must fail it).
#include "diag_log.h"
#include <cstdlib>
#include <fstream>
#include <sstream>

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

// An independent reader: the same rule as tools/diag_report.py. Returns "" when the chain holds, else what is wrong.
static std::string Verify(const std::string& data, int* lines) {
    uint64_t h = 0xcbf29ce484222325ULL; size_t pos = 0; int n = 0;
    while (pos < data.size()) {
        size_t end = data.find('\n', pos); if (end == std::string::npos) break;
        std::string raw = data.substr(pos, end - pos); pos = end + 1;
        size_t a = raw.find(' '), b = raw.find(' ', a + 1), c = raw.find(' ', b + 1);
        if (c == std::string::npos) return "malformed line " + std::to_string(n);
        if (raw.substr(0, a) != std::to_string(n)) return "line number " + raw.substr(0, a) + " where " + std::to_string(n) + " was expected";
        std::string payload = raw.substr(0, b) + " " + raw.substr(c + 1);
        for (unsigned char ch : payload) { h ^= ch; h *= 0x100000001b3ULL; }
        char want[20]; snprintf(want, sizeof want, "%016llx", (unsigned long long)h);
        if (raw.substr(b + 1, c - b - 1) != want) return "chain mismatch at line " + std::to_string(n);
        n++;
    }
    if (lines) *lines = n;
    return "";
}
static std::string Slurp(const std::wstring& p) { std::ifstream f(std::string(diag::Session::Narrow(p)), std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
static void Put(const std::wstring& p, size_t bytes) { FILE* f = _wfopen(p.c_str(), L"wb"); std::string s(bytes, 'x'); fwrite(s.data(), 1, s.size(), f); fclose(f); }
static bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

int main() {
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
    std::wstring root = std::wstring(tmp) + L"witness-diag-test-" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(root.c_str(), nullptr);

    // --- a session log: lines numbered, chained, closed with the clean-exit line, newlines flattened
    {
        diag::Session s; CHECK(s.Open(root + L"\\a", L"p", 0), "open");
        s.Line("first"); s.Line("two\nlines\r here"); for (int i = 0; i < 50; i++) { char t[64]; snprintf(t, sizeof t, "state: line %d", i); s.Line(t); }
        s.Close();
        std::string data = Slurp(s.Base() + L".log"); int n = 0;
        CHECK(Verify(data, &n).empty(), "%s", Verify(data, &n).c_str());
        CHECK(n == 53, "lines %d", n);
        CHECK(data.find("two lines  here") != std::string::npos, "newline not flattened");
        CHECK(data.size() > 8 && data.substr(data.size() - 24) == "session end: clean exit\n", "no clean exit line");
        // an edited line, a changed case, a removed line, a cut tail
        std::string edited = data; size_t at = edited.find("state: line 7"); edited[at + 7] = 'L';     // 'l' -> 'L': the same low five bits
        CHECK(!Verify(edited, nullptr).empty(), "a case change went unnoticed");
        std::string digit = data; at = digit.find("line 12"); digit[at + 6] = '3';
        CHECK(!Verify(digit, nullptr).empty(), "a changed digit went unnoticed");
        std::string cut = data; size_t l3 = cut.find('\n', cut.find('\n') + 1); cut.erase(0, l3 + 1);      // first two lines gone
        CHECK(!Verify(cut, nullptr).empty(), "missing lines went unnoticed");
        size_t a = data.find('\n'), b = data.find('\n', a + 1); std::string hole = data.substr(0, a + 1) + data.substr(b + 1);
        CHECK(!Verify(hole, nullptr).empty(), "a removed line went unnoticed");
        CHECK(Verify(data + "7 1 abc half", nullptr).empty(), "a half-written last line is not a broken chain");
        // the ring has the tail for the flight recorder
        std::string ring = s.RingText(); CHECK(ring.find("L ") == 0 && ring.find("state: line 49") != std::string::npos, "ring: %s", ring.c_str());
        // the crash marker
        CHECK(!Exists(s.Base() + L".crash"), "marker before a crash"); s.MarkCrashed(); CHECK(Exists(s.Base() + L".crash"), "no marker");
    }
    // --- the chain is the same text the Python/PowerShell readers hash: pinned value
    {
        uint64_t c = diag::kBasis; std::string l = diag::FormatLine(&c, 0, 1761570000000ULL, "hello");
        CHECK(l == "0 1761570000000 " + [&] { char h[20]; snprintf(h, sizeof h, "%016llx", (unsigned long long)c); return std::string(h); }() + " hello\n", "format %s", l.c_str());
        CHECK(c != diag::kBasis, "chain did not move");
        uint64_t c2 = diag::kBasis; diag::FormatLine(&c2, 0, 1761570000000ULL, "hellp"); CHECK(c2 != c, "different text, same chain");
    }
    // --- pruning: oldest first, never the running one, never a crashed one
    {
        std::wstring d = root + L"\\prune"; CreateDirectoryW(d.c_str(), nullptr);
        const wchar_t* keys[] = {L"2026-10-01T00-00-00Z_p1", L"2026-10-02T00-00-00Z_p2", L"2026-10-03T00-00-00Z_p3", L"2026-10-04T00-00-00Z_p4", L"2026-10-05T00-00-00Z_p5"};
        for (auto k : keys) { Put(d + L"\\" + k + L".log", 1000); }
        Put(d + L"\\2026-10-01T00-00-00Z_p1.crash", 0); Put(d + L"\\2026-10-01T00-00-00Z_p1.dmp", 1000);      // the oldest crashed
        Put(d + L"\\2026-10-02T00-00-00Z_p2.flight-3-mark.txt", 500);
        int gone = diag::Prune(d, 3000, keys[4]);       // 5 logs + dump + flight = 6500 bytes; cap 3000
        CHECK(Exists(d + L"\\2026-10-01T00-00-00Z_p1.log") && Exists(d + L"\\2026-10-01T00-00-00Z_p1.dmp"), "a crashed session was pruned");
        CHECK(!Exists(d + L"\\2026-10-02T00-00-00Z_p2.log") && !Exists(d + L"\\2026-10-02T00-00-00Z_p2.flight-3-mark.txt"), "the oldest plain session stayed");
        CHECK(Exists(d + L"\\2026-10-05T00-00-00Z_p5.log"), "the running session was pruned");
        CHECK(gone >= 2, "deleted %d", gone);
        unsigned long long total = 0; WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((d + L"\\*").c_str(), &fd);
        do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) total += fd.nFileSizeLow; } while (FindNextFileW(h, &fd)); FindClose(h);
        CHECK(total <= 3000 || Exists(d + L"\\2026-10-03T00-00-00Z_p3.log") == false, "folder still over the cap: %llu", total);
        CHECK(total <= 4000, "cap not honoured: %llu", total);       // crashed + running stay: 2000 + 1000
    }
    // --- a folder shared with other files (the game's own logs): only our session files are ever counted or deleted
    {
        std::wstring d = root + L"\\shared"; CreateDirectoryW(d.c_str(), nullptr);
        Put(d + L"\\log_2026-11-01_21h30m.txt", 5000); Put(d + L"\\nvmesh_2026-11-01_21h30m.txt", 5000); Put(d + L"\\2026-10-01T00-00-00Z_p1.log", 100);
        diag::Prune(d, 10, L"2026-10-09T00-00-00Z_p9");
        CHECK(Exists(d + L"\\log_2026-11-01_21h30m.txt") && Exists(d + L"\\nvmesh_2026-11-01_21h30m.txt"), "a file that is not ours was pruned");
        CHECK(!Exists(d + L"\\2026-10-01T00-00-00Z_p1.log"), "our own old session was kept although the cap is tiny");
    }
    // --- a name with no dot, and a folder with a subfolder, do not break pruning
    { std::wstring d = root + L"\\odd"; CreateDirectoryW(d.c_str(), nullptr); CreateDirectoryW((d + L"\\sub").c_str(), nullptr); Put(d + L"\\noext", 100); diag::Prune(d, 10, L"zzz"); CHECK(Exists(d + L"\\noext"), "a file that is not a session file was deleted"); }
    printf(g_fail ? "diag_test: %d FAILED\n" : "diag_test: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
