// See race_game.h. HUD: the countdown in the middle, the clock and the goal at the top, the standings at the right, and the winner when it is certain.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include "race_game.h"
#include "race/race.h"
#include "config.h"
#include "coop.h"
#include "playercolor.h"
#include "ui.h"

void Log(const char* msg);

namespace {
CRITICAL_SECTION* Lk() { static struct Init { CRITICAL_SECTION cs; Init() { InitializeCriticalSection(&cs); } } init; return &init.cs; }   // from first use, not from RaceStart
bool g_init; wrace::Race* g_race; unsigned g_my; wrace::Goal g_goal; ULONGLONG g_lastTick; volatile LONG g_hudFrames; ULONGLONG g_goShown;
struct Lock { Lock() { EnterCriticalSection(Lk()); } ~Lock() { LeaveCriticalSection(Lk()); } };
std::string NameOf(unsigned id) {
    if (id == g_my) return CoopMyName();
    for (const auto& rp : CoopGetRemotes()) if (rp.id == id && rp.name[0]) return rp.name;
    char b[24]; snprintf(b, sizeof b, "Player %u", id); return b;
}
std::string Clock(uint32_t ms) { char b[24]; snprintf(b, sizeof b, "%u:%02u.%u", ms / 60000, (ms / 1000) % 60, (ms / 100) % 10); return b; }
const char* PhaseName(wrace::Phase p) { return p == wrace::kWaiting ? "loading" : p == wrace::kReady ? "ready" : p == wrace::kCountdown ? "countdown" : p == wrace::kRunning ? "running" : "finished"; }
}  // namespace

void RaceStart(unsigned myId, const std::vector<unsigned>& others, const std::function<void(const std::vector<uint8_t>&)>& send) {
    g_init = true;
    const Cfg& c = GetCfg(); wrace::Goal g; if (!wrace::ParseGoal(c.raceGoal.c_str(), &g)) { char m[120]; snprintf(m, sizeof m, "race: goal '%s' not understood, using panel:9F7F", c.raceGoal.c_str()); Log(m); wrace::ParseGoal("panel:9F7F", &g); }
    Lock l; g_my = myId; g_goal = g; g_race = new wrace::Race(myId, g, (uint32_t)time(nullptr), send, (uint32_t)c.raceCountdownMs); g_race->SetRoster(others);
    char m[160]; snprintf(m, sizeof m, "race: %zu other players, goal %s", others.size(), wrace::GoalText(g).c_str()); Log(m);
}
bool RaceActive() { return g_race != nullptr; }
// The result of a race, for the launcher's lobby (witness_race_result.txt next to the DLL): the goal, the winner, and one line per player: place|name|finish ms|phase.
static void WriteResult(uint64_t now) {
    wchar_t wp[MAX_PATH]; HMODULE self = nullptr; GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&WriteResult, &self); GetModuleFileNameW(self, wp, MAX_PATH);
    wchar_t* sl = wcsrchr(wp, L'\\'); if (sl) *(sl + 1) = 0; wcscat_s(wp, L"witness_race_result.txt"); FILE* f = _wfopen(wp, L"w"); if (!f) return;
    fprintf(f, "goal=%s\nwinner=%u\n", wrace::GoalText(g_goal).c_str(), g_race->winner());
    for (const auto& s : g_race->standings(now)) fprintf(f, "%d|%s|%u|%s\n", s.place, NameOf(s.id).c_str(), s.finishMs, PhaseName(s.phase));
    fclose(f);
}
void RaceTick(uint64_t now, bool worldReady, const std::set<unsigned>& solved) {
    if (!g_race) return; if (now - g_lastTick < 20) return; g_lastTick = now;
    Lock l; wrace::Phase before = g_race->phase(); g_race->Tick(now, worldReady, solved);
    if (g_race->phase() != before) { char m[120]; snprintf(m, sizeof m, "race: %s -> %s", PhaseName(before), PhaseName(g_race->phase())); Log(m); }
    static bool written; if (!written && g_race->decided(now)) { written = true; WriteResult(now); Log("race: result written"); }
}
void RaceOnPacket(const uint8_t* d, size_t n, uint64_t now) { if (!g_race) return; Lock l; g_race->OnPacket(d, n, now); }

