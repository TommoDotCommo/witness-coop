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

static bool HookImport(const char* dll, const char* fn, void* hook, void** orig) {
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

void InstallHooks() {   // called from DllMain: memory patching only
    HookImport("SHELL32.dll", "SHGetFolderPathW", (void*)HookSHGetFolderPathW, (void**)&g_origSH);
    HookImport("KERNEL32.dll", "CreateFileW", (void*)HookCreateFileW, (void**)&g_origCFW);
    HookImport("KERNEL32.dll", "CreateFileA", (void*)HookCreateFileA, (void**)&g_origCFA);
}
