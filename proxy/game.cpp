#include "radio/radio_game.h"
// Game-function bridge. Commands arrive from other threads and run on the render thread, between frames.
// witness_cmd.txt (one per line):  door <hexid> [amount] | cable <hexid>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include "game.h"
#include "race_game.h"
#include "model_gpu.h"
#include "coop.h"
#include "input.h"
#include "config.h"
#include "devmenu.h"
#include "mapgen.h"
#include "social.h"
#include "video.h"
#include "tv.h"

void Log(const char* msg);
void RequestShot(const char* name);
void OverlayLogTags();
#include "ui.h"
void OverlayPovState(unsigned* aimed, int* rosterOpen);
unsigned OverlayTvAim();
extern HMODULE g_selfModule();

static const uintptr_t kGlobals = 0x62D0A0, kDoorTypeInfo = 0x62DE70, kDoorOpenStart = 0x8E4B0;
static const uintptr_t kPanelTypeInfo = 0x62E670, kPanelPowerOn = 0xC2890;
static const uintptr_t kCableTypeInfo = 0x62EB10, kCablePowerOn = 0x175EE0;
typedef void(__fastcall* DoorOpenFn)(void* door, float amount);

struct Cmd { int type; unsigned id; float amount; unsigned vk, scan; bool down; int dx, dy; unsigned bf; };   // type 0 door, 1 key, 2 mouse, 3 cable
struct Tp { float v[5]; int frames; }; static Tp g_tp; static bool g_tpOn;      // teleport: x y z yaw pitch, held for a few frames
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
    if (g_n < 64) g_q[g_n++] = {0, id, amount, 0, 0, false, 0, 0, 0};
    LeaveCriticalSection(&g_lock);
}

void GameEnqueueCablePower(unsigned id) {
    if (!g_lockInit) return;
    EnterCriticalSection(&g_lock);
    if (g_n < 64) g_q[g_n++] = {3, id, 0, 0, 0, false, 0, 0, 0};
    LeaveCriticalSection(&g_lock);
}

