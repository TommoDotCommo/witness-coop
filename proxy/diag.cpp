// Diagnostics, so a report from a machine nobody can touch is enough (the same design as the MDK2 co-op mod's bridge/diag.c).
//  - Session logs: common/diag_log.h. Every run has its own %LOCALAPPDATA%\witness-coop\logs\<UTC start>_p<pid>.log (diag_dir= moves it,
//    diag_dir=instance puts it beside the DLL for test copies); witness_proxy.log is still the last run, for the test tools.
//  - Crash handler: an UNHANDLED-exception filter, not a vectored handler. A vectored handler runs for every first-chance exception, including
//    the ones the game catches itself (IsBadReadPtr and the game's own guards raise and handle access violations all the time): a dump from there
//    would be written for runs that carry on. The filter runs only when nothing handled the exception. The game could replace the filter, so
//    DiagFrame puts ours back (and logs who replaced it) once a second. A stack overflow runs the filter on a fresh thread's stack for the dump.
//    Not caught by any filter: __fastfail / STACK_BUFFER_OVERRUN and a TerminateProcess; the session then has no "clean exit" line and no marker.
//    On a crash: the "crash:" lines (module+offset, registers, the return addresses found on the stack), <session>.crash, the flight recorder
//    (<session>.flight-0-crash.txt), and <session>.dmp (a small minidump with the proxy module's data segments; tools/diag_report.py reads it).
//  - Flight recorder: the last 60 s at 10 Hz (frame rate, where the players are, round trip and loss to each link, sync queue depth, world
//    generation), written on a crash, on a desync the sync core reports, and when F2 is pressed (the marker): the marker is also sent to the
//    other players as a chat line only the diagnostics read (\x01diag mark N), so every machine writes its recording of the same moment.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include "../common/diag_log.h"
#include "../common/crash_handler.h"
#include "config.h"
#include "coop.h"
#include "devmenu.h"
#include "diag.h"
#include "version.h"

void Log(const char* msg);
extern HMODULE g_selfModule();

static diag::Session g_sess; static volatile LONG g_state;      // 0: not opened, 1: opening, 2: opened (or failed: g_sess.Ready() says)
static std::wstring g_dir;
const std::wstring& DiagDir() { return g_dir; }

static void RotateProxyLog() {      // witness_proxy.log is the last run: the one before it is kept as witness_proxy.previous.log
    wchar_t path[MAX_PATH], prev[MAX_PATH]; wcscpy_s(path, DataDir().c_str());
    wcscpy_s(prev, path); wcscat_s(path, L"witness_proxy.log"); wcscat_s(prev, L"witness_proxy.previous.log");
    MoveFileExW(path, prev, MOVEFILE_REPLACE_EXISTING);
}

static void EnsureOpen() {
    if (g_state == 2) return;
    if (InterlockedCompareExchange(&g_state, 1, 0) != 0) { for (int i = 0; i < 200 && g_state != 2; i++) Sleep(1); return; }
    RotateProxyLog();
    const Cfg& c = GetCfg();
    g_dir = c.diagDir.empty() ? diag::DefaultLogsDir() : c.diagDir;
    if (!g_dir.empty() && g_sess.Open(g_dir, L"p", (unsigned long long)c.maxLogMb * 1024 * 1024)) {
        char l[200]; snprintf(l, sizeof l, "session %s: mod %s", g_sess.KeyUtf8().c_str(), MOD_VERSION); g_sess.Line(l);
    }
    InterlockedExchange(&g_state, 2);
}

void DiagLine(const char* text) { EnsureOpen(); if (g_sess.Ready()) g_sess.Line(text); }
void DiagStop() { if (g_state == 2 && g_sess.Ready()) g_sess.Close(); }

// ---------------------------------------------------------------------------------------------------------------- flight recorder
namespace {
constexpr int kFlightN = 600, kSampleMs = 100, kMaxLinks = 16, kMaxDumps = 12;
struct FPlayer { unsigned id; short ping; unsigned short ageMs; unsigned short loss10; float pos[3]; };
struct FSample { uint64_t utc; unsigned short fps10; unsigned unacked, queued, sent, resent; uint64_t gen; float pos[3]; int n; FPlayer p[kMaxLinks]; };
FSample g_flight[kFlightN]; unsigned g_flightN; CRITICAL_SECTION g_flightCs; volatile LONG g_flightCsInit;
int g_dumps; unsigned g_markNo; std::atomic<int> g_markPending{0};
struct Link { volatile LONG rx, first, maxSeq; };      // pose datagrams received from a player and the sequence numbers they carried: loss is the gap
Link g_link[kMaxLinks];

void FlightLock(bool crash) {
    if (!g_flightCsInit && InterlockedCompareExchange(&g_flightCsInit, 1, 0) == 0) InitializeCriticalSection(&g_flightCs);
    (void)crash;
}
}  // namespace

