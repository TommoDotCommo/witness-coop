// Save points and presets (see savepoints.h). The game saves its campaign when the pause menu opens and keeps one file per session, so a
// save point is: open the pause menu, copy the newest save file out, close the menu. A restore copies a file back in as the newest save
// and loads it through "Load a Game". Everything is driven one step per frame from the render thread.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "savepoints.h"
#include "input.h"
#include "config.h"

void Log(const char* msg);
extern HMODULE g_selfModule();

namespace {
enum Op { KeyTap, Wait, CopyOut, CopyIn, ApplyState };
struct Step { Op op; unsigned vk, sc; DWORD ms; };
std::vector<SaveEntry> g_list; std::vector<Step> g_steps; size_t g_i; DWORD g_t0; int g_phase; bool g_busy;
std::string (*g_write)(bool); void (*g_apply)(const std::string&); std::wstring g_stateIn;
std::wstring g_outName, g_inPath; bool g_outPreset; std::string g_outNote; char g_status[200] = "";
void Status(const char* fmt, ...) { va_list ap; va_start(ap, fmt); vsnprintf(g_status, sizeof g_status, fmt, ap); va_end(ap); Log(g_status); }

std::wstring DllDir() { wchar_t p[MAX_PATH]; GetModuleFileNameW(g_selfModule(), p, MAX_PATH); wchar_t* s = wcsrchr(p, L'\\'); if (s) *(s + 1) = 0; return p; }
std::wstring SaveDir() {
    const Cfg& c = GetCfg(); if (!c.saveDir.empty()) return c.saveDir + L"\\The Witness";
    wchar_t p[MAX_PATH]; if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, p))) return std::wstring(p) + L"\\The Witness"; return L"";
}
std::wstring W(const std::string& s) { wchar_t w[300]; MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w, 300); return w; }
std::string U(const std::wstring& s) { char b[600]; WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, b, 600, nullptr, nullptr); return b; }
unsigned long long Ft(const FILETIME& f) { return ((unsigned long long)f.dwHighDateTime << 32) | f.dwLowDateTime; }

bool Newest(const std::wstring& dir, const wchar_t* pat, std::wstring* path) {
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\" + pat).c_str(), &fd); if (h == INVALID_HANDLE_VALUE) return false;
    unsigned long long best = 0; bool any = false;
    do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && Ft(fd.ftLastWriteTime) >= best) { best = Ft(fd.ftLastWriteTime); *path = dir + L"\\" + fd.cFileName; any = true; } } while (FindNextFileW(h, &fd));
    FindClose(h); return any;
}
std::wstring Swap(const std::wstring& p, const wchar_t* ext) { size_t d = p.rfind(L'.'); return (d == std::wstring::npos ? p : p.substr(0, d)) + ext; }

void DoCopyOut() {
    std::wstring src; std::wstring dir = DllDir() + (g_outPreset ? L"presets" : L"savepoints");
    if (!Newest(SaveDir(), L"*.witness_campaign", &src)) { Status("save point failed: no save file in the game's save folder"); return; }
    CreateDirectoryW(dir.c_str(), nullptr); std::wstring dst = dir + L"\\" + g_outName + L".witness_campaign";
    if (!CopyFileW(src.c_str(), dst.c_str(), FALSE)) { Status("save point failed: could not copy the save file"); return; }
    CopyFileW(Swap(src, L".png").c_str(), Swap(dst, L".png").c_str(), FALSE);
    FILE* f = _wfopen(Swap(dst, L".txt").c_str(), L"w"); if (f) { fputs(g_outNote.c_str(), f); fclose(f); }
    if (g_write) { FILE* sf = _wfopen(Swap(dst, L".state").c_str(), L"w"); if (sf) { fputs(g_write(true).c_str(), sf); fclose(sf); } }
    Status("saved %s '%s'", g_outPreset ? "preset" : "save point", U(g_outName).c_str());
}
void DoCopyIn() {
    if (GetCfg().noRestoreCopy) { Status("restore skipped (no_restore_copy)"); return; }
    std::wstring dir = SaveDir(), newest;
    // The load screen lists saves newest first by the time stored inside each save, and the game has just written a save of the running
    // world, which is therefore the top entry. Ours goes second, so every other file but that one is removed first (only in a redirected
    // instance save folder: a real one is never touched).
    if (!GetCfg().saveDir.empty() && Newest(dir, L"*.witness_campaign", &newest)) {
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*.witness_campaign").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) { do { std::wstring p = dir + L"\\" + fd.cFileName; if (p != newest) { DeleteFileW(p.c_str()); DeleteFileW(Swap(p, L".png").c_str()); } } while (FindNextFileW(h, &fd)); FindClose(h); }
    }
    SYSTEMTIME st; GetLocalTime(&st); wchar_t stem[80]; swprintf(stem, 80, L"%04d.%02d.%02d__time_%02d.%02d.%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    std::wstring dst = dir + L"\\" + stem + L".witness_campaign";
    if (!CopyFileW(g_inPath.c_str(), dst.c_str(), FALSE)) { Status("restore failed: could not copy the save file in"); return; }
    CopyFileW(Swap(g_inPath, L".png").c_str(), Swap(dst, L".png").c_str(), FALSE);
    Status("restoring: loading the save through the pause menu");
}
const Step kEsc = {KeyTap, 0x1B, 0x01, 0}, kDown = {KeyTap, 0x53, 0x1F, 0}, kEnter = {KeyTap, 0x0D, 0x1C, 0};
}  // namespace

