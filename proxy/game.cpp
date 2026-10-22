// Game-function bridge. Commands arrive from other threads and run on the render thread, between frames.
// witness_cmd.txt (one per line):  door <hexid> [amount]
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include "game.h"

void Log(const char* msg);
extern HMODULE g_selfModule();

static const uintptr_t kGlobals = 0x62D0A0, kDoorTypeInfo = 0x62DE70, kDoorOpenStart = 0x8E4B0;
typedef void(__fastcall* DoorOpenFn)(void* door, float amount);

struct Cmd { unsigned id; float amount; };
static CRITICAL_SECTION g_lock; static bool g_lockInit; static Cmd g_q[64]; static int g_n;

static uintptr_t Base() { return (uintptr_t)GetModuleHandleA(nullptr); }
uintptr_t GameEntity(unsigned id) {
    uintptr_t g = Base() + kGlobals;
    if (IsBadReadPtr((void*)g, 8)) return 0;
    uintptr_t glob = *(uintptr_t*)g; if (!glob || IsBadReadPtr((void*)(glob + 0x18), 8)) return 0;
    uintptr_t arr = *(uintptr_t*)(glob + 0x18); if (!arr || IsBadReadPtr((void*)(arr + (uintptr_t)id * 8), 8)) return 0;
    return *(uintptr_t*)(arr + (uintptr_t)id * 8);
}

void GameEnqueueDoorOpen(unsigned id, float amount) {
    if (!g_lockInit) return;
    EnterCriticalSection(&g_lock);
    if (g_n < 64) g_q[g_n++] = {id, amount};
    LeaveCriticalSection(&g_lock);
}

void GameRunQueued() {
    if (!g_lockInit || g_n == 0) return;
    Cmd batch[64]; int n;
    EnterCriticalSection(&g_lock); n = g_n; memcpy(batch, g_q, sizeof(Cmd) * n); g_n = 0; LeaveCriticalSection(&g_lock);
    for (int i = 0; i < n; i++) {
        uintptr_t e = GameEntity(batch[i].id); char m[160];
        if (!e) { snprintf(m, sizeof m, "game: door %#x: no such entity", batch[i].id); Log(m); continue; }
        if (*(uintptr_t*)(e + 8) != Base() + kDoorTypeInfo) { snprintf(m, sizeof m, "game: entity %#x is not a Door, refusing", batch[i].id); Log(m); continue; }
        ((DoorOpenFn)(Base() + kDoorOpenStart))((void*)e, batch[i].amount);
        snprintf(m, sizeof m, "game: DoorOpenStart(%#x, %.2f) called", batch[i].id, batch[i].amount); Log(m);
    }
}

static DWORD WINAPI CmdThread(LPVOID) {
    wchar_t p[MAX_PATH]; GetModuleFileNameW(g_selfModule(), p, MAX_PATH);
    wchar_t* sl = wcsrchr(p, L'\\'); if (sl) *(sl + 1) = 0;
    wchar_t cmd[MAX_PATH], taken[MAX_PATH]; wcscpy_s(cmd, p); wcscat_s(cmd, L"witness_cmd.txt"); wcscpy_s(taken, p); wcscat_s(taken, L"witness_cmd.taken");
    for (;;) {
        Sleep(250);
        if (!MoveFileExW(cmd, taken, MOVEFILE_REPLACE_EXISTING)) continue;
        FILE* f = _wfopen(taken, L"r"); if (!f) continue;
        char line[160];
        while (fgets(line, sizeof line, f)) {
            unsigned id; float amt = 1.0f;
            if (sscanf(line, "door %x %f", &id, &amt) >= 1) GameEnqueueDoorOpen(id, amt);
        }
        fclose(f); DeleteFileW(taken);
    }
}

void GameStart() {
    if (!g_lockInit) { InitializeCriticalSection(&g_lock); g_lockInit = true; }
    HANDLE t = CreateThread(nullptr, 0, CmdThread, nullptr, 0, nullptr); if (t) CloseHandle(t);
    // sanity: the function prologue must be what the signature scan found
    const unsigned char* fn = (const unsigned char*)(Base() + kDoorOpenStart);
    bool ok = !IsBadReadPtr(fn, 8) && fn[0] == 0x48 && fn[1] == 0x8B && fn[2] == 0xC4;
    char m[100]; snprintf(m, sizeof m, "game: DoorOpenStart at base+%#llx %s", (unsigned long long)kDoorOpenStart, ok ? "prologue ok" : "PROLOGUE MISMATCH"); Log(m);
}

bool GameDoorOpenNow(unsigned id, float amount) {
    uintptr_t e = GameEntity(id);
    if (!e || IsBadReadPtr((void*)(e + 8), 8) || *(uintptr_t*)(e + 8) != Base() + kDoorTypeInfo) return false;
    ((DoorOpenFn)(Base() + kDoorOpenStart))((void*)e, amount);
    return true;
}
