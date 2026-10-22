// Shared puzzle state (build 11370412). A detector thread polls every Machine_Panel and Door for local changes
// (panel solved, door opened) and sends them as reliable events; remote events are applied on the render thread.
// Echo guard: applying an event first marks it as already known, so the detector does not send it back.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include "coop.h"
#include "game.h"
#include "state.h"

void Log(const char* msg);

static const uintptr_t kGlobals = 0x62D0A0, kDoorTI = 0x62DE70, kPanelTI = 0x62E670;
static const int kSolvedF = 0x294, kEver = 0x298, kPanelState = 0x280, kDoorPower = 0x1D0;
static const uint32_t kOne = 0x3F800000;
static const unsigned kMaxId = 0x40000;

static uint8_t g_knownPanel[kMaxId], g_knownDoor[kMaxId];
static volatile LONG g_ready;                           // world stable: only then detect, resync or apply
static DWORD g_suppressPanel[kMaxId], g_suppressDoor[kMaxId];   // echo guard: ignore local changes until this tick
static volatile LONG g_appliedPanels, g_appliedDoors, g_emittedPanels, g_emittedDoors;

static uintptr_t Base() { return (uintptr_t)GetModuleHandleA(nullptr); }
static bool Rd(uintptr_t a, void* out, size_t n) { SIZE_T got; return ReadProcessMemory(GetCurrentProcess(), (void*)a, out, n, &got) && got == n; }
static uint32_t Rd32(uintptr_t a) { uint32_t v = 0; Rd(a, &v, 4); return v; }

static bool PanelSolved(uintptr_t e) { return Rd32(e + kSolvedF) == kOne && Rd32(e + kEver) == 1; }
static bool DoorOpen(uintptr_t e) { return Rd32(e + kDoorPower) == kOne; }

static std::vector<unsigned> g_panels, g_doors; static uintptr_t g_arr;

// FNV-1a over which panels are solved and which doors are open, to see whether start-up is still changing them.
static uint64_t StateSignature() {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
    for (unsigned id : g_panels) { uintptr_t e = GameEntity(id); if (e && PanelSolved(e)) mix(id); }
    mix(0xFFFF);
    for (unsigned id : g_doors) { uintptr_t e = GameEntity(id); if (e && DoorOpen(e)) mix(id); }
    return h;
}
static const DWORD kSettleMs = 15000;   // changes this long after the gate are start-up, not play

static bool Scan() {   // find all Machine_Panel and Door entities from the live entity table
    uintptr_t glob = 0; if (!Rd(Base() + kGlobals, &glob, 8) || !glob) return false;
    uintptr_t arr = 0; if (!Rd(glob + 0x18, &arr, 8) || !arr) return false;
    static std::vector<uintptr_t> tbl; tbl.assign(kMaxId, 0);
    if (!Rd(arr, tbl.data(), kMaxId * 8)) return false;
    std::vector<unsigned> panels, doors;
    for (unsigned id = 1; id < kMaxId; id++) {
        if (!tbl[id]) continue;
        uintptr_t ti = 0; if (!Rd(tbl[id] + 8, &ti, 8)) continue;
        if (ti == Base() + kPanelTI) panels.push_back(id); else if (ti == Base() + kDoorTI) doors.push_back(id);
    }
    bool changed = arr != g_arr || panels.size() != g_panels.size() || doors.size() != g_doors.size();
    g_arr = arr; g_panels.swap(panels); g_doors.swap(doors);
    return changed;
}

static void BaselineAndMaybeFull(bool full) {
    for (unsigned id : g_panels) { uintptr_t e = GameEntity(id); bool s = e && PanelSolved(e); g_knownPanel[id] = s; if (full && s) CoopSendEvent(EV_PANEL_SOLVED, id); }
    for (unsigned id : g_doors) { uintptr_t e = GameEntity(id); bool o = e && DoorOpen(e); g_knownDoor[id] = o; if (full && o) CoopSendEvent(EV_DOOR_OPENED, id); }
}

