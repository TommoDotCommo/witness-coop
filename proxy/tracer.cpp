// In-game line tracer for the bot (tools/win/wdraw.py with WDRAW_TRACER=1). Drawing one edge from outside the game costs a round trip per
// mouse step (a command file the game reads once per frame, then a memory read from another process): about 0.5 s an edge. Here the same
// closed loop runs on the render thread, one frame per step: for every edge the mouse is pushed along the edge's screen direction, first a
// planned number of counts (in chunks, one event per frame), then 2 counts a frame until the panel's traced-node count reaches the edge's
// target, then an optional push past the node (so a turn is taken at it). The verdict is the game's own count, as in wdraw.py.
// Command (witness_cmd.txt): trace <panel hex> <items> then per item: vx vy first target hair   (vx, vy: a unit direction in mouse counts)
// Result: witness_trace.json {"done": edges finished, "ok": 0|1, "why": "..."} when the list ends or an edge fails.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "game.h"
#include "input.h"

void Log(const char* msg);
HMODULE g_selfModule();
bool InputIdle();

namespace {
struct Item { float vx, vy, first, hair; unsigned target; };
struct State { bool on = false; unsigned panel = 0; std::vector<Item> items; size_t at = 0; int phase = 0; float pushed = 0, carry[2] = {0, 0}; int frames = 0; };
State g;
CRITICAL_SECTION g_cs; bool g_init;
void Init() { if (!g_init) { InitializeCriticalSection(&g_cs); g_init = true; } }

void Result(bool ok, const char* why) {
    wchar_t path[MAX_PATH]; GetModuleFileNameW(g_selfModule(), path, MAX_PATH); wchar_t* sl = wcsrchr(path, L'\\'); if (sl) sl[1] = 0;
    wchar_t tmp[MAX_PATH], fin[MAX_PATH]; wcscpy_s(tmp, path); wcscat_s(tmp, L"witness_trace.tmp"); wcscpy_s(fin, path); wcscat_s(fin, L"witness_trace.json");
    FILE* f = _wfopen(tmp, L"w"); if (!f) return;
    fprintf(f, "{\"done\": %zu, \"ok\": %d, \"why\": \"%s\"}\n", g.at, ok ? 1 : 0, why); fclose(f); MoveFileExW(tmp, fin, MOVEFILE_REPLACE_EXISTING);
    char m[160]; snprintf(m, sizeof m, "tracer: panel %#x %s after %zu of %zu edges %s", g.panel, ok ? "done" : "FAILED", g.at, g.items.size(), why); Log(m);
    g.on = false;
}
void Move(float counts) {      // one event along the current edge, with the fractions carried to the next
    const Item& it = g.items[g.at]; g.carry[0] += it.vx * counts; g.carry[1] += it.vy * counts;
    int dx = (int)std::lround(g.carry[0]), dy = (int)std::lround(g.carry[1]); g.carry[0] -= dx; g.carry[1] -= dy;
    if (dx || dy) InputMouse(dx, dy, 0);
}
}

// witness_cmd.txt line (any thread): starts a trace; a trace already running is replaced
void TracerCommand(const char* line) {
    Init(); unsigned panel = 0; int n = 0, used = 0;
    if (sscanf(line, "trace %x %d%n", &panel, &n, &used) != 2 || n <= 0 || n > 400) { Log("tracer: bad command"); return; }
    std::vector<Item> items; const char* p = line + used;
    for (int i = 0; i < n; i++) {
        Item it; int k = 0;
        if (sscanf(p, " %f %f %f %u %f%n", &it.vx, &it.vy, &it.first, &it.target, &it.hair, &k) != 5) { Log("tracer: bad item"); return; }
        items.push_back(it); p += k;
    }
    EnterCriticalSection(&g_cs); g = State(); g.on = true; g.panel = panel; g.items = items; LeaveCriticalSection(&g_cs);
}

// render thread, once per frame, before the input pump
void TracerTick() {
    if (!g_init) return;
    EnterCriticalSection(&g_cs);
    if (!g.on || !InputIdle()) { LeaveCriticalSection(&g_cs); return; }      // one event in flight at a time: the count is read after it landed
    uintptr_t e = GameEntity(g.panel);
    if (!e || IsBadReadPtr((void*)(e + 0x228), 4)) { Result(false, "panel gone"); LeaveCriticalSection(&g_cs); return; }
    unsigned traced = *(volatile unsigned*)(e + 0x228);
    if (g.at >= g.items.size()) { Result(true, ""); LeaveCriticalSection(&g_cs); return; }
    Item& it = g.items[g.at];
    if (it.target && traced > it.target) { char w[80]; snprintf(w, sizeof w, "overshot: traced %u, wanted %u", traced, it.target); Result(false, w); LeaveCriticalSection(&g_cs); return; }
    if (g.phase == 0) {      // the planned push, up to 14 counts a frame
        float left = it.first - g.pushed;
        if (left > 0.5f && !(it.target && traced >= it.target)) { float c = left < 14 ? left : 14; Move(c); g.pushed += c; }
        else g.phase = 1;
    }
    if (g.phase == 1) {      // fine steps until the node is reached (no target: an end stub, pushed by plan alone)
        if (!it.target || traced >= it.target) { g.phase = 2; g.pushed = 0; }
        else if (++g.frames > 250) { char w[80]; snprintf(w, sizeof w, "stuck before traced %u (at %u)", it.target, traced); Result(false, w); LeaveCriticalSection(&g_cs); return; }
        else Move(2);
    }
    if (g.phase == 2) {      // a hair past the node, then the next edge
        if (it.hair > 0 && g.pushed < it.hair) { Move(it.hair); g.pushed = it.hair; }
        else { g.at++; g.phase = 0; g.pushed = 0; g.frames = 0; }
    }
    LeaveCriticalSection(&g_cs);
}
