// Third-person view while emoting: the game side (see thirdperson.h; the camera itself is core/thirdperson.h, tested game-free).
// Build 11370412 (measured by disassembly, see the README): the game's main loop calls its world drawing, exe+0x1C8970 (no arguments),
// from exe+0x696BD and exe+0x69A17. That function builds its view from the camera at exe+0x62D578 (position, then the x y z w
// quaternion at +0xC), a copy of the camera at exe+0x6304C0 / +0x6304D0 the game rebuilds from the player every frame. While the view
// is out, both call sites go through a small stub to RenderWorldHook, which puts the third-person camera into both copies, lets the game
// draw, and puts the player's own camera back. Nothing is patched until the first emote with the setting on.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "thirdperson.h"
#include "core/thirdperson.h"
#include "config.h"
#include "gamecam.h"
#include "model_gpu.h"
#include "social.h"
#include "devmenu.h"
#include "guide_ui.h"
#include "povui.h"
#include "mapgen.h"

void Log(const char* msg);
extern HMODULE g_selfModule();
bool GamePaused();

namespace {
using namespace witness::tp;
const uintptr_t kRenderWorld = 0x1C8970, kCallSites[2] = {0x696BD, 0x69A17};
const uintptr_t kCamPos = 0x6304C0, kCamQuat = 0x6304D0, kCamAng = 0x6303EC, kViewPos = 0x62D578, kViewQuat = 0x62D584;

Cam g_cam;
bool g_patched = false, g_patchFailed = false;
unsigned g_hookCalls = 0, g_swapped = 0, g_selfDraws = 0, g_selfFrames = 0, g_firstPersonFrames = 0;
bool g_viewUsed = false, g_frameView = false; float g_viewPos[3], g_viewAng[2];      // set by the hook; g_frameView: this Present's frame was drawn from it
float g_qErr = -1, g_maxOffset = 0, g_lastOffset = 0;      // g_qErr: how far the game's own quaternion is from QuatFromYawPitch (a convention check, logged)
volatile LONG g_clicks = 0, g_stateReq = 0; volatile LONG g_force = -1;      // g_force: test override of the setting (-1: the cfg's)
unsigned g_lastSeq = 0; bool g_seqInit = false; ULONGLONG g_lastTick = 0;
ProbeSet g_probe; bool g_probeOut = false; float g_probeDepth[5] = {0, 0, 0, 0, 0};
Phase g_lastPhase = Phase::Off;

uintptr_t Base() { return (uintptr_t)GetModuleHandleA(nullptr); }
bool Readable(const void* p, size_t n) { return !IsBadReadPtr(p, n); }
bool PlayerCamera(V3* eye, float* yaw, float* pitch) {
    const float* c = (const float*)(Base() + kCamPos); const float* a = (const float*)(Base() + kCamAng);
    if (!Readable(c, 12) || !Readable(a, 8) || !std::isfinite(c[0]) || !std::isfinite(a[0])) return false;
    *eye = {c[0], c[1], c[2]}; *yaw = a[0]; *pitch = a[1]; return true;
}
const char* PhaseName(Phase p) { switch (p) { case Phase::Probe: return "probe"; case Phase::In: return "in"; case Phase::Hold: return "hold"; case Phase::Out: return "out"; default: return "off"; } }

// The game presents its frame from inside the world drawing (exe+0x1C95B7), so the mod's Present runs while the third-person camera is
// still in place: ThirdPersonPresentBegin puts the player's camera back first. Other threads that read the camera (the pose sent to the
// others, the voice listener) use ThirdPersonGen / ThirdPersonFixEye, a sequence lock: an odd generation means the swap is in place.
volatile LONG g_gen = 0; float g_realCp[3], g_realCq[4], g_realVp[3], g_realVq[4];
void RestoreReal() {
    if (!(g_gen & 1)) return;
    memcpy((void*)(Base() + kCamPos), g_realCp, 12); memcpy((void*)(Base() + kCamQuat), g_realCq, 16); memcpy((void*)(Base() + kViewPos), g_realVp, 12); memcpy((void*)(Base() + kViewQuat), g_realVq, 16);
    InterlockedIncrement(&g_gen);
}
void RenderWorldHook() {
    typedef void (*Fn)(); Fn fn = (Fn)(Base() + kRenderWorld);
    ++g_hookCalls;
    V3 eye; float yaw, pitch; View v;
    float* cp = (float*)(Base() + kCamPos); float* cq = (float*)(Base() + kCamQuat); float* vp = (float*)(Base() + kViewPos); float* vq = (float*)(Base() + kViewQuat);
    if (!g_cam.Active() || !PlayerCamera(&eye, &yaw, &pitch) || !g_cam.ViewFor(eye, yaw, pitch, &v)) { fn(); return; }
    float q[4]; QuatFromYawPitch(yaw, pitch, q);
    float e1 = 0, e2 = 0; for (int i = 0; i < 4; ++i) { e1 = fmaxf(e1, fabsf(q[i] - cq[i])); e2 = fmaxf(e2, fabsf(q[i] + cq[i])); }
    g_qErr = fminf(e1, e2);      // the same rotation as the game's own (q and -q are one rotation)
    memcpy(g_realCp, cp, 12); memcpy(g_realCq, cq, 16); memcpy(g_realVp, vp, 12); memcpy(g_realVq, vq, 16);
    QuatFromYawPitch(v.yaw, v.pitch, q);
    float p[3] = {v.pos.x, v.pos.y, v.pos.z};
    memcpy(g_viewPos, p, 12); g_viewAng[0] = v.yaw; g_viewAng[1] = v.pitch; g_viewUsed = true; ++g_swapped;
    g_lastOffset = Len(Sub(v.pos, eye)); if (g_lastOffset > g_maxOffset) g_maxOffset = g_lastOffset;
    InterlockedIncrement(&g_gen);      // odd: the swap is in place
    memcpy(cp, p, 12); memcpy(cq, q, 16); memcpy(vp, p, 12); memcpy(vq, q, 16);
    fn();
    RestoreReal();
}

// Both call sites of the world drawing go through a stub near the exe (a call reaches +-2 GB) to RenderWorldHook. Render thread only:
// the game's loop is inside Present, so neither call is running. Refuses unless both are the expected `call exe+0x1C8970`.
bool PatchCalls() {
    uintptr_t base = Base(), target = base + kRenderWorld;
    for (uintptr_t site : kCallSites) {
        const unsigned char* c = (const unsigned char*)(base + site); if (!Readable(c, 5) || c[0] != 0xE8) return false;
        int32_t rel; memcpy(&rel, c + 1, 4); if (base + site + 5 + (intptr_t)rel != target) return false;
    }
    unsigned char* stub = nullptr;
    for (uintptr_t a = (base & ~(uintptr_t)0xFFFF) - 0x10000; a > 0x10000 && base - a < 0x70000000; a -= 0x10000)
        if ((stub = (unsigned char*)VirtualAlloc((void*)a, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE))) break;
    if (!stub) return false;
    stub[0] = 0xFF; stub[1] = 0x25; memset(stub + 2, 0, 4); uint64_t to = (uint64_t)(uintptr_t)&RenderWorldHook; memcpy(stub + 6, &to, 8);      // jmp [rip+0] -> RenderWorldHook
    FlushInstructionCache(GetCurrentProcess(), stub, 16);
    for (uintptr_t site : kCallSites) {
        unsigned char* c = (unsigned char*)(base + site); intptr_t d = (intptr_t)stub - (intptr_t)(base + site + 5);
        if (d > INT32_MAX || d < INT32_MIN) return false;
        int32_t rel = (int32_t)d; DWORD old;
        if (!VirtualProtect(c + 1, 4, PAGE_EXECUTE_READWRITE, &old)) return false;
        memcpy(c + 1, &rel, 4); VirtualProtect(c + 1, 4, old, &old);
    }
    FlushInstructionCache(GetCurrentProcess(), (void*)(base + kCallSites[0]), 0x400);
    return true;
}

void WriteState() {
    wchar_t path[MAX_PATH]; GetModuleFileNameW(g_selfModule(), path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\'); if (slash) *(slash + 1) = 0;
    wcscat_s(path, L"witness_tp.json"); wchar_t tmp[MAX_PATH]; wcscpy_s(tmp, path); wcscat_s(tmp, L".tmp");
    FILE* f = _wfopen(tmp, L"w"); if (!f) return;
    V3 eye{0, 0, 0}; float yaw = 0, pitch = 0; PlayerCamera(&eye, &yaw, &pitch);
    const float* vp = (const float*)(Base() + kViewPos);
    fprintf(f, "{\"enabled\":%d,\"phase\":\"%s\",\"blend\":%.4f,\"reason\":\"%s\",\"scale\":%.4f,\"starts\":%u,\"returns\":%u,\"patched\":%d,\"patch_failed\":%d,"
               "\"hook_calls\":%u,\"swapped\":%u,\"frame_view\":%d,\"view\":[%.4f,%.4f,%.4f,%.4f,%.4f],\"eye\":[%.4f,%.4f,%.4f,%.4f,%.4f],"
               "\"game_view_pos\":[%.4f,%.4f,%.4f],\"offset\":%.4f,\"max_offset\":%.4f,\"self_draws\":%u,\"self_frames\":%u,\"qerr\":%.6f,\"probe\":[%.3f,%.3f,%.3f,%.3f,%.3f]}",
        (g_force >= 0 ? g_force != 0 : GetCfg().emoteThirdPerson) ? 1 : 0, PhaseName(g_cam.phase), g_cam.Active() ? witness::Ease(g_cam.s) : 0.0f, WhyName(g_cam.last), g_cam.scale,
        g_cam.starts, g_cam.returns, g_patched ? 1 : 0, g_patchFailed ? 1 : 0, g_hookCalls, g_swapped, g_frameView ? 1 : 0,
        g_viewPos[0], g_viewPos[1], g_viewPos[2], g_viewAng[0], g_viewAng[1], eye.x, eye.y, eye.z, yaw, pitch,
        Readable(vp, 12) ? vp[0] : 0, Readable(vp, 12) ? vp[1] : 0, Readable(vp, 12) ? vp[2] : 0, g_frameView ? g_lastOffset : 0.0f, g_maxOffset, g_selfDraws, g_selfFrames, g_qErr,
        g_probeDepth[0], g_probeDepth[1], g_probeDepth[2], g_probeDepth[3], g_probeDepth[4]);
    fclose(f); MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING);
}
} // namespace