std::wstring SaveFolder() { return SaveDir(); }
void SaveSetHooks(std::string (*w)(bool), void (*a)(const std::string&)) { g_write = w; g_apply = a; }
const std::vector<SaveEntry>& SaveList() { return g_list; }
bool SaveBusy() { return g_busy; }
const char* SaveStatus() { return g_status; }

void SaveListRefresh() {
    g_list.clear();
    for (int pass = 0; pass < 2; pass++) {
        std::wstring dir = DllDir() + (pass ? L"presets" : L"savepoints"); std::vector<std::wstring> names;
        for (const wchar_t* pat : {L"*.witness_campaign", L"*.state"}) {
            WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\" + pat).c_str(), &fd); if (h == INVALID_HANDLE_VALUE) continue;
            do { std::wstring n = fd.cFileName; n = Swap(n, L""); if (std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n); } while (FindNextFileW(h, &fd)); FindClose(h);
        }
        for (auto& n : names) {
            SaveEntry e; e.name = U(n); e.preset = pass == 1; WIN32_FILE_ATTRIBUTE_DATA ad = {}; e.hasGameSave = GetFileAttributesExW((dir + L"\\" + n + L".witness_campaign").c_str(), GetFileExInfoStandard, &ad) != 0;
            if (!e.hasGameSave) GetFileAttributesExW((dir + L"\\" + n + L".state").c_str(), GetFileExInfoStandard, &ad); e.when = Ft(ad.ftLastWriteTime);
            FILE* f = _wfopen((dir + L"\\" + n + L".txt").c_str(), L"r"); if (f) { char b[200] = {}; size_t k = fread(b, 1, sizeof b - 1, f); b[k] = 0; fclose(f); e.note = b; }
            g_list.push_back(e);
        }
    }
    std::sort(g_list.begin(), g_list.end(), [](const SaveEntry& a, const SaveEntry& b) { return a.preset != b.preset ? a.preset : a.when > b.when; });
}

void SaveStartSave(const std::string& name, bool asPreset, const std::string& note) {
    if (g_busy) return; g_outName = W(name); g_outPreset = asPreset; g_outNote = note;
    g_steps = {kEsc, {Wait, 0, 0, 2000}, {CopyOut, 0, 0, 0}, kEsc, {Wait, 0, 0, 600}}; g_i = 0; g_phase = 0; g_busy = true; Status("saving '%s': opening the pause menu so the game writes its save", name.c_str());
}
void SaveStartPreset(const std::string& name, const std::string& note) {
    std::wstring dir = DllDir() + L"presets"; CreateDirectoryW(dir.c_str(), nullptr); std::wstring base = dir + L"\\" + W(name);
    FILE* sf = _wfopen((base + L".state").c_str(), L"w"); if (sf && g_write) { fputs(g_write(false).c_str(), sf); } if (sf) fclose(sf);
    FILE* f = _wfopen((base + L".txt").c_str(), L"w"); if (f) { fputs(note.c_str(), f); fclose(f); }
    Status("saved list-only preset '%s'", name.c_str()); SaveListRefresh();
}
void SaveStartRestore(const SaveEntry& e) {
    if (g_busy) return; std::wstring dir = DllDir() + (e.preset ? L"presets\\" : L"savepoints\\"); g_inPath = dir + W(e.name) + L".witness_campaign"; g_stateIn = dir + W(e.name) + L".state";
    if (e.hasGameSave) g_steps = {kEsc, {Wait, 0, 0, 1500}, {CopyIn, 0, 0, 0}, kDown, kDown, kEnter, {Wait, 0, 0, 1500}, kDown, kEnter, {Wait, 0, 0, 12000}, {ApplyState, 0, 0, 0}, {Wait, 0, 0, 1000}};
    else g_steps = {{ApplyState, 0, 0, 0}, {Wait, 0, 0, 500}};
    g_i = 0; g_phase = 0; g_busy = true; Status("restoring '%s'%s", e.name.c_str(), e.hasGameSave ? ": opening the pause menu" : " (progress list only)");
}

void SaveTick(unsigned long now) {
    if (!g_busy) return;
    if (g_i >= g_steps.size()) { g_busy = false; SaveListRefresh(); return; }
    Step& s = g_steps[g_i];
    switch (s.op) {
    case KeyTap:      // down now, up 150 ms later, then a pause so the game's menu can react
        if (g_phase == 0) { InputKey(s.vk, s.sc, true); g_t0 = now; g_phase = 1; }
        else if (g_phase == 1 && now - g_t0 >= 150) { InputKey(s.vk, s.sc, false); g_phase = 2; }
        else if (g_phase == 2 && now - g_t0 >= 600) { g_phase = 0; g_i++; }
        break;
    case Wait: if (g_phase == 0) { g_t0 = now; g_phase = 1; } else if (now - g_t0 >= s.ms) { g_phase = 0; g_i++; } break;
    case CopyOut: DoCopyOut(); g_i++; g_phase = 0; break;
    case CopyIn: DoCopyIn(); g_i++; g_phase = 0; break;
    case ApplyState: { FILE* f = _wfopen(g_stateIn.c_str(), L"r"); std::string t; if (f) { char b[4096]; size_t k; while ((k = fread(b, 1, sizeof b, f)) > 0) t.append(b, k); fclose(f); } if (g_apply && !t.empty()) g_apply(t); else Status("nothing to apply from the progress list"); g_i++; g_phase = 0; break; }
    }
}
