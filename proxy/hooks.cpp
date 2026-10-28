#include "../common/guide.h"
// Import-table hooks on the game's own executable (build 11370412), installed from DllMain before the game starts:
//  - SHGetFolderPathW: the game finds its save folder with CSIDL_APPDATA, so every copy (and the owner's real game) shares
//    %APPDATA%\The Witness. With save_dir set, that one folder is redirected, which isolates test instances and protects real saves.
//  - CreateFileA/W: optionally adds FILE_SHARE_READ to data-pc.zip (share_zip=1) so one hard-linked archive serves several instances.
// DllMain does no I/O: the hooks read the config and write the log when the game calls them.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include "config.h"

void Log(const char* msg);

bool HookImport(const char* dll, const char* fn, void* hook, void** orig) {
    BYTE* base = (BYTE*)GetModuleHandleW(nullptr);
    auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;
    for (auto d = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); d->Name; d++) {
        if (_stricmp((const char*)(base + d->Name), dll)) continue;
        auto names = (IMAGE_THUNK_DATA*)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        auto iat = (IMAGE_THUNK_DATA*)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; names++, iat++) {
            if (names->u1.Ordinal & IMAGE_ORDINAL_FLAG64) continue;
            if (strcmp((const char*)((IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData))->Name, fn)) continue;
            DWORD old; VirtualProtect(&iat->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
            *orig = (void*)iat->u1.Function; iat->u1.Function = (ULONG_PTR)hook;
            VirtualProtect(&iat->u1.Function, sizeof(void*), old, &old);
            return true;
        }
    }
    return false;
}

// ---- save folder
typedef HRESULT(WINAPI* SHGetFolderPathWFn)(HWND, int, HANDLE, DWORD, LPWSTR);
static SHGetFolderPathWFn g_origSH;
static HRESULT WINAPI HookSHGetFolderPathW(HWND w, int csidl, HANDLE tok, DWORD flags, LPWSTR out) {
    const Cfg& c = GetCfg();
    static volatile LONG logged;
    if (InterlockedIncrement(&logged) <= 6) { char m[120]; snprintf(m, sizeof m, "hooks: SHGetFolderPathW(csidl %#x)%s", csidl, ((csidl & 0xFF) == 0x1A && !c.saveDir.empty()) ? " -> redirected" : ""); Log(m); }
    if ((csidl & 0xFF) == 0x1A && !c.saveDir.empty() && c.saveDir.size() < MAX_PATH - 1) {      // CSIDL_APPDATA
        CreateDirectoryW(c.saveDir.c_str(), nullptr);
        wcscpy(out, c.saveDir.c_str());
        return S_OK;
    }
    return g_origSH(w, csidl, tok, flags, out);
}

// ---- data-pc.zip sharing
typedef HANDLE(WINAPI* CreateFileWFn)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef HANDLE(WINAPI* CreateFileAFn)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static CreateFileWFn g_origCFW; static CreateFileAFn g_origCFA;
static void NoteZip(const char* api, DWORD access, DWORD share, bool changed) {
    static volatile LONG n; if (InterlockedIncrement(&n) > 4) return;
    char m[160]; snprintf(m, sizeof m, "hooks: %s opens data-pc.zip access %#lx share %#lx%s", api, (unsigned long)access, (unsigned long)share, changed ? " -> FILE_SHARE_READ added" : ""); Log(m);
}
static HANDLE WINAPI HookCreateFileW(LPCWSTR p, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD cd, DWORD fl, HANDLE t) {
    if (p && wcsstr(p, L"data-pc.zip")) { bool ch = GetCfg().shareZip && !(s & FILE_SHARE_READ); NoteZip("CreateFileW", a, s, ch); if (ch) s |= FILE_SHARE_READ; }
    return g_origCFW(p, a, s, sa, cd, fl, t);
}
static HANDLE WINAPI HookCreateFileA(LPCSTR p, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD cd, DWORD fl, HANDLE t) {
    if (p && strstr(p, "data-pc.zip")) { bool ch = GetCfg().shareZip && !(s & FILE_SHARE_READ); NoteZip("CreateFileA", a, s, ch); if (ch) s |= FILE_SHARE_READ; }
    return g_origCFA(p, a, s, sa, cd, fl, t);
}