void ThirdPersonPresentBegin() { RestoreReal(); }
unsigned ThirdPersonGen() { return (unsigned)g_gen; }
void ThirdPersonFixEye(unsigned before, float pos[3]) {
    unsigned now = (unsigned)g_gen; if (before == now && !(now & 1)) return;
    memcpy(pos, g_realCp, 12);      // a third-person frame was being drawn while the camera was read: the player's own eye
}
void ThirdPersonFrame(float aspect) {
    g_frameView = g_viewUsed; g_viewUsed = false;
    if (g_frameView) ++g_selfFrames; else ++g_firstPersonFrames;
    ULONGLONG now = GetTickCount64(); float dt = g_lastTick ? float(now - g_lastTick) / 1000.0f : 0; g_lastTick = now; if (dt > 0.1f) dt = 0.1f;
    V3 eye; float yaw, pitch;
    if (!PlayerCamera(&eye, &yaw, &pitch)) { if (InterlockedExchange(&g_stateReq, 0)) WriteState(); return; }
    int emote = 0; float t = 0; unsigned seq = 0; bool playing = SocialMyEmote(&emote, &t, &seq);
    bool enabled = g_force >= 0 ? g_force != 0 : GetCfg().emoteThirdPerson;
    bool menu = GamePaused() || DevMenuOpen() || PovRosterOpen() || GuideOpen() || SocialMouseCaptured() || MapGenActive();
    bool clicked = InterlockedExchange(&g_clicks, 0) != 0;
    if (!g_seqInit) { g_seqInit = true; g_lastSeq = seq; }
    if (seq != g_lastSeq) {      // an emote was started here: measure the room in front of the player in the frame the game just drew (first person)
        g_lastSeq = seq;
        if (playing && enabled && !menu && !g_patchFailed && g_cam.Start(emote, eye, yaw, pitch)) {
            if (MakeProbe(g_cam.p, eye, yaw, pitch, GameProjection().vfovDeg, aspect > 0.2f ? aspect : 16.0f / 9.0f, &g_probe)) { ModelGpuRequestDepthPoints(g_probe.fx, g_probe.fy, 5); g_probeOut = true; }
            else g_cam.ProbeAnswer(nullptr, nullptr);
        }
    }
    if (g_cam.phase == Phase::Probe && g_probeOut) {
        int r = ModelGpuTakeDepthPoints(g_probeDepth, 5);
        if (r == 1) { g_probeOut = false; g_cam.ProbeAnswer(&g_probe, g_probeDepth); }
        else if (r < 0) { g_probeOut = false; g_cam.ProbeAnswer(nullptr, nullptr); }
    }
    if (GetCfg().tpNeverReturn) g_cam.Tick(dt, true, g_cam.eye0, g_cam.yaw0, g_cam.pitch0, false, false, true);      // mutant: nothing ever brings the view back
    else g_cam.Tick(dt, playing, eye, yaw, pitch, clicked, menu, enabled);
    if (g_cam.Active() && !g_patched) {
        g_patched = PatchCalls(); g_patchFailed = !g_patched;
        Log(g_patched ? "thirdperson: the world drawing now goes through the mod (exe+0x696BD, exe+0x69A17)" : "thirdperson: the world drawing is not the expected call: the view stays first person");
        if (!g_patched) { g_cam.phase = Phase::Off; g_cam.s = 0; }
    }
    if (g_cam.phase != g_lastPhase) {
        char m[300]; snprintf(m, sizeof m, "thirdperson: %s -> %s (reason %s, emote %d, scale %.2f, depth %.2f, eye moved %.3f %.3f %.3f, look moved %.3f %.3f)", PhaseName(g_lastPhase), PhaseName(g_cam.phase), WhyName(g_cam.last), g_cam.emote, g_cam.scale, g_probeDepth[0],
            eye.x - g_cam.eye0.x, eye.y - g_cam.eye0.y, eye.z - g_cam.eye0.z, WrapAngle(yaw - g_cam.yaw0), pitch - g_cam.pitch0); Log(m);
        g_lastPhase = g_cam.phase;
    }
    if (InterlockedExchange(&g_stateReq, 0)) WriteState();
}