void DiagRxPose(const uint8_t* d, size_t n) {
    if (n != 40) return;
    uint32_t magic, from, seq; memcpy(&magic, d, 4); memcpy(&from, d + 4, 4); memcpy(&seq, d + 8, 4);
    if ((magic != 0x36505457u && magic != 0x37505457u) || from == 0 || from >= (unsigned)kMaxLinks) return;
    Link& l = g_link[from];
    if (!l.rx || seq + 100 < (uint32_t)l.maxSeq) { l.first = (LONG)seq; l.maxSeq = (LONG)seq; l.rx = 0; }      // the first one, or the player restarted
    if ((LONG)seq > l.maxSeq) l.maxSeq = (LONG)seq;
    l.rx++;
}

static unsigned Loss10(unsigned id) {
    if (id >= (unsigned)kMaxLinks) return 0;
    const Link& l = g_link[id]; long span = (long)l.maxSeq - (long)l.first + 1;
    return span > 20 && l.rx <= span ? (unsigned)((span - l.rx) * 1000L / span) : 0;
}

static void Sample(const float* cam, double fps) {
    if (GetCfg().noDiagFlight) return;
    FlightLock(false); EnterCriticalSection(&g_flightCs);
    FSample& f = g_flight[g_flightN++ % kFlightN]; memset(&f, 0, sizeof f);
    f.utc = diag::UtcMs(); f.fps10 = (unsigned short)(fps * 10 + 0.5); if (cam) memcpy(f.pos, cam, sizeof f.pos);
    if (auto* c = CoopCore()) { f.unacked = (unsigned)c->unacked(); f.queued = (unsigned)c->queuedRemote(); auto st = c->stats(); f.sent = st.sent; f.resent = st.resent; }
    if (auto* w = GameWorldInstance()) f.gen = w->generation();
    for (const auto& r : CoopGetRemotes()) {
        if (f.n >= kMaxLinks) break;
        FPlayer& p = f.p[f.n++]; p.id = r.id; p.ping = (short)r.pingMs; p.ageMs = (unsigned short)(r.ageMs > 65535 ? 65535 : r.ageMs); p.loss10 = (unsigned short)Loss10(r.id); memcpy(p.pos, r.pos, sizeof p.pos);
    }
    LeaveCriticalSection(&g_flightCs);
}

// kind: crash, mark, markp<N> (a marker another player pressed), desync. The file is new each time and never replaced.
static void DumpFlight(const char* kind, unsigned marker, const char* why) {
    if (!g_sess.Ready()) return;
    bool crash = !strcmp(kind, "crash");
    static ULONGLONG lastDesync;
    if (!crash) {
        if (g_dumps >= kMaxDumps) return;
        if (!strcmp(kind, "desync")) { ULONGLONG now = GetTickCount64(); if (lastDesync && now - lastDesync < 5000) return; lastDesync = now; }
    }
    g_dumps++;
    char name[96]; snprintf(name, sizeof name, ".flight-%u-%s.txt", marker, kind);
    std::wstring path = g_sess.Base() + std::wstring(name, name + strlen(name));
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    std::string o; char b[400];
    snprintf(b, sizeof b, "# flight recorder: kind=%s marker=%u utc=%llu session=%s mod=%s\n# reason: %s\n", kind, marker, (unsigned long long)diag::UtcMs(), g_sess.KeyUtf8().c_str(), MOD_VERSION, why ? why : ""); o += b;
    snprintf(b, sizeof b, "# me: player %u, %s\n", CoopMyId(), GetCfg().race ? "race" : "co-op"); o += b;
    o += "# S utc fps x y z generation unacked queued sent resent | id ping_ms pose_age_ms loss% x y z ; ... (10 samples a second, oldest first)\n";
    FlightLock(crash);
    bool locked = crash ? TryEnterCriticalSection(&g_flightCs) != 0 : (EnterCriticalSection(&g_flightCs), true);
    unsigned count = g_flightN < (unsigned)kFlightN ? g_flightN : (unsigned)kFlightN, from = g_flightN - count;
    for (unsigned i = from; i < g_flightN; i++) {
        const FSample& f = g_flight[i % kFlightN];
        snprintf(b, sizeof b, "S %llu %.1f %.2f %.2f %.2f %llx %u %u %u %u |", (unsigned long long)f.utc, f.fps10 / 10.0, f.pos[0], f.pos[1], f.pos[2], (unsigned long long)f.gen, f.unacked, f.queued, f.sent, f.resent); o += b;
        for (int k = 0; k < f.n; k++) { const FPlayer& p = f.p[k]; snprintf(b, sizeof b, " %u %d %u %.1f %.2f %.2f %.2f ;", p.id, p.ping, p.ageMs, p.loss10 / 10.0, p.pos[0], p.pos[1], p.pos[2]); o += b; }
        o += "\n";
    }
    if (locked) LeaveCriticalSection(&g_flightCs);
    o += "# log tail\n"; o += g_sess.RingText();
    DWORD w; WriteFile(h, o.data(), (DWORD)o.size(), &w, nullptr); CloseHandle(h);
}

