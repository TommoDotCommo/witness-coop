// Third-person view while emoting: the game side (see thirdperson.h; the camera itself is core/thirdperson.h, tested game-free).
// Build 11370412, found by disassembly and an in-process write watchpoint (tpwatch): every frame the main loop calls exe+0x2C9380, which
// copies the player's camera (exe+0x6304C0 position, +0x6304D0 x y z w quaternion) into the camera the world drawing culls with
// (exe+0x62D578) and builds the views the GPU draws with (main view at exe+0x649860: position, then its matrices; more at 0x649AB0 ...);
// then it calls the world drawing exe+0x1C8970, which presents the frame from inside (exe+0x1C95B7). Both functions take no arguments and
// are called from two places each (exe+0x69653 / 0x699AD and exe+0x696BD / 0x69A17). While the view is out, those calls go through the
// mod: the views are built a second time from the third-person camera, the world is drawn with it, and afterwards the views are built again
// from the player's camera, so the game's logic (puzzle aiming, the pose the others receive) keeps the player's own. Nothing is patched
// until the first emote with the setting on.
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
const uintptr_t kRenderWorld = 0x1C8970, kRenderCalls[2] = {0x696BD, 0x69A17}, kBuildViews = 0x2C9380, kViewCalls[2] = {0x69653, 0x699AD}, kMainView = 0x649860;
const uintptr_t kCamPos = 0x6304C0, kCamQuat = 0x6304D0, kCamAng = 0x6303EC, kViewPos = 0x62D578, kViewQuat = 0x62D584;

Cam g_cam;
bool g_patched = false, g_patchFailed = false;
unsigned g_hookCalls = 0, g_swapped = 0, g_selfDraws = 0, g_selfFrames = 0, g_firstPersonFrames = 0;
bool g_viewUsed = false, g_frameView = false; float g_viewPos[3], g_viewAng[2];      // set by the hook; g_frameView: this Present's frame was drawn from it
float g_qErr = -1, g_maxOffset = 0, g_lastOffset = 0, g_gameView[3] = {0, 0, 0};      // g_gameView: where the game's own main view stood when it drew the last third-person frame
View g_frameV; bool g_frameVValid = false, g_viewsSwapped = false;      // g_qErr: how far the game's own quaternion is from QuatFromYawPitch (a convention check, logged)
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

// The game presents from inside the world drawing, so the mod's Present runs while the third-person camera is still in place:
// ThirdPersonPresentBegin puts the player's camera back first. Other threads that read the camera (the pose sent to the others, the voice
// listener) use ThirdPersonGen / ThirdPersonFixEye, a sequence lock: an odd generation means the third-person camera is in place.
volatile LONG g_gen = 0; float g_realCp[3], g_realCq[4];
void RestoreReal() {
    if (!(g_gen & 1)) return;
    memcpy((void*)(Base() + kCamPos), g_realCp, 12); memcpy((void*)(Base() + kCamQuat), g_realCq, 16);
    InterlockedIncrement(&g_gen);
}
void PutCamera(const View& v) {      // the third-person camera into the player's camera (odd generation while it is there)
    float* cp = (float*)(Base() + kCamPos); float* cq = (float*)(Base() + kCamQuat);
    memcpy(g_realCp, cp, 12); memcpy(g_realCq, cq, 16); InterlockedIncrement(&g_gen);
    float p[3] = {v.pos.x, v.pos.y, v.pos.z}, q[4]; QuatFromYawPitch(v.yaw, v.pitch, q); memcpy(cp, p, 12); memcpy(cq, q, 16);
}
typedef void (*VoidFn)();
void BuildViewsHook() {
    VoidFn build = (VoidFn)(Base() + kBuildViews);
    build();      // the game's own, from the player's camera
    g_frameVValid = false;
    V3 eye; float yaw, pitch; View v;
    if (!g_cam.Active() || !PlayerCamera(&eye, &yaw, &pitch) || !g_cam.ViewFor(eye, yaw, pitch, &v)) return;
    const float* cq = (const float*)(Base() + kCamQuat); float q[4]; QuatFromYawPitch(yaw, pitch, q);
    float e1 = 0, e2 = 0; for (int i = 0; i < 4; ++i) { e1 = fmaxf(e1, fabsf(q[i] - cq[i])); e2 = fmaxf(e2, fabsf(q[i] + cq[i])); }
    g_qErr = fminf(e1, e2);      // QuatFromYawPitch is the game's own rotation (q and -q are one rotation)
    PutCamera(v); build(); RestoreReal();      // the views and the culling camera now stand at the third-person camera
    g_frameV = v; g_frameVValid = true; g_viewsSwapped = true;
    g_lastOffset = Len(Sub(v.pos, eye)); if (g_lastOffset > g_maxOffset) g_maxOffset = g_lastOffset;
}
void RenderWorldHook() {
    VoidFn draw = (VoidFn)(Base() + kRenderWorld);
    ++g_hookCalls;
    if (!g_frameVValid) { draw(); return; }
    const float* mv = (const float*)(Base() + kMainView); memcpy(g_gameView, mv, 12);      // the view the game is about to draw with
    g_viewPos[0] = g_frameV.pos.x; g_viewPos[1] = g_frameV.pos.y; g_viewPos[2] = g_frameV.pos.z; g_viewAng[0] = g_frameV.yaw; g_viewAng[1] = g_frameV.pitch; g_viewUsed = true; ++g_swapped;
    PutCamera(g_frameV);      // (parts of the drawing read the player's camera itself)
    draw();
    RestoreReal();
    if (g_viewsSwapped) { ((VoidFn)(Base() + kBuildViews))(); g_viewsSwapped = false; }      // the views back to the player's camera
    g_frameVValid = false;
}

