#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>
#include "input.h"
#include "guide_ui.h"
#include "../common/guide.h"
#include "config.h"
#include "devmenu.h"
#include "video.h"
#include "povui.h"
#include "social.h"

bool HookImport(const char* dll, const char* fn, void* hook, void** orig);   // hooks.cpp
void Log(const char* msg);
#include <cstdio>

struct Ev { bool key; unsigned vk, scan; bool down; int dx, dy; unsigned bflags; bool ch = false; };
static CRITICAL_SECTION g_lock; static bool g_init;
static Ev g_q[512]; static int g_n;                         // not yet posted
static Ev g_sent[256]; static volatile LONG g_stats[4];   // posted, answered, calls for real (OS) handles, calls for our handles     // posted, indexed by token & 255
static const ULONG_PTR kTokenBase = 0xC0DE0000;

static void Init() { if (!g_init) { InitializeCriticalSection(&g_lock); g_init = true; } }
static void Push(const Ev& e) { Init(); EnterCriticalSection(&g_lock); if (g_n < 512) g_q[g_n++] = e; LeaveCriticalSection(&g_lock); }
void InputKey(unsigned vk, unsigned scan, bool down) { Push({true, vk, scan, down, 0, 0, 0}); }
void InputChar(unsigned ch) { Ev e = {true, ch, 0, true, 0, 0, 0}; e.ch = true; Push(e); }
void InputMouse(int dx, int dy, unsigned bf) { Push({false, 0, 0, false, dx, dy, bf}); }
unsigned InputStats(int w) { return (unsigned)g_stats[w]; }

void InputPump(HWND wnd) {      // one event per frame, so a sequence of mouse deltas spreads over frames like real motion
    if (!g_init || !wnd) return;
    Ev e; bool have = false;
    EnterCriticalSection(&g_lock);
    if (g_n > 0) { e = g_q[0]; memmove(g_q, g_q + 1, sizeof(Ev) * (--g_n)); have = true; }
    LeaveCriticalSection(&g_lock);
    if (!have) return;
    static unsigned seq; unsigned t = seq++; g_sent[t & 255] = e;
    if (e.ch) PostMessageW(wnd, WM_CHAR, e.vk, 1);
    else if (e.key) {   // keys as ordinary key messages; NOTE: the game ignores these for movement (see NOTES.md), only the mouse path is used by the bot
        LPARAM lp = 1 | ((LPARAM)e.scan << 16) | (e.down ? 0 : ((LPARAM)1 << 30) | ((LPARAM)1 << 31));
        PostMessageW(wnd, e.down ? WM_KEYDOWN : WM_KEYUP, e.vk, lp);
    } else if (e.bflags) {   // a click: the game gets buttons as ordinary mouse messages (a real click makes no raw button record)
        RECT c; GetClientRect(wnd, &c); LPARAM at = MAKELPARAM(c.right / 2, c.bottom / 2);   // where the virtual cursor is
        bool left = e.bflags & 0x3, down = e.bflags & 0x5;      // RI_MOUSE_LEFT_BUTTON_DOWN 1/UP 2, RIGHT_DOWN 4/UP 8
        UINT msg = left ? (down ? WM_LBUTTONDOWN : WM_LBUTTONUP) : (down ? WM_RBUTTONDOWN : WM_RBUTTONUP);
        PostMessageW(wnd, msg, down ? (left ? MK_LBUTTON : MK_RBUTTON) : 0, at);
    } else PostMessageW(wnd, WM_INPUT, RIM_INPUT, (LPARAM)(kTokenBase + (t & 255)));
    InterlockedIncrement(&g_stats[0]);
}