static DWORD WINAPI StateThread(LPVOID) {
    // Wait until the world is stable: camera valid, and the same panel/door counts AND the same set of solved panels and open
    // doors on 3 scans 3 s apart. (A baseline taken while the world was loading reported 39 transient "open" doors, and a
    // counts-only gate still let a default-open door power up late; see also the settling window below.)
    size_t lp = 0, ld = 0; uint64_t lsig = 0; int same = 0;
    for (;;) {
        Sleep(3000); Scan();
        const float* cam = (const float*)(Base() + 0x6304B0 + 0x10);
        bool camOk = !IsBadReadPtr(cam, 12) && (cam[0] != 0 || cam[1] != 0);
        uint64_t sig = StateSignature();
        if (camOk && !g_panels.empty() && g_panels.size() == lp && g_doors.size() == ld && sig == lsig) { if (++same >= 3) break; } else same = 0;
        lp = g_panels.size(); ld = g_doors.size(); lsig = sig;
    }
    char m[160]; snprintf(m, sizeof m, "state: world stable, %zu panels, %zu doors", g_panels.size(), g_doors.size()); Log(m);
    BaselineAndMaybeFull(false);
    InterlockedExchange(&g_ready, 1);
    DWORD settleUntil = GetTickCount() + kSettleMs, nextScan = GetTickCount() + 3000, nextStats = GetTickCount() + 10000;
    for (;;) {
        Sleep(100);
        DWORD now = GetTickCount();
        if ((int)(now - nextScan) >= 0) { nextScan = now + 3000; if (Scan()) { Log("state: entity table changed, new baseline"); BaselineAndMaybeFull(false); } }
        if (CoopTakeResyncRequest()) { BaselineAndMaybeFull(true); Log("state: full state queued for the peer"); }
        for (unsigned id : g_panels) {
            uintptr_t e = GameEntity(id); if (!e) continue;
            bool s = PanelSolved(e);
            if ((int)(now - g_suppressPanel[id]) < 0) { g_knownPanel[id] = s; continue; }
            if ((int)(now - settleUntil) < 0 && s != (bool)g_knownPanel[id]) { g_knownPanel[id] = s; char l[100]; snprintf(l, sizeof l, "state: panel %#x changed during start-up, folded into the baseline", id); Log(l); continue; }
            if (s && !g_knownPanel[id]) { g_knownPanel[id] = 1; CoopSendEvent(EV_PANEL_SOLVED, id); InterlockedIncrement(&g_emittedPanels); char l[100]; snprintf(l, sizeof l, "state: local panel %#x solved -> event", id); Log(l); }
            else if (!s) g_knownPanel[id] = 0;
        }
        for (unsigned id : g_doors) {
            uintptr_t e = GameEntity(id); if (!e) continue;
            bool o = DoorOpen(e);
            if ((int)(now - g_suppressDoor[id]) < 0) { g_knownDoor[id] = o; continue; }
            if ((int)(now - settleUntil) < 0 && o != (bool)g_knownDoor[id]) { g_knownDoor[id] = o; char l[100]; snprintf(l, sizeof l, "state: door %#x changed during start-up, folded into the baseline", id); Log(l); continue; }
            if (o && !g_knownDoor[id]) { g_knownDoor[id] = 1; CoopSendEvent(EV_DOOR_OPENED, id); InterlockedIncrement(&g_emittedDoors); char l[100]; snprintf(l, sizeof l, "state: local door %#x opened -> event", id); Log(l); }
            else if (!o) g_knownDoor[id] = 0;
        }
        if ((int)(now - nextStats) >= 0) {
            nextStats = now + 10000;
            char l[200]; snprintf(l, sizeof l, "state: emitted panels %ld doors %ld, applied panels %ld doors %ld | net sent %u resent %u in-order %u dup %u ooo %u acked %u",
                (long)g_emittedPanels, (long)g_emittedDoors, (long)g_appliedPanels, (long)g_appliedDoors, CoopStats(0), CoopStats(1), CoopStats(2), CoopStats(3), CoopStats(4), CoopStats(5));
            Log(l);
        }
    }
}

void StateStart() { HANDLE t = CreateThread(nullptr, 0, StateThread, nullptr, 0, nullptr); if (t) CloseHandle(t); }

void StateApplyRemote() {   // render thread
    if (!g_ready) return;   // events stay queued until this world is stable
    DWORD now = GetTickCount();
    uint8_t kind; uint32_t id; char l[140];
    while (CoopPopRemoteEvent(&kind, &id)) {
        uintptr_t e = id < kMaxId ? GameEntity(id) : 0;
        if (!e) { snprintf(l, sizeof l, "state: remote event kind %u entity %#x: no such entity", kind, id); Log(l); continue; }
        uintptr_t ti = 0; Rd(e + 8, &ti, 8);
        if (kind == EV_PANEL_SOLVED && ti == Base() + kPanelTI) {
            if (PanelSolved(e)) { g_knownPanel[id] = 1; continue; }
            if (!CoopTestFlag(2)) { g_knownPanel[id] = 1; g_suppressPanel[id] = now + 5000; }   // echo guard first
            const struct { int off; uint32_t v; } w[] = {{0x278, kOne}, {kPanelState, 1}, {0x290, kOne}, {kSolvedF, kOne}, {kEver, 1}};
            for (auto& x : w) memcpy((void*)(e + x.off), &x.v, 4);
            InterlockedIncrement(&g_appliedPanels); snprintf(l, sizeof l, "state: remote panel %#x applied as solved", id); Log(l);
        } else if (kind == EV_DOOR_OPENED && ti == Base() + kDoorTI) {
            if (DoorOpen(e)) { g_knownDoor[id] = 1; continue; }
            if (!CoopTestFlag(2)) { g_knownDoor[id] = 1; g_suppressDoor[id] = now + 5000; }
            bool ok = GameDoorOpenNow(id, 1.0f);
            InterlockedIncrement(&g_appliedDoors); snprintf(l, sizeof l, "state: remote door %#x opened (call %s)", id, ok ? "ok" : "REFUSED"); Log(l);
        } else { snprintf(l, sizeof l, "state: remote event kind %u entity %#x: wrong entity type, ignored", kind, id); Log(l); }
    }
}