bool ThirdPersonViewCam(float pos[3], float ang[2]) {
    if (!g_frameView) return false;
    memcpy(pos, g_viewPos, 12); ang[0] = g_viewAng[0]; ang[1] = g_viewAng[1]; return true;
}
bool ThirdPersonSelf(float pos[3], float* yaw, int* emote, float* seconds) {
    if (!g_frameView) return false;
    V3 eye; float pitch; if (!PlayerCamera(&eye, yaw, &pitch)) return false;
    pos[0] = eye.x; pos[1] = eye.y; pos[2] = eye.z;
    unsigned seq; if (!SocialMyEmote(emote, seconds, &seq)) { *emote = 0; *seconds = 0; }
    return true;
}
void ThirdPersonSelfDrawn(bool drawn) { if (drawn) ++g_selfDraws; }
void ThirdPersonClick() { InterlockedExchange(&g_clicks, 1); }
bool ThirdPersonCommand(const char* line) {
    if (!strncmp(line, "tpstate", 7)) { InterlockedExchange(&g_stateReq, 1); return true; }
    if (!strncmp(line, "tpforce on", 10)) { InterlockedExchange(&g_force, 1); return true; }
    if (!strncmp(line, "tpforce off", 11)) { InterlockedExchange(&g_force, 0); return true; }
    if (!strncmp(line, "tpforce cfg", 11)) { InterlockedExchange(&g_force, -1); return true; }
    return false;
}