void PanelBoostMsg(bool on, float gain);      // devmenu.cpp
// Panel boost (F3): in a panel the game moves its own cursor about 1/3 pixel per mouse count while the owner's real pointer moves 1 pixel, so the circle falls behind the
// pointer and the pointer leaves the window. With the boost on, the REAL mouse motion is multiplied (the bot's injected events are not touched), the fraction carried over.
static volatile LONG g_boost = -1; static float g_carry[2];
void PanelBoostToggle() { if (g_boost < 0) g_boost = GetCfg().panelMouseBoost; g_boost = !g_boost; PanelBoostMsg(g_boost != 0, GetCfg().panelMouseGain); }
static void BoostMouse(RAWINPUT* ri) {
    if (g_boost < 0) g_boost = GetCfg().panelMouseBoost;
    if (!g_boost || ri->header.dwType != RIM_TYPEMOUSE || (ri->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) return;
    float g = GetCfg().panelMouseGain; LONG* d[2] = {&ri->data.mouse.lLastX, &ri->data.mouse.lLastY};
    for (int i = 0; i < 2; i++) { g_carry[i] += *d[i] * g; LONG w = (LONG)g_carry[i]; g_carry[i] -= w; *d[i] = w; }
}

typedef UINT(WINAPI* GetRawInputDataFn)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);
static GetRawInputDataFn g_orig;
static UINT WINAPI HookGetRawInputData(HRAWINPUT h, UINT cmd, LPVOID data, PUINT size, UINT hdr) {
    ULONG_PTR v = (ULONG_PTR)h;
    if ((v & ~(ULONG_PTR)0xFF) != kTokenBase) {
        InterlockedIncrement(&g_stats[2]);
        UINT r = g_orig(h, cmd, data, size, hdr);
        if (data && r != (UINT)-1 && cmd == RID_INPUT && r >= sizeof(RAWINPUTHEADER) && !DevMenuOpen()) BoostMouse((RAWINPUT*)data);
        if ((DevMenuOpen() || PovRosterOpen() || SocialMouseCaptured()) && data && r != (UINT)-1 && cmd == RID_INPUT && r >= sizeof(RAWINPUTHEADER)) {      // the dev menu or the player list is open: it takes the mouse motion, the game gets an empty event
            RAWINPUT* ri = (RAWINPUT*)data;
            if (ri->header.dwType == RIM_TYPEMOUSE) { if (!(ri->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) { if (DevMenuOpen()) DevMenuRawMove(ri->data.mouse.lLastX, ri->data.mouse.lLastY); else if (SocialMouseCaptured()) SocialRawMove(ri->data.mouse.lLastX, ri->data.mouse.lLastY); } ri->data.mouse.lLastX = ri->data.mouse.lLastY = 0; ri->data.mouse.usButtonFlags = 0; ri->data.mouse.usButtonData = 0; }
            else if (ri->header.dwType == RIM_TYPEKEYBOARD && !(ri->data.keyboard.Flags & RI_KEY_BREAK) && (DevMenuOpen() || PovRosterOpen() || SocialCapturing())) { ri->data.keyboard.VKey = 0; ri->data.keyboard.MakeCode = 0; ri->data.keyboard.Flags |= RI_KEY_BREAK; }
        }
        if (data && r != (UINT)-1 && cmd == RID_INPUT && r >= sizeof(RAWINPUTHEADER) && ((RAWINPUT*)data)->header.dwType == RIM_TYPEKEYBOARD && !(((RAWINPUT*)data)->data.keyboard.Flags & RI_KEY_BREAK) && (SocialSwallowRaw(((RAWINPUT*)data)->data.keyboard.VKey) || ((RAWINPUT*)data)->data.keyboard.VKey == guide::Help)) { RAWINPUT* ri = (RAWINPUT*)data; ri->data.keyboard.VKey = 0; ri->data.keyboard.MakeCode = 0; ri->data.keyboard.Flags |= RI_KEY_BREAK; }      // a key the mod took (it closed the chat box): the game must not see its raw press either
        if (GetCfg().debugMsgs && data && r != (UINT)-1 && r >= sizeof(RAWINPUTHEADER) && cmd == RID_INPUT) {    // diagnostic (debug_msgs=1): what a REAL event looks like (first few, and every button event)
            const RAWINPUT* ri = (const RAWINPUT*)data; static LONG n;
            bool btn = ri->header.dwType == RIM_TYPEMOUSE && ri->data.mouse.usButtonFlags;
            if ((btn || InterlockedIncrement(&n) <= 3)) { char m[220]; snprintf(m, sizeof m, "rawin: REAL type %lu size %lu dev %p wParam %llu | mouse flags %#x btnflags %#x btndata %#x raw %#lx dx %ld dy %ld extra %#lx",
                (unsigned long)ri->header.dwType, (unsigned long)ri->header.dwSize, ri->header.hDevice, (unsigned long long)ri->header.wParam, ri->data.mouse.usFlags, ri->data.mouse.usButtonFlags, ri->data.mouse.usButtonData, ri->data.mouse.ulRawButtons, (long)ri->data.mouse.lLastX, (long)ri->data.mouse.lLastY, ri->data.mouse.ulExtraInformation); Log(m); }
        }
        return r;
    }
    InterlockedIncrement(&g_stats[3]);
    const Ev& e = g_sent[v & 255];
    RAWINPUT ri = {}; ri.header.hDevice = (HANDLE)(ULONG_PTR)(e.key ? 0xC0DE1001 : 0xC0DE1002); ri.header.wParam = RIM_INPUT;
    if (e.key) {
        ri.header.dwType = RIM_TYPEKEYBOARD; ri.header.dwSize = sizeof(RAWINPUTHEADER) + sizeof(RAWKEYBOARD);
        ri.data.keyboard.MakeCode = (USHORT)e.scan; ri.data.keyboard.Flags = e.down ? 0 : RI_KEY_BREAK;
        ri.data.keyboard.VKey = (USHORT)e.vk; ri.data.keyboard.Message = e.down ? WM_KEYDOWN : WM_KEYUP;
    } else {
        ri.header.dwType = RIM_TYPEMOUSE; ri.header.dwSize = sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE);
        ri.data.mouse.usFlags = MOUSE_MOVE_RELATIVE; ri.data.mouse.lLastX = e.dx; ri.data.mouse.lLastY = e.dy;
        ri.data.mouse.usButtonFlags = (USHORT)e.bflags;
    }
    if (DevMenuOpen()) {      // our own (bot) events go to the dev menu while it is open
        if (!e.key) { DevMenuRawMove(e.dx, e.dy); ri.data.mouse.lLastX = ri.data.mouse.lLastY = 0; ri.data.mouse.usButtonFlags = 0; }
        else if (e.down) { ri.data.keyboard.VKey = 0; ri.data.keyboard.MakeCode = 0; ri.data.keyboard.Flags = RI_KEY_BREAK; }
    }
    else if (SocialMouseCaptured()) {      // ... and to the emote wheel / chat box
        if (!e.key) { SocialRawMove(e.dx, e.dy); ri.data.mouse.lLastX = ri.data.mouse.lLastY = 0; ri.data.mouse.usButtonFlags = 0; }
        else if (e.down && SocialCapturing()) { ri.data.keyboard.VKey = 0; ri.data.keyboard.MakeCode = 0; ri.data.keyboard.Flags = RI_KEY_BREAK; }
    }
    if (e.key && e.down && ri.data.keyboard.VKey && (SocialSwallowRaw(e.vk) || e.vk == guide::Help)) { ri.data.keyboard.VKey = 0; ri.data.keyboard.MakeCode = 0; ri.data.keyboard.Flags = RI_KEY_BREAK; }      // (as above, for the bot's own keys)
    UINT total = ri.header.dwSize;
    if (cmd == RID_HEADER) { if (!data) { *size = sizeof(RAWINPUTHEADER); return 0; } if (*size < sizeof(RAWINPUTHEADER)) return (UINT)-1; memcpy(data, &ri.header, sizeof(RAWINPUTHEADER)); return sizeof(RAWINPUTHEADER); }
    if (!data) { *size = total; return 0; }
    if (*size < total) { *size = total; return (UINT)-1; }
    memcpy(data, &ri, total); InterlockedIncrement(&g_stats[1]); return total;
}

