// The sync core's view of the game (build 11370412): reads and writes panel and door state in the game's memory. All the logic
// (events, stable-world gate, settling window, echo guard) is in core/sync_core.cpp and is tested without the game.
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


static uintptr_t Base() { return (uintptr_t)GetModuleHandleA(nullptr); }
static bool Rd(uintptr_t a, void* out, size_t n) { SIZE_T got; return ReadProcessMemory(GetCurrentProcess(), (void*)a, out, n, &got) && got == n; }
static uint32_t Rd32(uintptr_t a) { uint32_t v = 0; Rd(a, &v, 4); return v; }

static bool PanelSolved(uintptr_t e) { return Rd32(e + kSolvedF) == kOne && Rd32(e + kEver) == 1; }
static bool DoorOpen(uintptr_t e) { return Rd32(e + kDoorPower) == kOne; }
static uintptr_t Type(uintptr_t e) { uintptr_t ti = 0; Rd(e + 8, &ti, 8); return ti; }

class GameWorld : public witness::World {
public:
    bool cameraValid() override { const float* cam = (const float*)(Base() + 0x6304B0 + 0x10); return !IsBadReadPtr(cam, 12) && (cam[0] != 0 || cam[1] != 0); }
    bool scan(std::vector<unsigned>& panels, std::vector<unsigned>& doors, uint64_t* tableId) override {   // all Machine_Panel and Door entities from the live table
        uintptr_t glob = 0; if (!Rd(Base() + kGlobals, &glob, 8) || !glob) return false;
        uintptr_t arr = 0; if (!Rd(glob + 0x18, &arr, 8) || !arr) return false;
        tbl_.assign(kMaxId, 0);
        if (!Rd(arr, tbl_.data(), kMaxId * 8)) return false;
        for (unsigned id = 1; id < kMaxId; id++) {
            if (!tbl_[id]) continue;
            uintptr_t ti = 0; if (!Rd(tbl_[id] + 8, &ti, 8)) continue;
            if (ti == Base() + kPanelTI) panels.push_back(id); else if (ti == Base() + kDoorTI) doors.push_back(id);
        }
        *tableId = arr; return true;
    }
    bool entityExists(unsigned id) override { return id < kMaxId && GameEntity(id) != 0; }
    bool isPanel(unsigned id) override { uintptr_t e = id < kMaxId ? GameEntity(id) : 0; return e && Type(e) == Base() + kPanelTI; }
    bool isDoor(unsigned id) override { uintptr_t e = id < kMaxId ? GameEntity(id) : 0; return e && Type(e) == Base() + kDoorTI; }
    bool panelSolved(unsigned id) override { uintptr_t e = GameEntity(id); return e && PanelSolved(e); }
    bool doorOpen(unsigned id) override { uintptr_t e = GameEntity(id); return e && DoorOpen(e); }
    void applyPanelSolved(unsigned id) override {   // the fields a real solve sets; the door itself is opened by its own event
        uintptr_t e = GameEntity(id); if (!e) return;
        const struct { int off; uint32_t v; } w[] = {{0x278, kOne}, {kPanelState, 1}, {0x290, kOne}, {kSolvedF, kOne}, {kEver, 1}};
        for (auto& x : w) memcpy((void*)(e + x.off), &x.v, 4);
    }
    bool applyDoorOpen(unsigned id) override { return GameDoorOpenNow(id, 1.0f); }
    bool pose(float pos[3], float ang[2]) override {
        const float* cam = (const float*)(Base() + 0x6304B0 + 0x10); const float* a = (const float*)(Base() + 0x6303EC);
        if (IsBadReadPtr(cam, 12) || IsBadReadPtr(a, 8) || (cam[0] == 0 && cam[1] == 0)) return false;
        for (int i = 0; i < 3; i++) pos[i] = cam[i];
        ang[0] = a[0]; ang[1] = a[1]; return true;
    }
private:
    std::vector<uintptr_t> tbl_;
};

static GameWorld g_world;
witness::World* GameWorldInstance() { return &g_world; }

static DWORD WINAPI StateThread(LPVOID) {   // the detector: about every 100 ms
    for (;;) { Sleep(100); if (witness::SyncCore* c = CoopCore()) c->pollState(GetTickCount64()); }
}

void StateStart() { HANDLE t = CreateThread(nullptr, 0, StateThread, nullptr, 0, nullptr); if (t) CloseHandle(t); }

void StateApplyRemote() {   // render thread, once per frame
    if (witness::SyncCore* c = CoopCore()) c->applyRemote(GetTickCount64());
}