// A call site `call exe+target` is made to go through a stub near the exe (a call reaches +-2 GB) to the hook. Render thread only (the
// game's loop is inside Present, so none of these calls is running). Refuses unless every site is the expected call.
bool CheckCall(uintptr_t site, uintptr_t target) {
    const unsigned char* c = (const unsigned char*)(Base() + site); if (!Readable(c, 5) || c[0] != 0xE8) return false;
    int32_t rel; memcpy(&rel, c + 1, 4); return Base() + site + 5 + (intptr_t)rel == Base() + target;
}
bool Redirect(uintptr_t site, void* hook, unsigned char* stub) {
    stub[0] = 0xFF; stub[1] = 0x25; memset(stub + 2, 0, 4); uint64_t to = (uint64_t)(uintptr_t)hook; memcpy(stub + 6, &to, 8);      // jmp [rip+0] -> hook
    unsigned char* c = (unsigned char*)(Base() + site); intptr_t d = (intptr_t)stub - (intptr_t)(Base() + site + 5);
    if (d > INT32_MAX || d < INT32_MIN) return false;
    int32_t rel = (int32_t)d; DWORD old;
    if (!VirtualProtect(c + 1, 4, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(c + 1, &rel, 4); VirtualProtect(c + 1, 4, old, &old); return true;
}
bool PatchCalls() {
    uintptr_t base = Base();
    for (uintptr_t site : kRenderCalls) if (!CheckCall(site, kRenderWorld)) return false;
    for (uintptr_t site : kViewCalls) if (!CheckCall(site, kBuildViews)) return false;
    unsigned char* stub = nullptr;
    for (uintptr_t a = (base & ~(uintptr_t)0xFFFF) - 0x10000; a > 0x10000 && base - a < 0x70000000; a -= 0x10000)
        if ((stub = (unsigned char*)VirtualAlloc((void*)a, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE))) break;
    if (!stub) return false;
    bool ok = true;
    for (uintptr_t site : kViewCalls) ok = Redirect(site, (void*)&BuildViewsHook, stub) && ok;
    for (uintptr_t site : kRenderCalls) ok = Redirect(site, (void*)&RenderWorldHook, stub + 32) && ok;
    FlushInstructionCache(GetCurrentProcess(), stub, 64); FlushInstructionCache(GetCurrentProcess(), (void*)(base + kViewCalls[0]), 0x400);
    return ok;
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
               "\"game_view_pos\":[%.4f,%.4f,%.4f],\"offset\":%.4f,\"max_offset\":%.4f,\"self_draws\":%u,\"self_frames\":%u,\"qerr\":%.6f,\"game_view\":[%.4f,%.4f,%.4f],\"probe\":[%.3f,%.3f,%.3f,%.3f,%.3f]}",
        (g_force >= 0 ? g_force != 0 : GetCfg().emoteThirdPerson) ? 1 : 0, PhaseName(g_cam.phase), g_cam.Active() ? witness::Ease(g_cam.s) : 0.0f, WhyName(g_cam.last), g_cam.scale,
        g_cam.starts, g_cam.returns, g_patched ? 1 : 0, g_patchFailed ? 1 : 0, g_hookCalls, g_swapped, g_frameView ? 1 : 0,
        g_viewPos[0], g_viewPos[1], g_viewPos[2], g_viewAng[0], g_viewAng[1], eye.x, eye.y, eye.z, yaw, pitch,
        Readable(vp, 12) ? vp[0] : 0, Readable(vp, 12) ? vp[1] : 0, Readable(vp, 12) ? vp[2] : 0, g_frameView ? g_lastOffset : 0.0f, g_maxOffset, g_selfDraws, g_selfFrames, g_qErr, g_gameView[0], g_gameView[1], g_gameView[2],
        g_probeDepth[0], g_probeDepth[1], g_probeDepth[2], g_probeDepth[3], g_probeDepth[4]);
    fclose(f); MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING);
}
} // namespace