// ---- diagnostic: which Sleep calls shape the frame time (debug_sleep=1)
typedef VOID(WINAPI* SleepFn)(DWORD);
static SleepFn g_origSleep;
static VOID WINAPI HookSleep(DWORD ms) {
    if (GetCfg().debugSleep) {
        static struct { DWORD ms; void* ret; LONG n; } seen[40]; static LONG used;
        void* ret = __builtin_return_address(0); int i = 0;
        for (; i < used; i++) if (seen[i].ms == ms && seen[i].ret == ret) break;
        if (i == used && used < 40) { seen[used].ms = ms; seen[used].ret = ret; used++; }
        if (i < 40 && InterlockedIncrement(&seen[i].n) == 3) {      // log a call the third time it happens: skips one-off startup sleeps
            char m[140]; snprintf(m, sizeof m, "sleep: Sleep(%lu) from exe+%#llx", (unsigned long)ms, (unsigned long long)((ULONG_PTR)ret - (ULONG_PTR)GetModuleHandleW(nullptr))); Log(m);
        }
    }
    g_origSleep(ms);
}

// ---- diagnostic: which window messages the game's loop gets (debug_msgs=1)
typedef BOOL(WINAPI* PeekMessageAFn)(LPMSG, HWND, UINT, UINT, UINT);
static PeekMessageAFn g_origPeek;
static BOOL WINAPI HookPeekMessageA(LPMSG m, HWND w, UINT a, UINT b, UINT rm) {
    BOOL r = g_origPeek(m, w, a, b, rm);
    if (GetCfg().debugMsgs) {   // every distinct call site of PeekMessageA (there may be a second pump that handles input itself)
        static void* sites[16]; static LONG used; void* ra = __builtin_return_address(0); int k = 0;
        for (; k < used; k++) if (sites[k] == ra) break;
        if (k == used && used < 16) { sites[used++] = ra; char s[140]; snprintf(s, sizeof s, "msg: PeekMessageA call site exe+%#llx filter %#x-%#x flags %#x", (unsigned long long)((ULONG_PTR)ra - (ULONG_PTR)GetModuleHandleW(nullptr)), a, b, rm); Log(s); }
    }
    if (r && GetCfg().debugMsgs && (rm & PM_REMOVE)) {
        static LONG seen[0x400]; UINT id = m->message;
        if (id < 0x400 && id != 0x200 && id != 0x113 && id != 0x1 /* mouse move, timer, create */) {
            LONG n = InterlockedIncrement(&seen[id]);
            if (n == 1 || n == 20) { char s[140]; snprintf(s, sizeof s, "msg: %#x wParam %#llx lParam %#llx (x%ld)", id, (unsigned long long)m->wParam, (unsigned long long)m->lParam, (long)n); Log(s); }
        }
    }
    return r;
}

// ---- the game installs a Windows hook: note its type and callback (the keyboard may come through it, not through messages)
typedef HHOOK(WINAPI* SetWindowsHookExAFn)(int, HOOKPROC, HINSTANCE, DWORD);
static SetWindowsHookExAFn g_origSWH;
HOOKPROC g_gameKbdHook;        // low-level keyboard hook procedure of the game, if it installs one (input.cpp calls it)
static HHOOK WINAPI HookSetWindowsHookExA(int id, HOOKPROC proc, HINSTANCE mod, DWORD tid) {
    if (id == WH_KEYBOARD_LL) g_gameKbdHook = proc;
    char m[140]; snprintf(m, sizeof m, "hooks: game installs hook type %d, callback exe+%#llx, thread %lu", id, (unsigned long long)((ULONG_PTR)proc - (ULONG_PTR)GetModuleHandleW(nullptr)), (unsigned long)tid); Log(m);
    return g_origSWH(id, proc, mod, tid);
}

