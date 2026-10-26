// The sync core's view of the game (build 11370412): reads and writes panel and door state in the game's memory. All the logic
// (events, stable-world gate, settling window, echo guard) is in core/sync_core.cpp and is tested without the game.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>
#include "coop.h"
#include "frame/panelframe.h"
#include "game.h"
#include "state.h"
#include "config.h"

void Log(const char* msg);

static const uintptr_t kGlobals = 0x62D0A0, kDoorTI = 0x62DE70, kPanelTI = 0x62E670, kCableTI = 0x62EB10;
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
        tbl_.assign(kMaxId, 0); cables_.clear(); panelIds_.clear();
        if (!Rd(arr, tbl_.data(), kMaxId * 8)) return false;
        // Table index zero is valid: it is the door opened by Tutorial Bend (panel index 0x182).
        for (unsigned id = GetCfg().skipEntityZero ? 1u : 0u; id < kMaxId; id++) {
            if (!tbl_[id]) continue;
            uintptr_t ti = 0; if (!Rd(tbl_[id] + 8, &ti, 8)) continue;
            if (ti == Base() + kPanelTI) { panels.push_back(id); panelIds_.push_back(id); } else if (ti == Base() + kDoorTI) doors.push_back(id); else if (ti == Base() + kCableTI) cables_.push_back(id);
        }
        *tableId = arr; return true;
    }
    bool scanCables(std::vector<unsigned>& cables) override { cables = cables_; return true; }       // filled by the last scan()
    bool isCable(unsigned id) override { uintptr_t e = id < kMaxId ? GameEntity(id) : 0; return e && Type(e) == Base() + kCableTI; }
    bool cableLit(unsigned id) override { uintptr_t e = GameEntity(id); return e && Rd32(e + 0xC4) == kOne && Rd32(e + 0xC8) == kOne; }
    bool cableRequested(unsigned id) override { uintptr_t e = GameEntity(id); return e && Rd32(e + 0xC8) == kOne; }
    bool applyCableLit(unsigned id) override { return GameCablePowerOnNow(id); }
    bool applyCableOff(unsigned id) override { return GameCableOffNow(id); }
    bool entityExists(unsigned id) override { return id < kMaxId && GameEntity(id) != 0; }
    bool isPanel(unsigned id) override { uintptr_t e = id < kMaxId ? GameEntity(id) : 0; return e && Type(e) == Base() + kPanelTI; }
    bool isDoor(unsigned id) override { uintptr_t e = id < kMaxId ? GameEntity(id) : 0; return e && Type(e) == Base() + kDoorTI; }
    bool panelSolved(unsigned id) override { uintptr_t e = GameEntity(id); return e && PanelSolved(e); }
    bool doorOpen(unsigned id) override { uintptr_t e = GameEntity(id); return e && DoorOpen(e); }
    bool panelPowered(unsigned id) override { uintptr_t e = GameEntity(id); return e && Rd32(e + 0x2A4) == kOne; }
    bool applyPanelPower(unsigned id) override { return GamePanelPowerOnNow(id); }
    void applyPanelSolved(unsigned id) override {   // the fields a real solve sets; the door itself is opened by its own event
        GameMarkPanelSolved(id);
        if (!GetCfg().noTargetDispatch) GameDispatchTargetNow(id);       // targets without a dedicated event (laser, plate, gauge, ...) react as in a native solve
    }
    bool applyDoorOpen(unsigned id) override { return GameDoorOpenNow(id, 1.0f); }
    // The game keeps a drawn line as an array of segment records at +0x230 (count at +0x228, 13 dwords each: node a, node b, the panel's engine id twice,
    // 1.0, 1.0, the world position of a (3 floats), of b (3 floats), a zero). It stays after the panel is solved. Each node goes out with its place in
    // millimetres from the panel's own position, so the other game never has to know how the panel's graph sits in the world.
    bool scanLines(std::vector<witness::LineOut>& out) override {
        // A drawn line is kept by the game as world positions that are written while it is drawn and never updated, but a panel can move afterwards (the first ones are part of a door that
        // swings open). So the line is sent in the panel's own frame, turned into it with the panel's pose from the moment the trace last changed, and the other game turns it back with
        // its own panel's pose (overlay.cpp). Each record is a segment: its two node numbers, where it starts and where it ends in the world; the tip of the segment being drawn is the
        // fraction of the way (word 4) from the start to the end, which the game keeps apart from the end.
        for (unsigned id : panelIds_) {
            uintptr_t e = GameEntity(id); if (!e) continue;
            uint32_t n = Rd32(e + 0x228); if (n == 0 || n > 600) continue;
            uintptr_t ptr = 0; if (!Rd(e + 0x230, &ptr, 8) || !ptr) continue;
            std::vector<uint32_t> rec(size_t(n) * 13); if (!Rd(ptr, rec.data(), rec.size() * 4)) continue;
            float pp[3], qq[4]; if (!Rd(e + 0x24, pp, 12) || !Rd(e + 0x34, qq, 16)) continue;
            Frame& fr = frames_[id]; if (fr.rec != rec) { fr.rec = rec; memcpy(fr.pos, pp, 12); memcpy(fr.q, qq, 16); }      // the trace changed: the panel is where the trace was written
            if (GetCfg().lineOldAnchor) { memcpy(fr.pos, pp, 12); fr.q[0] = fr.q[1] = fr.q[2] = 0; fr.q[3] = 1; }      // (mutant: the old way, world offsets from where the panel is now)
            wframe::Q q = {fr.q[0], fr.q[1], fr.q[2], fr.q[3]};
            witness::LineOut l; l.panel = id; l.final = PanelSolved(e);
            auto point = [&](uint32_t node, const float* world) {
                float local[3]; wframe::ToLocal(fr.pos, q, world, local); witness::LinePoint p; p.node = uint16_t(node);
                for (int k = 0; k < 3; ++k) { float mm = local[k] * 1000.0f; p.d[k] = int16_t(mm > 32000 ? 32000 : mm < -32000 ? -32000 : mm); }
                l.pts.push_back(p);
            };
            bool bad = false;
            for (uint32_t i = 0; i < n && !bad; ++i) {
                const uint32_t* r = rec.data() + size_t(i) * 13;
                if (r[0] > 4000 || r[1] > 4000) { bad = true; break; }
                float a[3], b[3], tip[3], t; memcpy(a, r + 6, 12); memcpy(b, r + 9, 12); memcpy(&t, r + 4, 4);
                wframe::Tip(a, b, (i + 1 == n && !GetCfg().lineWholeEdge) ? t : 1.0f, tip);
                if (i == 0) point(r[0], a);
                point(r[1], tip);
            }
            if (!bad) out.push_back(std::move(l));
        }
        return true;
    }
    bool pose(float pos[3], float ang[2]) override {
        const float* cam = (const float*)(Base() + 0x6304B0 + 0x10); const float* a = (const float*)(Base() + 0x6303EC);
        if (IsBadReadPtr(cam, 12) || IsBadReadPtr(a, 8) || (cam[0] == 0 && cam[1] == 0)) return false;
        for (int i = 0; i < 3; i++) pos[i] = cam[i];
        ang[0] = a[0]; ang[1] = a[1]; return true;
    }
private:
    struct Frame { float pos[3] = {}, q[4] = {0, 0, 0, 1}; std::vector<uint32_t> rec; }; std::map<unsigned, Frame> frames_;      // the pose of each panel when its trace last changed
    std::vector<uintptr_t> tbl_; std::vector<unsigned> cables_, panelIds_;
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
