#include "../common/guide.h"
// The Witness co-op launcher, in the shape every co-op mod shares (co-op standard A, D, E, F, K): four tabs, Play (your name, Host a game / Join a friend / Play solo, one
// primary button; hosting or joining turns it into the lobby), Saves (the co-op saves and a new game with parts of the map done), Settings (sections in the standard order,
// saved on change) and About (version, updates, quick guide, diagnostics). It works with the mouse, the keyboard (Enter, Esc, Tab, Ctrl+Tab, hold F1) and a pad (XInput), and
// the window is resizable (1280x800 on a Steam Deck; it fills the screen in Game Mode). What it shows is in shell.h (tested without a window), the files in core.cpp.
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <uxtheme.h>
#include "core.h"
#include "../common/randomizers.h"
#include "shell.h"
#include "../common/handover.h"
#include "../proxy/core/appearance.h"
#include "theme.h"
#include "lobby.h"
#include "lobby_win.h"
#include "joinglue.h"
#include "netfind.h"
#include "../common/steam_ids.h"
#include "../proxy/progress/progress.h"
#include "../common/diag_log.h"
#include "../common/crash_handler.h"
#include "version.h"
#include "update_win.h"
#include "../common/userdata.h"
#include "editor.h"
#include "diag_ps1.h"
#include "notices_txt.h"      // dist/THIRD-PARTY-NOTICES.txt
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "../proxy/third_party/stb_image.h"

namespace {
std::wstring W(const std::string& s) { if (s.empty()) return L""; int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0); std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n); w.resize(n - 1); return w; }
std::string U(const std::wstring& w) { if (w.empty()) return ""; int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr); std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr); s.resize(n - 1); return s; }
std::string ReadFile8(const std::wstring& p) { std::string t; FILE* f = _wfopen(p.c_str(), L"rb"); if (f) { char b[4096]; size_t k; while ((k = fread(b, 1, sizeof b, f)) > 0) t.append(b, k); fclose(f); } return t; }
bool WriteFile8(const std::wstring& p, const std::string& t) { FILE* f = _wfopen(p.c_str(), L"wb"); if (!f) return false; fwrite(t.data(), 1, t.size(), f); fclose(f); return true; }
std::wstring Dir(const std::wstring& p) { size_t s = p.find_last_of(L"\\/"); return s == std::wstring::npos ? L"" : p.substr(0, s); }
bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

enum { ID_TAB = 100, ID_APPLY, ID_LAUNCH, ID_NEWGAME, ID_BROWSE, ID_GAMEDIR, ID_STATUS, ID_REFRESH, ID_OPENSAVES, ID_BACKUP, ID_TRASH, ID_SAVELIST, ID_SAVEINFO, ID_SAVEPICK, ID_DIAG, ID_UP_VER, ID_UP_STATUS, ID_UP_NOTES, ID_UP_CHECK, ID_UP_APPLY, ID_UP_LATER, ID_UP_AUTO, ID_UP_PRE, ID_UP_BANNER, ID_UP_BANNER_APPLY, ID_UP_BANNER_LATER,
       ID_SAVEPICK2, ID_PRIMARY, ID_NAME, ID_TILE_HOST, ID_TILE_JOIN, ID_TILE_SOLO, ID_TILE_HOST_TXT, ID_TILE_JOIN_TXT, ID_TILE_SOLO_TXT, ID_HOST_STEAM, ID_HOST_DIRECT, ID_JOIN_LABEL, ID_SOLO_NOTE, ID_SOLO_NEW, ID_UP_WHATSNEW, ID_HINTS, ID_FOOT_ACTION,
       ID_SECTIONS, ID_RESET, ID_GUIDE_BTN, ID_LINK_HOME, ID_LINK_NOTES, ID_CREDITS, ID_FOLDER_LABEL, ID_ABOUT_TITLE, ID_ABOUT_GUIDE_TXT, ID_ABOUT_DIAG_TXT, ID_SECTION_TITLE, ID_PICK_LABEL, ID_PICK_LABEL2, ID_RESET2, ID_SAVEPIC, ID_UP_REL, ID_UP_WHICH,
       ID_NG_OK = 300, ID_NG_LASERS, ID_NG_PRESET, ID_NG_INFO, ID_NG_AREAS = 400, ID_FIELD = 1000,
       // the Randomizers page (common/randomizers.h, proxy/trainer.cpp): the randomizer (a list), what it is and who made it, its options; the Witness Trainer; the credits
       ID_RZ_TITLE = 800, ID_RZ_INTRO, ID_RZ_MODE, ID_RZ_BLURB, ID_RZ_WARN, ID_RZ_BY, ID_RZ_SEED_LABEL, ID_RZ_SEED, ID_RZ_NEWSEED, ID_RZ_ROLL, ID_RZ_SHUFFLE, ID_RZ_COLOR, ID_RZ_FAST,
       ID_RZ_STATUS, ID_RZ_H_TRAINER, ID_RZ_TR_SPEED_LABEL, ID_RZ_TR_SPEED, ID_RZ_TR_FOV_LABEL, ID_RZ_TR_FOV, ID_RZ_TR_NOCLIP, ID_RZ_TR_CHALLENGE, ID_RZ_TR_EPS, ID_RZ_TR_NOTE,
       ID_RZ_H_CREDITS, ID_RZ_CREDITS, ID_RZ_LINK_MODE, ID_RZ_LINK_TRAINER, ID_RZ_SET_LABEL, ID_RZ_SET, ID_RZ_EDIT, ID_RZ_LICENCE };
static_assert(ID_UP_WHICH < ID_NG_OK, "control ids run into the New game dialog's");
static_assert(ID_RZ_LICENCE < ID_FIELD, "the Randomizers page's ids run into the settings fields");
enum Kind { kCheck, kCombo, kEdit, kMulti, kSlider, kKeyBtn, kNoteK };
struct Field { int id; ls::Section section; std::string label, key; Kind kind; std::vector<std::string> choices; std::string def, hint; const ls::Row* row; HWND h = nullptr, hLabel = nullptr, hHint = nullptr, hValue = nullptr; };
guide::Hold g_guideHold;
static unsigned g_guideShows, g_guidePaints;      // tests: how often the guide window was shown or moved, and painted (a held F1 must show it once and paint it once)
HWND g_guide;
std::vector<Field> g_fields; HWND g_main, g_tab, g_pages[ls::kTabCount], g_status, g_hints; int g_npages = ls::kTabCount;
HWND g_tiles, g_lobbyPanel, g_secList, g_secPanel[ls::kSectionCount]; int g_section;
std::map<int, HWND> g_ctl;                // every control by its id (the pages nest: Play holds the tiles and the lobby, Settings one panel per section)
std::wstring g_dir;                       // the game folder
std::wstring g_data;                      // where the mod's settings and run-time files live: %LOCALAPPDATA%\witness-coop, or the game folder for a per-instance cfg (common/userdata.h)
bool g_noActivate;                        // --no-activate (tests): the game's window is shown without taking the focus
bool g_loading, g_dirty, g_padLast;       // values being put into the controls (no saving) / a change made while the lobby's game runs (saved when it ends) / the last input came from a pad
int g_capture = -1; std::wstring g_captureOld;      // the key field waiting for a key press
ls::Choice g_choice = ls::kHost; ls::PadNav g_padNav;
std::string g_steamAtStart = "-";      // the Steam API when Steam started the launcher: "-" not tried, "ok", or why not

std::wstring CfgPath() { return g_data + L"\\witness_coop.cfg"; }
std::wstring SaveFolder(const wl::CfgFile& c) {      // where the game keeps its saves, as the mod redirects them (save_dir=instance|<path>)
    std::string sd = c.Get("save_dir");
    if (sd == "instance") return g_dir + L"\\appdata\\The Witness";
    if (!sd.empty()) return W(sd) + L"\\The Witness";
    wchar_t p[MAX_PATH]; if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, p))) return std::wstring(p) + L"\\The Witness"; return L"";
}
wl::CfgFile LoadCfg() { return wl::CfgFile::Parse(ReadFile8(CfgPath())); }
diag::Session g_log; bool g_logOpen;      // this run's own log (common/diag_log.h), in the same folder as the game's
void Dlog(const char* text) { static CRITICAL_SECTION cs; static LONG made; if (InterlockedCompareExchange(&made, 1, 0) == 0) InitializeCriticalSection(&cs); EnterCriticalSection(&cs); if (g_logOpen) g_log.Line(text); LeaveCriticalSection(&cs); }      // (netfind.c's workers log too)
int g_footAction;                          // the command the footer's next-step button sends (F4), 0: hidden
void LayoutAll(); RECT OuterFor(int w, int h);
std::wstring g_fleeting; ULONGLONG g_fleetingUntil;      // a status that only reports a moment ("You left the lobby."): it clears itself after a few seconds (it stayed for good: owner, 2026-11-06)
void Status(const std::wstring& s, const wchar_t* action = nullptr, int cmd = 0) {
    g_fleetingUntil = 0; SetWindowTextW(g_status, s.c_str()); Dlog(("status: " + U(s)).c_str());
    HWND b = g_ctl.count(ID_FOOT_ACTION) ? g_ctl[ID_FOOT_ACTION] : nullptr; g_footAction = action ? cmd : 0;
    if (b) { if (action) SetWindowTextW(b, action); ShowWindow(b, action ? SW_SHOW : SW_HIDE); }
}
void LobbyPartsChosen(const std::string& spec);
std::string RzForLaunch(bool newGame); void RzRefresh(); void RzLoad(); void RzCommand(int id, int code); void RzSetFromCommand(const std::string& arg); std::string RzStat(); uint32_t FreshSeed();
extern uint32_t g_rzRound;      // the Randomizers page (below): the spec a game starts with, the page's state, this round's seed
HWND C(int id); void LoadPlayUi(const wl::CfgFile& c); void LoadSaveOptions(int pick);

void AddFields() {
    int id = ID_FIELD; wl::CfgFile cfg = LoadCfg();
    for (const auto& r : ls::Rows()) {
        if (r.hideAtDefault && (r.key.rfind("cfg:", 0) != 0 || cfg.Get(r.key.substr(4).c_str(), r.def.c_str()) == r.def)) continue;      // (a setting left at its default is not offered; the cfg key still works)
        Kind k = r.kind == ls::kToggle ? kCheck : r.kind == ls::kChoice ? kCombo : r.kind == ls::kSlider ? kSlider : r.kind == ls::kKey ? kKeyBtn : r.kind == ls::kPeers ? kMulti : r.kind == ls::kNote ? kNoteK : kEdit;
        std::string label = r.label + (r.hostDecides ? "  (host decides)" : "");
        g_fields.push_back({id++, r.section, label, r.key, k, r.choices, r.def, r.hint, &r});
        if (r.key == "cfg:voice_channel" && cfg.Get("experimental_radio", "0") != "1" && !g_fields.back().choices.empty()) g_fields.back().choices.pop_back();      // (no Radio entry without the handheld radio)
    }
}
Field* ByKey(const std::string& k) { for (auto& f : g_fields) if (f.key == k) return &f; return nullptr; }
Field* ByHwnd(HWND h) { for (auto& f : g_fields) if (f.h == h) return &f; return nullptr; }
Field* ById(int id) { for (auto& f : g_fields) if (f.id == id) return &f; return nullptr; }
bool IndexValue(const Field& f) { return f.key.rfind("vid:", 0) == 0 || f.key.rfind("cfg:appearance_", 0) == 0; }
void SliderText(Field& f) { if (f.hValue) SetWindowTextW(f.hValue, W(ls::ValueText(*f.row, ls::SliderValue(*f.row, (int)SendMessageW(f.h, TBM_GETPOS, 0, 0)))).c_str()); }
std::string GetVal(Field& f) {
    wchar_t b[2000] = {};
    if (f.kind == kNoteK) return "";
    if (f.kind == kCheck) return SendMessageW(f.h, BM_GETCHECK, 0, 0) == BST_CHECKED ? "1" : "0";
    if (f.kind == kCombo) { int i = (int)SendMessageW(f.h, CB_GETCURSEL, 0, 0); return i < 0 ? (IndexValue(f) ? "-1" : "") : (IndexValue(f) ? std::to_string(i) : (f.row && (size_t)i < f.row->values.size() ? f.row->values[i] : f.choices[i])); }
    if (f.kind == kSlider) { char t[40]; snprintf(t, sizeof t, "%g", ls::SliderValue(*f.row, (int)SendMessageW(f.h, TBM_GETPOS, 0, 0))); return t; }
    GetWindowTextW(f.h, b, 2000); return U(b);
}
void SetVal(Field& f, const std::string& v) {
    if (f.kind == kNoteK) return;
    if (f.kind == kCheck) SendMessageW(f.h, BM_SETCHECK, (v == "1" || v == "true") ? BST_CHECKED : BST_UNCHECKED, 0);
    else if (f.kind == kCombo) { int i = -1; if (f.key.rfind("cfg:appearance_", 0) == 0) i = v.size() == 1 && v[0] >= '0' && v[0] <= '3' ? v[0] - '0' : 0; else if (f.key.rfind("vid:", 0) == 0) i = atoi(v.c_str()); else for (size_t k = 0; k < f.choices.size(); ++k) if ((f.row && k < f.row->values.size() ? f.row->values[k] : f.choices[k]) == v) i = (int)k;
        if (f.key == "vid:customSize" && i < 0) { SendMessageW(f.h, CB_SETCURSEL, (WPARAM)-1, 0); return; }      // (a size that is not in the list stays as it is)
        if ((i < 0 || size_t(i) >= f.choices.size()) && !f.choices.empty()) i = 0; SendMessageW(f.h, CB_SETCURSEL, i, 0); }
    else if (f.kind == kSlider) { double d = v.empty() ? atof(f.def.c_str()) : atof(v.c_str()); SendMessageW(f.h, TBM_SETPOS, TRUE, ls::SliderPos(*f.row, d)); SliderText(f); }
    else SetWindowTextW(f.h, W(v).c_str());
}
void FillPeers(Field& f, const wl::CfgFile& c) { std::string t; for (const auto& p : c.GetAll("peer")) t += p + "\r\n"; SetWindowTextW(f.h, W(t).c_str()); }

// ---- the saves page
std::vector<wl::SaveInfo> g_saves; HWND g_saveList, g_saveInfo, g_savePick, g_savePick2;
HWND g_savePic; std::vector<uint8_t> g_picRgba; int g_picW, g_picH, g_savesW, g_savesH; std::wstring g_picPath;
void LayoutSaves(int W, int H); void FillThumbs();
std::wstring Stamp() { SYSTEMTIME st; GetLocalTime(&st); wchar_t b[40]; swprintf(b, 40, L"%04d-%02d-%02d_%02d.%02d.%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond); return b; }
std::wstring When(unsigned long long ft) { FILETIME f; f.dwLowDateTime = (DWORD)ft; f.dwHighDateTime = (DWORD)(ft >> 32); FILETIME l; SYSTEMTIME st; FileTimeToLocalFileTime(&f, &l); FileTimeToSystemTime(&l, &st); wchar_t b[40]; swprintf(b, 40, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute); return b; }
void RefreshSaves() {
    wl::CfgFile c = LoadCfg(); g_saves = wl::ListSaves(SaveFolder(c)); ListView_DeleteAllItems(g_saveList); FillThumbs(); if (!g_picRgba.empty()) { g_picRgba.clear(); g_picPath.clear(); if (g_savesW) LayoutSaves(g_savesW, g_savesH); } SendMessageW(g_savePick, CB_RESETCONTENT, 0, 0); SendMessageW(g_savePick2, CB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < g_saves.size(); ++i) {
        const auto& s = g_saves[i]; LVITEMW it = {}; it.mask = LVIF_TEXT | LVIF_IMAGE; it.iImage = (int)i; it.iItem = (int)i; std::wstring w = When(s.when); it.pszText = &w[0]; ListView_InsertItem(g_saveList, &it);
        std::wstring who, prog, las, tm; if (s.hasMeta) { for (size_t k = 0; k < s.meta.players.size(); ++k) who += (k ? L", " : L"") + W(s.meta.players[k].name); wchar_t b[80]; swprintf(b, 80, L"%u%%  (%u of %u)", s.meta.total ? s.meta.solved * 100 / s.meta.total : 0, s.meta.solved, s.meta.total); prog = b; swprintf(b, 80, L"%u of %u", s.meta.lasers, s.meta.lasersTotal); las = b; swprintf(b, 80, L"%u h %02u min", s.meta.totalSeconds / 3600, (s.meta.totalSeconds % 3600) / 60); tm = b; } else who = L"(no co-op record)";
        ListView_SetItemText(g_saveList, (int)i, 1, &who[0]); ListView_SetItemText(g_saveList, (int)i, 2, &prog[0]); ListView_SetItemText(g_saveList, (int)i, 3, &las[0]); ListView_SetItemText(g_saveList, (int)i, 4, &tm[0]);
        SendMessageW(g_savePick, CB_ADDSTRING, 0, (LPARAM)When(s.when).c_str()); SendMessageW(g_savePick2, CB_ADDSTRING, 0, (LPARAM)When(s.when).c_str());
    }
    SendMessageW(g_savePick, CB_SETCURSEL, 0, 0); SendMessageW(g_savePick2, CB_SETCURSEL, 0, 0);
}
// A picture file decoded to RGBA (the game's save pictures are PNGs); false: missing, not a picture, or larger than 8192 px.
bool DecodePicture(const std::wstring& png, std::vector<uint8_t>* rgba, int* pw, int* ph) {
    rgba->clear(); *pw = *ph = 0; FILE* f = _wfopen(png.c_str(), L"rb"); if (!f) return false;
    std::vector<uint8_t> raw; uint8_t b[65536]; size_t k; while ((k = fread(b, 1, sizeof b, f)) > 0 && raw.size() < (32u << 20)) raw.insert(raw.end(), b, b + k); fclose(f);
    int w = 0, h = 0, n = 0; uint8_t* px = raw.empty() ? nullptr : stbi_load_from_memory(raw.data(), (int)raw.size(), &w, &h, &n, 4); if (!px) return false;
    if (w > 0 && h > 0 && w <= 8192 && h <= 8192) { rgba->assign(px, px + (size_t)w * h * 4); *pw = w; *ph = h; }
    stbi_image_free(px); return !rgba->empty();
}
// The save's own picture (the .png the game writes beside each save), beside its details on the Saves page; none: the details take the whole width as before.
bool LoadSavePic(const std::wstring& png) {
    g_picPath.clear(); if (!DecodePicture(png, &g_picRgba, &g_picW, &g_picH)) return false; g_picPath = png; return true;
}
// Each save's thumbnail in the Saves list (its picture cropped to fill, or the drawn panel), in the list's small image list.
HIMAGELIST g_thumbs;
void FillThumbs() {
    const int tw = 80, tht = 45; HIMAGELIST il = ImageList_Create(tw, tht, ILC_COLOR32, (int)g_saves.size() + 1, 4); std::vector<uint8_t> px; int w = 0, h = 0;
    for (const auto& sv : g_saves) { bool ok = sv.hasPng && DecodePicture(wl::PicturePath(sv), &px, &w, &h); HBITMAP b = th::Thumb(ok ? px.data() : nullptr, w, h, tw, tht); ImageList_Add(il, b, nullptr); DeleteObject(b); }
    HIMAGELIST old = ListView_SetImageList(g_saveList, il, LVSIL_SMALL); if (old && old != il) ImageList_Destroy(old); g_thumbs = il;
}
// The picture scaled (halftone) into the box, keeping its shape; the control is placed on the scaled picture.
void ApplySavePic(int x, int y, int bw, int bh) {
    if (!g_savePic) return;
    if (g_picRgba.empty() || bw < 16 || bh < 16) { ShowWindow(g_savePic, SW_HIDE); return; }
    double k = std::min((double)bw / g_picW, (double)bh / g_picH); int w = std::max(1, (int)(g_picW * k)), h = std::max(1, (int)(g_picH * k));
    std::vector<uint8_t> bgra(g_picRgba.size()); for (size_t i = 0; i < bgra.size(); i += 4) { bgra[i] = g_picRgba[i + 2]; bgra[i + 1] = g_picRgba[i + 1]; bgra[i + 2] = g_picRgba[i]; bgra[i + 3] = 255; }
    BITMAPINFO src = {}; src.bmiHeader = {sizeof(BITMAPINFOHEADER), g_picW, -g_picH, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
    HDC screen = GetDC(nullptr); HDC mem = CreateCompatibleDC(screen); HBITMAP bmp = CreateCompatibleBitmap(screen, w, h); HGDIOBJ o = SelectObject(mem, bmp);
    SetStretchBltMode(mem, HALFTONE); SetBrushOrgEx(mem, 0, 0, nullptr); StretchDIBits(mem, 0, 0, w, h, 0, 0, g_picW, g_picH, bgra.data(), &src, DIB_RGB_COLORS, SRCCOPY);
    SelectObject(mem, o); DeleteDC(mem); ReleaseDC(nullptr, screen);
    HBITMAP old = (HBITMAP)SendMessageW(g_savePic, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)bmp); if (old && old != bmp) DeleteObject(old);
    SetWindowPos(g_savePic, nullptr, x + (bw - w) / 2, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE); ShowWindow(g_savePic, SW_SHOW);
}
void ShowSaveInfo(int i) {
    std::wstring t; bool hadPic = !g_picRgba.empty();
    if (i >= 0 && i < (int)g_saves.size() && g_saves[i].hasPng) { std::wstring png = g_saves[i].path.substr(0, g_saves[i].path.rfind(L'.')) + L".png"; if (png != g_picPath) LoadSavePic(png); } else { g_picRgba.clear(); g_picPath.clear(); }
    if ((hadPic || !g_picRgba.empty()) && g_savesW) LayoutSaves(g_savesW, g_savesH);
    if (i < 0 || i >= (int)g_saves.size()) { SetWindowTextW(g_saveInfo, L""); return; }
    const auto& s = g_saves[i]; t = L"File: " + W(s.stem) + L".witness_campaign (" + std::to_wstring(s.bytes / 1024) + L" KB)\r\n";
    if (!s.hasMeta) t += L"The game wrote this save without the mod running, so there is no record of who played or how far.\r\n";
    else {
        const auto& m = s.meta;
        { wrand::Spec r = wrand::ParseSpec(m.randomizer); t += L"Puzzles: " + (r.on() ? W(wrand::SpecSummary(r)) + L" (continuing this save keeps them)" : std::wstring(L"the game's own")) + L"\r\n"; }
        t += L"Saved " + W(m.savedAt) + L";  playing time " + std::to_wstring(m.totalSeconds / 3600) + L" h " + std::to_wstring((m.totalSeconds % 3600) / 60) + L" min\r\n";
        t += L"Panels solved: " + std::to_wstring(m.solved) + L" of " + std::to_wstring(m.total) + L";  lasers lit: " + std::to_wstring(m.lasers) + L" of " + std::to_wstring(m.lasersTotal) + L";  doors open: " + std::to_wstring(m.doorsOpen) + L"\r\n\r\nPlayers:\r\n";
        for (const auto& p : m.players) t += L"  " + W(p.name) + (p.character.empty() ? L"" : L"  as " + W(p.character)) + L"   " + std::to_wstring(p.seconds / 3600) + L" h " + std::to_wstring((p.seconds % 3600) / 60) + L" min in " + std::to_wstring(p.sessions) + L" session(s)\r\n";
        t += L"\r\nProgress by area:\r\n"; for (const auto& a : m.areas) if (a.solved) t += L"  " + W(a.name) + L":  " + std::to_wstring(a.solved) + L" of " + std::to_wstring(a.total) + L"\r\n";
    }
    SetWindowTextW(g_saveInfo, t.c_str());
}
// a save's files (campaign, picture, co-op record) copied or moved into the launcher-backup folder next to the saves, in a folder named by the time
bool BackupSave(const wl::SaveInfo& s, bool move, const std::wstring& stampDir) {
    std::wstring dir = Dir(s.path); CreateDirectoryW((dir + L"\\launcher-backup").c_str(), nullptr); CreateDirectoryW(stampDir.c_str(), nullptr); bool ok = true;
    for (const wchar_t* ext : {L".witness_campaign", L".png", L".coop.txt"}) {
        std::wstring from = dir + L"\\" + W(s.stem) + ext, to = stampDir + L"\\" + W(s.stem) + ext; if (!Exists(from)) continue;
        if (move) ok = ok && MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING); else ok = ok && CopyFileW(from.c_str(), to.c_str(), FALSE);
    }
    return ok;
}

// ---- load and apply
Cfg GuideSettings(bool migrate) {
    Cfg keys;
    for (const auto& b : guide::bindings) {
        auto* f = ByKey(std::string("cfg:") + b.config);
        if (f) keys.*b.member = GetVal(*f).empty() ? b.initial : guide::MigrateDefault(b.config, guide::ParseKey(GetVal(*f)));      // (a key still on its old default shows the new default)
    }
    if (migrate) guide::MigrateF1(keys);
    if (auto* f = ByKey("cfg:experimental_tv")) keys.experimentalTv = GetVal(*f) == "1";      // (the guide lists the TV and radio keys only while their option is on)
    if (auto* f = ByKey("cfg:experimental_radio")) keys.experimentalRadio = GetVal(*f) == "1";
    if (auto* f = ByKey("cfg:pad_voice")) { std::string v = GetVal(*f); keys.padVoice = v == "ls" ? 1 : v == "rs" ? 2 : 0; }      // (the guide's controller lines)
    if (auto* f = ByKey("cfg:voice_mode")) { std::string v = GetVal(*f); keys.voiceMode = v == "off" ? 0 : v == "open" ? 1 : v == "ptt" ? 2 : 3; }
    return keys;
}
void MigrateGuideKeys() {
    Cfg keys = GuideSettings(true);
    for (const auto& b : guide::bindings) if (auto* f = ByKey(std::string("cfg:") + b.config))
        if (guide::ParseKey(GetVal(*f)) == guide::Help) SetVal(*f, guide::KeyName(keys.*b.member));
}
void LoadSaveOptions(int pick) {      // the game's own options, from the save chosen in "Save to change" (the newest first)
    std::vector<uint8_t> data; if (pick >= 0 && pick < (int)g_saves.size()) { std::string t = ReadFile8(g_saves[pick].path); data.assign(t.begin(), t.end()); }
    auto opts = wl::ReadSaveOptions(data); bool was = g_loading; g_loading = true;
    for (auto& f : g_fields) if (f.key.rfind("opt:", 0) == 0) { bool found = false; for (const auto& o : opts) if (o.name == f.key.substr(4)) { found = true; char b[40]; snprintf(b, sizeof b, "%g", o.value); SetVal(f, f.kind == kCheck ? (o.value != 0 ? "1" : "0") : b); } EnableWindow(f.h, found); if (f.hValue) EnableWindow(f.hValue, found); }
    g_loading = was;
}
void LoadAll() {
    g_loading = true; wl::CfgFile c = LoadCfg(); wl::Video v; std::wstring sf = SaveFolder(c);
    if (!wl::ParseVideo(ReadFile8(sf + L"\\Launcher.variables"), &v)) v = wl::Video();
    auto vid = [&](const std::string& k) -> std::string { if (k == "customSize") { char b[40]; snprintf(b, sizeof b, "%d x %d", v.customW, v.customH); Field* fc = ByKey("vid:customSize"); for (size_t i = 0; fc && i < fc->choices.size(); ++i) if (fc->choices[i] == b) return std::to_string(i); return std::string("-1"); } if (k == "renderProfile") return std::to_string(v.renderProfile); if (k == "fullscreen") return v.fullscreen ? "1" : "0"; if (k == "vsync") return v.vsync ? "1" : "0"; if (k == "highdpi") return v.highdpi ? "1" : "0"; if (k == "resolution") return std::to_string(v.resolution);
        if (k == "customW") return std::to_string(v.customW); if (k == "customH") return std::to_string(v.customH); if (k == "multisampling") return std::to_string(v.multisampling); if (k == "shading") return std::to_string(v.shading); if (k == "reflection") return std::to_string(v.reflection); if (k == "texture") return std::to_string(v.texture); if (k == "textureDetail") return std::to_string(v.textureDetail); return std::to_string(v.geometry); };
    for (auto& f : g_fields) {
        if (f.key.rfind("vid:", 0) == 0) SetVal(f, vid(f.key.substr(4)));
        else if (f.key.rfind("cfg:", 0) == 0) SetVal(f, c.Get(f.key.substr(4), f.def));
        else if (f.key == "cfgmulti:peer") FillPeers(f, c);
    }
    MigrateGuideKeys();
    RefreshSaves(); LoadPlayUi(c);
    LoadSaveOptions(0);
    ShowSaveInfo(g_saves.empty() ? -1 : 0); g_loading = false;
    RzLoad();      // the Randomizers page
}
bool ApplyAll(std::wstring* msg) {
    std::string keyError = guide::Validate(GuideSettings(false));
    if (!keyError.empty()) { *msg = W(keyError); return false; }
    wl::CfgFile c = LoadCfg(); wl::Video v; std::wstring sf = SaveFolder(c); wl::ParseVideo(ReadFile8(sf + L"\\Launcher.variables"), &v);
    for (auto& f : g_fields) {
        if (f.kind == kNoteK || f.key.rfind("upd:", 0) == 0) continue;      // (a note; the updater keeps its own settings)
        std::string val = GetVal(f);
        if (f.key.rfind("vid:", 0) == 0) { std::string k = f.key.substr(4); int n = atoi(val.c_str());
            if (k == "customSize") { int cw, ch; Field* fc = ByKey("vid:customSize"); if (fc && n >= 0 && n < (int)fc->choices.size() && sscanf(fc->choices[n].c_str(), "%d x %d", &cw, &ch) == 2) { v.customW = cw; v.customH = ch; } }
            else if (k == "renderProfile") v.renderProfile = n; else if (k == "fullscreen") v.fullscreen = n; else if (k == "vsync") v.vsync = n; else if (k == "highdpi") v.highdpi = n; else if (k == "resolution") v.resolution = n; else if (k == "customW") v.customW = n; else if (k == "customH") v.customH = n;
            else if (k == "multisampling") v.multisampling = n; else if (k == "shading") v.shading = n; else if (k == "reflection") v.reflection = n; else if (k == "texture") v.texture = n; else if (k == "textureDetail") v.textureDetail = n; else v.geometry = n; }
        else if (f.key.rfind("cfg:", 0) == 0) {
            std::string k = f.key.substr(4); if (f.kind == kCombo && (val == "(default)" || val.empty())) val = "";
            if (k.rfind("key_", 0) == 0 && !val.empty()) val = guide::KeyName(guide::ParseKey(val));
            if (f.kind == kEdit && (k == "cap_fps" || k == "game_volume" || k == "voice_volume" || k == "mic_volume" || k == "voice_range" || k == "voice_radio_range" || k == "id" || k == "listen" || k == "lobby_port")) { char* e; long n = strtol(val.c_str(), &e, 10); if (val.empty() || *e || n < 0 || n > 65535) { *msg = W(f.label + ": a whole number from 0 to 65535"); return false; } if ((k == "game_volume" || k == "voice_volume" || k == "mic_volume") && n > 100) { *msg = W(f.label + ": 0 to 100"); return false; } }
            if (val.empty()) c.Unset(k); else if (val == f.def && !c.Has(k) && k != "id" && k != "listen" && k != "name") {} else c.Set(k, val);      // a value that is the default is not written unless the line was already there
        } else if (f.key == "cfgmulti:peer") {
            std::vector<std::string> peers; std::string line; for (char ch : val + "\n") { if (ch == '\n') { while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back(); if (!line.empty()) peers.push_back(line); line.clear(); } else line.push_back(ch); }
            for (const auto& p : peers) { unsigned id, port; char host[100]; bool legacy = !p.empty() && p.find_first_not_of("0123456789") == std::string::npos && atoi(p.c_str()) <= 65535;      // the old form: one port on this machine
                if (!legacy && (sscanf(p.c_str(), "%u,%99[^,],%u", &id, host, &port) != 3 || port > 65535)) { *msg = W("Other players: \"" + p + "\" is not number,address,port"); return false; } }
            c.SetAll("peer", peers);
        }
    }
    std::string err = wl::CheckVideo(v); if (!err.empty()) { *msg = W("Video: " + err); return false; }
    CreateDirectoryW(Dir(sf).c_str(), nullptr); CreateDirectoryW(sf.c_str(), nullptr);
#ifndef WL_MUT_NO_VIDEO_WRITE
    if (!WriteFile8(sf + L"\\Launcher.variables", wl::FormatVideo(v))) { *msg = L"Could not write Launcher.variables in " + sf; return false; }
#endif
#ifdef WL_MUT_CFG_DROP
    { wl::CfgFile only; for (auto& f : g_fields) if (f.key == "cfg:name") only.Set("name", GetVal(f)); c = only; }      // (mutant) the lines the launcher does not know are lost
#endif
    if (!WriteFile8(CfgPath(), c.Text())) { *msg = L"Could not write witness_coop.cfg in " + g_data; return false; }
    // the game's options go into the chosen save (a copy of the original is kept once)
    int pick = (int)SendMessageW(g_savePick, CB_GETCURSEL, 0, 0);
    if (pick >= 0 && pick < (int)g_saves.size()) {
        std::string t = ReadFile8(g_saves[pick].path); std::vector<uint8_t> data(t.begin(), t.end()); bool changed = false;
        for (auto& f : g_fields) if (f.key.rfind("opt:", 0) == 0 && IsWindowEnabled(f.h)) {
            std::string name = f.key.substr(4); float val = (float)atof(GetVal(f).c_str()); float cur = -1e9f; for (const auto& o : wl::ReadSaveOptions(data)) if (o.name == name) cur = o.value;
            if (fabsf(cur - val) < 1e-6f) continue;
            if (!wl::WriteSaveOption(&data, name, val)) { *msg = W(f.label + ": out of range"); return false; } changed = true;
        }
#ifdef WL_MUT_NO_SAVE_WRITE
        changed = false;
#endif
        if (changed) { std::wstring bak = g_saves[pick].path + L".before-launcher"; if (!Exists(bak)) CopyFileW(g_saves[pick].path.c_str(), bak.c_str(), TRUE); if (!WriteFile8(g_saves[pick].path, std::string(data.begin(), data.end()))) { *msg = L"Could not write the save"; return false; } }
    }
    *msg = L"Saved."; return true;
}

// ---- the new game window
HWND g_ng; bool g_ngLobby; std::vector<std::pair<int, std::string>> g_ngAreas;
std::string NgSpec() {
    std::vector<std::string> areas; for (auto& a : g_ngAreas) if (SendDlgItemMessageW(g_ng, a.first, BM_GETCHECK, 0, 0) == BST_CHECKED) areas.push_back(a.second);
    wchar_t b[20]; GetDlgItemTextW(g_ng, ID_NG_LASERS, b, 20); int lasers = _wtoi(b);
    int p = (int)SendDlgItemMessageW(g_ng, ID_NG_PRESET, CB_GETCURSEL, 0, 0); std::vector<std::string> presets; auto names = wprog::PresetNames(); if (p > 0 && p <= (int)names.size()) presets.push_back(names[p - 1]);
    return wl::ProgressSpec(areas, lasers, presets);
}
void NgUpdate() {
    std::string spec = NgSpec(), err; std::wstring t;
    if (spec.empty()) t = L"A plain new game: nothing is completed.";
    else { auto ids = wprog::Resolve(wprog::Builtin(), spec, &err); if (ids.empty()) t = L"Not possible: " + W(err); else {
        std::set<int> own; for (auto& a : g_ngAreas) if (SendDlgItemMessageW(g_ng, a.first, BM_GETCHECK, 0, 0) == BST_CHECKED) own.insert(wprog::Builtin().AreaByName(a.second));
        std::map<int, int> extra; for (unsigned id : ids) { int a = wprog::Builtin().panels[wprog::Builtin().index.at(id)].area; if (!own.count(a)) extra[a]++; }
        t = std::to_wstring(ids.size()) + L" panels will be solved at the start, in the order the game needs them."; if (!extra.empty()) { t += L"  Also needed first: "; bool first = true; for (auto& e : extra) { t += (first ? L"" : L", ") + W(wprog::Builtin().areas[e.first]) + L" (" + std::to_wstring(e.second) + L")"; first = false; } } } }
    SetDlgItemTextW(g_ng, ID_NG_INFO, t.c_str());
}
LRESULT CALLBACK NgProc(HWND h, UINT m, WPARAM w, LPARAM l);
void LaunchGame(bool newGame);
void StartNewGame() {
    std::string spec = NgSpec(); wl::CfgFile c = LoadCfg(); std::wstring sf = SaveFolder(c);
    if (!spec.empty()) { std::string err; if (wprog::Resolve(wprog::Builtin(), spec, &err).empty()) { MessageBoxW(g_ng, W("Not possible: " + err).c_str(), L"New game", MB_OK); return; } }
    if (g_ngLobby) { DestroyWindow(g_ng); g_ng = nullptr; LobbyPartsChosen(spec); return; }      // (for the lobby: only the choice is kept; the host's start launches it)
    DeleteFileW((sf + L"\\coop_progress.txt").c_str());      // the forced-progress list of the old world must not be put into this one
    if (spec.empty()) c.Unset("start_progress"); else c.Set("start_progress", spec);
    WriteFile8(CfgPath(), c.Text()); DestroyWindow(g_ng); g_ng = nullptr; LaunchGame(true);
}
LRESULT CALLBACK NgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_ERASEBKGND) { RECT rc; GetClientRect(h, &rc); th::FillBg((HDC)w, h, rc); return 1; }
    if (m == WM_CTLCOLORSTATIC || m == WM_CTLCOLOREDIT || m == WM_CTLCOLORLISTBOX || m == WM_CTLCOLORBTN) return th::CtlColor(m, (HDC)w, (HWND)l);
    if (m == WM_COMMAND) { int id = LOWORD(w); if (id == ID_NG_OK) { StartNewGame(); return 0; } if (id == IDCANCEL) { DestroyWindow(h); g_ng = nullptr; return 0; } if (id >= ID_NG_AREAS || id == ID_NG_PRESET || id == ID_NG_LASERS) NgUpdate(); return 0; }
    if (m == WM_CLOSE) { DestroyWindow(h); g_ng = nullptr; return 0; }
    return DefWindowProcW(h, m, w, l);
}
void OpenNewGame(bool forLobby = false) {
    if (g_ng) { SetForegroundWindow(g_ng); return; }
    g_ngLobby = forLobby;
    static bool reg; if (!reg) { WNDCLASSW wc = {}; wc.lpfnWndProc = NgProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"WitnessNewGame"; wc.hbrBackground = th::BgBrush(); wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc); reg = true; }
    g_ng = CreateWindowExW(WS_EX_DLGMODALFRAME, L"WitnessNewGame", L"New game: what is already completed", WS_CAPTION | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 620, 665, g_main, nullptr, GetModuleHandleW(nullptr), nullptr);
    HFONT font = (HFONT)SendMessageW(g_main, WM_GETFONT, 0, 0); auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) { HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, g_ng, (HMENU)(INT_PTR)id, nullptr, nullptr); SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE); th::Style(c); return c; };
    mk(L"STATIC", L"Tick the parts of the map that are done when the game starts. Whatever the game needs before them (the gate of the tutorial, the doors on the way) is solved too, in its own order.", SS_LEFT, 12, 8, 590, 58, 0);
    const auto& d = wprog::Builtin(); g_ngAreas.clear(); int y = 70, col = 0, k = 0;
    for (size_t i = 0; i < d.areas.size(); ++i) { if (!d.AreaCount((int)i)) continue; int id = ID_NG_AREAS + k++; g_ngAreas.push_back({id, d.areas[i]}); std::string lab = d.areas[i]; for (size_t q = 0; q < lab.size(); ++q) if (lab[q] == '&') { lab.insert(q, "&"); ++q; }      // (an & in a button label would underline the next letter)
        mk(L"BUTTON", W(lab + "  (" + std::to_string(d.AreaCount((int)i)) + ")").c_str(), BS_AUTOCHECKBOX, 16 + col * 290, y, 280, 22, id); if (++col == 2) { col = 0; y += 24; } }
    if (col) y += 24; y += 8;
    mk(L"STATIC", L"Or light this many lasers (the cheapest first):", SS_LEFT, 16, y + 3, 330, 20, 0); mk(L"EDIT", L"0", WS_BORDER | ES_NUMBER, 350, y, 50, 22, ID_NG_LASERS);
    y += 32; mk(L"STATIC", L"Or a ready-made start:", SS_LEFT, 16, y + 3, 280, 20, 0); HWND cb = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 300, y, 200, 200, ID_NG_PRESET);
    SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"(none)"); for (const auto& n : wprog::PresetNames()) SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)W(n).c_str()); SendMessageW(cb, CB_SETCURSEL, 0, 0);
    y += 40; mk(L"STATIC", L"", SS_LEFT, 16, y, 580, 60, ID_NG_INFO); y += 70;
    mk(L"BUTTON", forLobby ? L"Use these" : L"Start new game", BS_DEFPUSHBUTTON, 16, y, 160, 30, ID_NG_OK); mk(L"BUTTON", L"Cancel", BS_PUSHBUTTON, 190, y, 100, 30, IDCANCEL);
    NgUpdate();
}