// ---- GetActiveWindow: the game looks this up by name at run time (GetProcAddress) to learn whether its window is active.
// With force_active=1 it is told its window always is, so it neither throttles nor ignores the keyboard in the background.
typedef FARPROC(WINAPI* GetProcAddressFn)(HMODULE, LPCSTR);
typedef HWND(WINAPI* GetActiveWindowFn)();
static GetProcAddressFn g_origGPA; static GetActiveWindowFn g_realGAW;
HWND g_gameWnd;                // set by the overlay from the swap chain
static HWND WINAPI FakeGetActiveWindow() { return g_gameWnd ? g_gameWnd : g_realGAW(); }
static FARPROC WINAPI HookGetProcAddress(HMODULE m, LPCSTR name) {
    FARPROC p = g_origGPA(m, name);
    if (GetCfg().debugMsgs && !IS_INTRESOURCE(name)) { static LONG n; if (InterlockedIncrement(&n) <= 80) { char s[140]; snprintf(s, sizeof s, "gpa: %s", name); Log(s); } }
    if (p && !IS_INTRESOURCE(name) && !strcmp(name, "GetActiveWindow") && GetCfg().forceActive) {
        g_realGAW = (GetActiveWindowFn)p; Log("hooks: GetActiveWindow resolved by the game -> always the game window (force_active)"); return (FARPROC)FakeGetActiveWindow;
    }
    return p;
}

typedef ATOM(WINAPI* RegisterClassExAFn)(const WNDCLASSEXA*);
static RegisterClassExAFn g_origRC;
static ATOM WINAPI HookRegisterClassExA(const WNDCLASSEXA* c) {
    char m[160]; snprintf(m, sizeof m, "hooks: RegisterClassExA(%s) wndproc exe+%#llx", c && c->lpszClassName && !IS_INTRESOURCE(c->lpszClassName) ? c->lpszClassName : "?", (unsigned long long)((ULONG_PTR)(c ? c->lpfnWndProc : 0) - (ULONG_PTR)GetModuleHandleW(nullptr))); Log(m);
    return g_origRC(c);
}