// ---------------------------------------------------------------------------------------------------------------- markers
static const char kMarkPrefix[] = "\x01" "diag mark ";

void DiagMarkKey() { g_markPending.fetch_add(1); }

static void DoMark() {      // the render thread
    unsigned n = ++g_markNo; char l[120];
    snprintf(l, sizeof l, "MARK %u pressed here", n); Log(l);
    DumpFlight("mark", n, l);
    if (!GetCfg().noDiagMarkSend) if (auto* c = CoopCore()) { char t[64]; snprintf(t, sizeof t, "%s%u", kMarkPrefix, n); c->sendChat(t); }
    char toast[80]; snprintf(toast, sizeof toast, "Marker %u saved. Send diagnostics from the launcher.", n); DevMenuNotice(toast);
}

bool DiagChat(unsigned from, const std::string& text) {
    if (text.compare(0, sizeof kMarkPrefix - 1, kMarkPrefix) != 0) return false;
    unsigned n = (unsigned)strtoul(text.c_str() + sizeof kMarkPrefix - 1, nullptr, 10);
    char l[120], k[24]; snprintf(l, sizeof l, "MARK %u (from player %u)", n, from); Log(l);
    snprintf(k, sizeof k, "markp%u", from); DumpFlight(k, n, l);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------- desync
void DiagCoreLog(const char* t) {
    if (!strstr(t, "wrong entity type") && !strstr(t, "group backlog full")) return;
    char why[300]; snprintf(why, sizeof why, "the sync core reports the worlds differ: %s", t);
    Log(("DESYNC " + std::string(why)).c_str());
    DumpFlight("desync", (unsigned)(diag::UtcMs() / 1000), why);
}

// ---------------------------------------------------------------------------------------------------------------- crash handler
// common/crash_handler.h (the unhandled-exception filter, the dump) is shared and tested game-free; this file gives it the session, the log and the flight recorder.
static void CrashFlight() { DumpFlight("crash", 0, "unhandled exception, see the crash lines in the session log"); }
void DiagFirstChance(EXCEPTION_POINTERS* ep) { diag::FirstChanceMutant(ep); }

void DiagStart() {
    EnsureOpen();
    diag::CrashOptions o; o.session = &g_sess; o.dataModule = g_selfModule(); o.enabled = !GetCfg().noCrashHandler; o.firstChance = GetCfg().diagFirstChance;
    o.log = Log; o.onCrash = CrashFlight;
    diag::InstallCrashHandler(o);
    char m[300]; snprintf(m, sizeof m, "diag: logs in %s, mod %s", diag::Session::Narrow(g_dir).c_str(), MOD_VERSION); Log(m);
}

// ---------------------------------------------------------------------------------------------------------------- per frame, commands
static void DiagRenderTest();
void DiagFrame(const float* cam, unsigned frames) {
    static ULONGLONG nextSample, lastSample, nextCheck; static unsigned lastFrames;
    ULONGLONG now = GetTickCount64();
    if (now >= nextSample) {
        if (lastSample) Sample(cam, now > lastSample ? (frames - lastFrames) * 1000.0 / (double)(now - lastSample) : 0);
        lastSample = now; lastFrames = frames; nextSample = now + kSampleMs;
    }
    if (now >= nextCheck) { nextCheck = now + 1000; if (!GetCfg().noCrashHandler) diag::RecheckCrashHandler(); }
    if (g_markPending.load()) { g_markPending.fetch_sub(1); DoMark(); }
    DiagRenderTest();
}

static volatile LONG g_testAction;      // set by the command thread, done on the render thread: 1 crash, 2 handled exception
bool DiagCommand(const char* line) {
    if (!strncmp(line, "diagmark", 8)) { DiagMarkKey(); return true; }
    if (!strncmp(line, "diagdesync", 10)) { DiagCoreLog("state: remote event kind 1 entity 0x1: wrong entity type, ignored"); return true; }
    if (!strncmp(line, "testcrash", 9)) { if (GetCfg().testCrash) { Log("diag: test crash requested"); InterlockedExchange(&g_testAction, 1); } else Log("diag: testcrash ignored (test_crash=1 is not set)"); return true; }
    if (!strncmp(line, "testhandled", 11)) { if (GetCfg().testCrash) InterlockedExchange(&g_testAction, 2); return true; }
    return false;
}

static void DiagRenderTest() {      // render thread, from the same Present hook as DiagFrame
    LONG a = InterlockedExchange(&g_testAction, 0);
    if (a == 1) { SetErrorMode(GetErrorMode() | SEM_NOGPFAULTERRORBOX); volatile int* p = nullptr; *p = 1; }      // test only: no Windows error dialog during automated runs
    if (a == 2) {
        // exceptions the game handles itself: IsBadReadPtr raises an access violation and catches it inside kernel32. A first-chance
        // handler sees it; the unhandled filter must not.
        BOOL bad = IsBadReadPtr(nullptr, 4); char m[80]; snprintf(m, sizeof m, "diag: handled access violation done (IsBadReadPtr=%d)", (int)bad); Log(m);
    }
}