void GameRunQueued() {
    if (g_tpOn && g_lockInit) {   // camera position and angles are the player's: write them for a few frames so the game keeps them
        float* pos = (float*)(Base() + 0x6304B0 + 0x10); float* ang = (float*)(Base() + 0x6303EC);
        uintptr_t pl = GameEntity(0x1E465);       // the player ('Human'): the camera follows it, 1.69 above the feet
        if (pl && !IsBadWritePtr((void*)(pl + 0x24), 12)) { float* fp = (float*)(pl + 0x24); fp[0] = g_tp.v[0]; fp[1] = g_tp.v[1]; fp[2] = g_tp.v[2] - 1.69f; }
        if (!IsBadWritePtr(pos, 12) && !IsBadWritePtr(ang, 8)) { pos[0] = g_tp.v[0]; pos[1] = g_tp.v[1]; pos[2] = g_tp.v[2]; ang[0] = g_tp.v[3]; ang[1] = g_tp.v[4]; }
        if (--g_tp.frames <= 0) { g_tpOn = false; Log("game: teleport done"); }
    }
    if (!g_lockInit || g_n == 0) return;
    Cmd batch[64]; int n;
    EnterCriticalSection(&g_lock); n = g_n; memcpy(batch, g_q, sizeof(Cmd) * n); g_n = 0; LeaveCriticalSection(&g_lock);
    for (int i = 0; i < n; i++) {
        if (batch[i].type == 10) { TvDump(); continue; }
        if (batch[i].type == 11) { TvEnable(batch[i].id != 0); continue; }
        if (batch[i].type == 12) { TvAllow(batch[i].id != 0); continue; }
        if (batch[i].type == 13) { TvDeployAction(batch[i].id); continue; }
        if (batch[i].type == 9) { ModelGpuLogStats(); continue; }
        if (batch[i].type == 8) { ModelGpuDumpPlayers(); continue; }
        if (batch[i].type == 7) { auto social=SocialState(); CoopDumpStatus(ModelGpuStats().submittedPlayers, &social); continue; }
        if (batch[i].type == 1) { InputKey(batch[i].vk, batch[i].scan, batch[i].down); continue; }
        if (batch[i].type == 2) { InputMouse(batch[i].dx, batch[i].dy, batch[i].bf); continue; }
        if (batch[i].type == 4 || batch[i].type == 5) {      // test commands: 'solve' makes the panel solved as a player's solve would (fields + dispatcher), 'dispatch' only runs the dispatcher
            if (batch[i].type == 5) GameMarkPanelSolved(batch[i].id);
            char m[100]; snprintf(m, sizeof m, "game: %s %#x -> dispatcher %s", batch[i].type == 5 ? "solve" : "dispatch", batch[i].id, GameDispatchTargetNow(batch[i].id) ? "called" : "not called"); Log(m); continue;
        }
        if (batch[i].type == 6) { char m[100]; snprintf(m, sizeof m, "game: cable %#x power-off %s", batch[i].id, GameCableOffNow(batch[i].id) ? "called" : "refused"); Log(m); continue; }
        if (batch[i].type == 3) { char m[100]; snprintf(m, sizeof m, "game: cable %#x power-on %s", batch[i].id, GameCablePowerOnNow(batch[i].id) ? "called" : "refused"); Log(m); continue; }
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
        Sleep(10);      // was 250: every command a bot sent waited for the next poll, and a drawn line is hundreds of commands
        if (!MoveFileExW(cmd, taken, MOVEFILE_REPLACE_EXISTING)) continue;
        FILE* f = _wfopen(taken, L"r"); if (!f) continue;
        char line[160];
        while (fgets(line, sizeof line, f)) {
            unsigned id; float amt = 1.0f;
            char a[16], b[16], a2[64], nm[64]; int dx, dy;
            if (sscanf(line, "door %x %f", &id, &amt) >= 1) GameEnqueueDoorOpen(id, amt);
            else if (sscanf(line, "cableoff %x", &id) == 1) GameEnqueueCableOff(id);
            else if (sscanf(line, "partnershot %15s", a) == 1) VideoSavePartnerShot(a);
            else if (sscanf(line, "partner %15s", a) == 1) { if (!strcmp(a, "on")) VideoSetMode(1); else if (!strcmp(a, "large")) VideoSetMode(2); else if (!strcmp(a, "off")) VideoSetMode(0); else if (!strcmp(a, "stats")) { unsigned s1, r1, d1; VideoStats(&s1, &r1, &d1); char mm[100]; snprintf(mm, sizeof mm, "video: sent %u received %u decoded %u", s1, r1, d1); Log(mm); } }
            else if (sscanf(line, "savepoint %63s", nm) == 1) { std::string w2 = std::string("save:") + nm; DevMenuCommand(w2.c_str(), 0); }
            else if (sscanf(line, "savepreset %63s", nm) == 1) { std::string w2 = std::string("preset:") + nm; DevMenuCommand(w2.c_str(), 0); }
            else if (sscanf(line, "restore %63s", nm) == 1) { std::string w2 = std::string("restore:") + nm; DevMenuCommand(w2.c_str(), 0); }
            else if (!strncmp(line, "progress ", 9)) { std::string t = line + 9; while (!t.empty() && (t.back() == '\r' || t.back() == '\n')) t.pop_back(); DevMenuCommand(("progress:" + t).c_str(), 0); }
            else if (!strncmp(line, "progressstat", 12)) DevMenuCommand("progressstat", 0);
            else if (sscanf(line, "complete %d", &dx) == 1) DevMenuCommand("complete", (unsigned)dx);
            else if (sscanf(line, "menusel %x", &id) == 1) DevMenuCommand("select", id);
            else if (sscanf(line, "menudo %15s", a) == 1) DevMenuCommand(a, 0);
            else if (sscanf(line, "menuat %d %d", &dx, &dy) == 2) DevMenuSetCursor((float)dx, (float)dy);
            else if (sscanf(line, "menu %15s", a) == 1) { if (!strcmp(a, "map")) DevMenuSetPage(0); else if (!strcmp(a, "nearby")) DevMenuSetPage(1); else if (!strcmp(a, "toggle")) DevMenuToggle(); else if (!strcmp(a, "saves")) DevMenuSetPage(2); }
            else if (sscanf(line, "cable %x", &id) == 1) GameEnqueueCablePower(id);
            else if (sscanf(line, "solve %x", &id) == 1 || sscanf(line, "dispatch %x", &id) == 1) { Cmd c = {line[0] == 's' ? 5 : 4, id, 0, 0, 0, false, 0, 0, 0}; EnterCriticalSection(&g_lock); if (g_n < 64) g_q[g_n++] = c; LeaveCriticalSection(&g_lock); }
            else if (!strncmp(line, "chat ", 5) || !strncmp(line, "typing ", 7) || !strncmp(line, "typetext ", 9) || !strncmp(line, "ping", 4) || !strncmp(line, "socialstat", 10) || !strncmp(line, "emote ", 6) || !strncmp(line, "wheel ", 6) || !strncmp(line, "canvas ", 7) || !strncmp(line, "reticle ", 8) || !strncmp(line, "drawstat", 8) || !strncmp(line, "drawclear", 9)) SocialCommand(line);
            else if (!strncmp(line, "radiodump ", 10)) RadioRequestDump((unsigned)strtoul(line+10,nullptr,10));
            else if (!strncmp(line, "voiceplayer ", 12)) { unsigned id; int volume; if (sscanf(line+12, "%u %d", &id, &volume)==2 && id>0 && id<=255 && volume>=0 && volume<=100) VoicePlayerVolume(id, volume/100.0f); }
            else if (!strncmp(line, "voicestat", 9)) VoiceStat();
            else if (!strncmp(line, "racestat", 8)) RaceStat();
            else if (!strncmp(line, "voicechannel ", 13)) VoiceSetChannel(line + 13);
            else if (!strncmp(line, "mapgen ", 7)) MapGenCommand(line + 7);
            else if (!strncmp(line, "mapstat", 7)) DevMenuCommand("mapstat", 0);
            else if (sscanf(line, "key %15s %15s", a, b) == 2) {       // key W down|up
                struct { const char* n; unsigned vk, sc; } keys[] = {{"W", 0x57, 0x11}, {"A", 0x41, 0x1E}, {"S", 0x53, 0x1F}, {"D", 0x44, 0x20}, {"SPACE", 0x20, 0x39}, {"ENTER", 0x0D, 0x1C}, {"ESC", 0x1B, 0x01}, {"F1", 0x70, 0x3B}, {"F2", 0x71, 0x3C}, {"F4", 0x73, 0x3E}, {"F6", 0x75, 0x40}, {"F7", 0x76, 0x41}, {"F8", 0x77, 0x42}, {"TAB", 0x09, 0x0F}, {"F9", 0x78, 0x43}, {"F5", 0x74, 0x3F}, {"V", 0x56, 0x2F}, {"LB", 0xDB, 0x1A}, {"RB", 0xDD, 0x1B}, {"BACK", 0x08, 0x0E}, {"UP", 0x26, 0x48}, {"DOWN", 0x28, 0x50}, {"F3", 0x72, 0x3D}, {"SHIFT", 0xA0, 0x2A}, {"T", 0x54, 0x14}, {"P", 0x50, 0x19}, {"G", 0x47, 0x22}, {"X", 0x58, 0x2D}, {"C", 0x43, 0x2E}, {"Z", 0x5A, 0x2C}, {"B", 0x42, 0x30}, {"N", 0x4E, 0x31}, {"H", 0x48, 0x23}, {"L", 0x4C, 0x26}, {"M", 0x4D, 0x32}, {"R", 0x52, 0x13}, {"O", 0x4F, 0x18}, {"J", 0x4A, 0x24}, {"F10", 0x79, 0x44}, {"F11", 0x7A, 0x57}, {"F12", 0x7B, 0x58}};
                for (auto& k : keys) if (!_stricmp(a, k.n)) { Cmd c = {1, 0, 0, k.vk, k.sc, !_stricmp(b, "down"), 0, 0, 0}; EnterCriticalSection(&g_lock); if (g_n < 64) g_q[g_n++] = c; LeaveCriticalSection(&g_lock); }
            } else if (sscanf(line, "mouse %d %d", &dx, &dy) == 2) { Cmd c = {2, 0, 0, 0, 0, false, dx, dy, 0}; EnterCriticalSection(&g_lock); if (g_n < 64) g_q[g_n++] = c; LeaveCriticalSection(&g_lock); }
            else if (sscanf(line, "shot %15s", a) == 1) RequestShot(a);
            else if (!strncmp(line, "probe", 5)) ModelGpuRequestProbe();
            else if (!strncmp(line, "depthview ", 10)) ModelGpuDepthView(!strncmp(line + 10, "on", 2));
            else if (!strncmp(line, "fakepose off", 12)) CoopSetFakeRemote(false, nullptr, 0);
            else if (!strncmp(line, "fakepose ", 9)) { float f[4]; if (sscanf(line, "fakepose %f %f %f %f", &f[0], &f[1], &f[2], &f[3]) == 4) CoopSetFakeRemote(true, f, f[3]); }
            else if (sscanf(line, "tvdeploy %u", &id) == 1 || !strncmp(line, "tvdeploy", 8) || !strncmp(line, "tvretract", 9)) {
                if(!strncmp(line, "tvretract", 9)) id=0;
                else if(sscanf(line, "tvdeploy %u", &id) != 1) id=OverlayTvAim();
                EnterCriticalSection(&g_lock); if (g_n < 64) g_q[g_n++] = {13, id, 0, 0, 0, false, 0, 0, 0}; LeaveCriticalSection(&g_lock);
            }
            else if (!strncmp(line, "tvstate", 7) || sscanf(line, "tv %u", &id) == 1 || sscanf(line, "tvconsent %u", &id) == 1) {
                int type = !strncmp(line, "tvstate", 7) ? 10 : (!strncmp(line, "tvconsent", 9) ? 12 : 11);
                EnterCriticalSection(&g_lock); if (g_n < 64) g_q[g_n++] = {type, id, 0, 0, 0, false, 0, 0, 0}; LeaveCriticalSection(&g_lock);
            }
            else if (!strncmp(line, "modelstate", 10)) { EnterCriticalSection(&g_lock); if (g_n < 64) g_q[g_n++] = {8, 0, 0, 0, 0, false, 0, 0, 0}; LeaveCriticalSection(&g_lock); }
            else if (!strncmp(line, "netstate", 8)) { EnterCriticalSection(&g_lock); if (g_n < 64) g_q[g_n++] = {7, 0, 0, 0, 0, false, 0, 0, 0}; LeaveCriticalSection(&g_lock); }
            else if (sscanf(line, "atlasdump %15s", a) == 1) UiDumpAtlas(a);
            else if (!strncmp(line, "tagstat", 7)) OverlayLogTags();
            else if (!strncmp(line, "povstat", 7)) { unsigned w, s, rq, aimed; int ro; unsigned sent, recv, dec; VideoPovStats(&w, &s, &rq); VideoStats(&sent, &recv, &dec); OverlayPovState(&aimed, &ro); unsigned box, pic; OverlayPovDrawn(&box, &pic); char m[240]; snprintf(m, sizeof m, "povstat: watching %u subscribers %u requests_sent %u pictures_sent %u received %u decoded %u aimed %u roster %d box %u pic %u", w, s, rq, sent, recv, dec, aimed, ro, box, pic); Log(m); }
            else if (sscanf(line, "pov %15s", a) == 1) { if (!strcmp(a, "off")) VideoClear(); else if (!strcmp(a, "next")) { std::vector<unsigned> ids; for (const auto& r : CoopGetRemotes()) if (r.valid) ids.push_back(r.id); VideoNext(ids); } else VideoWatch((unsigned)atoi(a)); }
            else if (!strncmp(line, "linestat", 8)) ModelGpuLogLineStats();
            else if (!strncmp(line, "linedump", 8)) OverlayLineDump();
            else if (!strncmp(line, "cursorstat", 10)) { unsigned c, p; HooksCursorStat(&c, &p); char m[120]; snprintf(m, sizeof m, "cursorstat: unfocused_clip %u unfocused_setpos %u", c, p); Log(m); }
            else if (!strncmp(line, "modelstat", 9)) {
                if (GetCfg().noModelStatQueue) ModelGpuLogStats(); // mutant: old command-thread read
                else { EnterCriticalSection(&g_lock); if (g_n < 64) g_q[g_n++] = {9, 0, 0, 0, 0, false, 0, 0, 0}; LeaveCriticalSection(&g_lock); }
            }
            else if (!strncmp(line, "hover off", 9)) GameHoverOff();
            else if (!strncmp(line, "hover ", 6)) { float f[5]; if (sscanf(line, "hover %f %f %f %f %f", &f[0], &f[1], &f[2], &f[3], &f[4]) == 5) GameHover(f[0], f[1], f[2], f[3], f[4], 600); }
            else if (!strncmp(line, "tp ", 3)) { Tp t = {}; if (sscanf(line, "tp %f %f %f %f %f", &t.v[0], &t.v[1], &t.v[2], &t.v[3], &t.v[4]) == 5) { t.frames = 12; EnterCriticalSection(&g_lock); g_tp = t; g_tpOn = true; LeaveCriticalSection(&g_lock); } }
            else if (!strncmp(line, "stats", 5)) { char m[160]; snprintf(m, sizeof m, "input: posted %u answered %u | GetRawInputData calls: real handles %u, ours %u", InputStats(0), InputStats(1), InputStats(2), InputStats(3)); Log(m); }
            else if (sscanf(line, "button %15s %15s", a, b) == 2) { bool left = !_stricmp(a, "L"), dn = !_stricmp(b, "down"); Cmd c = {2, 0, 0, 0, 0, false, 0, 0, left ? (dn ? 0x1u : 0x2u) : (dn ? 0x4u : 0x8u)}; EnterCriticalSection(&g_lock); if (g_n < 64) g_q[g_n++] = c; LeaveCriticalSection(&g_lock); }
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

bool GamePanelPowerOnNow(unsigned id) {
    uintptr_t e = id < 0x40000 ? GameEntity(id) : 0;
    if (!e || IsBadReadPtr((void*)(e + 8), 8) || *(uintptr_t*)(e + 8) != Base() + kPanelTypeInfo) return false;
    // Build 11370412: sets requested power (+0x2A4), handles redraw/randomization and linked doors.
    // The game's own frame updates the visible power level (+0x2A0).
    const unsigned char* fn = (const unsigned char*)(Base() + kPanelPowerOn);
    const unsigned char prologue[] = {0x48, 0x89, 0x5C, 0x24, 0x10};
    if (IsBadReadPtr(fn, sizeof prologue) || memcmp(fn, prologue, sizeof prologue)) return false;
    ((void(__fastcall*)(void*))(fn))((void*)e);
    // The game's target dispatcher (exe+0xC24F0) also writes the source panel's engine id to the target's +0x2C8. The source is the
    // panel whose target field (+0x2B4) names this one (engine id = table index + the table's base id); a solved one wins.
    if (!GetCfg().noPowerLink && !IsBadReadPtr((void*)(Base() + kGlobals), 8)) {
        uintptr_t glob = *(uintptr_t*)(Base() + kGlobals); unsigned baseId = glob && !IsBadReadPtr((void*)(glob + 8), 4) ? *(unsigned*)(glob + 8) : 0;
        unsigned want = id + baseId, src = 0;
        for (unsigned i = 0; i < 0x40000 && glob; i++) {
            uintptr_t o = GameEntity(i);
            if (!o || o == e || IsBadReadPtr((void*)(o + 0x2B4), 4) || *(uintptr_t*)(o + 8) != Base() + kPanelTypeInfo || *(unsigned*)(o + 0x2B4) != want) continue;
            src = i + baseId; if (*(unsigned*)(o + 0x298) == 1) break;
        }
        if (src && !*(unsigned*)(e + 0x2C8)) *(unsigned*)(e + 0x2C8) = src;
    }
    return true;
}

bool GameCablePowerOnNow(unsigned id) {
    uintptr_t e = id < 0x40000 ? GameEntity(id) : 0;
    if (!e || IsBadReadPtr((void*)(e + 8), 8) || *(uintptr_t*)(e + 8) != Base() + kCableTypeInfo) return false;
    // Build 11370412: the game's own target dispatcher (exe+0xC24F0) calls this for a Power_Cable target of a solved panel (second
    // argument 0). It lights the cable (+0xC4/+0xC8 to 1.0) and its glow.
    const unsigned char* fn = (const unsigned char*)(Base() + kCablePowerOn);
    const unsigned char prologue[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x60};
    if (IsBadReadPtr(fn, sizeof prologue) || memcmp(fn, prologue, sizeof prologue)) return false;
    ((void(__fastcall*)(void*, unsigned char))(fn))((void*)e, 0);
    return true;
}

void GameMarkPanelSolved(unsigned id) {
    uintptr_t e = id < 0x40000 ? GameEntity(id) : 0;
    if (!e || IsBadReadPtr((void*)(e + 8), 8) || *(uintptr_t*)(e + 8) != Base() + kPanelTypeInfo) return;
    const struct { int off; unsigned v; } w[] = {{0x278, 0x3F800000}, {0x280, 1}, {0x290, 0x3F800000}, {0x294, 0x3F800000}, {0x298, 1}};
    for (auto& x : w) memcpy((void*)(e + x.off), &x.v, 4);
}

bool GameDispatchTargetNow(unsigned panelId, bool any) {
    uintptr_t e = panelId < 0x40000 ? GameEntity(panelId) : 0;
    if (!e || IsBadReadPtr((void*)(e + 8), 8) || *(uintptr_t*)(e + 8) != Base() + kPanelTypeInfo || IsBadReadPtr((void*)(e + 0x2B4), 4)) return false;
    uintptr_t glob = *(uintptr_t*)(Base() + kGlobals); if (!glob || IsBadReadPtr((void*)(glob + 8), 4)) return false;
    unsigned target = *(unsigned*)(e + 0x2B4), idx = target - *(unsigned*)(glob + 8);
    uintptr_t t = idx < 0x40000 ? GameEntity(idx) : 0;
    if (!t || IsBadReadPtr((void*)(t + 8), 8)) return false;
    uintptr_t ti = *(uintptr_t*)(t + 8);     // panels, cables and doors have their own events; every other target type goes through the game's dispatcher
    if (!any && (ti == Base() + kPanelTypeInfo || ti == Base() + kCableTypeInfo || ti == Base() + kDoorTypeInfo)) return false;
    // Build 11370412: exe+0xC24F0(engine id of the target, source entity) switches on the target's type.
    const unsigned char* fn = (const unsigned char*)(Base() + 0xC24F0);
    const unsigned char prologue[] = {0x85, 0xC9, 0x0F, 0x84, 0x89, 0x01, 0x00, 0x00, 0x57, 0x48, 0x83, 0xEC, 0x20};
    if (IsBadReadPtr(fn, sizeof prologue) || memcmp(fn, prologue, sizeof prologue)) return false;
    ((void(__fastcall*)(unsigned, void*))(fn))(target, (void*)e);
    return true;
}

bool GameCableOffNow(unsigned id) {
    uintptr_t e = id < 0x40000 ? GameEntity(id) : 0;
    if (!e || IsBadReadPtr((void*)(e + 8), 8) || *(uintptr_t*)(e + 8) != Base() + kCableTypeInfo) return false;
    // Build 11370412: the counterpart of exe+0x175EE0, called by the game's own switches (Double_Ramp) with the same type check.
    const unsigned char* fn = (const unsigned char*)(Base() + 0x175E50);
    const unsigned char prologue[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x60, 0x48, 0x8B, 0xD9};
    if (IsBadReadPtr(fn, sizeof prologue) || memcmp(fn, prologue, sizeof prologue)) return false;
    ((void(__fastcall*)(void*))(fn))((void*)e);
    return true;
}

void GameEnqueueCableOff(unsigned id) {
    if (!g_lockInit) return;
    EnterCriticalSection(&g_lock);
    if (g_n < 64) g_q[g_n++] = {6, id, 0, 0, 0, false, 0, 0, 0};
    LeaveCriticalSection(&g_lock);
}

// Hover: the world around a far panel has no collision until the player has been there a while, so a released player falls through it and
// the game puts it back at the spawn point; and the game's physics resets the player's height once per frame, so a write per frame is
// not enough. This thread writes the player's position about every millisecond until it is switched off (hover off, another teleport, a
// real W/A/S/D press, or the time limit), which holds the player where it was put. Angles are not written: mouse look still works.
static bool OwnForeground() { DWORD p = 0; GetWindowThreadProcessId(GetForegroundWindow(), &p); return p == GetCurrentProcessId(); }      // a real key press only counts while this game has the focus (bots inject input, and never touch it)
static volatile LONG g_hovOn; static float g_hov[3]; static volatile DWORD g_hovUntil;
static DWORD WINAPI HoverThread(LPVOID) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    DWORD checked = 0; volatile float* fp = nullptr;
    for (;;) {
        if (!g_hovOn) { fp = nullptr; Sleep(10); continue; }
        DWORD now = GetTickCount();
        if (now - checked >= 40) {       // the stop conditions are checked 25 times a second; the write below spins
            checked = now;
            uintptr_t pl = GameEntity(0x1E465); fp = (pl && !IsBadWritePtr((void*)(pl + 0x24), 12)) ? (volatile float*)(pl + 0x24) : nullptr;      // looked up 25 times a second, not on every write (IsBad*Ptr in a spin is risky)
            if ((LONG)(now - g_hovUntil) > 0 || (OwnForeground() && ((GetAsyncKeyState('W') | GetAsyncKeyState('A') | GetAsyncKeyState('S') | GetAsyncKeyState('D')) & 0x8000))) { g_hovOn = 0; Log("game: hover off"); continue; }
        }
        // The game snaps the player to the ground under it once per frame (to the lowest level where no collision is loaded), and builds the
        // camera from the player a little later in the same frame: a write every millisecond left a visible share of frames underground.
        // Spinning keeps the gap between the snap and our write to microseconds.
        if (fp) { fp[0] = g_hov[0]; fp[1] = g_hov[1]; fp[2] = g_hov[2] - 1.69f; }
        YieldProcessor();
    }
}
void GameHover(float x, float y, float z, float yaw, float pitch, unsigned seconds) {
    if (GetCfg().noTeleportHold) { GameTeleport(x, y, z + 8.0f, yaw, pitch); return; }      // twin: the old drop from above
    static bool started; if (!started) { started = true; CreateThread(nullptr, 0, HoverThread, nullptr, 0, nullptr); }
    g_hov[0] = x; g_hov[1] = y; g_hov[2] = z; g_hovUntil = GetTickCount() + seconds * 1000; InterlockedExchange(&g_hovOn, 1);
    GameTeleport(x, y, z, yaw, pitch);      // the angles (and the camera for a few frames)
}
void GameHoverOff() { InterlockedExchange(&g_hovOn, 0); }

void GameTeleport(float x, float y, float z, float yaw, float pitch) {
    if (!g_lockInit) return;
    Tp t = {}; t.v[0] = x; t.v[1] = y; t.v[2] = z; t.v[3] = yaw; t.v[4] = pitch; t.frames = 12;
    EnterCriticalSection(&g_lock); g_tp = t; g_tpOn = true; LeaveCriticalSection(&g_lock);
}