void Log(const char* msg);

// The game's message loop reads mouse messages itself, before the window procedure sees them: the dev menu takes its keys and mouse
// messages here, and a message it took becomes WM_NULL. Key-ups and button-ups are shown to the menu but still reach the game.
typedef BOOL(WINAPI* PeekMessageAFn)(LPMSG, HWND, UINT, UINT, UINT);
static PeekMessageAFn g_origPeekA;
static BOOL WINAPI HookPeekMessageA(LPMSG m, HWND w, UINT lo, UINT hi, UINT rm) {
    BOOL r = g_origPeekA(m, w, lo, hi, rm);
    if (!r || !m || !(rm & PM_REMOVE)) return r;
    if (m->message == WM_KILLFOCUS || (m->message == WM_ACTIVATEAPP && !m->wParam)) GuideReset();
    if ((m->message == WM_KEYDOWN || m->message == WM_SYSKEYDOWN || m->message == WM_KEYUP || m->message == WM_SYSKEYUP) &&
        GuideKey((unsigned)m->wParam, m->message == WM_KEYDOWN || m->message == WM_SYSKEYDOWN)) { m->message = WM_NULL; return r; }
    if (!(DevMenuEnabled() || VideoEnabled())) return r;
    bool take = false;
    switch (m->message) {
    case WM_KEYDOWN: case WM_SYSKEYDOWN: take = DevMenuKey((unsigned)m->wParam, true) || SocialKey((unsigned)m->wParam, true); break;
    case WM_KEYUP: case WM_SYSKEYUP: DevMenuKey((unsigned)m->wParam, false); take = SocialKey((unsigned)m->wParam, false); break;
    case WM_CHAR: take = DevMenuOpen() || PovRosterOpen() || SocialChar((unsigned)m->wParam) || SocialCapturing(); break;
    case WM_LBUTTONDOWN: take = DevMenuMouseButton(0, true) || PovRosterOpen() || SocialCapturing(); break;
    case WM_RBUTTONDOWN: take = DevMenuMouseButton(1, true) || PovRosterOpen() || SocialCapturing(); break;
    case WM_MBUTTONDOWN: take = DevMenuMouseButton(2, true) || PovRosterOpen() || SocialCapturing(); break;
    case WM_LBUTTONUP: DevMenuMouseButton(0, false); break;
    case WM_RBUTTONUP: DevMenuMouseButton(1, false); break;
    case WM_MOUSEWHEEL: take = DevMenuWheel(GET_WHEEL_DELTA_WPARAM(m->wParam)) || PovRosterOpen() || SocialCapturing(); break;
    case WM_MOUSEMOVE: take = DevMenuOpen() || PovRosterOpen() || SocialCapturing(); break;
    }
    if (take) m->message = WM_NULL;
    return r;
}
void InputInstallHook() {
    HookImport("USER32.dll", "GetRawInputData", (void*)HookGetRawInputData, (void**)&g_orig);
    HookImport("USER32.dll", "PeekMessageA", (void*)HookPeekMessageA, (void**)&g_origPeekA);
}