// Diagnostic (finding which code writes a game address each frame): `tpwatch <rva>` arms a hardware write watchpoint (DR0, 4 bytes) on the
// render thread; a vectored handler counts the instructions that wrote there; `tpwatch dump` logs them (as exe RVAs after the write) with
// two stack slots that held return addresses for the view builder exe+0x2C8D50. `tpwatch 0` disarms. Needs dev_menu=1.
static volatile DWORD g_renderTid = 0; struct WatchHit { uintptr_t rip, c1, c2; unsigned n; }; static WatchHit g_hits[16]; static volatile LONG g_hitCount = 0, g_watchTotal = 0;
static LONG CALLBACK WatchHandler(EXCEPTION_POINTERS* e) {
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(e->ContextRecord->Dr6 & 1)) return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t rip = (uintptr_t)e->ExceptionRecord->ExceptionAddress - Base(); e->ContextRecord->Dr6 = 0;
    if (InterlockedIncrement(&g_watchTotal) > 2000) e->ContextRecord->Dr7 = 0;      // enough: disarm
    uintptr_t sp = (uintptr_t)e->ContextRecord->Rsp, c1 = 0, c2 = 0;      // two stack slots that hold return addresses when the write is in exe+0x2C8D50 (called directly / through exe+0x2C8D00)
    if (!IsBadReadPtr((void*)(sp + 0xb8), 8)) c1 = *(uintptr_t*)(sp + 0xb8) - Base();
    if (!IsBadReadPtr((void*)(sp + 0xf8), 8)) c2 = *(uintptr_t*)(sp + 0xf8) - Base();
    for (LONG i = 0; i < g_hitCount; ++i) if (g_hits[i].rip == rip && g_hits[i].c1 == c1 && g_hits[i].c2 == c2) { ++g_hits[i].n; return EXCEPTION_CONTINUE_EXECUTION; }
    if (g_hitCount < 16) { g_hits[g_hitCount].rip = rip; g_hits[g_hitCount].c1 = c1; g_hits[g_hitCount].c2 = c2; g_hits[g_hitCount].n = 1; ++g_hitCount; }
    return EXCEPTION_CONTINUE_EXECUTION;
}
static void WatchArm(uintptr_t rva) {
    static PVOID veh; if (!veh) veh = AddVectoredExceptionHandler(1, WatchHandler);
    HANDLE t = g_renderTid ? OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, g_renderTid) : nullptr; if (!t) { Log("tpwatch: no render thread"); return; }
    g_hitCount = 0; g_watchTotal = 0;
    SuspendThread(t); CONTEXT c = {}; c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(t, &c)) { c.Dr0 = Base() + rva; c.Dr7 = rva ? (1 | (1u << 16) | (3u << 18)) : 0; c.Dr6 = 0; SetThreadContext(t, &c); }
    ResumeThread(t); CloseHandle(t);
    char m[100]; snprintf(m, sizeof m, "tpwatch: write watch on exe+%#llx %s", (unsigned long long)rva, rva ? "armed" : "off"); Log(m);
}
static void WatchDump() {
    char m[160]; snprintf(m, sizeof m, "tpwatch: %ld writes, %ld places", (long)g_watchTotal, (long)g_hitCount); Log(m);
    for (LONG i = 0; i < g_hitCount; ++i) { snprintf(m, sizeof m, "tpwatch:   after exe+%#llx  x%u  (stack +0xb8 exe+%#llx, +0xf8 exe+%#llx)", (unsigned long long)g_hits[i].rip, g_hits[i].n, (unsigned long long)g_hits[i].c1, (unsigned long long)g_hits[i].c2); Log(m); }
}
void ThirdPersonPresentBegin() { g_renderTid = GetCurrentThreadId(); RestoreReal(); }
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
        Log(g_patched ? "thirdperson: the view building and the world drawing now go through the mod (exe+0x69653, 0x699AD, 0x696BD, 0x69A17)" : "thirdperson: the world drawing is not the expected call: the view stays first person");
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
    if (!strncmp(line, "tpwatch", 7)) {      // developer diagnostic: only with the dev menu on
        unsigned long long rva; if (!GetCfg().devMenu) Log("tpwatch: needs dev_menu=1"); else if (!strncmp(line, "tpwatch dump", 12)) WatchDump(); else if (sscanf(line, "tpwatch %llx", &rva) == 1) WatchArm((uintptr_t)rva);
        return true;
    }
    if (!strncmp(line, "tpforce on", 10)) { InterlockedExchange(&g_force, 1); return true; }
    if (!strncmp(line, "tpforce off", 11)) { InterlockedExchange(&g_force, 0); return true; }
    if (!strncmp(line, "tpforce cfg", 11)) { InterlockedExchange(&g_force, -1); return true; }
    return false;
}