// The game opens on the launcher's monitor: the mod (proxy/hooks.cpp) centres the game's window in the work area passed in WITNESS_MONITOR, which the game inherits.
void PassMonitor() {
    MONITORINFO mi = {sizeof mi}; if (!GetMonitorInfoW(MonitorFromWindow(g_main, MONITOR_DEFAULTTONEAREST), &mi)) return;
    wchar_t v[64]; swprintf(v, 64, L"%ld %ld %ld %ld", mi.rcWork.left, mi.rcWork.top, mi.rcWork.right, mi.rcWork.bottom);
#ifndef WL_MUT_NO_MONITOR
    SetEnvironmentVariableW(L"WITNESS_MONITOR", v);
#endif
}
// Co-op needs the one build of the game the mod was made for. For any other the launcher says so in plain words instead of starting
// a game with co-op silently off (the proxy in the game says the same if the game is started some other way).
void LobbyStatus(const std::wstring& s);
bool BuildOk(const std::wstring& exe, bool lobby = false) {
    std::string problem = wl::GameBuildProblemOfFile(exe); if (problem.empty()) return true;
    std::wstring shortText = W(WITNESS_UNSUPPORTED_SHORT);
    if (lobby) LobbyStatus(shortText); else Status(shortText);
    MessageBoxW(g_main, W(problem).c_str(), L"The Witness Co-op", MB_OK | MB_ICONINFORMATION);
    return false;
}
// ---- Send diagnostics: the embedded dist/diag.ps1 writes a zip (logs, dumps, settings, a summary of this PC; names and addresses replaced) to the desktop
std::wstring g_diagOut;                     // --diag-out <folder> (tests): write the zip there, open no Explorer window and show no message box
HANDLE g_diagProc; std::wstring g_diagResult, g_diagScript;
std::wstring LogsFolder() {
    std::string d = LoadCfg().Get("diag_dir");
    if (d == "instance") return g_dir + L"\\witness-coop-logs";
    if (!d.empty()) return W(d);
    return diag::DefaultLogsDir();
}
void SendDiagnostics() {
    if (g_diagProc) { Status(L"Diagnostics are already being packed."); return; }
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp); std::wstring id = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    g_diagScript = std::wstring(tmp) + L"witness-coop-diag-" + id + L".ps1"; g_diagResult = std::wstring(tmp) + L"witness-coop-diag-" + id + L".txt";
    DeleteFileW(g_diagResult.c_str());
    if (!WriteFile8(g_diagScript, kDiagScript)) { Status(L"Could not write the diagnostics script to the temporary folder."); return; }
    wchar_t sys[MAX_PATH]; GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring cmd = std::wstring(L"\"") + sys + L"\\WindowsPowerShell\\v1.0\\powershell.exe\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + g_diagScript + L"\" -GameDir \"" + g_dir +
        L"\" -DataDir \"" + g_data + L"\" -LogsDir \"" + LogsFolder() + L"\" -Version \"" + W(MOD_VERSION) + L"\" -ResultFile \"" + g_diagResult + L"\"" + (g_diagOut.empty() ? L"" : L" -NoOpen -OutDir \"" + g_diagOut + L"\"");
    STARTUPINFOW si; memset(&si, 0, sizeof si); si.cb = sizeof si; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE; PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) { Status(L"Could not start PowerShell for the diagnostics."); return; }
    CloseHandle(pi.hThread); g_diagProc = pi.hProcess; Dlog("diagnostics: packing started"); Status(L"Packing diagnostics ...");
}
void DiagPoll() {      // the 100 ms timer
    if (!g_diagProc || WaitForSingleObject(g_diagProc, 0) == WAIT_TIMEOUT) return;
    CloseHandle(g_diagProc); g_diagProc = nullptr;
    std::string r = ReadFile8(g_diagResult); while (!r.empty() && (r.back() == '\n' || r.back() == '\r')) r.pop_back();
    DeleteFileW(g_diagScript.c_str()); DeleteFileW(g_diagResult.c_str());
    Dlog(("diagnostics: " + (r.empty() ? std::string("no result") : r)).c_str());
    if (r.compare(0, 3, "OK ") == 0) {
        std::wstring zip = W(r.substr(3)); Status(L"Diagnostics saved: " + zip);
        if (g_diagOut.empty()) MessageBoxW(g_main, (L"Saved to your Desktop:\n" + zip + L"\n\nIt holds the logs of your recent game and launcher runs, a crash dump if the game crashed, your settings file, and a summary of this PC "
            L"(Windows, graphics card, the game's build). Your Windows and computer names, your profile folder, IP addresses and Steam IDs are replaced. No saves and no game files are in it, and nothing was uploaded: "
            L"attach the file to your bug report.").c_str(), L"Diagnostics", MB_OK | MB_ICONINFORMATION);
    } else Status(L"Diagnostics failed: " + W(r.size() > 4 ? r.substr(4) : std::string("the packing script did not finish")), L"Try again", ID_DIAG);
}

void LaunchGame(bool newGame) {
    std::wstring exe = g_dir + L"\\witness64_d3d11.exe"; if (!Exists(exe)) exe = g_dir + L"\\witness_d3d11.exe";
    if (!Exists(exe)) { Status(L"The game was not found in " + g_dir, L"Choose the game folder", ID_BROWSE); return; }
    if (!BuildOk(exe)) return;
    std::wstring ApplyMsg; if (!ApplyAll(&ApplyMsg)) { Status(ApplyMsg); return; }
    { wl::CfgFile c = LoadCfg(); c.Set("randomizer", RzForLaunch(newGame)); WriteFile8(CfgPath(), c.Text()); }      // a new game: the pick (a new seed if wanted); continued: its save's
    PassMonitor();
    std::wstring cmd = L"\"" + exe + L"\" " + W(wl::LaunchArgs(LoadCfg(), newGame));
    Dlog(("launching the game: " + U(cmd)).c_str());
    STARTUPINFOW si = {sizeof si}; PROCESS_INFORMATION pi = {}; if (g_noActivate) { si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_SHOWNOACTIVATE; }
    if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, g_dir.c_str(), &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); Status(L"The game is starting."); } else Status(L"Could not start the game.", L"Send diagnostics", ID_DIAG);
}


// ---- the window
bool Interactive(const std::wstring& cls, DWORD style) {
    if (cls == L"BUTTON" || cls == L"EDIT" || cls == L"COMBOBOX" || cls == L"LISTBOX" || cls == TRACKBAR_CLASSW) return true;
    (void)style; return false;
}
HWND Make(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, DWORD ex = 0) {
    std::wstring c = cls; for (auto& ch : c) ch = (wchar_t)towupper(ch);
    if (Interactive(c, style) && !(style & ES_READONLY)) style |= WS_TABSTOP;
    HWND k = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(k, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE); th::Style(k); if (id) g_ctl[id] = k; return k;
}
HWND Panel(HWND parent) { HWND p = CreateWindowExW(WS_EX_CONTROLPARENT, L"WitnessLauncherPage", L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 10, 10, parent, nullptr, GetModuleHandleW(nullptr), nullptr); return p; }
HWND C(int id) { auto it = g_ctl.find(id); return it == g_ctl.end() ? nullptr : it->second; }
void Place(HWND h, int x, int y, int w, int hh) { if (h) SetWindowPos(h, nullptr, x, y, std::max(1, w), std::max(1, hh), SWP_NOZORDER | SWP_NOACTIVATE); }
void PlaceId(int id, int x, int y, int w, int hh) { Place(C(id), x, y, w, hh); }
void Show(HWND h, bool on) { if (h) ShowWindow(h, on ? SW_SHOW : SW_HIDE); }
void ShowId(int id, bool on) { Show(C(id), on); }
std::string EditText(HWND h) { wchar_t b[1000] = {}; GetWindowTextW(h, b, 1000); return U(b); }

LRESULT CALLBACK PageProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_ERASEBKGND) { RECT rc; GetClientRect(h, &rc); th::FillBg((HDC)w, h, rc); return 1; }      // (the faint panels, lined up with the window)
    if (m == WM_CTLCOLORSTATIC || m == WM_CTLCOLOREDIT || m == WM_CTLCOLORLISTBOX || m == WM_CTLCOLORBTN) return th::CtlColor(m, (HDC)w, (HWND)l);
    if (m == WM_DRAWITEM || m == WM_MEASUREITEM) return SendMessageW(g_main, m, w, l);
    if (m == WM_COMMAND || m == WM_HSCROLL) { SendMessageW(g_main, m, w, l); return 0; }
    if (m == WM_NOTIFY) { NMHDR* n = (NMHDR*)l; if (n->idFrom == ID_SAVELIST && n->code == LVN_ITEMCHANGED) { NMLISTVIEW* nl = (NMLISTVIEW*)l; if (nl->uNewState & LVIS_SELECTED) ShowSaveInfo(nl->iItem); } return 0; }
    return DefWindowProcW(h, m, w, l);
}
wup::Updater g_upd; bool g_updFromLobby;
enum { ID_LB_HOST = 600, ID_LB_JOIN, ID_LB_INVITE, ID_LB_COPY, ID_LB_LEAVE, ID_LB_READY, ID_LB_START, ID_LB_MEMBERS, ID_LB_LOG, ID_LB_CHAT, ID_LB_SEND, ID_LB_MODE, ID_LB_GOAL, ID_LB_NEWGAME, ID_LB_PARTS, ID_LB_STATUS, ID_LB_PORT, ID_LB_ADVERTISE, ID_LB_MUTE, ID_LB_DEAF, ID_LB_RESULT, ID_LB_LEVEL, ID_LB_PARTSINFO, ID_LB_WHO, ID_LB_STEAM_HOST, ID_LB_STEAM_INVITE, ID_LB_STEAM_FRIENDS, ID_LB_STEAM_SEND, ID_LB_UPDATE,
       ID_LB_INVITE_SHOW, ID_LB_H_PLAYERS, ID_LB_H_CHAT, ID_LB_H_INVITE, ID_LB_H_PLAY, ID_LB_H_GAME, ID_LB_H_GOAL, ID_LB_H_GOALHINT, ID_LB_H_VOICE, ID_LB_H_STEAMHINT, ID_LB_RZ, ID_LB_RZ_BTN,
       ID_LB_FJ0, ID_LB_FJ1, ID_LB_FJ2, ID_LB_FJ3, ID_LB_FJ4, ID_JOIN_WHAT, ID_JOIN_SOURCES, ID_JOIN_LIST, ID_JOIN_CLIP, ID_JOIN_CLIP_YES, ID_JOIN_LIST_HEAD };