// ---- virtual cursor (force_active=1). The game thinks it is active, so it would lock the REAL mouse cursor into its window
// (SetCursorPos to the centre, ClipCursor to a 2x2 box): the owner could not use the mouse. Instead it gets a virtual cursor:
// those calls do nothing and GetCursorPos answers with the centre of the game window, which is where the game puts it in play.
extern HWND g_gameWnd;
typedef BOOL(WINAPI* ClipCursorFn)(const RECT*); typedef BOOL(WINAPI* SetCursorPosFn)(int, int); typedef BOOL(WINAPI* GetCursorPosFn)(LPPOINT);
typedef HWND(WINAPI* SetCaptureFn)(HWND); typedef BOOL(WINAPI* ReleaseCaptureFn)(); typedef int(WINAPI* ShowCursorFn)(BOOL);
static ClipCursorFn g_oClip; static SetCursorPosFn g_oSetPos; static GetCursorPosFn g_oGetPos; static SetCaptureFn g_oSetCap; static ReleaseCaptureFn g_oRelCap; static ShowCursorFn g_oShow;
// A game whose window is not the foreground one must never take the real mouse: what it tries is counted (a test reads it) and, with run_unfocused, not done (and a cage it set is lifted).
static volatile LONG g_unfocusedClip, g_unfocusedSetPos; extern HWND g_gameWnd;
static bool Unfocused() { return g_gameWnd && GetForegroundWindow() != g_gameWnd; }
void HooksCursorStat(unsigned* clip, unsigned* setpos) { *clip = (unsigned)g_unfocusedClip; *setpos = (unsigned)g_unfocusedSetPos; }
static BOOL WINAPI HookClipCursor(const RECT* r) { if (r && Unfocused()) { InterlockedIncrement(&g_unfocusedClip); if (GetCfg().runUnfocused && !GetCfg().forceActive) return g_oClip(nullptr); }  static LONG n; if (InterlockedIncrement(&n) <= 3) { char m[120]; snprintf(m, sizeof m, "hooks: ClipCursor(%s) force_active=%d", r ? "rect" : "NULL", (int)GetCfg().forceActive); Log(m); } return GetCfg().forceActive ? TRUE : g_oClip(r); }
static BOOL WINAPI HookSetCursorPos(int x, int y) { if (Unfocused()) { InterlockedIncrement(&g_unfocusedSetPos); if (GetCfg().runUnfocused && !GetCfg().forceActive) return TRUE; }  static LONG n; if (InterlockedIncrement(&n) <= 2) { char m[100]; snprintf(m, sizeof m, "hooks: SetCursorPos(%d,%d) force_active=%d", x, y, (int)GetCfg().forceActive); Log(m); } return GetCfg().forceActive ? TRUE : g_oSetPos(x, y); }
static BOOL WINAPI HookGetCursorPos(LPPOINT p) {
    if (GetCfg().forceActive && g_gameWnd && p) { RECT c; GetClientRect(g_gameWnd, &c); POINT q = {c.right / 2, c.bottom / 2}; ClientToScreen(g_gameWnd, &q); *p = q; return TRUE; }
    return g_oGetPos(p);
}
static HWND WINAPI HookSetCapture(HWND w) { return GetCfg().forceActive ? nullptr : g_oSetCap(w); }
static BOOL WINAPI HookReleaseCapture() { return GetCfg().forceActive ? TRUE : g_oRelCap(); }
static int WINAPI HookShowCursor(BOOL s) { return GetCfg().forceActive ? (s ? 0 : -1) : g_oShow(s); }

void InputInstallHook();
void InstallHooks() {   // called from DllMain: memory patching only
    InputInstallHook();
    HookImport("USER32.dll", "ClipCursor", (void*)HookClipCursor, (void**)&g_oClip);
    HookImport("USER32.dll", "SetCursorPos", (void*)HookSetCursorPos, (void**)&g_oSetPos);
    HookImport("USER32.dll", "GetCursorPos", (void*)HookGetCursorPos, (void**)&g_oGetPos);
    HookImport("USER32.dll", "SetCapture", (void*)HookSetCapture, (void**)&g_oSetCap);
    HookImport("USER32.dll", "ReleaseCapture", (void*)HookReleaseCapture, (void**)&g_oRelCap);
    HookImport("USER32.dll", "ShowCursor", (void*)HookShowCursor, (void**)&g_oShow);
    HookImport("USER32.dll", "RegisterClassExA", (void*)HookRegisterClassExA, (void**)&g_origRC);
    HookImport("KERNEL32.dll", "GetProcAddress", (void*)HookGetProcAddress, (void**)&g_origGPA);
    HookImport("USER32.dll", "SetWindowsHookExA", (void*)HookSetWindowsHookExA, (void**)&g_origSWH);
    HookImport("USER32.dll", "PeekMessageA", (void*)HookPeekMessageA, (void**)&g_origPeek);
    HookImport("KERNEL32.dll", "Sleep", (void*)HookSleep, (void**)&g_origSleep);
    HookImport("SHELL32.dll", "SHGetFolderPathW", (void*)HookSHGetFolderPathW, (void**)&g_origSH);
    HookImport("KERNEL32.dll", "CreateFileW", (void*)HookCreateFileW, (void**)&g_origCFW);
    HookImport("KERNEL32.dll", "CreateFileA", (void*)HookCreateFileA, (void**)&g_origCFA);
}