void RaceDraw(float W, float H) {
    if (!g_race || GetCfg().noRaceHud) return;
    Lock l; ULONGLONG now = GetTickCount64(); float k = std::max(1.0f, H / 1080.0f); InterlockedIncrement(&g_hudFrames);
    wrace::Phase ph = g_race->phase(); char b[160]; float cx = W / 2;
    auto centered = [&](const char* s, float y, float sc, UiColor col) { float tw = UiTextWidth(s, sc * k); UiRect(cx - tw / 2 - 14 * k, y - 6 * k, tw + 28 * k, UiLineHeight(sc * k) + 12 * k, Rgba(0, 0, 0, 0.55f)); UiText(cx - tw / 2, y, s, col, sc * k); };
    if (ph == wrace::kWaiting) centered("RACE   loading the world", 60 * k, 1.0f, Rgba(0.85f, 0.88f, 0.95f));
    else if (ph == wrace::kReady) { unsigned w = g_race->waitingFor(now); if (w) snprintf(b, sizeof b, "RACE   waiting for %u player%s", w, w == 1 ? "" : "s"); else snprintf(b, sizeof b, "RACE   everybody is ready"); centered(b, 60 * k, 1.0f, Rgba(1, 0.9f, 0.5f)); }
    else if (ph == wrace::kCountdown) { int64_t ms = g_race->msToGo(now); snprintf(b, sizeof b, "%lld", (long long)std::max<int64_t>(1, (ms + 999) / 1000)); centered(b, H * 0.28f, 6.0f, Rgba(1, 0.95f, 0.6f)); centered("GET READY", H * 0.28f - 70 * k, 1.2f, Rgba(0.9f, 0.9f, 0.95f)); g_goShown = 0; }
    else {
        if (ph == wrace::kRunning) { if (!g_goShown) g_goShown = now; }
        uint32_t el = g_race->elapsedMs(now); centered(Clock(el).c_str(), 22 * k, 1.8f, ph == wrace::kFinished ? Rgba(0.5f, 1, 0.6f) : Rgba(1, 1, 1));
        if (g_goal.kind == wrace::Goal::kPanel) snprintf(b, sizeof b, "goal: solve panel %X", g_goal.panel); else snprintf(b, sizeof b, "goal: solve %u panels   (%u)", g_goal.count, g_race->solvedSinceGo());
        centered(b, 22 * k + 50 * k, 0.8f, Rgba(0.75f, 0.8f, 0.9f));
        if (g_goShown && now - g_goShown < 1500) centered("GO!", H * 0.3f, 5.0f, Rgba(0.5f, 1, 0.6f));
    }
    if (ph >= wrace::kCountdown) {      // the standings
        auto st = g_race->standings(now); float x = W - 330 * k, y = 120 * k, lh = UiLineHeight(0.9f * k) + 8 * k;
        UiRect(x - 10 * k, y - 10 * k, 320 * k, lh * st.size() + 20 * k, Rgba(0, 0, 0, 0.55f));
        for (const auto& s : st) {
            float col[3]; PlayerColor(s.id, col); UiRect(x, y + 4 * k, 8 * k, lh - 12 * k, Rgba(col[0], col[1], col[2], 1));
            std::string nm = NameOf(s.id); if (s.me) nm += "  (you)"; snprintf(b, sizeof b, "%d  %s", s.place, nm.c_str()); UiText(x + 16 * k, y, b, s.gone ? Rgba(0.5f, 0.5f, 0.55f) : Rgba(1, 1, 1), 0.9f * k);
            if (s.phase == wrace::kFinished) snprintf(b, sizeof b, "%s", Clock(s.finishMs).c_str()); else if (g_goal.kind == wrace::Goal::kCount) snprintf(b, sizeof b, "%u / %u", s.solved, g_goal.count); else snprintf(b, sizeof b, "%u solved", s.solved);
            UiText(x + 310 * k - UiTextWidth(b, 0.9f * k), y, b, s.phase == wrace::kFinished ? Rgba(0.5f, 1, 0.6f) : Rgba(0.8f, 0.85f, 0.95f), 0.9f * k); y += lh;
        }
    }
    if (g_race->decided(now)) {
        unsigned w = g_race->winner(); auto st = g_race->standings(now);
        if (w == g_my) snprintf(b, sizeof b, "YOU WIN   %s", Clock(st[0].finishMs).c_str()); else snprintf(b, sizeof b, "%s WINS   %s", NameOf(w).c_str(), Clock(st[0].finishMs).c_str());
        centered(b, H * 0.42f, 3.0f, w == g_my ? Rgba(0.5f, 1, 0.6f) : Rgba(1, 0.8f, 0.4f));
    }
}

void RaceStat() {
    if (!g_race) { Log("racestat: off"); return; }
    Lock l; ULONGLONG now = GetTickCount64(); char m[260];
    snprintf(m, sizeof m, "racestat: phase %s go_in %lld elapsed %u solved %u winner %u decided %d goal %s waiting %u hud %ld", PhaseName(g_race->phase()), (long long)g_race->msToGo(now), g_race->elapsedMs(now), g_race->solvedSinceGo(), g_race->winner(), g_race->decided(now) ? 1 : 0, wrace::GoalText(g_goal).c_str(), g_race->waitingFor(now), g_hudFrames); Log(m);
    for (const auto& s : g_race->standings(now)) { snprintf(m, sizeof m, "racestat: standing %u place %d phase %s solved %u finish %u me %d gone %d", s.id, s.place, PhaseName(s.phase), s.solved, s.finishMs, s.me ? 1 : 0, s.gone ? 1 : 0); Log(m); }
}