HWND LB(int id) { return C(id); }
void BuildLobbyPanel(HWND pg);
void UpdateTabs();
void LayoutAll();
std::string g_inviteLink;      // the invite from a witness-coop:// link (Setup's handler starts the launcher with it): put into the join box, never joined without a click
uint64_t g_autoJoin;      // a Steam lobby to join at start (an invite accepted in Steam while the launcher was closed)
lw::Session g_sess; lw::Voice g_voice; wlobby::State g_shown; std::wstring g_chatLog; HANDLE g_game; DWORD g_gamePid; std::string g_lobbySpec, g_cfgBackup, g_lastResult; bool g_gameWasRace;
std::wstring g_statFile, g_cmdFile; ULONGLONG g_statAt, g_launchedAt; unsigned g_launches; std::string g_lastLaunchCfg; bool g_voiceWanted = true; std::set<unsigned> g_everHeard;
void LobbyStatus(const std::wstring& s) { SetWindowTextW(LB(ID_LB_STATUS), s.c_str()); if (!g_sess.Active()) Status(s); }
std::string CfgGet(const char* k, const char* d = "") { return LoadCfg().Get(k, d); }
lw::VoiceCfg VoiceCfgNow() {
    wl::CfgFile c = LoadCfg(); lw::VoiceCfg v; std::string m = c.Get("voice_mode", "auto"); v.mode = m == "off" ? 0 : m == "open" ? 1 : m == "ptt" ? 2 : g_padLast ? 1 : 2; Cfg keys;      // (automatic: open mic when the launcher was last used with a pad)
    for (const auto& b : guide::bindings) {
        unsigned k = guide::MigrateDefault(b.config, guide::ParseKey(c.Get(b.config, guide::KeyName(b.initial))));
        if (k) keys.*b.member = k;
    }
    guide::MigrateAll(keys); v.key = keys.keyVoice; v.key2 = keys.keyVoiceAlt;
    v.micVolume = atoi(c.Get("mic_volume", "100").c_str()); v.outVolume = atoi(c.Get("voice_volume", "100").c_str()); v.testTone = c.Get("voice_test_tone", ""); v.muteOutput = c.Get("voice_mute_output", "0") == "1"; return v;
}
void StartVoice() {
#ifdef WL_MUT_LOBBY_NO_VOICE
    return;
#endif
    if (!g_sess.Active() || g_game || !g_sess.Joined() || g_voice.Running() || !g_voiceWanted) return;
    g_sess.SetVoiceSink([](const wvoice::Packet& p) { g_voice.Push(p); g_everHeard.insert(p.from); });
    g_voice.SetMuted(SendMessageW(LB(ID_LB_MUTE), BM_GETCHECK, 0, 0) == BST_CHECKED); g_voice.SetDeaf(SendMessageW(LB(ID_LB_DEAF), BM_GETCHECK, 0, 0) == BST_CHECKED); g_voice.Start(&g_sess, VoiceCfgNow());
}
wlobby::Settings SettingsFromUi() {
    wlobby::Settings s; s.mode = SendMessageW(LB(ID_LB_MODE), CB_GETCURSEL, 0, 0) == 1 ? wlobby::kRace : wlobby::kCoop; s.raceGoal = EditText(LB(ID_LB_GOAL)); s.newGame = SendMessageW(LB(ID_LB_NEWGAME), BM_GETCHECK, 0, 0) == BST_CHECKED; s.progress = g_lobbySpec;
    s.randomizer = RzForLaunch(s.newGame); return s;      // the host's randomizer, with this round's seed: every member's game gets the same
}
void PushSettings() { if (g_sess.Active() && g_sess.IsHost()) g_sess.SetSettings(SettingsFromUi()); }
void PartsInfo() { SetWindowTextW(LB(ID_LB_PARTSINFO), g_lobbySpec.empty() ? L"nothing done yet" : W(g_lobbySpec).c_str()); }
void RefreshLobbyUi(); void HostUiTick(bool now = false);
void LobbyPartsChosen(const std::string& spec) { g_lobbySpec = spec; PartsInfo(); PushSettings(); }
nf_settings NetSettings();
// ---- C: hosting. "Friends can join by" (netfind.c's checks), the invite (joinglue.h MakeInvite)
void HostChecksOpen(unsigned lobbyPort, unsigned gamePort, bool steam) {
    char exe[MAX_PATH] = {}; GetModuleFileNameA(nullptr, exe, MAX_PATH); std::wstring g = g_dir + L"\\witness64_d3d11.exe"; std::string game = U(g);
    nf_settings s = NetSettings(); std::string adv = CfgGet("lobby_advertise"); uint32_t advIp = adv.empty() ? 0 : lw::Session::Resolve(adv);
    nf_host_open(lobbyPort, gamePort, steam ? 1 : 0, g_sess.InviteOnly() ? 1 : 0, &s, exe, game.c_str(), advIp);
}
bool HostInvite(std::string* code, std::string* msg) {
    jg::InviteIn in; in.name = CfgGet("name", "Player"); in.secret = g_sess.Secret(); in.inviteOnly = g_sess.InviteOnly(); lh_addr lan[8]; int nlan = 0, ok = 0;
    if (g_sess.SteamMode()) { in.steamLobby = g_sess.SteamLobbyId(); in.port = NetSettings().port ? NetSettings().port : LJ_DEFAULT_PORT; if (!in.steamLobby) return false; }
    else { nf_host_routes(lan, 8, &nlan, &in.tailscaleIp, &in.internetIp, &ok); in.lan = lan; in.nlan = nlan; in.internetOk = ok != 0; in.port = g_sess.LobbyPort();
           std::string adv = CfgGet("lobby_advertise"); if (!adv.empty()) in.advertiseIp = lw::Session::Resolve(adv); }
    return jg::MakeInvite(in, code, msg);
}
void HostLobby() {
    wl::CfgFile c = LoadCfg(); std::string err; unsigned port = (unsigned)atoi(c.Get("lobby_port", "47100").c_str()); if (port < 1 || port > 65535) { Status(L"The lobby port is a number from 1 to 65535 (Settings, Network)."); return; }
    uint16_t gp = (uint16_t)atoi(c.Get("listen", "47001").c_str()); std::string name = c.Get("name", "Player");
    g_sess.SetInviteOnly(c.Get("lobby_visibility", "open") == "invite");
    if (!g_sess.Host(name, gp, (uint16_t)port, c.Get("lobby_advertise"), &err)) { Status(W(err), L"Try again", ID_PRIMARY); return; }
    HostChecksOpen(port, gp, false); HostUiTick(true);
    SetWindowTextW(LB(ID_LB_INVITE_SHOW), W(g_sess.Invite()).c_str()); g_chatLog.clear(); SetWindowTextW(LB(ID_LB_LOG), L""); PushSettings(); StartVoice(); RefreshLobbyUi();
}
std::vector<std::pair<uint64_t, std::string>> g_friends;
void FillFriends() {      // the friends who are online, for an invite through Steam chat (works without the Steam overlay)
    HWND cb = LB(ID_LB_STEAM_FRIENDS); SendMessageW(cb, CB_RESETCONTENT, 0, 0); g_friends = g_sess.SteamFriends();
    if (g_friends.empty()) SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"(no friends online)");
    for (const auto& f : g_friends) SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)W(f.second).c_str());
    SendMessageW(cb, CB_SETCURSEL, 0, 0);
}
void HostSteamLobby() {
    wl::CfgFile c = LoadCfg(); std::string err; uint16_t gp = (uint16_t)atoi(c.Get("listen", "47001").c_str());
    if (!g_sess.HostSteam(c.Get("name", "Player"), gp, g_dir, &err)) { Status(W(err), L"Host direct instead", ID_LB_HOST); return; }
    HostChecksOpen((unsigned)atoi(c.Get("lobby_port", "47100").c_str()), gp, true);
    SetWindowTextW(LB(ID_LB_INVITE_SHOW), L""); g_chatLog.clear(); SetWindowTextW(LB(ID_LB_LOG), L""); PushSettings(); FillFriends(); LobbyStatus(W(g_sess.SteamStatus())); RefreshLobbyUi();
}
void JoinSteamLobby(uint64_t id) {
    wl::CfgFile c = LoadCfg(); std::string err; uint16_t gp = (uint16_t)atoi(c.Get("listen", "47001").c_str());
    if (g_sess.Active() && g_sess.SteamMode() && g_sess.SteamLobbyId() == id) return;
    if (g_game) { LobbyStatus(L"Close the game first, then accept the invite again."); return; }
    if (!g_sess.JoinSteam(id, c.Get("name", "Player"), gp, g_dir, &err)) { LobbyStatus(W(err)); return; }
    SetWindowTextW(LB(ID_LB_INVITE_SHOW), W(steamids::Text(id)).c_str()); g_chatLog.clear(); SetWindowTextW(LB(ID_LB_LOG), L""); LobbyStatus(W(g_sess.SteamStatus())); RefreshLobbyUi();
}
// ---- B: finding each other. The Join list (netfind.c scans while it is shown), the one paste box (joinglue.h PlanPaste), an invite's routes at once (C4), recents (B4)
nf_settings NetSettings() {
    static nf_settings cached; static ULONGLONG at; if (at && GetTickCount64() - at < 1000) return cached; at = GetTickCount64();
    wl::CfgFile c = LoadCfg(); cached.find_lan = c.Get("find_lan", "1") != "0"; cached.find_tailscale = c.Get("find_tailscale", "1") != "0"; cached.upnp = c.Get("upnp", "1") != "0";
    cached.internet_in_invite = c.Get("internet_in_invite", "1") != "0"; cached.port = (unsigned)atoi(c.Get("lobby_port", "47100").c_str()); return cached;
}
struct PendingJoin { bool active = false; jg::Plan plan; } g_pend;
int g_joinRoute = CD_R_ADDRESS; bool g_recentDone = false; std::string g_joinSaid;
std::vector<nf_row> g_rows; std::string g_rowsKey;
void FocusFirst(HWND page);
void BeginJoin() { g_chatLog.clear(); SetWindowTextW(LB(ID_LB_LOG), L""); g_recentDone = false; LobbyStatus(L"Joining..."); RefreshLobbyUi(); UpdateTabs(); FocusFirst(g_lobbyPanel); }
bool JoinAddress(uint32_t ip, unsigned port, const std::string& code, int recentRoute) {      // a Direct lobby at ip:port; code: the invite's (empty: a lobby open to my networks)
    wl::CfgFile c = LoadCfg(); std::string err; uint16_t gp = (uint16_t)atoi(c.Get("listen", "47001").c_str());
    if (g_game) { LobbyStatus(L"Close the game first, then join."); return false; }
    if (!g_sess.JoinAt(ip, (uint16_t)port, code, c.Get("name", "Player"), gp, &err)) { Status(W(err), L"Try again", ID_PRIMARY); return false; }
    g_joinRoute = recentRoute; Dlog(("join: " + wlobby::IpText(ip) + ":" + std::to_string(port) + (code.empty() ? " (no code)" : " (with the invite's code)")).c_str());
    SetWindowTextW(LB(ID_LB_INVITE_SHOW), W(wlobby::IpText(ip) + ":" + std::to_string(port)).c_str()); BeginJoin(); return true;
}
void JoinRow(int i) {
    if (i < 0 || i >= (int)g_rows.size()) return; const nf_row& r = g_rows[i];
    if (r.mismatch) { char b[300]; lj_failure(LJ_F_VERSION, r.name, r.version, b, sizeof b); Status(W(b), L"Update", ID_UP_CHECK); return; }
    if (r.full) { char b[300]; lj_failure(LJ_F_FULL, r.name, (std::to_string(r.players) + " of " + std::to_string(r.limit)).c_str(), b, sizeof b); Status(W(b)); return; }
    JoinAddress(r.ip, r.port, "", lj_recent_route(r.route, CD_R_ADDRESS));
}
void JoinLobby() {      // the paste box (B7): an invite code or link, a witness-coop:// link, a Steam lobby number, an address (old address:port/CODE invites too), a host or Tailscale name
    std::string text = EditText(LB(ID_LB_INVITE)); jg::Plan p = jg::PlanPaste(text, NetSettings().port ? NetSettings().port : LJ_DEFAULT_PORT); g_joinSaid = p.said;
    switch (p.action) {
    case jg::kNothing: { int sel = (int)SendMessageW(LB(ID_JOIN_LIST), LB_GETCURSEL, 0, 0); if (sel >= 0) { JoinRow(sel); return; } char b[200]; lj_failure(LJ_F_NOTHING, "", "", b, sizeof b); Status(W(b)); SetFocus(LB(ID_LB_INVITE)); return; }
    case jg::kSteam: JoinSteamLobby(p.lobby); return;
    case jg::kDirect: JoinAddress(p.ip, p.port, p.token, CD_R_ADDRESS); return;
    case jg::kResolve: {
        uint32_t ip = lw::Session::Resolve(p.host);
        if (!ip) { char b[300]; lj_failure(LJ_F_RESOLVE, p.host.c_str(), "no address for that name", b, sizeof b); Status(W(b), L"Try again", ID_PRIMARY); return; }
        JoinAddress(ip, p.port, "", CD_R_ADDRESS); return; }
    case jg::kProbeInvite:      // C4: every route at once; LobbyTick joins the first answer (a better route within 300 ms instead)
        g_pend.active = true; g_pend.plan = p; nf_invite_probe_start(&p.invite); Status(L"Trying every way to reach the host (" + W(p.said) + L") ..."); return;
    default: Status(W(p.said)); SetFocus(LB(ID_LB_INVITE)); return;
    }
}
void PendingJoinTick() {
    if (!g_pend.active) return; uint32_t ip = 0; unsigned port = 0; int route = 0; int r = nf_invite_probe_result(&ip, &port, &route); if (r < 0) return; g_pend.active = false;
    if (r == 1) { JoinAddress(ip, port, g_pend.plan.token, lj_recent_route(route, CD_R_ADDRESS)); return; }
    if (g_pend.plan.invite.flags & CI_FLAG_STEAM) { JoinSteamLobby(g_pend.plan.invite.steam_lobby); return; }
    char routes[120], b[400]; lj_routes_text(&g_pend.plan.invite, routes, sizeof routes); lj_failure(LJ_F_NO_ANSWER, "the host", routes, b, sizeof b); Status(W(b), L"Try again", ID_PRIMARY);
}
void LeaveLobby() { nf_host_close(); g_voice.Stop(); g_sess.Stop(); g_shown = wlobby::State(); SendMessageW(LB(ID_LB_MEMBERS), LB_RESETCONTENT, 0, 0); SendMessageW(LB(ID_LB_READY), BM_SETCHECK, BST_UNCHECKED, 0); RefreshLobbyUi(); LobbyStatus(L"You left the lobby."); g_fleeting = L"You left the lobby."; g_fleetingUntil = GetTickCount64() + 4000; }
void AppendChat(const std::wstring& line) { g_chatLog += line + L"\r\n"; HWND l = LB(ID_LB_LOG); SetWindowTextW(l, g_chatLog.c_str()); SendMessageW(l, EM_SETSEL, g_chatLog.size(), g_chatLog.size()); SendMessageW(l, EM_SCROLLCARET, 0, 0); }
std::string WaitingFor() {      // who the host waits for: not ready, or still getting the host's own puzzles
    std::string w; for (const auto& m : g_shown.members) if (!m.host && (!m.ready || !m.puzzles)) w += (w.empty() ? "" : ", ") + m.name + (m.ready && !m.puzzles ? " (getting your puzzles)" : "");
    return w;
}
void StartLabel() {      // D3: the host's button says whom it waits for
    static std::wstring last; std::string w = WaitingFor(); std::wstring t = w.empty() ? L"Start game" : W("Waiting for " + w); if (t != last) { last = t; SetWindowTextW(LB(ID_LB_START), t.c_str()); }
}
std::string MemberName(unsigned id) { for (const auto& m : g_shown.members) if (m.id == id) return m.name; return "player " + std::to_string(id); }
std::string ClockText(unsigned ms) { char b[24]; snprintf(b, sizeof b, "%u:%02u.%u", ms / 60000, (ms / 1000) % 60, (ms / 100) % 10); return b; }
std::string RaceResultText() {      // witness_race_result.txt written by the game: goal=, winner=, then place|name|finish ms|phase
    std::string t = ReadFile8(g_data + L"\\witness_race_result.txt"); if (t.empty()) return ""; std::string out = "Race: "; size_t at = 0; bool any = false;
    while (at < t.size()) { size_t e = t.find('\n', at); if (e == std::string::npos) e = t.size(); std::string line = t.substr(at, e - at); at = e + 1; unsigned place = 0, ms = 0; char name[80] = {}, ph[24] = {};
        if (sscanf(line.c_str(), "%u|%79[^|]|%u|%23s", &place, name, &ms, ph) == 4) { if (any) out += "   "; any = true; out += std::to_string(place) + ". " + name + (!strcmp(ph, "finished") ? " " + ClockText(ms) : std::string(" (not finished)")); } }
    return any ? out : "";
}
void LaunchFromLobby(const wlobby::Launch& l) {
    std::wstring exe = g_dir + L"\\witness64_d3d11.exe"; if (!Exists(exe)) exe = g_dir + L"\\witness_d3d11.exe"; if (!Exists(exe)) { LobbyStatus(L"The game was not found in " + g_dir); g_sess.SetInGame(false); return; }
    if (!BuildOk(exe, true)) { g_sess.SetInGame(false); return; }
    std::wstring msg; if (!ApplyAll(&msg)) { LobbyStatus(msg); return; }
    g_cfgBackup = ReadFile8(CfgPath());      // the player's own settings: put back when the game is over
    std::string cfg = wlobby::BuildGameCfg(g_cfgBackup, l, g_sess.HostIp());
    { std::string inv = g_sess.Invite(); if (!inv.empty() && inv.find_first_of("\r\n") == std::string::npos) { if (!cfg.empty() && cfg.back() != '\n') cfg += "\n"; cfg += "invite=" + inv + "\n"; } }      // the game's player panel offers Copy invite (not kept: the cfg is restored afterwards)
#ifndef WL_MUT_LOBBY_NO_CFG
    WriteFile8(CfgPath(), cfg);
#endif
    g_lastLaunchCfg = cfg;
    wl::CfgFile c = LoadCfg(); std::wstring sf = SaveFolder(c); if (l.state.settings.newGame) DeleteFileW((sf + L"\\coop_progress.txt").c_str());
    DeleteFileW((g_data + L"\\witness_race_result.txt").c_str()); g_gameWasRace = l.state.settings.mode == wlobby::kRace;
    PassMonitor();
    std::wstring cmd = L"\"" + exe + L"\" " + W(wl::LaunchArgs(c, l.state.settings.newGame)); STARTUPINFOW si = {sizeof si}; PROCESS_INFORMATION pi = {}; if (g_noActivate) { si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_SHOWNOACTIVATE; }
    if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, g_dir.c_str(), &si, &pi)) {
        CloseHandle(pi.hThread); g_game = pi.hProcess; g_gamePid = pi.dwProcessId; g_launchedAt = GetTickCount64(); ++g_launches; g_voice.Stop(); g_sess.SetInGame(true); LobbyStatus(L"The game is running. The lobby stays here: you are back in it when the game ends.");
    } else { WriteFile8(CfgPath(), g_cfgBackup); LobbyStatus(L"Could not start the game."); g_sess.SetInGame(false); }
}
void GameEnded() {
    CloseHandle(g_game); g_game = nullptr; g_gamePid = 0;
#ifndef WL_MUT_LOBBY_NO_RESTORE
    if (!g_cfgBackup.empty()) WriteFile8(CfgPath(), g_cfgBackup);
#endif
    g_cfgBackup.clear();
    std::string res = g_gameWasRace ? RaceResultText() : ""; if (res.empty()) res = g_gameWasRace ? "Race: no result (the game was closed before it ended)" : "The game ended."; g_lastResult = res;
#ifndef WL_MUT_LOBBY_NO_RESULT
    if (g_sess.IsHost()) g_sess.SetResult(res);
#endif
    g_sess.SetInGame(false); StartVoice(); LobbyStatus(L"The game ended. Back in the lobby."); LoadAll();
    g_rzRound = FreshSeed(); PushSettings(); RzRefresh();      // the next round's new game gets another seed
}
void RefreshLobbyUi() {
    bool act = g_sess.Active(), host = act && g_sess.IsHost(), joined = act && g_sess.Joined(); bool lobby = g_shown.phase == wlobby::kLobby;
    EnableWindow(LB(ID_LB_HOST), !act); EnableWindow(LB(ID_LB_JOIN), !act); EnableWindow(LB(ID_LB_STEAM_HOST), !act);
    bool steamHost = host && g_sess.SteamMode(); EnableWindow(LB(ID_LB_STEAM_INVITE), steamHost); EnableWindow(LB(ID_LB_STEAM_FRIENDS), steamHost); EnableWindow(LB(ID_LB_STEAM_SEND), steamHost); EnableWindow(LB(ID_LB_LEAVE), act); EnableWindow(LB(ID_LB_COPY), host);
    SendMessageW(LB(ID_LB_INVITE), EM_SETREADONLY, act ? TRUE : FALSE, 0); EnableWindow(LB(ID_LB_PORT), !act); EnableWindow(LB(ID_LB_ADVERTISE), !act);
    EnableWindow(LB(ID_LB_READY), joined && !host && lobby); EnableWindow(LB(ID_LB_START), host && lobby); EnableWindow(LB(ID_LB_SEND), joined); EnableWindow(LB(ID_LB_CHAT), joined);
    ShowWindow(LB(ID_LB_READY), act && !host ? SW_SHOW : SW_HIDE); ShowWindow(LB(ID_LB_START), host ? SW_SHOW : SW_HIDE); EnableWindow(LB(ID_LB_INVITE_SHOW), act);
    EnableWindow(LB(ID_LB_MODE), !act || host); EnableWindow(LB(ID_LB_GOAL), !act || host); EnableWindow(LB(ID_LB_NEWGAME), !act || host); EnableWindow(LB(ID_LB_PARTS), !act || host);
}
void ApplyRemoteSettings(const wlobby::State& st) {      // a guest sees what the host chose
    if (g_sess.IsHost()) return; bool wasRace = SendMessageW(LB(ID_LB_MODE), CB_GETCURSEL, 0, 0) == 1; SendMessageW(LB(ID_LB_MODE), CB_SETCURSEL, st.settings.mode == wlobby::kRace ? 1 : 0, 0); if (wasRace != (st.settings.mode == wlobby::kRace)) LayoutAll(); SetWindowTextW(LB(ID_LB_GOAL), W(st.settings.raceGoal).c_str());
    SendMessageW(LB(ID_LB_NEWGAME), BM_SETCHECK, st.settings.newGame ? BST_CHECKED : BST_UNCHECKED, 0); g_lobbySpec = st.settings.progress; PartsInfo();
    RzRefresh();
}
int CurTab(); void LayoutAll();
// the Join list on screen (B6: it fills as soon as Join is chosen, refreshes every 5 s while shown; netfind.c sends nothing while it is hidden)
bool JoinListShown() { return g_choice == ls::kJoin && !g_sess.Active() && CurTab() == ls::kPlay && IsWindowVisible(g_main) && !IsIconic(g_main); }
void JoinUiTick() {
    static ULONGLONG at; bool shown = JoinListShown(); nf_settings set = NetSettings(); nf_join_show(shown ? 1 : 0, &set, MOD_VERSION);
    PendingJoinTick();
    if (g_sess.Active() && !g_sess.IsHost() && !g_sess.SteamMode() && g_sess.Joined() && !g_recentDone && !g_shown.members.empty()) {      // B4: remembered after each successful join
        g_recentDone = true; nf_recent_add(MemberName(1).c_str(), g_sess.HostIp(), g_sess.HostPort(), g_joinRoute); Dlog(("join: remembered " + MemberName(1) + " as a recent host").c_str());
    }
    if (!shown || GetTickCount64() - at < 400) return; at = GetTickCount64();
    nf_row rows[64]; int n = nf_join_rows(rows, 64); std::string key;
    for (int i = 0; i < n; ++i) key += std::string(rows[i].name) + "|" + rows[i].line + "|" + std::to_string(rows[i].ip) + ":" + std::to_string(rows[i].port) + "\n";
    HWND lst = LB(ID_JOIN_LIST);
    if (key != g_rowsKey) {
        int sel = (int)SendMessageW(lst, LB_GETCURSEL, 0, 0); std::string selName = sel >= 0 && sel < (int)g_rows.size() ? g_rows[sel].name : std::string(); uint32_t selIp = sel >= 0 && sel < (int)g_rows.size() ? g_rows[sel].ip : 0;
        g_rows.assign(rows, rows + n); g_rowsKey = key; SendMessageW(lst, WM_SETREDRAW, FALSE, 0); SendMessageW(lst, LB_RESETCONTENT, 0, 0);
        int keep = -1; for (int i = 0; i < n; ++i) { SendMessageW(lst, LB_ADDSTRING, 0, (LPARAM)W(rows[i].name).c_str()); if (rows[i].name == selName && rows[i].ip == selIp) keep = i; }
        if (keep >= 0) SendMessageW(lst, LB_SETCURSEL, keep, 0); SendMessageW(lst, WM_SETREDRAW, TRUE, 0); InvalidateRect(lst, nullptr, TRUE);
    }
    char src[300]; nf_join_sources(src, sizeof src); { static std::string last; if (last != src) { last = src; SetWindowTextW(C(ID_JOIN_SOURCES), W(src).c_str()); } }
    int sel = (int)SendMessageW(lst, LB_GETCURSEL, 0, 0); bool typed = !EditText(LB(ID_LB_INVITE)).empty();
    std::wstring label = W(ls::PrimaryLabel(ls::kJoin, !typed && sel >= 0 && sel < (int)g_rows.size() ? g_rows[sel].name : ""));
    { static std::wstring last; if (label != last) { last = label; SetWindowTextW(C(ID_PRIMARY), label.c_str()); } }
}
void DrawJoinRow(DRAWITEMSTRUCT* d) {      // B5: name, state, players of limit, route, ping; another version greyed with its version (never hidden)
    HDC dc = d->hDC; RECT r = d->rcItem; HBRUSH bg = CreateSolidBrush((d->itemState & ODS_SELECTED) ? th::kSurface2 : th::kSurface); FillRect(dc, &r, bg); DeleteObject(bg);
    if (d->itemID == (UINT)-1 || d->itemID >= g_rows.size()) return; const nf_row& row = g_rows[d->itemID]; bool grey = row.mismatch || row.full; SetBkMode(dc, TRANSPARENT);
    th::DrawDot(dc, r.left + 16, (r.top + r.bottom) / 2, 6, grey ? th::kMuted : row.state == CD_IN_LOBBY ? th::kWarm : th::kAccent, !grey);
    SelectObject(dc, th::Bold()); SetTextColor(dc, grey ? th::kMuted : th::kText); RECT nr = r; nr.left += 34; nr.right = r.left + 34 + (r.right - r.left) * 30 / 100; th::Text(dc, W(row.name).c_str(), -1, &nr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, th::Small()); SetTextColor(dc, grey ? th::kMuted : th::kLine); RECT lr = r; lr.left = nr.right + 12; lr.right -= 12; th::Text(dc, W(row.line).c_str(), -1, &lr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}
// "Friends can join by" (C1) and the invite box, while hosting
int g_fjState[NF_ROWS];      // the panel rows' states as last drawn
// Each way in is a row (owner, 2026-11-06: the wall of text replaced): a coloured dot and the way's name, a word for its state on the right, one line of what was found
// under it and, when it needs a look, one line of what to do. The whole text (cut short in the row when long) is the row's tooltip and its window text.
struct FjRow { std::wstring title, result, fix; }; FjRow g_fj[NF_ROWS]; HWND g_fjTips;
bool FjTall(int st) { return st == NF_WARN; }
int FjHeight(int st) { return FjTall(st) ? 66 : 48; }      // (a card is the row less 4; its last line ends 38 or 54 down: room under it, owner 2026-11-08)
void FjTip(HWND row, const std::wstring& text) {
    if (!g_fjTips) { g_fjTips = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0, 0, GetParent(row), nullptr, GetModuleHandleW(nullptr), nullptr); SendMessageW(g_fjTips, TTM_SETMAXTIPWIDTH, 0, 420); SendMessageW(g_fjTips, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000); }
    std::wstring t = text; TOOLINFOW ti = {}; ti.cbSize = sizeof ti; ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS; ti.hwnd = GetParent(row); ti.uId = (UINT_PTR)row; ti.lpszText = &t[0];
    wchar_t buf[600] = {}; TOOLINFOW q = ti; q.lpszText = buf;
    SendMessageW(g_fjTips, SendMessageW(g_fjTips, TTM_GETTOOLINFOW, 0, (LPARAM)&q) ? TTM_UPDATETIPTEXTW : TTM_ADDTOOLW, 0, (LPARAM)&ti);      // (empty text: no tip)
}
std::wstring FirstSentence(const std::wstring& t) { size_t e = t.find(L". "); return e == std::wstring::npos ? (t.size() && t.back() == L'.' ? t.substr(0, t.size() - 1) : t) : t.substr(0, e); }
void DrawFjRow(DRAWITEMSTRUCT* d, int i) {
    HDC dc = d->hDC; RECT r = d->rcItem; th::FillBg(dc, d->hwndItem, r); const FjRow& f = g_fj[i]; int st = g_fjState[i]; SetBkMode(dc, TRANSPARENT);
    if (f.title.empty()) { if (i == 0) { SelectObject(dc, th::Small()); SetTextColor(dc, th::kMuted); RECT tr = r; tr.left += 2; th::Text(dc, L"Shown while you host.", -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE); } return; }
    RECT card = {r.left, r.top, r.right, r.bottom - 4}; HBRUSH cb = CreateSolidBrush(th::kSurface); HPEN cp = CreatePen(PS_SOLID, 1, st == NF_WARN ? RGB(92, 76, 40) : th::kSurface2);
    HGDIOBJ ob = SelectObject(dc, cb), op = SelectObject(dc, cp); RoundRect(dc, card.left, card.top, card.right, card.bottom, 10, 10); SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(cb); DeleteObject(cp);
    COLORREF col = st == NF_OK ? RGB(92, 214, 140) : st == NF_WARN ? th::kWarm : st == NF_CHECKING ? th::kLine : th::kMuted;
    const wchar_t* word = st == NF_OK ? L"Ready" : st == NF_WARN ? L"Needs a look" : st == NF_CHECKING ? L"Checking\x2026" : L"Off";
    th::DrawDot(dc, card.left + 16, card.top + 14, 5, col, st != NF_OFF);
    SelectObject(dc, th::Small()); SetTextColor(dc, col); RECT wr = {card.left + 30, card.top + 4, card.right - 12, card.top + 24}; th::Text(dc, word, -1, &wr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    RECT ww = wr; th::Text(dc, word, -1, &ww, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_CALCRECT);
    SelectObject(dc, th::Bold()); SetTextColor(dc, st == NF_OFF ? th::kMuted : th::kText); RECT tr = {card.left + 30, card.top + 4, card.right - 12 - (ww.right - ww.left) - 10, card.top + 24}; th::Text(dc, f.title.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, th::Small()); SetTextColor(dc, th::kMuted); RECT dr = {card.left + 30, card.top + 21, card.right - 12, card.top + 38}; std::wstring det = FirstSentence(f.result); th::Text(dc, det.c_str(), -1, &dr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (!f.fix.empty() && FjTall(st)) { SetTextColor(dc, th::kWarm); RECT fr = {card.left + 30, card.top + 37, card.right - 12, card.top + 54}; std::wstring fx = FirstSentence(f.fix); th::Text(dc, fx.c_str(), -1, &fr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX); }
}
int TextHeight(HWND h, int width) {      // the height a static's text takes at its font, wrapped to width
    wchar_t t[600] = {}; GetWindowTextW(h, t, 600); if (!t[0]) return 16; HDC dc = GetDC(h); HGDIOBJ old = SelectObject(dc, (HFONT)SendMessageW(h, WM_GETFONT, 0, 0));
    RECT r = {0, 0, width, 0}; th::Text(dc, t, -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX); SelectObject(dc, old); ReleaseDC(h, dc); return std::max(16, (int)r.bottom);
}
int FjLaidHeight(int i) { HWND h = LB(ID_LB_FJ0 + i); RECT rc = {}; GetWindowRect(h, &rc); return (GetWindowLongW(h, GWL_STYLE) & WS_VISIBLE) ? (int)(rc.bottom - rc.top) : 0; }
unsigned g_fjBad;      // (tests) host ticks that left a row with a way in shown without its height
void HostUiTick(bool now) {      // now: at once (the lobby just opened: the rows appear while every check is still running)
    static ULONGLONG at; static bool laidSteam; if (!now && GetTickCount64() - at < 400) return; at = GetTickCount64();
    bool host = g_sess.Active() && g_sess.IsHost(), steamHost = host && g_sess.SteamMode(); if (steamHost != laidSteam) { laidSteam = steamHost; LayoutAll(); }
    nf_host_row rows[NF_ROWS]; if (host) nf_host_rows(rows); bool relayout = false;
    for (int i = 0; i < NF_ROWS; ++i) {
        int st = host ? rows[i].state : NF_OFF; bool tall = FjTall(st), wasTall = FjTall(g_fjState[i]); g_fjState[i] = st;
        FjRow r{host ? W(rows[i].title) : L"", host ? W(rows[i].result) : L"", host && rows[i].state == NF_WARN ? W(rows[i].fix) : L""};
        if (r.title != g_fj[i].title || r.result != g_fj[i].result || r.fix != g_fj[i].fix || tall != wasTall) {
#ifndef WL_MUT_FJ_NO_RELAYOUT
            if (r.title.empty() != g_fj[i].title.empty()) relayout = true;      // a row appears or goes: the layout gives an empty row no room (owner, 2026-11-08: on the first host only a sliver of Steam showed)
#endif
            g_fj[i] = r; std::wstring full = r.title.empty() ? L"" : r.title + L": " + r.result + (r.fix.empty() ? L"" : L"\n" + r.fix);
            SetWindowTextW(LB(ID_LB_FJ0 + i), full.c_str()); FjTip(LB(ID_LB_FJ0 + i), full); InvalidateRect(LB(ID_LB_FJ0 + i), nullptr, FALSE); if (tall != wasTall) relayout = true;
        }
    }
    if (relayout) LayoutAll();
    if (host) for (int i = 0; i < NF_ROWS; ++i) if (!g_fj[i].title.empty() && FjLaidHeight(i) != FjHeight(g_fjState[i])) { ++g_fjBad; break; }
    if (host) { std::string code; if (HostInvite(&code, nullptr)) { std::wstring wc = W(code); wchar_t cur[200] = {}; GetWindowTextW(LB(ID_LB_INVITE_SHOW), cur, 200); if (wc != cur) SetWindowTextW(LB(ID_LB_INVITE_SHOW), wc.c_str()); } }
}
void CopyInvite(HWND h) {      // C3: "<name> is hosting The Witness. Join: https://<join page>/#WIT-..." (before the checks found a way in: the old address:port/CODE invite)
    std::string code, msg; std::wstring inv = HostInvite(&code, &msg) ? W(msg) : W(g_sess.Invite());
    if (!inv.empty() && OpenClipboard(h)) { EmptyClipboard(); HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (inv.size() + 1) * 2); if (g) { memcpy(GlobalLock(g), inv.c_str(), (inv.size() + 1) * 2); GlobalUnlock(g); if (!SetClipboardData(CF_UNICODETEXT, g)) GlobalFree(g); } CloseClipboard(); LobbyStatus(L"The invite is on the clipboard: send it to your friends."); }
}
// B8: when the launcher gets the focus, an invite for The Witness on the clipboard is offered. The text is read for this check only: never stored, logged or sent.
ci_invite g_clipInvite; uint32_t g_clipDone; bool g_clipHave;
void ClipboardCheck() {
    if (g_sess.Active() || !IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(g_main)) return;
    std::string text; if (HANDLE hd = GetClipboardData(CF_UNICODETEXT)) { if (const wchar_t* p = (const wchar_t*)GlobalLock(hd)) { size_t n = wcsnlen(p, CI_SCAN_MAX); text = U(std::wstring(p, n)); GlobalUnlock(hd); } }
    CloseClipboard(); ci_invite v; char name[CI_NAME_MAX + 1] = {};
    bool found = ci_find_invite(text.data(), text.size(), LJ_GAME, &v, name) != 0; std::fill(text.begin(), text.end(), '\0'); text.clear();
    if (!found || (g_clipHave && v.secret == g_clipInvite.secret) || v.secret == g_clipDone) return;
    char prompt[120]; ci_clipboard_prompt(name, prompt, sizeof prompt); g_clipInvite = v; g_clipHave = true;
    SetWindowTextW(C(ID_JOIN_CLIP), W(prompt).c_str()); ShowId(ID_JOIN_CLIP, true); ShowId(ID_JOIN_CLIP_YES, true);
}
void SelectTab(int t); void ChooseTile(ls::Choice c);
void ClipboardJoin() {
    if (!g_clipHave) return; char code[CI_CODE_MAX]; g_clipDone = g_clipInvite.secret; g_clipHave = false; ShowId(ID_JOIN_CLIP, false); ShowId(ID_JOIN_CLIP_YES, false);
    if (!ci_encode(&g_clipInvite, LJ_GAME, code, sizeof code)) return;
    SelectTab(ls::kPlay); ChooseTile(ls::kJoin); SetWindowTextW(LB(ID_LB_INVITE), W(code).c_str()); JoinLobby();
}
void LobbyTick() {
    if (g_game && WaitForSingleObject(g_game, 0) == WAIT_OBJECT_0) GameEnded();
    JoinUiTick(); HostUiTick();
    g_sess.Idle(); if (uint64_t req = g_sess.SteamJoinRequest()) JoinSteamLobby(req);      // an invite accepted (or Join Game) in Steam while the launcher runs
    if (g_sess.Active()) {
        bool joined = g_sess.Joined();
        if (joined) {
            wlobby::State st = g_sess.State(); bool changed = st.seq != g_shown.seq || st.members.size() != g_shown.members.size() || st.phase != g_shown.phase; wlobby::Phase before = g_shown.phase; g_shown = st;
            if (changed) {
                HWND lst = LB(ID_LB_MEMBERS); SendMessageW(lst, LB_RESETCONTENT, 0, 0); for (size_t i = 0; i < st.members.size(); ++i) SendMessageW(lst, LB_ADDSTRING, 0, (LPARAM)W(st.members[i].name).c_str());
                ApplyRemoteSettings(st);
                if (!st.lastResult.empty()) SetWindowTextW(LB(ID_LB_RESULT), W("Last game:  " + st.lastResult).c_str());
                if (before != wlobby::kLobby && st.phase == wlobby::kLobby) SendMessageW(LB(ID_LB_READY), BM_SETCHECK, BST_UNCHECKED, 0);
                RefreshLobbyUi();
            }
            InvalidateRect(LB(ID_LB_MEMBERS), nullptr, FALSE); StartLabel();
            std::wstring t; if (g_sess.IsHost()) t = g_sess.SteamMode() ? L"Hosting a Steam lobby (number " + W(g_sess.Invite()) + L"). Invite friends: overlay, the list, or the number." : L"Hosting.  Send friends the invite (Copy invite)."; else t = L"In the lobby of " + W(MemberName(1)) + (g_sess.SteamMode() ? L" (Steam)." : L".");
            if (st.phase == wlobby::kStarting) t = L"The game starts in " + std::to_wstring((st.startInMs + 999) / 1000) + L" ...";
            else if (st.phase == wlobby::kInGame) t = g_game ? L"The game is running." : L"A game is running (you were not in it).";
            LobbyStatus(t); StartVoice();
            for (const auto& c : g_sess.TakeChat()) AppendChat(W(MemberName(c.from) + ":  " + c.text));
            wlobby::Launch l; if (g_sess.TakeLaunch(&l)) LaunchFromLobby(l);
        } else {
            wlobby::JoinResult r = g_sess.Refused();
            if (g_sess.SteamMode() && (g_sess.SteamFailed() || r == wlobby::kPending)) LobbyStatus(W(g_sess.SteamFailed() || !g_sess.SteamLobbyId() ? g_sess.SteamStatus() : std::string("In the Steam lobby. Waiting for the host's launcher ...")));
            else if (!g_sess.IsHost()) LobbyStatus(r == wlobby::kBadToken ? L"The invite is wrong (the code does not match)." : r == wlobby::kFull ? L"The lobby is full." : r == wlobby::kBadVersion ? L"This launcher is an older version than the host's lobby needs: update it (Play page: Check for updates)." : r == wlobby::kVersionMismatch ? W("The host is on " + g_sess.HostVersion() + ", you are on " + std::string(MOD_VERSION) + ": update both to the same version.") : r == wlobby::kPending && false ? L"" : L"Looking for the host ...");
            if (g_voice.Running()) g_voice.Stop();
        }
    }
    // the microphone level
    InvalidateRect(LB(ID_LB_LEVEL), nullptr, FALSE);
}
void DrawMember(DRAWITEMSTRUCT* d) {
    HDC dc = d->hDC; RECT r = d->rcItem; HBRUSH bg = CreateSolidBrush((d->itemState & ODS_SELECTED) ? th::kSurface2 : th::kSurface); FillRect(dc, &r, bg); DeleteObject(bg);
    if (d->itemID == (UINT)-1 || d->itemID >= g_shown.members.size()) return; const auto& m = g_shown.members[d->itemID]; SetBkMode(dc, TRANSPARENT);
    th::DrawDot(dc, r.left + 16, (r.top + r.bottom) / 2, 6, m.speaking ? th::kAccent : (m.ready || m.host ? th::kWarm : th::kMuted), m.speaking || m.ready || m.host);
    SelectObject(dc, m.host ? th::Bold() : th::Body()); SetTextColor(dc, th::kText); bool differs = !m.version.empty() && m.version != MOD_VERSION;      // (the version only when it is not mine: in a narrow column it left the name a few letters)
    RECT nr = r; nr.left += 34; nr.right -= differs ? 230 : 96; th::Text(dc, W(m.name + (m.id == g_sess.MyId() ? "  (you)" : "")).c_str(), -1, &nr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (differs) { SelectObject(dc, th::Small()); SetTextColor(dc, differs ? th::kAccent : th::kMuted); RECT vr = r; vr.right -= 120; vr.left = r.right - 230; th::Text(dc, W(m.version).c_str(), -1, &vr, DT_VCENTER | DT_SINGLELINE | DT_RIGHT | DT_END_ELLIPSIS); }
    const wchar_t* st = m.inGame ? L"in game" : m.host ? L"host" : !m.puzzles ? L"getting puzzles" : m.ready ? L"ready" : L"not ready"; SelectObject(dc, th::Small()); SetTextColor(dc, m.inGame ? th::kAccent : m.ready || m.host ? th::kWarm : th::kMuted); RECT sr = r; sr.right -= 12; th::Text(dc, st, -1, &sr, DT_VCENTER | DT_SINGLELINE | DT_RIGHT);
}
void DrawLevel(DRAWITEMSTRUCT* d) {
    HDC dc = d->hDC; RECT r = d->rcItem; HBRUSH bg = CreateSolidBrush(th::kSurface2); FillRect(dc, &r, bg); DeleteObject(bg); lw::VoiceStats v = g_voice.Stats();
    int w = (int)((r.right - r.left) * std::min(1.0f, v.level / 600.0f)); RECT f = r; f.right = f.left + w; HBRUSH fb = CreateSolidBrush(v.speaking ? th::kAccent : th::kMuted); FillRect(dc, &f, fb); DeleteObject(fb);
}
LRESULT CALLBACK ChatEditProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (m == WM_GETDLGCODE) return DLGC_WANTALLKEYS | DLGC_HASSETSEL;
    if (m == WM_KEYDOWN && w == VK_RETURN) { SendMessageW(g_main, WM_COMMAND, ID_LB_SEND, 0); return 0; }
    if (m == WM_CHAR && w == VK_RETURN) return 0;
    return DefSubclassProc(h, m, w, l);
}
void SendChatLine() { std::string t = EditText(LB(ID_LB_CHAT)); if (t.empty() || !g_sess.Active() || !g_sess.Joined()) return; g_sess.SendChat(t); SetWindowTextW(LB(ID_LB_CHAT), L""); }
// ---- Settings: a list of sections on the left, the rows of the chosen one on the right
void BuildSettings(HWND pg) {
    g_secList = Make(pg, L"LISTBOX", L"", LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 0, 0, 10, 10, ID_SECTIONS);
    SendMessageW(g_secList, LB_SETITEMHEIGHT, 0, 38);
    for (int s = 0; s < ls::kSectionCount; ++s) SendMessageW(g_secList, LB_ADDSTRING, 0, (LPARAM)W(ls::SectionName(s)).c_str());
    SendMessageW(g_secList, LB_SETCURSEL, 0, 0);
    for (int s = 0; s < ls::kSectionCount; ++s) {
        HWND p = g_secPanel[s] = Panel(pg);
        HWND t = Make(p, L"STATIC", W(ls::SectionName(s)).c_str(), SS_LEFT, 0, 0, 10, 10, 0); SendMessageW(t, WM_SETFONT, (WPARAM)th::Bold(), TRUE); SetPropW(t, L"title", (HANDLE)1);
        HWND r = Make(p, L"BUTTON", L"Reset section", BS_PUSHBUTTON, 0, 0, 10, 10, 0); SetWindowLongPtrW(r, GWLP_ID, ID_RESET); SetPropW(r, L"reset", (HANDLE)1);
        if (s == ls::kControls || s == ls::kDisplay) {      // the game's own options live inside a save: the save they are changed in
            HWND l = Make(p, L"STATIC", L"Options marked (in a save) are the game's own and live inside each save. Save to change:", SS_LEFT, 0, 0, 10, 10, 0); th::MarkMuted(l); SetPropW(l, L"picklabel", (HANDLE)1);
            HWND cb = Make(p, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 10, 200, s == ls::kControls ? ID_SAVEPICK2 : ID_SAVEPICK); SetPropW(cb, L"pick", (HANDLE)1);
            if (s == ls::kDisplay) g_savePick = cb; else g_savePick2 = cb;
        }
    }
    for (auto& f : g_fields) {
        HWND p = g_secPanel[f.section]; std::string label = f.label + (f.row->inSave ? "  (in a save)" : "");
        if (f.kind == kNoteK) { f.hLabel = Make(p, L"STATIC", W(f.label).c_str(), SS_LEFT, 0, 0, 10, 10, 0); th::MarkMuted(f.hLabel); continue; }
        f.hLabel = Make(p, L"STATIC", W(label).c_str(), SS_LEFT | SS_ENDELLIPSIS, 0, 0, 10, 10, 0);
        if (f.kind == kCheck) f.h = Make(p, L"BUTTON", L"", BS_AUTOCHECKBOX, 0, 0, 10, 10, f.id);
        else if (f.kind == kEdit) f.h = Make(p, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | (f.row->kind == ls::kPort ? ES_NUMBER : 0), 0, 0, 10, 10, f.id);
        else if (f.kind == kMulti) f.h = Make(p, L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL, 0, 0, 10, 10, f.id);
        else if (f.kind == kKeyBtn) f.h = Make(p, L"BUTTON", L"", BS_PUSHBUTTON, 0, 0, 10, 10, f.id);
        else if (f.kind == kSlider) {
            f.h = Make(p, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS, 0, 0, 10, 10, f.id);
            SendMessageW(f.h, TBM_SETRANGE, TRUE, MAKELPARAM(0, ls::SliderSteps(*f.row))); int coarse = std::max(1, (int)std::lround(f.row->coarse / f.row->step)); SendMessageW(f.h, TBM_SETLINESIZE, 0, coarse); SendMessageW(f.h, TBM_SETPAGESIZE, 0, coarse * 2);
            f.hValue = Make(p, L"STATIC", L"", SS_LEFT, 0, 0, 10, 10, 0);
        } else { f.h = Make(p, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 10, 200, f.id); for (auto& c : f.choices) SendMessageW(f.h, CB_ADDSTRING, 0, (LPARAM)W(c).c_str()); }
        if (!f.hint.empty()) { f.hHint = Make(p, L"STATIC", W(f.hint).c_str(), SS_LEFT | SS_ENDELLIPSIS, 0, 0, 10, 10, 0); th::MarkMuted(f.hHint); }
    }
}
void ShowSection(int s) {
    if (s < 0 || s >= ls::kSectionCount) return; g_section = s; SendMessageW(g_secList, LB_SETCURSEL, s, 0);
    for (int i = 0; i < ls::kSectionCount; ++i) ShowWindow(g_secPanel[i], i == s ? SW_SHOW : SW_HIDE);
}
void LayoutSection(HWND p, int s, int W, int H) {
    (void)H; int y = 8;
    for (HWND c = GetWindow(p, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        if (GetPropW(c, L"title")) Place(c, 0, y + 6, W - 200, 26);
        if (GetPropW(c, L"reset")) Place(c, W - 170, y, 160, 34);
    }
    y += 48;
    if (s == ls::kControls || s == ls::kDisplay) {
        for (HWND c = GetWindow(p, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) { if (GetPropW(c, L"picklabel")) Place(c, 0, y + 4, W - 270, 22); if (GetPropW(c, L"pick")) Place(c, W - 260, y, 250, 300); }
        y += 40;
    }
    std::vector<Field*> rows; for (auto& f : g_fields) if (f.section == s) rows.push_back(&f);
    auto rowH = [](const Field* f) { int lines = 1; for (char ch : f->label) lines += ch == '\n'; return f->kind == kMulti ? 96 : f->kind == kNoteK ? std::max(64, 20 * lines + 12) : f->hHint ? 56 : 36; };
    int total = 0; for (auto* f : rows) total += rowH(f);
    int cols = (W >= 900 && total > std::max(200, H - y - 8)) ? 2 : 1, colW = (W - (cols - 1) * 28) / cols;
    int per = 0, x = 0, cy = y, target = (total + cols - 1) / cols;
    int room = H - y - 8 - 40; float squeeze = target > room ? std::max(0.8f, float(room) / target) : 1.f;      // (40: a column may run one short row past the even split)      // a long section in a short window: the rows a little closer (Controls at 1280 x 800 under the banner)
    target = int(target * squeeze + .5f);
    for (auto* f : rows) {
        int rh = int(rowH(f) * squeeze + .5f);
        if (cols > 1 && x == 0 && per >= target) { x = colW + 28; cy = y; }
        int lw = std::min(cols > 1 ? 330 : 300, colW * (cols > 1 ? 60 : 50) / 100), cx = x + lw + 12, cw = colW - lw - 12;
        if (f->kind == kNoteK) { Place(f->hLabel, x, cy, colW, rh - 8); cy += rh; per += rh; continue; }
        if (f->kind == kMulti) { Place(f->hLabel, x, cy + 4, colW, 22); Place(f->h, x, cy + 28, colW, rh - 36); if (f->hHint) Place(f->hHint, x, cy + rh - 8, colW, 18); cy += rh + (f->hHint ? 14 : 0); per += rh; continue; }
        Place(f->hLabel, x, cy + 6, lw, 22);
        if (f->kind == kCheck) Place(f->h, cx, cy + 4, 28, 26);
        else if (f->kind == kSlider) { int sw = std::min(380, std::max(80, cw - 104)); Place(f->h, cx - 6, cy + 2, sw, 30); Place(f->hValue, cx + sw + 4, cy + 6, 96, 22); }
#ifdef WL_MUT_WIDE_COMBOS
        else if (f->kind == kCombo) Place(f->h, cx, cy + 2, std::min(cw, 260) + 320, 300);      // (mutant: a dropdown over the next column)
#else
        else if (f->kind == kCombo) Place(f->h, cx, cy + 2, std::min(cw, 260), 300);
#endif
        else if (f->kind == kKeyBtn) Place(f->h, cx, cy + 1, std::min(cw, 200), std::min(30, (f->hHint ? 34 : rh) - 4));
        else Place(f->h, cx, cy + 3, std::min(cw, 300), 28);
        if (f->hHint) { int hy = cy + std::min(34, rh - 16); Place(f->hHint, x, hy, colW, std::min(18, cy + rh - hy)); }
        cy += rh; per += rh;
    }
}

// ---- Play: the tiles, or the lobby
void ShowChoiceDetails() {
    bool host = g_choice == ls::kHost, join = g_choice == ls::kJoin, solo = g_choice == ls::kSolo;
    for (int id : {ID_HOST_STEAM, ID_HOST_DIRECT}) ShowId(id, host);
    for (int id : {(int)ID_JOIN_LABEL, (int)ID_LB_INVITE, (int)ID_JOIN_WHAT, (int)ID_JOIN_SOURCES, (int)ID_JOIN_LIST, (int)ID_JOIN_LIST_HEAD}) ShowId(id, join);
    for (int id : {ID_SOLO_NOTE, ID_SOLO_NEW}) ShowId(id, solo);
    SendMessageW(C(ID_TILE_HOST), BM_SETCHECK, host ? BST_CHECKED : BST_UNCHECKED, 0); SendMessageW(C(ID_TILE_JOIN), BM_SETCHECK, join ? BST_CHECKED : BST_UNCHECKED, 0); SendMessageW(C(ID_TILE_SOLO), BM_SETCHECK, solo ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(C(ID_PRIMARY), W(ls::PrimaryLabel(g_choice, "")).c_str());
}
void LoadPlayUi(const wl::CfgFile& c) {
    SetWindowTextW(C(ID_NAME), W(c.Get("name")).c_str()); g_choice = ls::ParseChoice(c.Get("launcher_choice")); bool direct = c.Get("launcher_host", "steam") == "direct";
    SendMessageW(C(ID_HOST_STEAM), BM_SETCHECK, direct ? BST_UNCHECKED : BST_CHECKED, 0); SendMessageW(C(ID_HOST_DIRECT), BM_SETCHECK, direct ? BST_CHECKED : BST_UNCHECKED, 0); ShowChoiceDetails();
}
void ChooseTile(ls::Choice c) {
    g_choice = c; ShowChoiceDetails(); LayoutAll();
    wl::CfgFile cfg = LoadCfg(); if (cfg.Get("launcher_choice") != ls::ChoiceKey(c)) { cfg.Set("launcher_choice", ls::ChoiceKey(c)); if (!g_game) WriteFile8(CfgPath(), cfg.Text()); }      // A2: the last choice is remembered
}
void BuildPlay(HWND pg) {
    g_tiles = Panel(pg); g_lobbyPanel = Panel(pg); HWND t = g_tiles;
    th::MarkMuted(Make(t, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, 0, 0, 10, 10, ID_UP_BANNER)); Make(t, L"BUTTON", L"Update", BS_PUSHBUTTON, 0, 0, 10, 10, ID_UP_BANNER_APPLY); Make(t, L"BUTTON", L"What's new", BS_PUSHBUTTON, 0, 0, 10, 10, ID_UP_WHATSNEW); Make(t, L"BUTTON", L"Later", BS_PUSHBUTTON, 0, 0, 10, 10, ID_UP_BANNER_LATER);
    for (int id : {ID_UP_BANNER, ID_UP_BANNER_APPLY, ID_UP_WHATSNEW, ID_UP_BANNER_LATER}) ShowId(id, false);
    Make(t, L"STATIC", L"Your name", SS_LEFT, 0, 0, 10, 10, ID_FOLDER_LABEL + 100); Make(t, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 0, 0, 10, 10, ID_NAME);
    Make(t, L"BUTTON", L"Host a game", BS_AUTORADIOBUTTON | WS_GROUP, 0, 0, 10, 10, ID_TILE_HOST); Make(t, L"BUTTON", L"Join a friend", BS_AUTORADIOBUTTON, 0, 0, 10, 10, ID_TILE_JOIN); Make(t, L"BUTTON", L"Play solo", BS_AUTORADIOBUTTON, 0, 0, 10, 10, ID_TILE_SOLO);
    th::MarkMuted(Make(t, L"STATIC", L"Open a lobby; friends join you there, you choose what to play and start.", SS_LEFT, 0, 0, 10, 10, ID_TILE_HOST_TXT));
    th::MarkMuted(Make(t, L"STATIC", L"Games on your network and Tailscale are listed; or paste the invite a friend sent, or accept a Steam invite.", SS_LEFT, 0, 0, 10, 10, ID_TILE_JOIN_TXT));
    th::MarkMuted(Make(t, L"STATIC", L"The game with co-op ready, on your latest save.", SS_LEFT, 0, 0, 10, 10, ID_TILE_SOLO_TXT));
    th::MarkTile(C(ID_TILE_HOST), C(ID_TILE_HOST_TXT)); th::MarkTile(C(ID_TILE_JOIN), C(ID_TILE_JOIN_TXT)); th::MarkTile(C(ID_TILE_SOLO), C(ID_TILE_SOLO_TXT));      // the three choices as tiles (the descriptions are drawn in them)
    Make(t, L"BUTTON", L"Through Steam (friends; no port forwarding)", BS_AUTORADIOBUTTON | WS_GROUP, 0, 0, 10, 10, ID_HOST_STEAM); Make(t, L"BUTTON", L"Direct (same network, VPN or a forwarded port)", BS_AUTORADIOBUTTON, 0, 0, 10, 10, ID_HOST_DIRECT);
    Make(t, L"STATIC", L"Paste an invite or address", SS_LEFT, 0, 0, 10, 10, ID_JOIN_LABEL); Make(t, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 0, 0, 10, 10, ID_LB_INVITE);
    th::MarkMuted(Make(t, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, 0, 0, 10, 10, ID_JOIN_WHAT));
    th::MarkMuted(Make(t, L"STATIC", L"GAMES FOUND", SS_LEFT, 0, 0, 10, 10, ID_JOIN_LIST_HEAD)); th::MarkMuted(Make(t, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, 0, 0, 10, 10, ID_JOIN_SOURCES));
    { HWND jl = Make(t, L"LISTBOX", L"", LBS_OWNERDRAWFIXED | LBS_NOTIFY | WS_VSCROLL | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | WS_BORDER, 0, 0, 10, 10, ID_JOIN_LIST); SendMessageW(jl, LB_SETITEMHEIGHT, 0, 34); }
    th::MarkMuted(Make(t, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, 0, 0, 10, 10, ID_JOIN_CLIP)); Make(t, L"BUTTON", L"Join", BS_PUSHBUTTON, 0, 0, 10, 10, ID_JOIN_CLIP_YES); ShowId(ID_JOIN_CLIP, false); ShowId(ID_JOIN_CLIP_YES, false);
    th::MarkMuted(Make(t, L"STATIC", L"Continue your latest save, or start a new game with chosen parts of the map done (also on the Saves tab).", SS_LEFT, 0, 0, 10, 10, ID_SOLO_NOTE)); Make(t, L"BUTTON", L"New game...", BS_PUSHBUTTON, 0, 0, 10, 10, ID_SOLO_NEW);
    Make(t, L"BUTTON", L"Open lobby", BS_DEFPUSHBUTTON, 0, 0, 10, 10, ID_PRIMARY);
    BuildLobbyPanel(g_lobbyPanel);
}
void LayoutPlay(int W, int H) {
    Place(g_tiles, 0, 0, W, H); Place(g_lobbyPanel, 0, 0, W, H);
    int m = 24, y = 12;
    PlaceId(ID_UP_BANNER, m, y + 6, W - 2 * m - 390, 22); PlaceId(ID_UP_BANNER_APPLY, W - m - 380, y, 120, 32); PlaceId(ID_UP_WHATSNEW, W - m - 250, y, 130, 32); PlaceId(ID_UP_BANNER_LATER, W - m - 110, y, 110, 32);
    y += 48; PlaceId(ID_FOLDER_LABEL + 100, m, y + 6, 120, 22); PlaceId(ID_NAME, m + 130, y + 2, 320, 30);
    PlaceId(ID_JOIN_CLIP, m + 470, y + 6, std::max(100, W - 2 * m - 470 - 110), 22); PlaceId(ID_JOIN_CLIP_YES, W - m - 100, y, 100, 32);
    y += 56; int tw = (W - 2 * m - 2 * 20) / 3;
    int ids[3] = {ID_TILE_HOST, ID_TILE_JOIN, ID_TILE_SOLO}, txt[3] = {ID_TILE_HOST_TXT, ID_TILE_JOIN_TXT, ID_TILE_SOLO_TXT};
    for (int i = 0; i < 3; ++i) { PlaceId(ids[i], m + i * (tw + 20), y, tw, 96); PlaceId(txt[i], m + i * (tw + 20) + 26, y + 38, tw - 26, 44); }      // (the description controls stay hidden: the tile draws their text)
    y += 112; PlaceId(ID_HOST_STEAM, m, y, std::min(W - 2 * m, 520), 30); PlaceId(ID_HOST_DIRECT, m, y + 34, std::min(W - 2 * m, 520), 30);
    PlaceId(ID_JOIN_LABEL, m, y + 6, 240, 22); PlaceId(ID_LB_INVITE, m + 250, y + 2, std::min(W - 2 * m - 250, 620), 30); PlaceId(ID_JOIN_WHAT, m + 250, y + 36, std::min(W - 2 * m - 250, 620), 20);
    int jy = y + 64, jh = std::max(90, H - jy - 30 - 64); PlaceId(ID_JOIN_LIST_HEAD, m, jy, 130, 20); PlaceId(ID_JOIN_SOURCES, m + 140, jy, W - 2 * m - 140, 20); PlaceId(ID_JOIN_LIST, m, jy + 24, W - 2 * m, jh - 24);
    PlaceId(ID_SOLO_NOTE, m, y + 4, std::min(W - 2 * m - 200, 760), 44); PlaceId(ID_SOLO_NEW, m, y + 50, 160, 34);
    y += 104; PlaceId(ID_PRIMARY, m, g_choice == ls::kJoin ? jy + jh + 8 : y, 300, 48);
}
void LayoutLobby(int W, int H) {
    int m = 16, top = 10;
    PlaceId(ID_LB_STATUS, m, top + 6, W - 2 * m - 340, 40); PlaceId(ID_LB_UPDATE, W - m - 330, top, 200, 34); PlaceId(ID_LB_LEAVE, W - m - 120, top, 120, 34);
    int y0 = top + 52, c1w = std::max(260, W * 27 / 100), c3w = std::max(300, W * 32 / 100), c2w = W - 4 * m - c1w - c3w, c1 = m, c2 = c1 + c1w + m, c3 = c2 + c2w + m;
    // players | chat | invite above what to play (D1)
    PlaceId(ID_LB_H_PLAYERS, c1, y0, c1w, 20); PlaceId(ID_LB_MEMBERS, c1, y0 + 24, c1w, H - y0 - 24 - 150);
    int vy = H - 120; PlaceId(ID_LB_MUTE, c1, vy, c1w / 2, 26); PlaceId(ID_LB_DEAF, c1 + c1w / 2, vy, c1w / 2, 26); PlaceId(ID_LB_LEVEL, c1, vy + 32, c1w, 8); PlaceId(ID_LB_H_VOICE, c1, vy + 44, c1w, 20);
    PlaceId(ID_LB_READY, c1, H - 48, c1w, 34); PlaceId(ID_LB_START, c1, H - 52, c1w, 42);
    PlaceId(ID_LB_H_CHAT, c2, y0, c2w, 20); PlaceId(ID_LB_LOG, c2, y0 + 24, c2w, H - y0 - 24 - 60); PlaceId(ID_LB_CHAT, c2, H - 50, c2w - 96, 32); PlaceId(ID_LB_SEND, c2 + c2w - 88, H - 52, 88, 36);
    int y = y0; PlaceId(ID_LB_H_INVITE, c3, y, c3w, 20); y += 22;
    for (int i = 0; i < NF_ROWS; ++i) { bool any = !g_fj[i].title.empty(); int rh = any ? FjHeight(g_fjState[i]) : (i == 0 ? 24 : 0); ShowId(ID_LB_FJ0 + i, rh > 0); if (rh) PlaceId(ID_LB_FJ0 + i, c3, y, c3w, rh); y += rh; }      // C1: Steam, Same network, Tailscale, Internet, Windows Firewall: a mark and the result; under a warning its fix
    // the invite: its code and Copy invite on one line; the Steam buttons only in a Steam lobby (they did nothing in a direct one)
    y += 6; PlaceId(ID_LB_INVITE_SHOW, c3, y + 2, c3w - 138, 28); PlaceId(ID_LB_COPY, c3 + c3w - 130, y, 130, 32); y += 40;
    bool steamHost = g_sess.Active() && g_sess.IsHost() && g_sess.SteamMode(); ShowId(ID_LB_STEAM_FRIENDS, steamHost); ShowId(ID_LB_STEAM_SEND, steamHost); ShowId(ID_LB_STEAM_INVITE, steamHost); ShowId(ID_LB_H_STEAMHINT, false);
    if (steamHost) { PlaceId(ID_LB_STEAM_FRIENDS, c3, y + 2, c3w - 268, 300); PlaceId(ID_LB_STEAM_SEND, c3 + c3w - 260, y, 122, 32); PlaceId(ID_LB_STEAM_INVITE, c3 + c3w - 130, y, 130, 32); y += 40; }
    y += 8;
    PlaceId(ID_LB_H_PLAY, c3, y, c3w, 20); y += 26; PlaceId(ID_LB_H_GAME, c3, y + 6, 90, 22); PlaceId(ID_LB_MODE, c3 + 96, y + 2, c3w - 96, 200); y += 38;
    bool race = SendMessageW(LB(ID_LB_MODE), CB_GETCURSEL, 0, 0) == 1;
#ifdef WL_MUT_GOAL_ALWAYS
    race = true;      // (mutant: the race goal shows for co-op too)
#endif
    ShowId(ID_LB_H_GOAL, race); ShowId(ID_LB_GOAL, race); ShowId(ID_LB_H_GOALHINT, race);      // the race goal only for a race (owner, 2026-11-06)
    if (race) { PlaceId(ID_LB_H_GOAL, c3, y + 6, 90, 22); PlaceId(ID_LB_GOAL, c3 + 96, y + 2, c3w - 96, 30); y += 34; PlaceId(ID_LB_H_GOALHINT, c3 + 96, y, c3w - 96, 20); y += 26; }
    PlaceId(ID_LB_NEWGAME, c3, y + 3, 140, 28); PlaceId(ID_LB_PARTS, c3 + 150, y, c3w - 150, 34); y += 40; PlaceId(ID_LB_PARTSINFO, c3 + 150, y, c3w - 150, 20); y += 26;
    PlaceId(ID_LB_RZ_BTN, c3, y, 140, 34); PlaceId(ID_LB_RZ, c3 + 150, y, c3w - 150, 40); y += 44; PlaceId(ID_LB_RESULT, c3, y, c3w, std::max(20, H - y - 8));
}
void BuildLobbyPanel(HWND pg) {
    auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id) { return Make(pg, cls, text, style, 0, 0, 10, 10, id); };
    auto head = [&](const wchar_t* t, int id) { HWND k = mk(L"STATIC", t, SS_LEFT, id); th::MarkMuted(k); return k; };
    mk(L"STATIC", L"", SS_LEFT, ID_LB_STATUS); { HWND ub = mk(L"BUTTON", L"Update the launcher", BS_PUSHBUTTON, ID_LB_UPDATE); ShowWindow(ub, SW_HIDE); } mk(L"BUTTON", L"Leave", BS_PUSHBUTTON, ID_LB_LEAVE);
    head(L"PLAYERS", ID_LB_H_PLAYERS); mk(L"LISTBOX", L"", LBS_OWNERDRAWFIXED | LBS_NOTIFY | WS_VSCROLL | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT, ID_LB_MEMBERS); SendMessageW(LB(ID_LB_MEMBERS), LB_SETITEMHEIGHT, 0, 32);
    mk(L"BUTTON", L"Mute me", BS_AUTOCHECKBOX, ID_LB_MUTE); mk(L"BUTTON", L"Deafen", BS_AUTOCHECKBOX, ID_LB_DEAF); mk(L"STATIC", L"", SS_OWNERDRAW, ID_LB_LEVEL); head(L"Voice mode and keys: Settings.", ID_LB_H_VOICE);
    mk(L"BUTTON", L"Ready", BS_AUTOCHECKBOX, ID_LB_READY); mk(L"BUTTON", L"Start game", BS_DEFPUSHBUTTON, ID_LB_START);
    head(L"CHAT", ID_LB_H_CHAT); mk(L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL, ID_LB_LOG);
    HWND ce = mk(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, ID_LB_CHAT); SetWindowSubclass(ce, ChatEditProc, 3, 0); mk(L"BUTTON", L"Send", BS_PUSHBUTTON, ID_LB_SEND);
    head(L"FRIENDS CAN JOIN BY", ID_LB_H_INVITE); for (int i = 0; i < NF_ROWS; ++i) { g_fjState[i] = NF_OFF; SendMessageW(mk(L"STATIC", L"", SS_OWNERDRAW | SS_NOTIFY, ID_LB_FJ0 + i), WM_SETFONT, (WPARAM)th::Small(), TRUE); }
    mk(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | ES_READONLY, ID_LB_INVITE_SHOW);
    mk(L"BUTTON", L"Copy invite", BS_PUSHBUTTON, ID_LB_COPY); mk(L"BUTTON", L"Steam overlay", BS_PUSHBUTTON, ID_LB_STEAM_INVITE);
    { HWND fc = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, ID_LB_STEAM_FRIENDS); SendMessageW(fc, CB_ADDSTRING, 0, (LPARAM)L"(Steam lobby only)"); SendMessageW(fc, CB_SETCURSEL, 0, 0); }
    mk(L"BUTTON", L"Invite friend", BS_PUSHBUTTON, ID_LB_STEAM_SEND); head(L"Friends join by the invite, or through Steam.", ID_LB_H_STEAMHINT);
    head(L"WHAT TO PLAY  (the host chooses)", ID_LB_H_PLAY); mk(L"STATIC", L"Game", SS_LEFT, ID_LB_H_GAME);
    { HWND mc = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, ID_LB_MODE); SendMessageW(mc, CB_ADDSTRING, 0, (LPARAM)L"Co-op"); SendMessageW(mc, CB_ADDSTRING, 0, (LPARAM)L"Race"); SendMessageW(mc, CB_SETCURSEL, 0, 0); }
    mk(L"STATIC", L"Race goal", SS_LEFT, ID_LB_H_GOAL); mk(L"EDIT", L"panel:9F7F", WS_BORDER | ES_AUTOHSCROLL, ID_LB_GOAL); head(L"panel:ID or count:N", ID_LB_H_GOALHINT);
    HWND ng = mk(L"BUTTON", L"New game", BS_AUTOCHECKBOX, ID_LB_NEWGAME); SendMessageW(ng, BM_SETCHECK, BST_CHECKED, 0); mk(L"BUTTON", L"Parts of the map done...", BS_PUSHBUTTON, ID_LB_PARTS); head(L"", ID_LB_PARTSINFO);
    mk(L"BUTTON", L"Puzzles...", BS_PUSHBUTTON, ID_LB_RZ_BTN); head(L"", ID_LB_RZ);
    head(L"", ID_LB_RESULT); PartsInfo(); RefreshLobbyUi();
}

// ---- Saves and About
void BuildSaves(HWND sp) {
    g_saveList = Make(sp, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 0, 0, 10, 10, ID_SAVELIST, WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(g_saveList, LVS_EX_FULLROWSELECT); th::StyleList(g_saveList);
    const wchar_t* cols[] = {L"Saved", L"Players", L"Progress", L"Lasers", L"Time played"}; int cw[] = {240, 300, 150, 90, 130};      // (the first column holds the save's thumbnail and when it was saved)
    for (int i = 0; i < 5; ++i) { LVCOLUMNW c = {}; c.mask = LVCF_TEXT | LVCF_WIDTH; c.pszText = (LPWSTR)cols[i]; c.cx = cw[i]; ListView_InsertColumn(g_saveList, i, &c); }
    g_saveInfo = Make(sp, L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL, 0, 0, 10, 10, ID_SAVEINFO);
    g_savePic = Make(sp, L"STATIC", L"", SS_BITMAP, 0, 0, 10, 10, ID_SAVEPIC); ShowWindow(g_savePic, SW_HIDE);
    Make(sp, L"BUTTON", L"New game...", BS_DEFPUSHBUTTON, 0, 0, 10, 10, ID_NEWGAME); Make(sp, L"BUTTON", L"Back up", BS_PUSHBUTTON, 0, 0, 10, 10, ID_BACKUP); Make(sp, L"BUTTON", L"Move to backup folder", BS_PUSHBUTTON, 0, 0, 10, 10, ID_TRASH);
    Make(sp, L"BUTTON", L"Open folder", BS_PUSHBUTTON, 0, 0, 10, 10, ID_OPENSAVES); Make(sp, L"BUTTON", L"Refresh", BS_PUSHBUTTON, 0, 0, 10, 10, ID_REFRESH);
}
void LayoutSaves(int W, int H) {
    g_savesW = W; g_savesH = H;
    int m = 16; Place(g_saveList, m, m, W - 2 * m, (H - 80) * 45 / 100); int y = m + (H - 80) * 45 / 100 + 10;
    int picW = g_picRgba.empty() ? 0 : std::min(400, (W - 2 * m) * 2 / 5);      // the selected save's picture to the right of its details
    Place(g_saveInfo, m, y, W - 2 * m - (picW ? picW + 12 : 0), H - y - 60); ApplySavePic(W - m - picW, y, picW, H - y - 60);
    int by = H - 48, x = m; for (auto p : {std::make_pair(ID_NEWGAME, 160), std::make_pair(ID_BACKUP, 120), std::make_pair(ID_TRASH, 220), std::make_pair(ID_OPENSAVES, 140), std::make_pair(ID_REFRESH, 110)}) { PlaceId(p.first, x, by, p.second, 36); x += p.second + 10; }
}
void BuildAbout(HWND pg) {
    HWND t = Make(pg, L"STATIC", (L"The Witness co-op, version " + W(MOD_VERSION)).c_str(), SS_LEFT, 0, 0, 10, 10, ID_UP_VER); SendMessageW(t, WM_SETFONT, (WPARAM)th::Bold(), TRUE);
    Make(pg, L"BUTTON", L"Check for updates", BS_DEFPUSHBUTTON, 0, 0, 10, 10, ID_UP_CHECK); Make(pg, L"BUTTON", L"Update", BS_PUSHBUTTON, 0, 0, 10, 10, ID_UP_APPLY); Make(pg, L"BUTTON", L"Later", BS_PUSHBUTTON, 0, 0, 10, 10, ID_UP_LATER);
    Make(pg, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, 0, 0, 10, 10, ID_UP_STATUS); Make(pg, L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL, 0, 0, 10, 10, ID_UP_NOTES);
    Make(pg, L"BUTTON", L"Check for updates when the launcher starts (once a day at most; nothing about you is sent)", BS_AUTOCHECKBOX, 0, 0, 10, 10, ID_UP_AUTO);
    Make(pg, L"STATIC", L"Updates to offer", SS_LEFT, 0, 0, 10, 10, ID_UP_WHICH);      // full releases only, or pre-releases too (owner, 2026-11-06); the same setting as Settings, Launcher
    Make(pg, L"BUTTON", L"Full releases", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 0, 0, 10, 10, ID_UP_REL); Make(pg, L"BUTTON", L"Pre-releases too (newer, less tested)", BS_AUTORADIOBUTTON, 0, 0, 10, 10, ID_UP_PRE);
    Make(pg, L"BUTTON", L"Quick guide", BS_PUSHBUTTON, 0, 0, 10, 10, ID_GUIDE_BTN); th::MarkMuted(Make(pg, L"STATIC", L"Every co-op key and what it does (hold F1 anywhere in the launcher or the game).", SS_LEFT, 0, 0, 10, 10, ID_ABOUT_GUIDE_TXT));
    Make(pg, L"BUTTON", L"Send diagnostics", BS_PUSHBUTTON, 0, 0, 10, 10, ID_DIAG); th::MarkMuted(Make(pg, L"STATIC", L"Report a problem: a zip of logs and settings on your Desktop (names and addresses replaced, nothing uploaded).", SS_LEFT, 0, 0, 10, 10, ID_ABOUT_DIAG_TXT));
    Make(pg, L"BUTTON", L"Project page", BS_PUSHBUTTON, 0, 0, 10, 10, ID_LINK_HOME); Make(pg, L"BUTTON", L"Release notes", BS_PUSHBUTTON, 0, 0, 10, 10, ID_LINK_NOTES);
    th::MarkMuted(Make(pg, L"STATIC", L"Credits: The Witness is by Thekla, Inc. This co-op mod is a fan project (MIT licence), not made or endorsed by Thekla. Steam features go through the game's own Steam library.", SS_LEFT, 0, 0, 10, 10, ID_CREDITS));
    Make(pg, L"STATIC", L"Game folder", SS_LEFT, 0, 0, 10, 10, ID_FOLDER_LABEL); Make(pg, L"EDIT", g_dir.c_str(), WS_BORDER | ES_AUTOHSCROLL | ES_READONLY, 0, 0, 10, 10, ID_GAMEDIR); Make(pg, L"BUTTON", L"Browse...", BS_PUSHBUTTON, 0, 0, 10, 10, ID_BROWSE);
}
bool g_upNotes;      // About shows the release notes box (an update found or just applied)
void LayoutAbout(int W, int H) {
    (void)H; int m = 24, y = 16, cw = std::min(W - 2 * m, 1000);
    PlaceId(ID_UP_VER, m, y, cw, 28); y += 40; PlaceId(ID_UP_CHECK, m, y, 200, 36); PlaceId(ID_UP_APPLY, m + 210, y, 110, 36); PlaceId(ID_UP_LATER, m + 330, y, 100, 36); y += 44;
    PlaceId(ID_UP_STATUS, m, y, cw, 22); y += 26; if (g_upNotes) { PlaceId(ID_UP_NOTES, m, y, cw, 90); y += 98; } ShowId(ID_UP_NOTES, g_upNotes); PlaceId(ID_UP_AUTO, m, y, cw, 26); y += 34;      // (the release notes only while there are some: owner, 2026-11-06)
    PlaceId(ID_UP_WHICH, m, y + 2, 160, 22); PlaceId(ID_UP_REL, m + 170, y, 170, 26); PlaceId(ID_UP_PRE, m + 350, y, std::max(10, cw - 350), 26); y += 44;
    PlaceId(ID_GUIDE_BTN, m, y, 200, 36); PlaceId(ID_ABOUT_GUIDE_TXT, m + 214, y + 8, cw - 214, 22); y += 46;
    PlaceId(ID_DIAG, m, y, 200, 36); PlaceId(ID_ABOUT_DIAG_TXT, m + 214, y + 8, cw - 214, 22); y += 46;
    PlaceId(ID_LINK_HOME, m, y, 200, 36); PlaceId(ID_LINK_NOTES, m + 210, y, 200, 36); y += 50;
    PlaceId(ID_FOLDER_LABEL, m, y + 6, 120, 22); PlaceId(ID_GAMEDIR, m + 130, y + 2, cw - 130 - 120, 30); PlaceId(ID_BROWSE, m + cw - 110, y, 110, 34); y += 50;
    PlaceId(ID_CREDITS, m, y, cw, 44);
}

// ---- Randomizers: the built-in randomizers (common/randomizers.h, proxy/randomizer). The player's pick is kept in the cfg (randomizer_pick: the spec with
// its seed; randomizer_new_seed: a new seed for each new game, the default) and the game gets the spec it starts with as randomizer= (RzForLaunch). In a
// lobby the host's choice is everyone's: a guest's page shows it and cannot change it. Everyone whose work it is is named here, with their links.
uint32_t g_rzRound;                      // this round's seed for "a new seed for each new game" (the lobby shows it before the start; a new one after each game)
bool g_rzLoading;
uint32_t FreshSeed() { LARGE_INTEGER q; QueryPerformanceCounter(&q); uint64_t x = (uint64_t)q.QuadPart ^ (GetTickCount64() << 21) ^ ((uint64_t)GetCurrentProcessId() << 40); x ^= x >> 33; x *= 0xff51afd7ed558ccdULL; x ^= x >> 33; return 1 + (uint32_t)(x % 999999); }      // 1..999999: easy to read out to a friend
// "Your puzzles" (the puzzle editor, launcher/editor.cpp): the pick names a set of the player's library (randomizer_set); the spec carries the hash of
// that set as it is now, whose copy is written to the puzzles folder for the game (and sent by the lobby to the others).
std::string RzSetName() { return CfgGet("randomizer_set", ""); }
std::string RzSetHash(const std::string& name) {
    wpz::Set set; if (name.empty() || !ped::LoadSet(g_data, name, &set)) return "";
    return ped::PublishSet(g_data, set);
}
wrand::Spec RzPick() {
    std::string pick = CfgGet("randomizer_pick", "off");
    if (pick.rfind("custom", 0) == 0) { std::string h = RzSetHash(RzSetName()); pick = h.empty() ? "off" : "custom;set:" + h; }
    return wrand::ParseSpec(pick);
}
bool RzNewSeed() { return CfgGet("randomizer_new_seed", "1") != "0"; }
std::string RzNewestSave() {      // the randomizer the newest save was made with ("" none): a continued game keeps it
    std::vector<wl::SaveInfo> v = wl::ListSaves(SaveFolder(LoadCfg()));
    return !v.empty() && v[0].hasMeta ? v[0].meta.randomizer : "";
}
std::string RzForLaunch(bool newGame) {
    if (!g_rzRound) g_rzRound = FreshSeed();
    return wl::RandomizerForLaunch(wrand::SpecText(RzPick()), RzNewSeed(), newGame, newGame ? "" : RzNewestSave(), g_rzRound);
}
bool RzGuest() { return g_sess.Active() && !g_sess.IsHost(); }
// The Witness Trainer's options (each player's own, unlike the randomizer: a guest sets them too): cfg trainer_*, read by proxy/trainer.cpp.
const int kSpeeds[] = {100, 125, 150, 200, 300};
const int kFovs[] = {0, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150};
std::string RzCreditsText() {
    std::string t;
    for (const auto& i : wrand::Randomizers()) {
        t += std::string(i.name) + "\r\n";
        for (const auto& c : i.credits) t += std::string("    ") + c.who + ": " + c.what + "\r\n" + (*c.link ? std::string("        ") + c.link + "\r\n" : std::string());
    }
    t += "Witness Trainer (its co-op options)\r\n    jbzdarkid: the Witness Trainer\r\n        https://github.com/jbzdarkid/witness-trainer\r\n";
    t += "\r\n" + std::string(wrand::RandomizerSourceCredit()) + "\r\n\r\nThe authors made these for everyone to use; they did not make or check this co-op version, so please report problems with it to this mod, not to them.\r\n\r\n";
    for (const char* c = kNotices; *c; ++c) { if (*c == '\n' && (c == kNotices || c[-1] != '\r')) t += '\r'; t += *c; }      // the licences in full (THIRD-PARTY-NOTICES.txt)
    return t;
}
void BuildRandomizers(HWND pg) {
    HWND t = Make(pg, L"STATIC", L"Randomized puzzles", SS_LEFT, 0, 0, 10, 10, ID_RZ_TITLE); SendMessageW(t, WM_SETFONT, (WPARAM)th::Bold(), TRUE);
    th::MarkMuted(Make(pg, L"STATIC", L"New puzzles for the island, made when the game starts. In a lobby the host's choice is everyone's, so all of you get the same puzzles. A continued game keeps the puzzles it was started with.", SS_LEFT, 0, 0, 10, 10, ID_RZ_INTRO));
    HWND mc = Make(pg, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 0, 10, 10, ID_RZ_MODE);
    SendMessageW(mc, CB_ADDSTRING, 0, (LPARAM)L"The game's own puzzles");
    for (const auto& i : wrand::Randomizers()) SendMessageW(mc, CB_ADDSTRING, 0, (LPARAM)W(i.name).c_str());
    th::MarkMuted(Make(pg, L"STATIC", L"", SS_LEFT, 0, 0, 10, 10, ID_RZ_BLURB)); Make(pg, L"STATIC", L"", SS_LEFT, 0, 0, 10, 10, ID_RZ_WARN); th::MarkMuted(Make(pg, L"STATIC", L"", SS_LEFT, 0, 0, 10, 10, ID_RZ_BY));
    Make(pg, L"BUTTON", L"A new seed for each new game", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 0, 10, 10, ID_RZ_NEWSEED);
    Make(pg, L"STATIC", L"Seed", SS_LEFT, 0, 0, 10, 10, ID_RZ_SEED_LABEL); Make(pg, L"EDIT", L"", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, 0, 0, 10, 10, ID_RZ_SEED);
    Make(pg, L"BUTTON", L"New seed", BS_PUSHBUTTON, 0, 0, 10, 10, ID_RZ_ROLL);
    Make(pg, L"STATIC", L"Puzzle set", SS_LEFT, 0, 0, 10, 10, ID_RZ_SET_LABEL); Make(pg, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 0, 10, 10, ID_RZ_SET);
    Make(pg, L"BUTTON", L"Open the puzzle editor", BS_PUSHBUTTON, 0, 0, 10, 10, ID_RZ_EDIT);
    Make(pg, L"BUTTON", L"Shuffle which panel is where", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 0, 10, 10, ID_RZ_SHUFFLE);
    Make(pg, L"BUTTON", L"Colour-blind friendly colours", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 0, 10, 10, ID_RZ_COLOR);
    Make(pg, L"BUTTON", L"Faster doors, bridges and elevators", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 0, 10, 10, ID_RZ_FAST);
    Make(pg, L"STATIC", L"", SS_LEFT, 0, 0, 10, 10, ID_RZ_STATUS);
    HWND ht = Make(pg, L"STATIC", L"WITNESS TRAINER  (by jbzdarkid; your own game only)", SS_LEFT, 0, 0, 10, 10, ID_RZ_H_TRAINER); SendMessageW(ht, WM_SETFONT, (WPARAM)th::Bold(), TRUE);
    Make(pg, L"STATIC", L"Walking speed", SS_LEFT, 0, 0, 10, 10, ID_RZ_TR_SPEED_LABEL);
    HWND sc = Make(pg, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 0, 10, 10, ID_RZ_TR_SPEED);
    for (int v : kSpeeds) SendMessageW(sc, CB_ADDSTRING, 0, (LPARAM)(v == 100 ? std::wstring(L"The game's own") : std::to_wstring(v) + L" %").c_str());
    Make(pg, L"STATIC", L"Field of view", SS_LEFT, 0, 0, 10, 10, ID_RZ_TR_FOV_LABEL);
    HWND fc = Make(pg, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 0, 10, 10, ID_RZ_TR_FOV);
    for (int v : kFovs) SendMessageW(fc, CB_ADDSTRING, 0, (LPARAM)(v == 0 ? std::wstring(L"The game's own") : std::to_wstring(v) + L" degrees").c_str());
    Make(pg, L"BUTTON", L"Noclip key: fly through walls", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 0, 10, 10, ID_RZ_TR_NOCLIP);
    Make(pg, L"BUTTON", L"No time limit in the challenge", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 0, 10, 10, ID_RZ_TR_CHALLENGE);
    Make(pg, L"BUTTON", L"Show the environmental puzzles (overlay)", BS_AUTOCHECKBOX | WS_TABSTOP, 0, 0, 10, 10, ID_RZ_TR_EPS);
    th::MarkMuted(Make(pg, L"STATIC", L"", SS_LEFT, 0, 0, 10, 10, ID_RZ_TR_NOTE));
    HWND h = Make(pg, L"STATIC", L"CREDITS", SS_LEFT, 0, 0, 10, 10, ID_RZ_H_CREDITS); SendMessageW(h, WM_SETFONT, (WPARAM)th::Bold(), TRUE);
    Make(pg, L"EDIT", W(RzCreditsText()).c_str(), WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL, 0, 0, 10, 10, ID_RZ_CREDITS);
    Make(pg, L"BUTTON", L"The randomizer's page", BS_PUSHBUTTON, 0, 0, 10, 10, ID_RZ_LINK_MODE); Make(pg, L"BUTTON", L"The Witness Trainer's page", BS_PUSHBUTTON, 0, 0, 10, 10, ID_RZ_LINK_TRAINER);
}
void LayoutRandomizers(int W, int H) {
    int m = 24, y = 14, cw = std::min(W - 2 * m, 1180), left = std::min(600, cw * 51 / 100), rx = m + left + 28, rw = cw - left - 28;
    PlaceId(ID_RZ_TITLE, m, y, cw, 26); y += 30; PlaceId(ID_RZ_INTRO, m, y, cw, 40); y += 46;
    int ly = y;
    PlaceId(ID_RZ_MODE, m, ly, left, 400); ly += 40;
    PlaceId(ID_RZ_BLURB, m, ly, left, 40); ly += 42; PlaceId(ID_RZ_BY, m, ly, left, 22); ly += 26; PlaceId(ID_RZ_WARN, m, ly, left, 40); ly += 46;      // (the author's warning, often none, last)
    PlaceId(ID_RZ_NEWSEED, m, ly, left, 26); ly += 32;
    PlaceId(ID_RZ_SEED_LABEL, m, ly + 6, 60, 22); PlaceId(ID_RZ_SEED, m + 64, ly + 2, 140, 30); PlaceId(ID_RZ_ROLL, m + 214, ly, 120, 34);
    PlaceId(ID_RZ_SET_LABEL, m, ly - 26, 90, 22); PlaceId(ID_RZ_SET, m + 94, ly - 30, std::max(120, left - 94), 300); PlaceId(ID_RZ_EDIT, m, ly + 8, std::min(left, 260), 34); ly += 42;
    PlaceId(ID_RZ_SHUFFLE, m, ly, left, 26); ly += 30; PlaceId(ID_RZ_COLOR, m, ly, left, 26); ly += 30; PlaceId(ID_RZ_FAST, m, ly, left, 26); ly += 34;
    PlaceId(ID_RZ_STATUS, m, ly, left, std::max(22, H - ly - 10));
    int ry = y;
    PlaceId(ID_RZ_H_TRAINER, rx, ry, rw, 22); ry += 28;
    PlaceId(ID_RZ_TR_SPEED_LABEL, rx, ry + 6, 130, 22); PlaceId(ID_RZ_TR_SPEED, rx + 136, ry, std::min(220, rw - 136), 300); ry += 38;
    PlaceId(ID_RZ_TR_FOV_LABEL, rx, ry + 6, 130, 22); PlaceId(ID_RZ_TR_FOV, rx + 136, ry, std::min(220, rw - 136), 300); ry += 40;
    PlaceId(ID_RZ_TR_NOCLIP, rx, ry, rw, 26); ry += 30; PlaceId(ID_RZ_TR_CHALLENGE, rx, ry, rw, 26); ry += 30; PlaceId(ID_RZ_TR_EPS, rx, ry, rw, 26); ry += 30;
    PlaceId(ID_RZ_TR_NOTE, rx, ry, rw, 40); ry += 46;
    PlaceId(ID_RZ_H_CREDITS, rx, ry, rw, 22); ry += 26;
    PlaceId(ID_RZ_CREDITS, rx, ry, rw, std::max(80, H - ry - 52)); ry = H - 44;
    int bw = (rw - 10) / 2; PlaceId(ID_RZ_LINK_MODE, rx, ry, bw, 34); PlaceId(ID_RZ_LINK_TRAINER, rx + bw + 10, ry, bw, 34);
}
std::string RzShownSet() {      // the set the page shows (the library's names, in the combo box)
    int k = (int)SendMessageW(C(ID_RZ_SET), CB_GETCURSEL, 0, 0); auto names = ped::ListSets(g_data);
    return k >= 0 && k < (int)names.size() ? names[k] : (names.empty() ? std::string() : names[0]);
}
void RzFillSets() {
    bool was = g_rzLoading; g_rzLoading = true;
    HWND c = C(ID_RZ_SET); auto names = ped::ListSets(g_data); std::string want = RzSetName(); int k = 0;
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < names.size(); i++) { SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)W(names[i]).c_str()); if (names[i] == want) k = (int)i; }
    if (names.empty()) SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)L"(none yet: open the editor)");
    SendMessageW(c, CB_SETCURSEL, k, 0); g_rzLoading = was;
}
std::string RzSummary(const wrand::Spec& s) {      // as wrand::SpecSummary, with the set's name when it is the set picked here
    if (s.mode != wrand::kCustom) return wrand::SpecSummary(s);
    std::string name = RzSetName(); wpz::Set set;
    if (!name.empty() && ped::LoadSet(g_data, name, &set) && wpz::Hash(set) == s.set) return "your puzzles \"" + name + "\" (" + std::to_string(set.puzzles.size()) + (set.puzzles.size() == 1 ? " puzzle)" : " puzzles)");
    return wrand::SpecSummary(s);
}
const wrand::Info* RzShownInfo() { int k = (int)SendMessageW(C(ID_RZ_MODE), CB_GETCURSEL, 0, 0); return k >= 1 && k <= (int)wrand::Randomizers().size() ? &wrand::Randomizers()[k - 1] : nullptr; }
wrand::Spec RzFromUi() {      // what the page shows, as a spec
    wrand::Spec s; const wrand::Info* inf = RzShownInfo(); s.mode = inf ? inf->mode : wrand::kOff;
    unsigned long seed = strtoul(EditText(C(ID_RZ_SEED)).c_str(), nullptr, 10); s.seed = (uint32_t)std::min<unsigned long>(seed, 0x7FFFFFFFul);
    s.shuffle = SendMessageW(C(ID_RZ_SHUFFLE), BM_GETCHECK, 0, 0) == BST_CHECKED; s.colorblind = SendMessageW(C(ID_RZ_COLOR), BM_GETCHECK, 0, 0) == BST_CHECKED; s.fast = SendMessageW(C(ID_RZ_FAST), BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (s.mode == wrand::kCustom) { s.seed = 0; s.set = RzSetHash(RzShownSet()); if (s.set.empty()) s.mode = wrand::kOff; return s; }
    if (!s.on()) { wrand::Spec keep = RzPick(); keep.mode = wrand::kOff; return keep; }      // (off: the options are kept for the next time one is chosen)
    return s;
}
void RzShow(const wrand::Spec& s, bool newSeed) {      // a spec into the page's controls
    g_rzLoading = true;
    int k = 0; for (size_t i = 0; i < wrand::Randomizers().size(); ++i) if (wrand::Randomizers()[i].mode == s.mode) k = (int)i + 1;
    SendMessageW(C(ID_RZ_MODE), CB_SETCURSEL, k, 0);
    SetWindowTextW(C(ID_RZ_SEED), std::to_wstring(s.seed).c_str());
    SendMessageW(C(ID_RZ_NEWSEED), BM_SETCHECK, newSeed ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(C(ID_RZ_SHUFFLE), BM_SETCHECK, s.shuffle ? BST_CHECKED : BST_UNCHECKED, 0); SendMessageW(C(ID_RZ_COLOR), BM_SETCHECK, s.colorblind ? BST_CHECKED : BST_UNCHECKED, 0); SendMessageW(C(ID_RZ_FAST), BM_SETCHECK, s.fast ? BST_CHECKED : BST_UNCHECKED, 0);
    g_rzLoading = false;
}
void RzTrainerShow() {      // the cfg's trainer_* into the Trainer's controls
    g_rzLoading = true; wl::CfgFile c = LoadCfg();
    int sp = atoi(c.Get("trainer_speed", "100").c_str()), fov = atoi(c.Get("trainer_fov", "0").c_str()), si = 0, fi = 0;
    for (int i = 0; i < (int)(sizeof kSpeeds / sizeof *kSpeeds); ++i) if (kSpeeds[i] == sp) si = i;
    for (int i = 0; i < (int)(sizeof kFovs / sizeof *kFovs); ++i) if (kFovs[i] == fov) fi = i;
    SendMessageW(C(ID_RZ_TR_SPEED), CB_SETCURSEL, si, 0); SendMessageW(C(ID_RZ_TR_FOV), CB_SETCURSEL, fi, 0);
    SendMessageW(C(ID_RZ_TR_NOCLIP), BM_SETCHECK, c.Get("trainer_noclip", "0") == "1" ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(C(ID_RZ_TR_CHALLENGE), BM_SETCHECK, c.Get("trainer_no_challenge_limit", "0") == "1" ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(C(ID_RZ_TR_EPS), BM_SETCHECK, c.Get("trainer_ep_overlay", "0") == "1" ? BST_CHECKED : BST_UNCHECKED, 0);
    std::string key = c.Get("key_noclip", "N"); SetWindowTextW(C(ID_RZ_TR_NOTE), W("Noclip key: " + key + " (Settings, Controls). These change only your own game; they apply when the game starts.").c_str());
    g_rzLoading = false;
}
void RzTrainerSave() {
    if (g_rzLoading) return;
    int si = (int)SendMessageW(C(ID_RZ_TR_SPEED), CB_GETCURSEL, 0, 0), fi = (int)SendMessageW(C(ID_RZ_TR_FOV), CB_GETCURSEL, 0, 0);
    auto put = [&](wl::CfgFile& c) {
        c.Set("trainer_speed", std::to_string(si >= 0 && si < (int)(sizeof kSpeeds / sizeof *kSpeeds) ? kSpeeds[si] : 100)); c.Set("trainer_fov", std::to_string(fi >= 0 && fi < (int)(sizeof kFovs / sizeof *kFovs) ? kFovs[fi] : 0));
        c.Set("trainer_noclip", SendMessageW(C(ID_RZ_TR_NOCLIP), BM_GETCHECK, 0, 0) == BST_CHECKED ? "1" : "0");
        c.Set("trainer_no_challenge_limit", SendMessageW(C(ID_RZ_TR_CHALLENGE), BM_GETCHECK, 0, 0) == BST_CHECKED ? "1" : "0");
        c.Set("trainer_ep_overlay", SendMessageW(C(ID_RZ_TR_EPS), BM_GETCHECK, 0, 0) == BST_CHECKED ? "1" : "0");
    };
    if (g_game && !g_cfgBackup.empty()) { wl::CfgFile c = wl::CfgFile::Parse(g_cfgBackup); put(c); g_cfgBackup = c.Text(); }
    else { wl::CfgFile c = LoadCfg(); put(c); WriteFile8(CfgPath(), c.Text()); }
}
void RzRefresh() {      // what may be changed, and what the next game gets
    if (!C(ID_RZ_STATUS)) return;
    bool guest = RzGuest(), host = g_sess.Active() && g_sess.IsHost();
    std::string status;
    if (guest) {
        wrand::Spec hs = wrand::ParseSpec(g_sess.State().settings.randomizer);
        RzShow(hs, false);
        status = "The host chooses the puzzles for this lobby: " + wrand::SpecSummary(hs) + ".";
    } else {
        wrand::Spec pick = RzPick();
        bool ng = !host || SendMessageW(LB(ID_LB_NEWGAME), BM_GETCHECK, 0, 0) == BST_CHECKED;
        wrand::Spec next = wrand::ParseSpec(RzForLaunch(true));
        const wrand::Info* pi = wrand::FindRandomizer(pick.mode);
        std::string cont = RzNewestSave();
        status = pick.on() ? "A new game gets " + RzSummary(next) + (RzNewSeed() && pi && pi->seeded ? " (a new seed each game; this one is shown so you can tell a friend)." : ".") : "A new game has the game's own puzzles.";
        status += "\r\nContinuing keeps your newest save's puzzles: " + (wrand::ParseSpec(cont).on() ? wrand::SpecSummary(wrand::ParseSpec(cont)) : std::string("the game's own")) + ".";
        if (host) status += ng ? "\r\nEveryone in your lobby gets this new game's puzzles." : "\r\nYour lobby continues your newest save: everyone gets its puzzles.";
    }
    SetWindowTextW(C(ID_RZ_STATUS), W(status).c_str());
    const wrand::Info* inf = RzShownInfo(); bool can = !guest, on = inf != nullptr;
    SetWindowTextW(C(ID_RZ_BLURB), inf ? W(inf->blurb).c_str() : L"The puzzles the game came with.");
    SetWindowTextW(C(ID_RZ_WARN), inf && *inf->warning ? W(inf->warning).c_str() : L"");
    SetWindowTextW(C(ID_RZ_BY), inf ? W("By " + wrand::CreditNames(inf->mode) + ".").c_str() : L"");
    EnableWindow(C(ID_RZ_MODE), can);
    bool newSeed = SendMessageW(C(ID_RZ_NEWSEED), BM_GETCHECK, 0, 0) == BST_CHECKED, seeded = on && inf->seeded;
    EnableWindow(C(ID_RZ_NEWSEED), can && seeded); EnableWindow(C(ID_RZ_SEED), can && seeded && !newSeed); EnableWindow(C(ID_RZ_ROLL), can && seeded);
    EnableWindow(C(ID_RZ_SHUFFLE), can && on && inf->canShuffle); EnableWindow(C(ID_RZ_COLOR), can && on && inf->canColorblind); EnableWindow(C(ID_RZ_FAST), can && on && inf->canFast);
    EnableWindow(C(ID_RZ_LINK_MODE), on && inf->credits.size() && *inf->credits[0].link);
    bool custom = on && inf->mode == wrand::kCustom;
    for (int id : {ID_RZ_NEWSEED, ID_RZ_SEED_LABEL, ID_RZ_SEED, ID_RZ_ROLL}) ShowWindow(C(id), custom ? SW_HIDE : SW_SHOW);
    for (int id : {ID_RZ_SET_LABEL, ID_RZ_SET, ID_RZ_EDIT}) ShowWindow(C(id), custom ? SW_SHOW : SW_HIDE);
    for (int id : {ID_RZ_SHUFFLE, ID_RZ_COLOR, ID_RZ_FAST}) ShowWindow(C(id), custom ? SW_HIDE : SW_SHOW);      // (none of these apply to your own puzzles)
    EnableWindow(C(ID_RZ_SET), can && custom); EnableWindow(C(ID_RZ_EDIT), true);
    if (newSeed && !guest && seeded) { g_rzLoading = true; SetWindowTextW(C(ID_RZ_SEED), std::to_wstring(g_rzRound ? g_rzRound : (g_rzRound = FreshSeed())).c_str()); g_rzLoading = false; }
    std::string lobbyLine = g_sess.Active() ? wrand::SpecSummary(wrand::ParseSpec(g_sess.State().settings.randomizer)) : "";
    SetWindowTextW(LB(ID_LB_RZ), W(lobbyLine.empty() ? std::string() : "Puzzles: " + lobbyLine).c_str());
    EnableWindow(LB(ID_LB_RZ_BTN), true);
}
void RzSave() {      // the page into the cfg (during a lobby game: into the settings that are put back when it ends), then the lobby's settings
    if (g_rzLoading || RzGuest()) return;
    wrand::Spec s = RzFromUi(); bool newSeed = SendMessageW(C(ID_RZ_NEWSEED), BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (newSeed && s.on()) { wrand::Spec keep = RzPick(); s.seed = keep.seed; }      // (the typed seed is kept for when "a new seed" is turned off)
    std::string setName = s.mode == wrand::kCustom ? RzShownSet() : RzSetName();
    auto put = [&](wl::CfgFile& c) { c.Set("randomizer_pick", s.on() ? wrand::SpecText(s) : "off"); c.Set("randomizer_new_seed", newSeed ? "1" : "0"); if (!setName.empty()) c.Set("randomizer_set", setName); };
    if (g_game && !g_cfgBackup.empty()) { wl::CfgFile c = wl::CfgFile::Parse(g_cfgBackup); put(c); g_cfgBackup = c.Text(); }
    else { wl::CfgFile c = LoadCfg(); put(c); WriteFile8(CfgPath(), c.Text()); }
    PushSettings(); RzRefresh();
}
void RzLoad() { RzFillSets(); RzShow(RzPick(), RzNewSeed()); RzTrainerShow(); RzRefresh(); }
void RzSetsChanged() { RzFillSets(); if (RzShownInfo() && RzShownInfo()->mode == wrand::kCustom) RzSave(); else RzRefresh(); }      // the editor changed the library: the pick's hash follows
void RzCommand(int id, int code) {
    if (id == ID_RZ_LINK_MODE || id == ID_RZ_LINK_TRAINER) {
        if (code != BN_CLICKED) return;
        const wrand::Info* inf = RzShownInfo();
        std::string url = id == ID_RZ_LINK_TRAINER ? "https://github.com/jbzdarkid/witness-trainer" : inf && inf->credits.size() ? inf->credits[0].link : "";
        if (!url.empty()) ShellExecuteW(nullptr, L"open", W(url).c_str(), nullptr, nullptr, SW_SHOW);
        return;
    }
    if (id == ID_RZ_TR_SPEED || id == ID_RZ_TR_FOV) { if (code == CBN_SELCHANGE) RzTrainerSave(); return; }
    if (id == ID_RZ_TR_NOCLIP || id == ID_RZ_TR_CHALLENGE || id == ID_RZ_TR_EPS) { if (code == BN_CLICKED) RzTrainerSave(); return; }
    if (id == ID_RZ_ROLL) { if (code != BN_CLICKED || RzGuest()) return; g_rzRound = FreshSeed(); g_rzLoading = true; SetWindowTextW(C(ID_RZ_SEED), std::to_wstring(g_rzRound).c_str()); g_rzLoading = false;
        if (SendMessageW(C(ID_RZ_NEWSEED), BM_GETCHECK, 0, 0) != BST_CHECKED) RzSave(); else { PushSettings(); RzRefresh(); } return; }
    if (id == ID_RZ_SEED) { if (code == EN_CHANGE) RzSave(); return; }
    if (id == ID_RZ_MODE) { if (code == CBN_SELCHANGE) RzSave(); return; }
    if (id == ID_RZ_SET) { if (code == CBN_SELCHANGE) RzSave(); return; }
    if (id == ID_RZ_EDIT) { if (code == BN_CLICKED) ped::OpenEditor(g_main, g_data, RzShownSet(), RzSetsChanged); return; }
    if (code == BN_CLICKED) RzSave();
}
void RzSetFromCommand(const std::string& arg) {      // tests: randomizer <spec> [newseed 0|1]
    std::string spec = arg.substr(0, arg.find(' ')); bool newSeed = arg.find("newseed 1") != std::string::npos;
    RzShow(wrand::ParseSpec(spec), newSeed); RzSave();
}
std::string RzStat() {
    return "randomizers pick " + wrand::SpecText(RzPick()) + " newseed " + (RzNewSeed() ? "1" : "0") + " round " + std::to_string(g_rzRound) + " guest " + (RzGuest() ? "1" : "0") +
           " seededit " + (IsWindowEnabled(C(ID_RZ_SEED)) ? "1" : "0") + " shuffleedit " + (IsWindowEnabled(C(ID_RZ_SHUFFLE)) ? "1" : "0") + " offedit " + (IsWindowEnabled(C(ID_RZ_MODE)) ? "1" : "0") +
           " trainer " + LoadCfg().Get("trainer_speed", "100") + "," + LoadCfg().Get("trainer_fov", "0") + "," + LoadCfg().Get("trainer_noclip", "0") + " set " + ped::SafeName(RzSetName()) + "\n" +
           ped::EditorStat() + "\n";
}

// ---- the update banner (Play) and the About block follow the updater's state
// which updates are offered, shown the same on About and in Settings (auto, an installed pre-release's default, shows as what it means: pre-releases too)
void SyncUpdChoice() {
    bool pre = g_upd.Get().offersPre; auto set = [](HWND h, bool on) { if (h && (SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED) != on) SendMessageW(h, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0); };
    set(C(ID_UP_REL), !pre); set(C(ID_UP_PRE), pre);
    if (Field* f = ByKey("upd:pre")) if (f->h && SendMessageW(f->h, CB_GETCURSEL, 0, 0) != (pre ? 1 : 0)) SendMessageW(f->h, CB_SETCURSEL, pre ? 1 : 0, 0);
}
void UpdateUi() {
    SyncUpdChoice();
    static std::string lastMsg; static int lastState = -1; static bool lastRefused, lastLobby;
    wup::Snapshot s = g_upd.Get(); bool refused = g_sess.Active() && !g_sess.IsHost() && g_sess.Refused() == wlobby::kVersionMismatch, lobby = g_sess.Active();
    if (s.message == lastMsg && (int)s.state == lastState && refused == lastRefused && lobby == lastLobby) return; lastMsg = s.message; lastState = (int)s.state; lastRefused = refused; lastLobby = lobby;
    bool avail = s.state == wup::kAvailable;
    SetWindowTextW(C(ID_UP_STATUS), W(s.message).c_str()); SetWindowTextW(C(ID_UP_NOTES), W(avail || s.state == wup::kApplied ? s.tag + "\r\n" + std::string(s.notes) : "").c_str());
    { bool notes = avail || s.state == wup::kApplied; if (notes != g_upNotes) { g_upNotes = notes; LayoutAll(); } }
    for (int id : {ID_UP_APPLY, ID_UP_LATER}) ShowId(id, avail && !s.managed);      // (a mod manager's install: the manager updates it, the message says so)
    EnableWindow(C(ID_UP_CHECK), !g_upd.Busy()); SendMessageW(C(ID_UP_AUTO), BM_SETCHECK, s.autoCheck ? BST_CHECKED : BST_UNCHECKED, 0);
    if (Field* f = ByKey("upd:auto")) SendMessageW(f->h, BM_SETCHECK, s.autoCheck ? BST_CHECKED : BST_UNCHECKED, 0);
    std::string first = s.notes.substr(0, s.notes.find_first_of("\r\n")); if (first.size() > 60) first = first.substr(0, 57) + "...";
    SetWindowTextW(C(ID_UP_BANNER), avail ? W("Update available: " + s.tag + (s.managed ? ". Installed by Vortex: update it in Vortex" : first.empty() ? "" : " (" + first + ")")).c_str() : L"");
    for (int id : {ID_UP_BANNER, ID_UP_WHATSNEW}) ShowId(id, avail && !lobby);      // F3: a banner on Play, never over a lobby
    for (int id : {ID_UP_BANNER_APPLY, ID_UP_BANNER_LATER}) ShowId(id, avail && !lobby && !s.managed);
    ShowId(ID_LB_UPDATE, refused);
}

int CurTab(); void SelectTab(int t); void ShowSection(int s); void ChooseTile(ls::Choice c); void PadPoll(); void OnFieldChanged(Field& f); void StartCapture(Field& f); void EndCapture(unsigned vk);
extern ls::PadState g_injected; extern bool g_injectOn;
void PollCommands() {      // tests: one command per line in launcher_cmd.txt, taken whole
    if (g_cmdFile.empty() || !Exists(g_cmdFile)) return; std::string t = ReadFile8(g_cmdFile); DeleteFileW(g_cmdFile.c_str()); size_t at = 0;
    while (at < t.size()) { size_t e = t.find('\n', at); if (e == std::string::npos) e = t.size(); std::string line = t.substr(at, e - at); at = e + 1; while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back(); if (line.empty()) continue;
        std::string cmd = line.substr(0, line.find(' ')), arg = line.find(' ') == std::string::npos ? "" : line.substr(line.find(' ') + 1);
        if (cmd == "host") { if (!arg.empty()) if (Field* f = ByKey("cfg:lobby_port")) { SetVal(*f, arg); std::wstring m; ApplyAll(&m); } HostLobby(); }
        else if (cmd == "join") { SetWindowTextW(LB(ID_LB_INVITE), W(arg).c_str()); JoinLobby(); }
        else if (cmd == "joinrow") { for (size_t i = 0; i < g_rows.size(); ++i) if (arg == g_rows[i].name) { SetWindowTextW(LB(ID_LB_INVITE), L""); SendMessageW(LB(ID_JOIN_LIST), LB_SETCURSEL, i, 0); SendMessageW(g_main, WM_COMMAND, ID_PRIMARY, 0); break; } }
        else if (cmd == "steamhost") HostSteamLobby();
        else if (cmd == "steamjoin") { uint64_t id = 0; if (steamids::ParseLobby(arg, &id)) JoinSteamLobby(id); else LobbyStatus(L"Not a Steam lobby number."); }
        else if (cmd == "leave") LeaveLobby();
        else if (cmd == "ready") { SendMessageW(LB(ID_LB_READY), BM_SETCHECK, arg == "1" ? BST_CHECKED : BST_UNCHECKED, 0); g_sess.SetReady(arg == "1"); }
        else if (cmd == "chat") { SetWindowTextW(LB(ID_LB_CHAT), W(arg).c_str()); SendChatLine(); }
        else if (cmd == "mode") { SendMessageW(LB(ID_LB_MODE), CB_SETCURSEL, arg == "race" ? 1 : 0, 0); PushSettings(); LayoutAll(); }
        else if (cmd == "goal") { SetWindowTextW(LB(ID_LB_GOAL), W(arg).c_str()); PushSettings(); }
        else if (cmd == "newgame") { SendMessageW(LB(ID_LB_NEWGAME), BM_SETCHECK, arg == "1" ? BST_CHECKED : BST_UNCHECKED, 0); PushSettings(); }
        else if (cmd == "parts") { g_lobbySpec = arg; PartsInfo(); PushSettings(); }
        else if (cmd == "start") { PushSettings(); if (!g_sess.Start()) LobbyStatus(L"Everybody has to be ready first."); }
        else if (cmd == "mute") { SendMessageW(LB(ID_LB_MUTE), BM_SETCHECK, arg == "1" ? BST_CHECKED : BST_UNCHECKED, 0); g_voice.SetMuted(arg == "1"); }
        else if (cmd == "deaf") { SendMessageW(LB(ID_LB_DEAF), BM_SETCHECK, arg == "1" ? BST_CHECKED : BST_UNCHECKED, 0); g_voice.SetDeaf(arg == "1"); }
        else if (cmd == "voicestart") { g_voiceWanted = true; StartVoice(); } else if (cmd == "voicestop") { g_voiceWanted = false; g_voice.Stop(); }
        else if (cmd == "diag") SendDiagnostics();
        else if (cmd == "updatecheck") g_upd.StartManual();
        else if (cmd == "updateapply") g_upd.StartApply();
        else if (cmd == "tab") SelectTab(atoi(arg.c_str()));
        else if (cmd == "randomizer") RzSetFromCommand(arg);      // randomizer <spec> [newseed 0|1]
        else if (cmd == "editor") ped::OpenEditor(g_main, g_data, arg, RzSetsChanged);      // editor [set]: the puzzle editor on that set
        else if (cmd == "editortest") ped::EditorTest(arg);      // editortest select <n> | solve | count | try | tool <n>
        else if (cmd == "section") ShowSection(atoi(arg.c_str()));
        else if (cmd == "choice") ChooseTile(ls::ParseChoice(arg));
        else if (cmd == "primary") SendMessageW(g_main, WM_COMMAND, ID_PRIMARY, 0);
        else if (cmd == "pad") { unsigned b = 0; int lx = 0, ly = 0; if (sscanf(arg.c_str(), "%x %d %d", &b, &lx, &ly) >= 1) { g_injectOn = true; g_injected.connected = true; g_injected.buttons = (uint16_t)b; g_injected.lx = (int16_t)lx; g_injected.ly = (int16_t)ly; PadPoll(); } }
        else if (cmd == "padoff") { g_injectOn = false; }
        else if (cmd == "focus") { if (Field* f = ByKey(arg)) SetFocus(f->h); else if (HWND h = C(atoi(arg.c_str()))) SetFocus(h); }
        else if (cmd == "setting") { std::string k = arg.substr(0, arg.find(' ')), v = arg.find(' ') == std::string::npos ? "" : arg.substr(arg.find(' ') + 1); if (Field* f = ByKey(k)) { SetVal(*f, v); OnFieldChanged(*f); } }
        else if (cmd == "capture") { std::string k = arg.substr(0, arg.find(' ')); unsigned vk = (unsigned)strtoul(arg.substr(arg.find(' ') + 1).c_str(), nullptr, 0); if (Field* f = ByKey(k)) { StartCapture(*f); EndCapture(vk); } }
        else if (cmd == "size") { int w = 0, h = 0; if (sscanf(arg.c_str(), "%d %d", &w, &h) == 2) { ShowWindow(g_main, SW_RESTORE); RECT r = OuterFor(w, h); SetWindowPos(g_main, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE); } }
        else if (cmd == "quit") PostMessageW(g_main, WM_CLOSE, 0, 0);
        else if (cmd == "paste") { SetWindowTextW(LB(ID_LB_INVITE), W(arg).c_str()); }
    }
}
void WriteStat() {      // tests: what the lobby is, as text lines, rewritten every 250 ms
    if (g_statFile.empty() || GetTickCount64() - g_statAt < 250) return; g_statAt = GetTickCount64(); char b[600]; std::string o;
    wlobby::State st = g_sess.Active() ? g_sess.State() : wlobby::State(); lw::VoiceStats v = g_voice.Stats();
    snprintf(b, sizeof b, "tabpaints %u\nlobby active %d host %d joined %d id %u phase %d start_in %d refused %d\n", th::TabPaintCount(), g_sess.Active() ? 1 : 0, g_sess.IsHost() ? 1 : 0, g_sess.Joined() ? 1 : 0, g_sess.MyId(), (int)st.phase, st.startInMs, (int)g_sess.Refused()); o += b;
    { wup::Snapshot us = g_upd.Get(); snprintf(b, sizeof b, "version mod %s host %s\nupdate state %d tag %s busy %d auto %d\n", MOD_VERSION, g_sess.HostVersion().empty() ? "-" : g_sess.HostVersion().c_str(), (int)us.state, us.tag.empty() ? "-" : us.tag.c_str(), g_upd.Busy() ? 1 : 0, us.autoCheck ? 1 : 0); o += b; }
    { wchar_t sb[400] = {}; GetWindowTextW(LB(ID_LB_STATUS), sb, 400); o += "lobbystatus " + U(sb) + "\n"; }
    o += std::string("guide ") + (g_guideHold.Open() ? "1\n" : "0\n");
    snprintf(b, sizeof b, "guideshows %u\nguidepaints %u\n", g_guideShows, g_guidePaints); o += b;
    snprintf(b, sizeof b, "invite %s\n", g_sess.Invite().c_str()); o += b;
    {   // the "Friends can join by" rows as laid out (fjbad: host ticks since start that left a row with a way in without its room)
        std::string hs; for (int i = 0; i < NF_ROWS; ++i) hs += " " + std::to_string(FjLaidHeight(i));
        snprintf(b, sizeof b, "fjrows bad %u heights%s\n", g_fjBad, hs.c_str()); o += b;
    }
    { std::string code, msg; bool ok = g_sess.Active() && g_sess.IsHost() && HostInvite(&code, &msg); o += "hostinvite_code " + (ok ? code : std::string("-")) + "\n" + "hostinvite_text " + (ok ? msg : std::string("-")) + "\n";      // (not "invite...": older test parsers read every line that starts so)
      char hd[2048]; hd[0] = 0; if (g_sess.Active() && g_sess.IsHost()) nf_host_dump(hd, sizeof hd); o += hd;
      snprintf(b, sizeof b, "probes answered %u invite_only %d\n", g_sess.ProbesAnswered(), g_sess.InviteOnly() ? 1 : 0); o += b;
      int st[LJ_SRCS], fd[LJ_SRCS]; nf_join_source_states(st, fd); snprintf(b, sizeof b, "joinlist shown %d scans %d rows %zu lan %d/%d tailscale %d/%d recent %d/%d steam %d recents %d route %d\n", JoinListShown() ? 1 : 0, nf_join_scans(), g_rows.size(), st[0], fd[0], st[1], fd[1], st[2], fd[2], st[3], nf_recents_count(), g_joinRoute); o += b;
      for (const auto& r : g_rows) { snprintf(b, sizeof b, "joinrow name %s ip %s port %u route %d mismatch %d full %d players %d limit %d state %d line %s\n", r.name, wlobby::IpText(r.ip).c_str(), r.port, r.route, r.mismatch, r.full, r.players, r.limit, r.state, r.line); o += b; }
      { char src[300]; nf_join_sources(src, sizeof src); o += std::string("joinsources ") + src + "\n"; wchar_t jw[300] = {}; GetWindowTextW(C(ID_JOIN_WHAT), jw, 300); o += "joinwhat " + U(jw) + "\n"; } }
    { uint64_t lid = g_sess.SteamLobbyId(); std::string mod = lid ? g_sess.SteamReadData(lsteam::kModKey) : "", proto = lid ? g_sess.SteamReadData(lsteam::kProtocolKey) : "", code = lid ? g_sess.SteamReadData(lsteam::kTokenKey) : "";
      snprintf(b, sizeof b, "steam mode %d lobby %llu members %u limit %u mod %s protocol %s code_set %d failed %d status %s\n", g_sess.SteamMode() ? 1 : 0, (unsigned long long)lid, g_sess.SteamMemberCount(), lid ? g_sess.SteamMemberLimit() : 0u, mod.c_str(), proto.c_str(), code.empty() ? 0 : 1, g_sess.SteamFailed() ? 1 : 0, g_sess.SteamStatus().c_str()); o += b; }
    for (const auto& m : st.members) { snprintf(b, sizeof b, "member %u name %s host %d ready %d ingame %d speaking %d playing %d port %u ip %s\n", m.id, m.name.c_str(), m.host, m.ready, m.inGame, m.speaking, m.playing, m.gamePort, wlobby::IpText(m.ip).c_str()); o += b; snprintf(b, sizeof b, "member_version %u %s\n", m.id, m.version.c_str()); o += b; }
    snprintf(b, sizeof b, "settings mode %d goal %s newgame %d progress %s randomizer %s\n", (int)st.settings.mode, st.settings.raceGoal.c_str(), st.settings.newGame ? 1 : 0, st.settings.progress.c_str(), st.settings.randomizer.empty() ? "-" : st.settings.randomizer.c_str()); o += b;
    o += RzStat();
    { std::string chat = U(g_chatLog); for (size_t i; (i = chat.find("\r\n")) != std::string::npos;) chat.replace(i, 2, " | "); o += "result " + st.lastResult + "\n"; o += "chat " + chat + "\n"; }
    snprintf(b, sizeof b, "voice running %d sent %u received %u level %u speaking %d muted_ui %d\n", g_voice.Running() ? 1 : 0, v.sent, v.received, v.level, v.speaking ? 1 : 0, (int)SendMessageW(LB(ID_LB_MUTE), BM_GETCHECK, 0, 0)); o += b;
    for (const auto& s : v.speakers) { snprintf(b, sizeof b, "voice_speaker %u talking %d rms %.4f tone %.4f %.4f %.4f packets %u\n", s.id, s.talking ? 1 : 0, s.rms, s.tone[0], s.tone[1], s.tone[2], s.packets); o += b; }
    snprintf(b, sizeof b, "game pid %lu running %d launches %u\n", (unsigned long)g_gamePid, g_game ? 1 : 0, g_launches); o += b;
    { wchar_t sb[400] = {}, hb[400] = {}; GetWindowTextW(g_status, sb, 400); GetWindowTextW(g_hints, hb, 400); HWND f = GetFocus(); Field* ff = f ? ByHwnd(f) : nullptr; RECT cr; GetClientRect(g_main, &cr); std::string fk = ff ? ff->key : (f ? std::to_string(GetDlgCtrlID(f)) : std::string("-"));
      snprintf(b, sizeof b, "ui tab %d label %s lobbyview %d section %d choice %s padlast %d capture %d focus %s client %ld %ld\n", CurTab(), ls::TabLabel(CurTab(), g_sess.Active()), IsWindowVisible(g_lobbyPanel) ? 1 : 0, g_section, ls::ChoiceKey(g_choice), g_padLast ? 1 : 0, g_capture, fk.c_str(), cr.right, cr.bottom); o += b;
      o += "font " + th::FontState() + "\n"; o += "textscreen " + std::to_string(th::ScreenTextDraws()) + "\n"; o += "art " + th::ArtState() + "\n"; o += "backdrop " + th::BackdropState() + "\n";
      { HWND ft = GetFocus(); wchar_t fc[40] = {}; if (ft) GetClassNameW(ft, fc, 40); o += "cues " + std::to_string(th::NavCues() ? 1 : 0) + " focustrace " + std::to_string(ft && GetPropW(ft, L"tr0") ? 1 : 0) + " focusclass " + U(fc) + "\n"; } o += "thumbs " + std::to_string(g_thumbs ? ImageList_GetImageCount(g_thumbs) : 0) + " of " + std::to_string(g_saves.size()) + "\n";
      o += "savepic " + std::to_string(g_savePic && IsWindowVisible(g_savePic) ? 1 : 0) + " " + std::to_string(g_picW) + " " + std::to_string(g_picH) + "\n";
      o += "uistatus " + U(sb) + "\n" + "hints " + U(hb) + "\n" + "steamatstart " + g_steamAtStart + "\n" + "joinbox " + EditText(LB(ID_LB_INVITE)) + "\n"; }
    std::wstring tmp = g_statFile + L".tmp"; if (WriteFile8(tmp, o)) MoveFileExW(tmp.c_str(), g_statFile.c_str(), MOVEFILE_REPLACE_EXISTING);
}


LRESULT CALLBACK GuideProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_ERASEBKGND) return 1;
    if (m != WM_PAINT) return DefWindowProcW(h, m, w, l);
    PAINTSTRUCT ps; HDC screen = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc); ++g_guidePaints;
    HDC dc = CreateCompatibleDC(screen); HBITMAP buffer = CreateCompatibleBitmap(screen, rc.right, rc.bottom); HGDIOBJ oldBuffer = SelectObject(dc, buffer);      // drawn off screen and copied once, so a repaint never shows the bare background
    FillRect(dc, &rc, th::BgBrush()); SetBkMode(dc, TRANSPARENT);
    auto cols = guide::Wrap(guide::Content(GuideSettings(true)));
    auto layout = guide::Fit(float(rc.right), float(rc.bottom), cols);
    int cw = int(layout.columnWidth), fh = std::max(1, int(layout.rowHeight));
    HFONT font = CreateFontW(-std::max(1, int(20 * layout.scale) - 2), std::max(1, int(10 * layout.scale)), 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
    HGDIOBJ old = SelectObject(dc, th::Bold()); SetTextColor(dc, th::kWarm);
    const wchar_t* title = L"QUICK GUIDE  -  The Witness Co-op";
    { RECT tr = {16, 12, 16, 12}; th::Text(dc, title, -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP); }
    SelectObject(dc, th::Small()); SetTextColor(dc, th::kAccent);
    const wchar_t* hint = L"Hold F1 to keep this open. The keys shown are your current settings.";
    TextOutW(dc, 16, 40, hint, (int)wcslen(hint)); SelectObject(dc, font);
    for (int c = 0; c < 3; ++c) {
        int y = 70;
        for (const auto& line : cols[c]) {
            bool heading = !line.empty() && line.find_first_of("abcdefghijklmnopqrstuvwxyz") == std::string::npos;
            SetTextColor(dc, heading ? th::kWarm : th::kText);
            std::wstring text = W(line); TextOutW(dc, 16 + c * cw, y, text.c_str(), (int)text.size()); y += fh;
        }
    }
    SelectObject(dc, old); DeleteObject(font);
    BitBlt(screen, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY); SelectObject(dc, oldBuffer); DeleteObject(buffer); DeleteDC(dc); EndPaint(h, &ps); return 0;
}
void ShowGuide(bool open) {
    if (!open) { ShowWindow(g_guide, SW_HIDE); return; }
    if (!g_guide) {
        WNDCLASSW cls = {}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"WitnessQuickGuide"; cls.lpfnWndProc = GuideProc;
        RegisterClassW(&cls);
        g_guide = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, cls.lpszClassName, L"Quick Guide", WS_POPUP,
            0, 0, 0, 0, g_main, nullptr, cls.hInstance, nullptr);
    }
    // Give the dense guide a wider canvas than the settings form, bounded by
    // this window's monitor. It never activates or takes focus from the form.
    MONITORINFO mi = {}; mi.cbSize = sizeof mi;
    GetMonitorInfoW(MonitorFromWindow(g_main, MONITOR_DEFAULTTONEAREST), &mi);
    RECT parent; GetWindowRect(g_main, &parent);
    int width = std::min(1280, int(mi.rcWork.right - mi.rcWork.left));
    int height = std::min(800, int(mi.rcWork.bottom - mi.rcWork.top));
    int x = std::max(int(mi.rcWork.left), std::min(int(mi.rcWork.right) - width, int(parent.left + parent.right - width) / 2));
    int y = std::max(int(mi.rcWork.top), std::min(int(mi.rcWork.bottom) - height, int(parent.top + parent.bottom - height) / 2));
    SetWindowPos(g_guide, HWND_TOP, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW); ++g_guideShows;
    InvalidateRect(g_guide, nullptr, FALSE);
}

// ---- tabs, layout, saving on change, keys and the pad
int CurTab() { return TabCtrl_GetCurSel(g_tab); }
void UpdateTabs() {      // "Play" reads "Lobby" while hosting or joined (A3), the tiles turn into the lobby, the footer says what the buttons do
    static int last = -1; bool lobby = g_sess.Active();
    if ((int)lobby != last) { last = lobby; TCITEMW it = {}; it.mask = TCIF_TEXT; std::wstring t = W(ls::TabLabel(ls::kPlay, lobby)); it.pszText = &t[0]; TabCtrl_SetItem(g_tab, 0, &it); InvalidateRect(g_tab, nullptr, FALSE);
        Show(g_tiles, !lobby); Show(g_lobbyPanel, lobby); UpdateUi(); }
    static std::string lastHints; ls::PageActions a = ls::ActionsFor((ls::Tab)CurTab(), lobby, lobby && g_sess.IsHost());
    if (CurTab() == ls::kPlay && !lobby) a.primary = ls::PrimaryLabel(g_choice, "");
    std::string h = ls::FooterText(a, g_padLast); if (h != lastHints) { lastHints = h; SetWindowTextW(g_hints, W(h).c_str()); }
}
void FocusFirst(HWND page) { HWND f = GetNextDlgTabItem(page, nullptr, FALSE); if (f && IsWindowVisible(f)) SetFocus(f); }
void SelectTab(int t) {
    t = (t % ls::kTabCount + ls::kTabCount) % ls::kTabCount; TabCtrl_SetCurSel(g_tab, t);
    for (int i = 0; i < g_npages; ++i) ShowWindow(g_pages[i], i == t ? SW_SHOW : SW_HIDE);
    if (t == ls::kSettings) ShowSection(g_section);
    FocusFirst(t == ls::kPlay ? (g_sess.Active() ? g_lobbyPanel : g_tiles) : t == ls::kSettings ? g_secList : g_pages[t]); UpdateTabs();
}
void ShowPage(int p) { SelectTab(p); }
void LayoutAll() {
    RECT rc; GetClientRect(g_main, &rc); int w = rc.right, h = rc.bottom; if (w < 200 || h < 200) return;
    int header = th::BannerHeight(h), foot = 64;
    Place(g_tab, 8, header, w - 16, h - header - foot);
    RECT r; GetClientRect(g_tab, &r); TabCtrl_AdjustRect(g_tab, FALSE, &r); int pw = r.right - r.left, ph = r.bottom - r.top;
    for (int i = 0; i < g_npages; ++i) Place(g_pages[i], r.left, r.top, pw, ph);
    LayoutPlay(pw, ph); LayoutLobby(pw, ph); LayoutSaves(pw, ph); LayoutRandomizers(pw, ph); LayoutAbout(pw, ph);
    Place(g_secList, 12, 12, 240, ph - 24);
    for (int s = 0; s < ls::kSectionCount; ++s) { Place(g_secPanel[s], 270, 8, pw - 282, ph - 16); LayoutSection(g_secPanel[s], s, pw - 282, ph - 16); }
    Place(g_status, 16, h - foot + 8, w - 300, 24); Place(C(ID_FOOT_ACTION), w - 270, h - foot + 4, 254, 30); Place(g_hints, 16, h - 28, w - 32, 22);
    RedrawWindow(g_main, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);      // (every page and control: a control moved by the layout left a stale copy of itself in the lobby)
}
void OnFieldChanged(Field& f) {
    if (g_loading) return;
    if (f.key == "upd:auto") { g_upd.SetAutoCheck(SendMessageW(f.h, BM_GETCHECK, 0, 0) == BST_CHECKED); return; }
    if (f.key == "upd:pre") { g_upd.SetPrereleases(SendMessageW(f.h, CB_GETCURSEL, 0, 0) == 1 ? 1 : 0); SyncUpdChoice(); Status(L"Saved: Updates to offer"); return; }
    if (f.key == "cfg:name") { g_loading = true; SetWindowTextW(C(ID_NAME), W(GetVal(f)).c_str()); g_loading = false; }
#ifdef WL_MUT_NO_SAVE_ON_CHANGE
    return;      // (mutant: a change is not saved)
#endif
    if (g_game) { g_dirty = true; Status(L"Saved when the game ends (the lobby's game is using the settings file)."); return; }
    std::wstring msg; if (ApplyAll(&msg)) Status(L"Saved: " + W(f.row ? f.row->label : f.label) + (f.row && f.row->inSave ? L" (in the chosen save)" : L"")); else Status(msg);
}
void ResetSection() {
    g_loading = true; wl::Video dv;
    for (auto& f : g_fields) {
        if (f.section != g_section || f.kind == kNoteK || f.row->kind == ls::kText || f.key.rfind("upd:", 0) == 0 || f.key.rfind("opt:", 0) == 0 || f.kind == kMulti) continue;
        if (f.key.rfind("vid:", 0) == 0) { std::string k = f.key.substr(4); int v = k == "renderProfile" ? dv.renderProfile : k == "fullscreen" ? dv.fullscreen : k == "vsync" ? dv.vsync : k == "highdpi" ? dv.highdpi : k == "resolution" ? dv.resolution : k == "multisampling" ? dv.multisampling : k == "shading" ? dv.shading : k == "reflection" ? dv.reflection : k == "texture" ? dv.texture : k == "textureDetail" ? dv.textureDetail : k == "geometry" ? dv.geometry : -1; if (v >= 0) SetVal(f, std::to_string(v)); continue; }
        SetVal(f, f.def);
    }
    g_loading = false; std::wstring msg; if (g_game) { g_dirty = true; Status(L"Section reset; saved when the game ends."); return; }
    if (ApplyAll(&msg)) Status(L"Reset: " + W(ls::SectionName(g_section)) + L" is back to the defaults."); else Status(msg);
}
// press-a-key capture (E3): the key field shows "Press a key", the next key or mouse button is checked against every other binding
void StartCapture(Field& f) {
    if (g_capture >= 0) return; g_capture = (int)(&f - &g_fields[0]); wchar_t b[60] = {}; GetWindowTextW(f.h, b, 60); g_captureOld = b;
    SetWindowTextW(f.h, L"Press a key (Esc: cancel)"); Status(L"Press the new key for " + W(f.label) + L". F1 stays the quick guide.");
}
void EndCapture(unsigned vk) {
    if (g_capture < 0) return; Field& f = g_fields[g_capture]; g_capture = -1; SetWindowTextW(f.h, g_captureOld.c_str());
    if (vk == VK_ESCAPE || !vk) { Status(L"Key unchanged."); return; }
    Cfg cur = GuideSettings(false), out; std::string err = ls::TryBind(cur, f.key.substr(4), vk, &out);
    if (!err.empty()) { Status(W(err)); return; }
    SetVal(f, guide::KeyName(ls::KeyFromVk(vk))); OnFieldChanged(f);
}
bool CaptureMessage(const MSG& m) {      // true: the message was the captured key (it goes no further)
    if (g_capture < 0) return false;
    unsigned vk = 0;
    if (m.message == WM_KEYDOWN || m.message == WM_SYSKEYDOWN) vk = (unsigned)m.wParam;
    else if (m.message == WM_MBUTTONDOWN) vk = guide::MouseMiddle;
    else if (m.message == WM_XBUTTONDOWN) vk = HIWORD(m.wParam) == XBUTTON1 ? guide::MouseX1 : guide::MouseX2;
    else if (m.message == WM_MOUSEWHEEL) vk = guide::Wheel;
    else if (m.message == WM_LBUTTONDOWN || m.message == WM_RBUTTONDOWN) { EndCapture(0); return false; }
    else if (m.message == WM_KEYUP || m.message == WM_SYSKEYUP || m.message == WM_CHAR) return true;
    if (!vk || vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) return vk != 0;
    EndCapture(vk); return true;
}
// the primary button of the page (Start on a pad, Ctrl+Enter), and the X / Y context actions (A2, K3)
void Primary() {
    int t = CurTab();
    if (t == ls::kPlay) { if (g_sess.Active()) { if (g_sess.IsHost()) SendMessageW(g_main, WM_COMMAND, ID_LB_START, 0); else { HWND r = LB(ID_LB_READY); SendMessageW(r, BM_CLICK, 0, 0); } } else SendMessageW(g_main, WM_COMMAND, ID_PRIMARY, 0); }
    else if (t == ls::kSaves) SendMessageW(g_main, WM_COMMAND, ID_NEWGAME, 0);
    else if (t == ls::kAbout) SendMessageW(g_main, WM_COMMAND, ID_UP_CHECK, 0);
}
void ShowGuide(bool open); bool GuideOpen() { return g_guide && IsWindowVisible(g_guide); }
void Context(bool y) {
    int t = CurTab(); bool lobby = g_sess.Active();
    if (t == ls::kPlay) { if (lobby) { if (y) SendMessageW(g_main, WM_COMMAND, ID_LB_LEAVE, 0); else if (g_sess.IsHost()) SendMessageW(g_main, WM_COMMAND, ID_LB_COPY, 0); } else if (y) ShowGuide(!GuideOpen()); }
    else if (t == ls::kSaves) SendMessageW(g_main, WM_COMMAND, y ? ID_REFRESH : ID_BACKUP, 0);
    else if (t == ls::kRandomizers) { if (!y) SendMessageW(g_main, WM_COMMAND, ID_RZ_ROLL, 0); }
    else if (t == ls::kSettings) { if (!y) ResetSection(); }
    else if (t == ls::kAbout) { if (y) ShowGuide(!GuideOpen()); else SendMessageW(g_main, WM_COMMAND, ID_DIAG, 0); }
}
bool Back() {      // Esc / B: close what is open, else back to Play; never leaves a lobby by itself (Leave is one click)
    if (g_capture >= 0) { EndCapture(VK_ESCAPE); return true; }
    if (GuideOpen()) { ShowGuide(false); return true; }
    HWND f = GetFocus(); if (f) { wchar_t c[40] = {}; GetClassNameW(f, c, 40); if (!wcscmp(c, L"ComboBox") && SendMessageW(f, CB_GETDROPPEDSTATE, 0, 0)) { SendMessageW(f, CB_SHOWDROPDOWN, FALSE, 0); return true; } }
    if (g_ng) { DestroyWindow(g_ng); g_ng = nullptr; return true; }
    if (CurTab() != ls::kPlay) { SelectTab(ls::kPlay); return true; }
    return false;
}
std::wstring ClassName(HWND h) { wchar_t c[40] = {}; if (h) GetClassNameW(h, c, 40); return c; }
void MoveFocus(bool prev) {
    HWND f = GetFocus(); HWND page = g_pages[CurTab()];
    if (!f || !IsChild(page, f)) { FocusFirst(CurTab() == ls::kPlay ? (g_sess.Active() ? g_lobbyPanel : g_tiles) : page); return; }
    HWND n = GetNextDlgTabItem(page, f, prev); if (n) SetFocus(n);
}
void PadAct(ls::PadAction a) {
    g_padLast = true; th::SetNavCues(true); HWND f = GetFocus(); std::wstring cls = ClassName(f); Field* fld = f ? ByHwnd(f) : nullptr;
    bool dropped = cls == L"ComboBox" && SendMessageW(f, CB_GETDROPPEDSTATE, 0, 0);
    switch (a) {
    case ls::kUp: case ls::kDown:
        if (dropped || (cls == L"ListBox" && f == g_secList)) {
            int cnt = (int)SendMessageW(f, dropped ? CB_GETCOUNT : LB_GETCOUNT, 0, 0), cur = (int)SendMessageW(f, dropped ? CB_GETCURSEL : LB_GETCURSEL, 0, 0), n = std::max(0, std::min(cnt - 1, cur + (a == ls::kUp ? -1 : 1)));
            if (dropped) SendMessageW(f, CB_SETCURSEL, n, 0); else { ShowSection(n); }
            break;
        }
        MoveFocus(a == ls::kUp); break;
    case ls::kLeft: case ls::kRight: {
        int d = a == ls::kLeft ? -1 : 1;
        if (cls == TRACKBAR_CLASSW && fld) { int line = (int)SendMessageW(f, TBM_GETLINESIZE, 0, 0); SendMessageW(f, TBM_SETPOS, TRUE, SendMessageW(f, TBM_GETPOS, 0, 0) + d * line); SliderText(*fld); OnFieldChanged(*fld); }
        else if (cls == L"ComboBox" && fld && !dropped) { int cnt = (int)SendMessageW(f, CB_GETCOUNT, 0, 0), cur = (int)SendMessageW(f, CB_GETCURSEL, 0, 0), n = std::max(0, std::min(cnt - 1, cur + d)); if (n != cur) { SendMessageW(f, CB_SETCURSEL, n, 0); OnFieldChanged(*fld); } }
        else if (f == g_secList && d > 0) FocusFirst(g_secPanel[g_section]);
        else if (f && IsChild(g_secPanel[g_section], f) && d < 0 && !fld) SetFocus(g_secList);
        else if (f && fld && IsChild(g_secPanel[g_section], f) && d < 0) SetFocus(g_secList);
        else if (f == C(ID_TILE_HOST) || f == C(ID_TILE_JOIN) || f == C(ID_TILE_SOLO)) { int c = (int)g_choice + d; if (c >= 0 && c <= 2) { ChooseTile((ls::Choice)c); SetFocus(C(ID_TILE_HOST + c)); } }
        else MoveFocus(d < 0);
        break; }
    case ls::kConfirm:
        if (cls == L"Button") SendMessageW(f, BM_CLICK, 0, 0);
        else if (cls == L"ComboBox") { SendMessageW(f, CB_SHOWDROPDOWN, !dropped, 0); if (dropped && fld) OnFieldChanged(*fld); }
        else if (f == g_secList) FocusFirst(g_secPanel[g_section]);
        else if (cls == L"Edit" && f == LB(ID_LB_CHAT)) SendChatLine();
        break;
    case ls::kBack: Back(); break;
    case ls::kPrevTab: SelectTab(CurTab() - 1); break;
    case ls::kNextTab: SelectTab(CurTab() + 1); break;
    case ls::kPrimary: Primary(); break;
    case ls::kActionX: Context(false); break;
    case ls::kActionY: Context(true); break;
    }
    UpdateTabs();
}
// XInput, loaded by hand (xinput1_4 on Windows 8 and later and in Proton, else xinput9_1_0): no new dependency, nothing when there is no pad
typedef struct { WORD wButtons; BYTE bLeftTrigger, bRightTrigger; SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; } XPad;
typedef struct { DWORD dwPacketNumber; XPad Gamepad; } XState;
typedef DWORD(WINAPI* XGetState)(DWORD, XState*);
XGetState g_xget; int g_padSlot = -1; ULONGLONG g_padScan;
ls::PadState g_injected; bool g_injectOn;      // tests: the pad state the "pad" command set (K10, a pad-injection hook)
void PadPoll() {
    static bool loaded; if (!loaded) { loaded = true; HMODULE x = LoadLibraryW(L"xinput1_4.dll"); if (!x) x = LoadLibraryW(L"xinput9_1_0.dll"); if (x) g_xget = (XGetState)GetProcAddress(x, "XInputGetState"); }
    ls::PadState s;
    if (g_injectOn) s = g_injected;
    else if (g_xget) {
        if (g_padSlot < 0 && GetTickCount64() - g_padScan > 2000) { g_padScan = GetTickCount64(); for (int i = 0; i < 4; ++i) { XState x = {}; if (g_xget(i, &x) == ERROR_SUCCESS) { g_padSlot = i; break; } } }
        if (g_padSlot >= 0) { XState x = {}; if (g_xget(g_padSlot, &x) == ERROR_SUCCESS) { s.connected = true; s.buttons = x.Gamepad.wButtons; s.lx = x.Gamepad.sThumbLX; s.ly = x.Gamepad.sThumbLY; } else g_padSlot = -1; }
    }
    if (GetForegroundWindow() != g_main && !g_injectOn && !g_noActivate) { g_padNav.Update(ls::PadState(), (uint32_t)GetTickCount64()); return; }      // a pad only drives the launcher while it is the active window
    for (auto a : g_padNav.Update(s, (uint32_t)GetTickCount64())) PadAct(a);
}
// keyboard (A6): Enter and Tab are the dialog's own; Esc goes back; Ctrl+Tab tabs, Ctrl+Enter the primary button, Ctrl+X / Ctrl+Y the context actions outside a text box
bool KeyMessage(const MSG& m) {
    if (m.message != WM_KEYDOWN) return false;
    g_padLast = false; bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (m.wParam == VK_TAB || (m.wParam >= VK_LEFT && m.wParam <= VK_DOWN)) th::SetNavCues(true);      // (moving with the keyboard: the focused button shows its line)
    if (m.wParam == VK_ESCAPE) { if (g_ng && GetForegroundWindow() == g_ng) return false; return Back() || true; }
    if (ctrl && m.wParam == VK_TAB) { SelectTab(CurTab() + (shift ? -1 : 1)); return true; }
    if (ctrl && m.wParam == VK_RETURN) { Primary(); return true; }
    if (ctrl && (m.wParam == 'X' || m.wParam == 'Y') && ClassName(GetFocus()) != L"Edit") { Context(m.wParam == 'Y'); return true; }
    if (m.hwnd == g_secList && m.wParam == VK_RIGHT) { FocusFirst(g_secPanel[g_section]); return true; }
    return false;
}

void DrawSectionItem(DRAWITEMSTRUCT* d) {
    HDC dc = d->hDC; RECT r = d->rcItem; bool sel = (d->itemState & ODS_SELECTED) != 0, focus = GetFocus() == d->hwndItem;
    HBRUSH bg = CreateSolidBrush(sel ? th::kSurface2 : th::kSurface); FillRect(dc, &r, bg); DeleteObject(bg);
    if (d->itemID == (UINT)-1) return;
    if (sel) { HBRUSH a = CreateSolidBrush(th::kWarm); RECT ar = {r.left, r.top + 6, r.left + 4, r.bottom - 6}; FillRect(dc, &ar, a); DeleteObject(a); }
    SetBkMode(dc, TRANSPARENT); SelectObject(dc, sel ? th::Bold() : th::Body()); SetTextColor(dc, sel ? th::kText : (focus ? th::kLine : th::kMuted)); RECT tr = r; tr.left += 16;
    th::Text(dc, W(ls::SectionName((int)d->itemID)).c_str(), -1, &tr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}
// The window draws its own title bar: the banner is the caption (drag, double-click, the system menu) with minimize, maximize and close on its top right; the
// sizing borders on the left, right and bottom stay Windows' own (invisible, with the shadow), the top edge sizes from the banner's first pixels. (Owner, 2026-11-06.)
RECT FrameInsets() { RECT f = {0, 0, 0, 0}; AdjustWindowRectEx(&f, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_CONTROLPARENT); return f; }      // (left negative; right and bottom the frame)
RECT OuterFor(int w, int h) { RECT f = FrameInsets(); return {0, 0, w - f.left + f.right, h + f.bottom}; }      // the window size for a client of w x h
int g_capHot, g_capDown; bool g_capActive = true;
void CaptionChanged() { if (!g_main) return; th::SetCaption(g_capHot, g_capDown, IsZoomed(g_main) != FALSE, g_capActive); RECT rc; GetClientRect(g_main, &rc); RECT r = th::CaptionRect(rc.right); InvalidateRect(g_main, &r, FALSE); }
int CapOf(WPARAM ht) { return ht == HTMINBUTTON ? 1 : ht == HTMAXBUTTON ? 2 : ht == HTCLOSE ? 3 : 0; }
LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
#ifndef WL_MUT_WINDOWS_CAPTION
    case WM_NCCALCSIZE: if (w) { RECT f = FrameInsets(); RECT& r = ((NCCALCSIZE_PARAMS*)l)->rgrc[0]; r.left -= f.left; r.right -= f.right; r.bottom -= f.bottom; if (IsZoomed(h)) r.top += f.bottom; return 0; } break;
    case WM_NCHITTEST: {
        LRESULT r = DefWindowProcW(h, m, w, l); if (r != HTCLIENT) return r;
        POINT p = {(short)LOWORD(l), (short)HIWORD(l)}; ScreenToClient(h, &p); RECT rc; GetClientRect(h, &rc);
        if (int k = th::CaptionHit(p.x, p.y, rc.right)) return k == 1 ? HTMINBUTTON : k == 2 ? HTMAXBUTTON : HTCLOSE;      // (HTMAXBUTTON: Windows 11 shows its snap layouts there)
        if (!IsZoomed(h) && p.y < 6) return p.x < 16 ? HTTOPLEFT : p.x >= rc.right - 16 ? HTTOPRIGHT : HTTOP;
        return p.y < th::BannerHeight(rc.bottom) ? HTCAPTION : HTCLIENT;
    }
    case WM_NCMOUSEMOVE: { int k = CapOf(w); if (k != g_capHot) { g_capHot = k; CaptionChanged(); } if (k) { TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE | TME_NONCLIENT, h, 0}; TrackMouseEvent(&t); return 0; } break; }
    case WM_NCMOUSELEAVE: if (g_capHot || g_capDown) { g_capHot = g_capDown = 0; CaptionChanged(); } break;
    case WM_NCLBUTTONDOWN: case WM_NCLBUTTONDBLCLK: if (int k = CapOf(w)) { g_capDown = k; CaptionChanged(); return 0; } break;      // (not to DefWindowProc: it would draw the old buttons)
    case WM_NCLBUTTONUP: if (int k = CapOf(w)) { bool fire = k == g_capDown; g_capDown = 0; CaptionChanged(); if (fire) PostMessageW(h, WM_SYSCOMMAND, k == 1 ? SC_MINIMIZE : k == 2 ? (IsZoomed(h) ? SC_RESTORE : SC_MAXIMIZE) : SC_CLOSE, 0); return 0; } break;
    case WM_NCACTIVATE: g_capActive = w != 0; CaptionChanged(); return DefWindowProcW(h, m, w, -1);      // (-1: Windows paints no title bar of its own)
#endif
    case WM_SETTEXT: case WM_SETICON: { LRESULT r = DefWindowProcW(h, m, w, l); RECT rc; GetClientRect(h, &rc); RECT top = {0, 0, rc.right, th::BannerHeight(rc.bottom)}; InvalidateRect(h, &top, FALSE); return r; }      // (Windows may draw its old title over the banner here)
    case WM_ACTIVATEAPP: if (!w) { g_guideHold.Reset(); ShowGuide(false); } else ClipboardCheck(); break;
    case WM_ERASEBKGND: { RECT rc; GetClientRect(h, &rc); th::FillBg((HDC)w, h, rc); return 1; }
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc); th::PaintHeader(dc, rc, th::BannerHeight(rc.bottom)); EndPaint(h, &ps); return 0; }
    case WM_SIZE: LayoutAll(); CaptionChanged(); return 0;
    case WM_GETMINMAXINFO: { MINMAXINFO* mm = (MINMAXINFO*)l; mm->ptMinTrackSize.x = 1024; mm->ptMinTrackSize.y = 640; return 0; }
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN: return th::CtlColor(m, (HDC)w, (HWND)l);
    case WM_DRAWITEM: {      // each row drawn off screen and copied once: the lobby's list is redrawn every tick, and a row drawn on screen in steps (its background, then its text) flickered
        DRAWITEMSTRUCT* src = (DRAWITEMSTRUCT*)l; DRAWITEMSTRUCT b = *src; DRAWITEMSTRUCT* d = &b; int iw = std::max<LONG>(1, src->rcItem.right - src->rcItem.left), ih = std::max<LONG>(1, src->rcItem.bottom - src->rcItem.top);
        HDC mem = CreateCompatibleDC(src->hDC); HBITMAP bmp = CreateCompatibleBitmap(src->hDC, iw, ih); HGDIOBJ ob = SelectObject(mem, bmp); b.hDC = mem; OffsetRect(&b.rcItem, -src->rcItem.left, -src->rcItem.top);
        if (d->CtlID == ID_LB_MEMBERS) DrawMember(d); else if (d->CtlID == ID_JOIN_LIST) DrawJoinRow(d); else if (d->CtlID == ID_LB_LEVEL) DrawLevel(d); else if (d->CtlID == ID_SECTIONS) DrawSectionItem(d); else if (d->CtlID >= ID_LB_FJ0 && d->CtlID <= ID_LB_FJ4) DrawFjRow(d, d->CtlID - ID_LB_FJ0);
        BitBlt(src->hDC, src->rcItem.left, src->rcItem.top, iw, ih, mem, 0, 0, SRCCOPY); SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem); return TRUE; }
    case WM_HSCROLL: { Field* f = ByHwnd((HWND)l); if (f && f->kind == kSlider) { SliderText(*f); if (LOWORD(w) == TB_ENDTRACK) OnFieldChanged(*f); } return 0; }
    case WM_TIMER:
        if (w == 1) { if (g_fleetingUntil && GetTickCount64() >= g_fleetingUntil) { wchar_t t[200] = {}; GetWindowTextW(g_status, t, 200); if (g_fleeting == t) Status(L""); g_fleetingUntil = 0; }
#ifdef WL_MUT_STATUS_STAYS
            g_fleetingUntil = 0;      // (mutant: "You left the lobby." stays for good)
#endif
            PollCommands(); LobbyTick(); WriteStat(); DiagPoll(); UpdateUi(); UpdateTabs(); if (g_updFromLobby && g_upd.Available()) { g_updFromLobby = false; g_upd.StartApply(); } }
        else if (w == 2) PadPoll();
        return 0;
    case WM_CLOSE: nf_host_close(); g_voice.Stop(); g_sess.Stop(); DestroyWindow(h); return 0;      // (nf_host_close: the router mappings go before the launcher does)
    case WM_COMMAND: {
        int id = LOWORD(w), code = HIWORD(w);
        if (id >= ID_FIELD) { Field* f = ById(id); if (!f) return 0;
            if (f->kind == kKeyBtn && code == BN_CLICKED) StartCapture(*f);
            else if (f->kind == kCheck && code == BN_CLICKED) OnFieldChanged(*f);
            else if (f->kind == kCombo && code == CBN_SELCHANGE) OnFieldChanged(*f);
            else if ((f->kind == kEdit || f->kind == kMulti) && code == EN_KILLFOCUS) OnFieldChanged(*f);
            return 0; }
        if (id == IDCANCEL) { Back(); return 0; }
        if (id == ID_APPLY) { std::wstring msg; ApplyAll(&msg); Status(msg); LoadAll(); }      // (tests: the old Save settings, now what every change does)
        else if (id == ID_NAME && code == EN_KILLFOCUS) { if (Field* f = ByKey("cfg:name")) { std::string t = EditText(C(ID_NAME)); if (t != GetVal(*f)) { SetVal(*f, t); OnFieldChanged(*f); } } }
        else if (id == ID_TILE_HOST || id == ID_TILE_JOIN || id == ID_TILE_SOLO) { if (code == BN_CLICKED) ChooseTile(id == ID_TILE_HOST ? ls::kHost : id == ID_TILE_JOIN ? ls::kJoin : ls::kSolo); }
        else if (id == ID_HOST_STEAM || id == ID_HOST_DIRECT) { if (code == BN_CLICKED) { wl::CfgFile c = LoadCfg(); c.Set("launcher_host", id == ID_HOST_STEAM ? "steam" : "direct"); if (!g_game) WriteFile8(CfgPath(), c.Text()); } }
        else if (id == ID_PRIMARY) {
            if (g_choice == ls::kHost) { if (SendMessageW(C(ID_HOST_DIRECT), BM_GETCHECK, 0, 0) == BST_CHECKED) HostLobby(); else HostSteamLobby(); }
            else if (g_choice == ls::kJoin) JoinLobby();      // the paste box when it holds something, else the chosen row of the Join list
            else LaunchGame(false);
            UpdateTabs(); if (g_sess.Active()) FocusFirst(g_lobbyPanel);
        }
        else if (id == ID_SOLO_NEW) OpenNewGame();
        else if (id == ID_FOOT_ACTION) { if (g_footAction) SendMessageW(h, WM_COMMAND, g_footAction, 0); }
        else if (id == ID_SECTIONS && code == LBN_SELCHANGE) ShowSection((int)SendMessageW(g_secList, LB_GETCURSEL, 0, 0));
        else if (id == ID_RESET) ResetSection();
        else if ((id == ID_SAVEPICK || id == ID_SAVEPICK2) && code == CBN_SELCHANGE) { int i = (int)SendMessageW((HWND)l, CB_GETCURSEL, 0, 0); SendMessageW(g_savePick, CB_SETCURSEL, i, 0); SendMessageW(g_savePick2, CB_SETCURSEL, i, 0); LoadSaveOptions(i); }
        else if (id == ID_GUIDE_BTN) ShowGuide(!GuideOpen());
        else if (id == ID_UP_WHATSNEW) SelectTab(ls::kAbout);
        else if (id == ID_LINK_HOME) ShellExecuteW(nullptr, L"open", L"https://github.com/TommoDotCommo/witness-coop", nullptr, nullptr, SW_SHOW);
        else if (id == ID_LINK_NOTES) ShellExecuteW(nullptr, L"open", L"https://github.com/TommoDotCommo/witness-coop/releases", nullptr, nullptr, SW_SHOW);
        else if (id == ID_DIAG) SendDiagnostics();
        else if (id == ID_LB_HOST) HostLobby();
        else if (id == ID_LB_STEAM_HOST) HostSteamLobby();
        else if (id == ID_LB_STEAM_INVITE) { if (!g_sess.SteamInviteDialog()) LobbyStatus(W(g_sess.SteamStatus())); }
        else if (id == ID_LB_STEAM_SEND) { int i = (int)SendMessageW(LB(ID_LB_STEAM_FRIENDS), CB_GETCURSEL, 0, 0); if (i >= 0 && i < (int)g_friends.size()) { g_sess.SteamInviteFriend(g_friends[i].first); LobbyStatus(W(g_sess.SteamStatus())); } else FillFriends(); }
        else if (id == ID_LB_JOIN) JoinLobby();
        else if (id == ID_LB_INVITE && code == EN_CHANGE) { std::string t = EditText(LB(ID_LB_INVITE)); SetWindowTextW(C(ID_JOIN_WHAT), t.empty() ? L"" : W(jg::PlanPaste(t, NetSettings().port).said).c_str()); }      // B7: says what it recognised before Join
        else if (id == ID_JOIN_LIST && code == LBN_SELCHANGE) { if (SendMessageW(LB(ID_JOIN_LIST), LB_GETCURSEL, 0, 0) >= 0) SetWindowTextW(LB(ID_LB_INVITE), L""); }
        else if (id == ID_JOIN_LIST && code == LBN_DBLCLK) JoinRow((int)SendMessageW(LB(ID_JOIN_LIST), LB_GETCURSEL, 0, 0));
        else if (id == ID_JOIN_CLIP_YES) ClipboardJoin();
        else if (id == ID_LB_LEAVE) { LeaveLobby(); UpdateTabs(); FocusFirst(g_tiles); }
        else if (id == ID_LB_COPY) CopyInvite(h);
        else if (id == ID_LB_READY && code == BN_CLICKED) g_sess.SetReady(SendMessageW(LB(ID_LB_READY), BM_GETCHECK, 0, 0) == BST_CHECKED);
        else if (id == ID_LB_START) { PushSettings(); if (!g_sess.Start()) LobbyStatus(L"Everybody has to be ready first: " + W(WaitingFor()) + L"."); }
        else if (id == ID_LB_SEND) SendChatLine();
        else if (id == ID_LB_MODE && code == CBN_SELCHANGE) { PushSettings(); LayoutAll(); }
        else if (id == ID_LB_GOAL && code == EN_CHANGE) PushSettings();
        else if (id == ID_LB_NEWGAME) PushSettings();
        else if (id == ID_LB_PARTS) OpenNewGame(true);
        else if (id == ID_LB_MUTE) g_voice.SetMuted(SendMessageW(LB(ID_LB_MUTE), BM_GETCHECK, 0, 0) == BST_CHECKED);
        else if (id == ID_LB_DEAF) g_voice.SetDeaf(SendMessageW(LB(ID_LB_DEAF), BM_GETCHECK, 0, 0) == BST_CHECKED);
        else if (id == ID_LAUNCH) LaunchGame(false);
        else if (id == ID_NEWGAME) OpenNewGame();
        else if (id == ID_REFRESH) { LoadAll(); Status(L"Reloaded."); }
        else if (id == ID_OPENSAVES) ShellExecuteW(nullptr, L"open", SaveFolder(LoadCfg()).c_str(), nullptr, nullptr, SW_SHOW);
        else if (id == ID_BACKUP || id == ID_TRASH) {
            int i = ListView_GetNextItem(g_saveList, -1, LVNI_SELECTED); if (i < 0 || i >= (int)g_saves.size()) { Status(L"Select a save first."); break; }
            std::wstring dir = SaveFolder(LoadCfg()) + L"\\launcher-backup\\" + Stamp();
            if (BackupSave(g_saves[i], id == ID_TRASH, dir)) Status(std::wstring(id == ID_TRASH ? L"Moved to " : L"Copied to ") + dir); else Status(L"Could not do that: the save files could not be copied.", L"Send diagnostics", ID_DIAG); LoadAll();
        }
        else if (id == ID_UP_CHECK) g_upd.StartManual();
        else if (id == ID_UP_APPLY || id == ID_UP_BANNER_APPLY) g_upd.StartApply();
        else if (id >= ID_RZ_TITLE && id <= ID_RZ_LICENCE) RzCommand(id, code);
        else if (id == ID_LB_RZ_BTN && code == BN_CLICKED) SelectTab(ls::kRandomizers);
        else if ((id == ID_UP_REL || id == ID_UP_PRE) && HIWORD(w) == BN_CLICKED) {
#ifndef WL_MUT_UPD_CHOICE_UNSAVED
            g_upd.SetPrereleases(id == ID_UP_PRE ? 1 : 0);
#endif
            SyncUpdChoice(); Status(id == ID_UP_PRE ? L"Saved: pre-releases are offered too" : L"Saved: only full releases are offered");
        }
        else if (id == ID_UP_LATER || id == ID_UP_BANNER_LATER) g_upd.Later();
        else if (id == ID_UP_AUTO && code == BN_CLICKED) g_upd.SetAutoCheck(SendMessageW(C(ID_UP_AUTO), BM_GETCHECK, 0, 0) == BST_CHECKED);
        else if (id == ID_LB_UPDATE) { if (g_upd.Available()) g_upd.StartApply(); else { g_updFromLobby = true; g_upd.StartManual(); } }      // (the mismatch's Update button: the same check, then the same apply)
        else if (id == ID_BROWSE) { BROWSEINFOW bi = {}; bi.hwndOwner = h; bi.lpszTitle = L"The Witness game folder"; PIDLIST_ABSOLUTE p = SHBrowseForFolderW(&bi); if (p) { wchar_t b[MAX_PATH]; if (SHGetPathFromIDListW(p, b)) { g_dir = b; SetWindowTextW(C(ID_GAMEDIR), b); LoadAll(); } CoTaskMemFree(p); } }
        return 0; }
    case WM_NOTIFY: { NMHDR* n = (NMHDR*)l; if (n->idFrom == ID_TAB && n->code == TCN_SELCHANGE) SelectTab(TabCtrl_GetCurSel(g_tab)); return 0; }
    case WM_DESTROY: if (g_logOpen) { g_log.Close(); g_logOpen = false; } PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
}  // namespace

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR cmdline, int show) {
    (void)cmdline; (void)show;
    INITCOMMONCONTROLSEX ic = {sizeof ic, ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES}; InitCommonControlsEx(&ic); CoInitialize(nullptr);
    int argc; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc); std::wstring dumpTo; int startPage = 0, startSection = 0, sizeW = 0, sizeH = 0; bool openNew = false, fill = false;
    wchar_t self[MAX_PATH]; GetModuleFileNameW(nullptr, self, MAX_PATH); g_dir = Dir(self);
    for (int i = 1; i < argc; ++i) { std::wstring a = argv[i]; if (a == L"--dir" && i + 1 < argc) g_dir = argv[++i]; else if (a == L"--dump-controls" && i + 1 < argc) dumpTo = argv[++i]; else if (a == L"--diag-out" && i + 1 < argc) g_diagOut = argv[++i]; else if (a == L"--no-activate") g_noActivate = true; else if (a == L"--coop-invite" && i + 1 < argc) g_inviteLink = wl::InviteFromLink(U(argv[++i])); else if (a.compare(0, 13, L"witness-coop:") == 0) g_inviteLink = wl::InviteFromLink(U(a)); else if (a == L"--page" && i + 1 < argc) startPage = _wtoi(argv[++i]); else if (a == L"--section" && i + 1 < argc) startSection = _wtoi(argv[++i]); else if (a == L"--size" && i + 2 < argc) { sizeW = _wtoi(argv[++i]); sizeH = _wtoi(argv[++i]); } else if (a == L"--fill") fill = true; else if (a == L"--stat-file" && i + 1 < argc) g_statFile = argv[++i]; else if (a == L"--cmd-file" && i + 1 < argc) g_cmdFile = argv[++i]; else if (a == L"--new-game-window") openNew = true; else if ((a == L"+connect_lobby" || a == L"--connect-lobby") && i + 1 < argc) { steamids::ParseLobby(U(argv[++i]), &g_autoJoin); } }
    { wchar_t e[MAX_PATH]; if (g_statFile.empty() && GetEnvironmentVariableW(L"WITNESS_LAUNCHER_STAT_FILE", e, MAX_PATH)) g_statFile = e; if (g_cmdFile.empty() && GetEnvironmentVariableW(L"WITNESS_LAUNCHER_CMD_FILE", e, MAX_PATH)) g_cmdFile = e; }      // (tests: a launcher the game started by itself)
    if (!Exists(g_dir + L"\\witness64_d3d11.exe") && !Exists(g_dir + L"\\witness_d3d11.exe") && Exists(Dir(g_dir) + L"\\witness64_d3d11.exe")) g_dir = Dir(g_dir);
    g_data = userdata::DataDir(g_dir); while (g_data.size() > 3 && g_data.back() == L'\\') g_data.pop_back();      // (moves a player's cfg out of the game folder once)
    SetEnvironmentVariableW(handover::kSessionEnv, L"1");      // every game this launcher starts carries it: that game never hands over to a launcher again (common/handover.h)
    bool deck = false; { wchar_t e[8]; if (GetEnvironmentVariableW(L"SteamDeck", e, 8) && e[0] == L'1') fill = deck = true; }      // a Steam Deck: the window fills the screen, without a frame (K8; Game Mode shows one window; Steam's menu quits)
    { std::wstring ld = LogsFolder(); if (!ld.empty() && g_log.Open(ld, L"launcher_p", (unsigned long long)100 * 1024 * 1024)) {
        g_logOpen = true; char l[200]; snprintf(l, sizeof l, "session %s: mod %s", g_log.KeyUtf8().c_str(), MOD_VERSION); Dlog(l); Dlog(("launcher started, game folder " + U(g_dir)).c_str());
        diag::CrashOptions co; co.session = &g_log; co.dataModule = GetModuleHandleW(nullptr); co.log = Dlog; diag::InstallCrashHandler(co); } }
    th::Init(g_dir); AddFields();
    WNDCLASSW wc = {}; wc.lpfnWndProc = MainProc; wc.hInstance = hi; wc.lpszClassName = L"WitnessLauncher"; wc.hbrBackground = th::BgBrush(); wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION); RegisterClassW(&wc);
    WNDCLASSW pc = {}; pc.lpfnWndProc = PageProc; pc.hInstance = hi; pc.lpszClassName = L"WitnessLauncherPage"; pc.hbrBackground = th::BgBrush(); pc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&pc);
    // 1280x800 (a Steam Deck's screen) or the work area if that is smaller; resizable (K8)
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0); int ww = sizeW ? sizeW : std::min(1280, (int)(wa.right - wa.left)), wh = sizeH ? sizeH : std::min(800, (int)(wa.bottom - wa.top));
    if (!sizeW && (wa.right - wa.left <= 1280 || wa.bottom - wa.top <= 800)) fill = true;
    RECT want = {0, 0, ww, wh}; if (sizeW) want = OuterFor(ww, wh);
    g_main = CreateWindowExW(WS_EX_CONTROLPARENT, L"WitnessLauncher", (std::wstring(L"The Witness co-op: launcher ") + W(MOD_VERSION)).c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, want.right - want.left, want.bottom - want.top, nullptr, nullptr, hi, nullptr);
    SetWindowPos(g_main, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE); CaptionChanged();
    g_tab = CreateWindowExW(WS_EX_CONTROLPARENT, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 8, 88, 600, 400, g_main, (HMENU)ID_TAB, hi, nullptr); SendMessageW(g_tab, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    static TCITEMW items[ls::kTabCount]; static std::wstring names[ls::kTabCount];
    for (int i = 0; i < ls::kTabCount; ++i) { names[i] = W(ls::TabLabel(i, false)); items[i] = {}; items[i].mask = TCIF_TEXT; items[i].pszText = &names[i][0]; TabCtrl_InsertItem(g_tab, i, &items[i]); }
    th::StyleTab(g_tab);
    for (int i = 0; i < g_npages; ++i) g_pages[i] = Panel(g_tab);
    BuildPlay(g_pages[ls::kPlay]); BuildSaves(g_pages[ls::kSaves]); BuildRandomizers(g_pages[ls::kRandomizers]); BuildSettings(g_pages[ls::kSettings]); BuildAbout(g_pages[ls::kAbout]);
    g_status = Make(g_main, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, 0, 0, 10, 10, ID_STATUS); Make(g_main, L"BUTTON", L"", BS_PUSHBUTTON, 0, 0, 10, 10, ID_FOOT_ACTION); ShowId(ID_FOOT_ACTION, false);
    g_hints = Make(g_main, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, 0, 0, 10, 10, ID_HINTS); th::MarkMuted(g_hints);
    g_sess.SetModVersion(MOD_VERSION);
    g_sess.SetPuzzleStore([](const std::string& h) { return ped::SetText(g_data, h); }, [](const std::string& h) { return ped::HaveSet(g_data, h); },
                          [](const std::string& h, const std::string& t) { ped::StoreSet(g_data, h, t); });      // the host's own puzzles (Your puzzles): sent to whoever lacks them
#ifndef WL_MUT_NO_STEAM_AT_START
    { wchar_t e[16]; if (!g_autoJoin && GetEnvironmentVariableW(L"SteamAppId", e, 16)) {      // Steam started this session (Play in Steam handed over, K1): the launcher runs the game's Steam API itself, so Steam
        std::string err; g_steamAtStart = g_sess.SteamInit(g_dir, &err) ? "ok" : err;      // sees The Witness running while the player is in the launcher, and an invite accepted in Steam reaches it
        Dlog(("steam: started from Steam; the launcher's Steam API: " + g_steamAtStart).c_str()); } }
#endif
    g_upd.Init(g_dir, g_data, MOD_VERSION, [](const std::string& l) { Dlog(l.c_str()); });
    LoadAll(); g_upd.StartAuto(); SetTimer(g_main, 1, 100, nullptr); SetTimer(g_main, 2, 16, nullptr);
    if (deck) { SetWindowLongPtrW(g_main, GWL_STYLE, WS_POPUP | WS_CLIPCHILDREN); MONITORINFO mi = {sizeof mi}; GetMonitorInfoW(MonitorFromWindow(g_main, MONITOR_DEFAULTTOPRIMARY), &mi); SetWindowPos(g_main, nullptr, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED); fill = false; }
    if (fill && g_noActivate) { MONITORINFO mi = {sizeof mi}; GetMonitorInfoW(MonitorFromWindow(g_main, MONITOR_DEFAULTTOPRIMARY), &mi); SetWindowPos(g_main, nullptr, mi.rcWork.left, mi.rcWork.top, mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top, SWP_NOZORDER | SWP_NOACTIVATE); }
    LayoutAll(); ShowWindow(g_main, g_noActivate ? SW_SHOWNOACTIVATE : (fill ? SW_SHOWMAXIMIZED : SW_SHOW)); LayoutAll();
    nf_log = [](const char* l) { Dlog(l); }; nf_recents_load((g_data + L"\\recents.tsv").c_str());      // B4: recent hosts, in the mod's user data folder
    if (!g_inviteLink.empty()) { g_inviteLink = jg::PasteFromLink(g_inviteLink); g_choice = ls::kJoin; ShowChoiceDetails(); LayoutAll(); SetWindowTextW(LB(ID_LB_INVITE), W(g_inviteLink).c_str()); LobbyStatus(L"An invite came from a link. Check it, then press Join."); startPage = ls::kPlay; }      // a witness-coop:// link: into the join box, never joined without a click (the choice is not remembered)
    if (g_autoJoin) { JoinSteamLobby(g_autoJoin); startPage = 0; } if (startPage < 0 || startPage >= g_npages) startPage = 0;
    ShowSection(startSection); SelectTab(startPage); UpdateTabs(); if (openNew) OpenNewGame();
    if (!dumpTo.empty()) { std::string t = std::to_string(ID_STATUS) + " status\n"; for (auto& f : g_fields) if (f.h) t += std::to_string(f.id) + " " + f.key + "\n"; t += std::to_string(ID_APPLY) + " apply\n" + std::to_string(ID_LAUNCH) + " launch\n" + std::to_string(ID_DIAG) + " diag\n" + std::to_string(ID_SAVEPICK) + " savepick\n" + std::to_string(ID_PRIMARY) + " primary\n" + std::to_string(ID_TILE_SOLO) + " tile:solo\n";
        const std::pair<int, const char*> lb[] = {{ID_LB_INVITE, "lobby:invite"}, {ID_LB_COPY, "lobby:copy"}, {ID_LB_LEAVE, "lobby:leave"}, {ID_LB_READY, "lobby:ready"}, {ID_LB_START, "lobby:start"}, {ID_LB_MEMBERS, "lobby:members"}, {ID_LB_LOG, "lobby:log"}, {ID_LB_CHAT, "lobby:chat"}, {ID_LB_SEND, "lobby:send"}, {ID_LB_MODE, "lobby:mode"}, {ID_LB_GOAL, "lobby:goal"}, {ID_LB_NEWGAME, "lobby:newgame"}, {ID_LB_PARTS, "lobby:parts"}, {ID_LB_STATUS, "lobby:status"}, {ID_LB_MUTE, "lobby:mute"}, {ID_LB_DEAF, "lobby:deaf"}, {ID_LB_RESULT, "lobby:result"}, {ID_LB_STEAM_INVITE, "lobby:steaminvite"}, {ID_LB_STEAM_FRIENDS, "lobby:steamfriends"}, {ID_LB_STEAM_SEND, "lobby:steamsend"}};
        for (const auto& k : lb) t += std::to_string(k.first) + " " + k.second + "\n";
        WriteFile8(dumpTo, t); }
    MSG msg; bool guideChanged = false; while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (CaptureMessage(msg)) continue;
        if ((msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN || msg.message == WM_KEYUP || msg.message == WM_SYSKEYUP) &&
            g_guideHold.Key((unsigned)msg.wParam, msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN, &guideChanged)) {
            // F1 held repeats WM_KEYDOWN about 30 times a second: the card is shown and hidden when the hold starts and ends, not re-shown and repainted on every repeat (that flickered)
#ifdef WL_MUT_GUIDE_REARM
            guideChanged = true;
#endif
            if (guideChanged) ShowGuide(g_guideHold.Open());
            continue;
        }
        if (msg.message == WM_LBUTTONDOWN || msg.message == WM_MOUSEMOVE) g_padLast = false;
        { static POINT last = {-99999, -99999}; POINT pt = msg.pt;      // the mouse in use: no focus line (a WM_MOUSEMOVE without movement, as Windows sends when a window appears under a resting pointer, does not count)
          if (msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN || (msg.message == WM_MOUSEMOVE && last.x != -99999 && (pt.x != last.x || pt.y != last.y))) th::SetNavCues(false);
          if (msg.message == WM_MOUSEMOVE) last = pt; }
        if ((!g_ng || GetForegroundWindow() != g_ng) && KeyMessage(msg)) continue;
        if (!IsDialogMessageW(g_ng ? g_ng : g_main, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    return 0;
}
