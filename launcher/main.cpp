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
#include "shell.h"
#include "../common/handover.h"
#include "../proxy/core/appearance.h"
#include "theme.h"
#include "lobby.h"
#include "lobby_win.h"
#include "../common/steam_ids.h"
#include "../proxy/progress/progress.h"
#include "../common/diag_log.h"
#include "../common/crash_handler.h"
#include "version.h"
#include "update_win.h"
#include "../common/userdata.h"
#include "diag_ps1.h"

namespace {
std::wstring W(const std::string& s) { if (s.empty()) return L""; int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0); std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n); w.resize(n - 1); return w; }
std::string U(const std::wstring& w) { if (w.empty()) return ""; int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr); std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr); s.resize(n - 1); return s; }
std::string ReadFile8(const std::wstring& p) { std::string t; FILE* f = _wfopen(p.c_str(), L"rb"); if (f) { char b[4096]; size_t k; while ((k = fread(b, 1, sizeof b, f)) > 0) t.append(b, k); fclose(f); } return t; }
bool WriteFile8(const std::wstring& p, const std::string& t) { FILE* f = _wfopen(p.c_str(), L"wb"); if (!f) return false; fwrite(t.data(), 1, t.size(), f); fclose(f); return true; }
std::wstring Dir(const std::wstring& p) { size_t s = p.find_last_of(L"\\/"); return s == std::wstring::npos ? L"" : p.substr(0, s); }
bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

enum { ID_TAB = 100, ID_APPLY, ID_LAUNCH, ID_NEWGAME, ID_BROWSE, ID_GAMEDIR, ID_STATUS, ID_REFRESH, ID_OPENSAVES, ID_BACKUP, ID_TRASH, ID_SAVELIST, ID_SAVEINFO, ID_SAVEPICK, ID_DIAG, ID_UP_VER, ID_UP_STATUS, ID_UP_NOTES, ID_UP_CHECK, ID_UP_APPLY, ID_UP_LATER, ID_UP_AUTO, ID_UP_PRE, ID_UP_BANNER, ID_UP_BANNER_APPLY, ID_UP_BANNER_LATER,
       ID_SAVEPICK2, ID_PRIMARY, ID_NAME, ID_TILE_HOST, ID_TILE_JOIN, ID_TILE_SOLO, ID_TILE_HOST_TXT, ID_TILE_JOIN_TXT, ID_TILE_SOLO_TXT, ID_HOST_STEAM, ID_HOST_DIRECT, ID_JOIN_LABEL, ID_SOLO_NOTE, ID_SOLO_NEW, ID_UP_WHATSNEW, ID_HINTS, ID_FOOT_ACTION,
       ID_SECTIONS, ID_RESET, ID_GUIDE_BTN, ID_LINK_HOME, ID_LINK_NOTES, ID_CREDITS, ID_FOLDER_LABEL, ID_ABOUT_TITLE, ID_ABOUT_GUIDE_TXT, ID_ABOUT_DIAG_TXT, ID_SECTION_TITLE, ID_PICK_LABEL, ID_PICK_LABEL2, ID_RESET2,
       ID_NG_OK = 300, ID_NG_LASERS, ID_NG_PRESET, ID_NG_INFO, ID_NG_AREAS = 400, ID_FIELD = 1000 };
enum Kind { kCheck, kCombo, kEdit, kMulti, kSlider, kKeyBtn, kNoteK };
struct Field { int id; ls::Section section; std::string label, key; Kind kind; std::vector<std::string> choices; std::string def, hint; const ls::Row* row; HWND h = nullptr, hLabel = nullptr, hHint = nullptr, hValue = nullptr; };
guide::Hold g_guideHold;
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
void Dlog(const char* text) { if (g_logOpen) g_log.Line(text); }
int g_footAction;                          // the command the footer's next-step button sends (F4), 0: hidden
void Status(const std::wstring& s, const wchar_t* action = nullptr, int cmd = 0) {
    SetWindowTextW(g_status, s.c_str()); Dlog(("status: " + U(s)).c_str());
    HWND b = g_ctl.count(ID_FOOT_ACTION) ? g_ctl[ID_FOOT_ACTION] : nullptr; g_footAction = action ? cmd : 0;
    if (b) { if (action) SetWindowTextW(b, action); ShowWindow(b, action ? SW_SHOW : SW_HIDE); }
}
void LobbyPartsChosen(const std::string& spec);
HWND C(int id); void LoadPlayUi(const wl::CfgFile& c); void LoadSaveOptions(int pick);

void AddFields() {
    int id = ID_FIELD;
    for (const auto& r : ls::Rows()) {
        Kind k = r.kind == ls::kToggle ? kCheck : r.kind == ls::kChoice ? kCombo : r.kind == ls::kSlider ? kSlider : r.kind == ls::kKey ? kKeyBtn : r.kind == ls::kPeers ? kMulti : r.kind == ls::kNote ? kNoteK : kEdit;
        std::string label = r.label + (r.hostDecides ? "  (host decides)" : "");
        g_fields.push_back({id++, r.section, label, r.key, k, r.choices, r.def, r.hint, &r});
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
std::wstring Stamp() { SYSTEMTIME st; GetLocalTime(&st); wchar_t b[40]; swprintf(b, 40, L"%04d-%02d-%02d_%02d.%02d.%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond); return b; }
std::wstring When(unsigned long long ft) { FILETIME f; f.dwLowDateTime = (DWORD)ft; f.dwHighDateTime = (DWORD)(ft >> 32); FILETIME l; SYSTEMTIME st; FileTimeToLocalFileTime(&f, &l); FileTimeToSystemTime(&l, &st); wchar_t b[40]; swprintf(b, 40, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute); return b; }
void RefreshSaves() {
    wl::CfgFile c = LoadCfg(); g_saves = wl::ListSaves(SaveFolder(c)); ListView_DeleteAllItems(g_saveList); SendMessageW(g_savePick, CB_RESETCONTENT, 0, 0); SendMessageW(g_savePick2, CB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < g_saves.size(); ++i) {
        const auto& s = g_saves[i]; LVITEMW it = {}; it.mask = LVIF_TEXT; it.iItem = (int)i; std::wstring w = When(s.when); it.pszText = &w[0]; ListView_InsertItem(g_saveList, &it);
        std::wstring who, prog, las, tm; if (s.hasMeta) { for (size_t k = 0; k < s.meta.players.size(); ++k) who += (k ? L", " : L"") + W(s.meta.players[k].name); wchar_t b[80]; swprintf(b, 80, L"%u%%  (%u of %u)", s.meta.total ? s.meta.solved * 100 / s.meta.total : 0, s.meta.solved, s.meta.total); prog = b; swprintf(b, 80, L"%u of %u", s.meta.lasers, s.meta.lasersTotal); las = b; swprintf(b, 80, L"%u h %02u min", s.meta.totalSeconds / 3600, (s.meta.totalSeconds % 3600) / 60); tm = b; } else who = L"(no co-op record)";
        ListView_SetItemText(g_saveList, (int)i, 1, &who[0]); ListView_SetItemText(g_saveList, (int)i, 2, &prog[0]); ListView_SetItemText(g_saveList, (int)i, 3, &las[0]); ListView_SetItemText(g_saveList, (int)i, 4, &tm[0]);
        SendMessageW(g_savePick, CB_ADDSTRING, 0, (LPARAM)When(s.when).c_str()); SendMessageW(g_savePick2, CB_ADDSTRING, 0, (LPARAM)When(s.when).c_str());
    }
    SendMessageW(g_savePick, CB_SETCURSEL, 0, 0); SendMessageW(g_savePick2, CB_SETCURSEL, 0, 0);
}
void ShowSaveInfo(int i) {
    std::wstring t; if (i < 0 || i >= (int)g_saves.size()) { SetWindowTextW(g_saveInfo, L""); return; }
    const auto& s = g_saves[i]; t = L"File: " + W(s.stem) + L".witness_campaign (" + std::to_wstring(s.bytes / 1024) + L" KB)\r\n";
    if (!s.hasMeta) t += L"The game wrote this save without the mod running, so there is no record of who played or how far.\r\n";
    else {
        const auto& m = s.meta; t += L"Saved " + W(m.savedAt) + L";  playing time " + std::to_wstring(m.totalSeconds / 3600) + L" h " + std::to_wstring((m.totalSeconds % 3600) / 60) + L" min\r\n";
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
    if (auto* f = ByKey("cfg:experimental_tv")) keys.experimentalTv = GetVal(*f) == "1";      // (the guide lists the TV keys only while the option is on)
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
    if (m == WM_CTLCOLORSTATIC || m == WM_CTLCOLOREDIT || m == WM_CTLCOLORLISTBOX || m == WM_CTLCOLORBTN) return th::CtlColor(m, (HDC)w, (HWND)l);
    if (m == WM_DRAWITEM || m == WM_MEASUREITEM) return SendMessageW(g_main, m, w, l);
    if (m == WM_COMMAND || m == WM_HSCROLL) { SendMessageW(g_main, m, w, l); return 0; }
    if (m == WM_NOTIFY) { NMHDR* n = (NMHDR*)l; if (n->idFrom == ID_SAVELIST && n->code == LVN_ITEMCHANGED) { NMLISTVIEW* nl = (NMLISTVIEW*)l; if (nl->uNewState & LVIS_SELECTED) ShowSaveInfo(nl->iItem); } return 0; }
    return DefWindowProcW(h, m, w, l);
}
wup::Updater g_upd; bool g_updFromLobby;
enum { ID_LB_HOST = 600, ID_LB_JOIN, ID_LB_INVITE, ID_LB_COPY, ID_LB_LEAVE, ID_LB_READY, ID_LB_START, ID_LB_MEMBERS, ID_LB_LOG, ID_LB_CHAT, ID_LB_SEND, ID_LB_MODE, ID_LB_GOAL, ID_LB_NEWGAME, ID_LB_PARTS, ID_LB_STATUS, ID_LB_PORT, ID_LB_ADVERTISE, ID_LB_MUTE, ID_LB_DEAF, ID_LB_RESULT, ID_LB_LEVEL, ID_LB_PARTSINFO, ID_LB_WHO, ID_LB_STEAM_HOST, ID_LB_STEAM_INVITE, ID_LB_STEAM_FRIENDS, ID_LB_STEAM_SEND, ID_LB_UPDATE,
       ID_LB_INVITE_SHOW, ID_LB_H_PLAYERS, ID_LB_H_CHAT, ID_LB_H_INVITE, ID_LB_H_PLAY, ID_LB_H_GAME, ID_LB_H_GOAL, ID_LB_H_GOALHINT, ID_LB_H_VOICE, ID_LB_H_STEAMHINT };
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
    wl::CfgFile c = LoadCfg(); lw::VoiceCfg v; std::string m = c.Get("voice_mode", "auto"); v.mode = m == "off" ? 0 : m == "open" ? 1 : m == "ptt" ? 2 : g_padLast ? 1 : 2;      // automatic: open mic when the launcher was last used with a pad Cfg keys;
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
    wlobby::Settings s; s.mode = SendMessageW(LB(ID_LB_MODE), CB_GETCURSEL, 0, 0) == 1 ? wlobby::kRace : wlobby::kCoop; s.raceGoal = EditText(LB(ID_LB_GOAL)); s.newGame = SendMessageW(LB(ID_LB_NEWGAME), BM_GETCHECK, 0, 0) == BST_CHECKED; s.progress = g_lobbySpec; return s;
}
void PushSettings() { if (g_sess.Active() && g_sess.IsHost()) g_sess.SetSettings(SettingsFromUi()); }
void PartsInfo() { SetWindowTextW(LB(ID_LB_PARTSINFO), g_lobbySpec.empty() ? L"nothing done yet" : W(g_lobbySpec).c_str()); }
void RefreshLobbyUi();
void LobbyPartsChosen(const std::string& spec) { g_lobbySpec = spec; PartsInfo(); PushSettings(); }
void HostLobby() {
    wl::CfgFile c = LoadCfg(); std::string err; unsigned port = (unsigned)atoi(c.Get("lobby_port", "47100").c_str()); if (port < 1 || port > 65535) { Status(L"The lobby port is a number from 1 to 65535 (Settings, Network)."); return; }
    uint16_t gp = (uint16_t)atoi(c.Get("listen", "47001").c_str()); std::string name = c.Get("name", "Player");
    if (!g_sess.Host(name, gp, (uint16_t)port, c.Get("lobby_advertise"), &err)) { Status(W(err), L"Try again", ID_PRIMARY); return; }
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
    SetWindowTextW(LB(ID_LB_INVITE_SHOW), L""); g_chatLog.clear(); SetWindowTextW(LB(ID_LB_LOG), L""); PushSettings(); FillFriends(); LobbyStatus(W(g_sess.SteamStatus())); RefreshLobbyUi();
}
void JoinSteamLobby(uint64_t id) {
    wl::CfgFile c = LoadCfg(); std::string err; uint16_t gp = (uint16_t)atoi(c.Get("listen", "47001").c_str());
    if (g_sess.Active() && g_sess.SteamMode() && g_sess.SteamLobbyId() == id) return;
    if (g_game) { LobbyStatus(L"Close the game first, then accept the invite again."); return; }
    if (!g_sess.JoinSteam(id, c.Get("name", "Player"), gp, g_dir, &err)) { LobbyStatus(W(err)); return; }
    SetWindowTextW(LB(ID_LB_INVITE_SHOW), W(steamids::Text(id)).c_str()); g_chatLog.clear(); SetWindowTextW(LB(ID_LB_LOG), L""); LobbyStatus(W(g_sess.SteamStatus())); RefreshLobbyUi();
}
void JoinLobby() {      // the invite field: a Steam lobby number, or address:port/CODE (direct)
    std::string text = EditText(LB(ID_LB_INVITE)); uint64_t lobby = 0;
    if (steamids::ParseLobby(text, &lobby)) { JoinSteamLobby(lobby); return; }
    wl::CfgFile c = LoadCfg(); std::string err; uint16_t gp = (uint16_t)atoi(c.Get("listen", "47001").c_str());
    if (!g_sess.Join(text, c.Get("name", "Player"), gp, &err)) { Status(W(err + "  (or paste the host's Steam lobby number)")); return; }
    SetWindowTextW(LB(ID_LB_INVITE_SHOW), W(text).c_str()); g_chatLog.clear(); SetWindowTextW(LB(ID_LB_LOG), L""); LobbyStatus(L"Joining..."); RefreshLobbyUi();
}
void LeaveLobby() { g_voice.Stop(); g_sess.Stop(); g_shown = wlobby::State(); SendMessageW(LB(ID_LB_MEMBERS), LB_RESETCONTENT, 0, 0); SendMessageW(LB(ID_LB_READY), BM_SETCHECK, BST_UNCHECKED, 0); RefreshLobbyUi(); LobbyStatus(L"You left the lobby."); }
void AppendChat(const std::wstring& line) { g_chatLog += line + L"\r\n"; HWND l = LB(ID_LB_LOG); SetWindowTextW(l, g_chatLog.c_str()); SendMessageW(l, EM_SETSEL, g_chatLog.size(), g_chatLog.size()); SendMessageW(l, EM_SCROLLCARET, 0, 0); }
std::string WaitingFor() { std::string w; for (const auto& m : g_shown.members) if (!m.host && !m.ready) w += (w.empty() ? "" : ", ") + m.name; return w; }
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
    if (g_sess.IsHost()) return; SendMessageW(LB(ID_LB_MODE), CB_SETCURSEL, st.settings.mode == wlobby::kRace ? 1 : 0, 0); SetWindowTextW(LB(ID_LB_GOAL), W(st.settings.raceGoal).c_str());
    SendMessageW(LB(ID_LB_NEWGAME), BM_SETCHECK, st.settings.newGame ? BST_CHECKED : BST_UNCHECKED, 0); g_lobbySpec = st.settings.progress; PartsInfo();
}
void LobbyTick() {
    if (g_game && WaitForSingleObject(g_game, 0) == WAIT_OBJECT_0) GameEnded();
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
    SelectObject(dc, m.host ? th::Bold() : th::Body()); SetTextColor(dc, th::kText); RECT nr = r; nr.left += 34; nr.right -= 230; DrawTextW(dc, W(m.name + (m.id == g_sess.MyId() ? "  (you)" : "")).c_str(), -1, &nr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    { bool differs = !m.version.empty() && m.version != MOD_VERSION; SelectObject(dc, th::Small()); SetTextColor(dc, differs ? th::kAccent : th::kMuted); RECT vr = r; vr.right -= 120; vr.left = r.right - 230; DrawTextW(dc, W(m.version).c_str(), -1, &vr, DT_VCENTER | DT_SINGLELINE | DT_RIGHT | DT_END_ELLIPSIS); }
    const wchar_t* st = m.inGame ? L"in game" : m.host ? L"host" : m.ready ? L"ready" : L"not ready"; SelectObject(dc, th::Small()); SetTextColor(dc, m.inGame ? th::kAccent : m.ready || m.host ? th::kWarm : th::kMuted); RECT sr = r; sr.right -= 12; DrawTextW(dc, st, -1, &sr, DT_VCENTER | DT_SINGLELINE | DT_RIGHT);
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
    for (auto* f : rows) {
        int rh = rowH(f);
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
        else if (f->kind == kKeyBtn) Place(f->h, cx, cy + 1, std::min(cw, 200), 30);
        else Place(f->h, cx, cy + 3, std::min(cw, 300), 28);
        if (f->hHint) Place(f->hHint, x, cy + 34, colW, 18);
        cy += rh; per += rh;
    }
}

// ---- Play: the tiles, or the lobby
void ShowChoiceDetails() {
    bool host = g_choice == ls::kHost, join = g_choice == ls::kJoin, solo = g_choice == ls::kSolo;
    for (int id : {ID_HOST_STEAM, ID_HOST_DIRECT}) ShowId(id, host);
    for (int id : {(int)ID_JOIN_LABEL, (int)ID_LB_INVITE}) ShowId(id, join);
    for (int id : {ID_SOLO_NOTE, ID_SOLO_NEW}) ShowId(id, solo);
    SendMessageW(C(ID_TILE_HOST), BM_SETCHECK, host ? BST_CHECKED : BST_UNCHECKED, 0); SendMessageW(C(ID_TILE_JOIN), BM_SETCHECK, join ? BST_CHECKED : BST_UNCHECKED, 0); SendMessageW(C(ID_TILE_SOLO), BM_SETCHECK, solo ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(C(ID_PRIMARY), W(ls::PrimaryLabel(g_choice, "")).c_str());
}
void LoadPlayUi(const wl::CfgFile& c) {
    SetWindowTextW(C(ID_NAME), W(c.Get("name")).c_str()); g_choice = ls::ParseChoice(c.Get("launcher_choice")); bool direct = c.Get("launcher_host", "steam") == "direct";
    SendMessageW(C(ID_HOST_STEAM), BM_SETCHECK, direct ? BST_UNCHECKED : BST_CHECKED, 0); SendMessageW(C(ID_HOST_DIRECT), BM_SETCHECK, direct ? BST_CHECKED : BST_UNCHECKED, 0); ShowChoiceDetails();
}
void ChooseTile(ls::Choice c) {
    g_choice = c; ShowChoiceDetails();
    wl::CfgFile cfg = LoadCfg(); if (cfg.Get("launcher_choice") != ls::ChoiceKey(c)) { cfg.Set("launcher_choice", ls::ChoiceKey(c)); if (!g_game) WriteFile8(CfgPath(), cfg.Text()); }      // A2: the last choice is remembered
}
void BuildPlay(HWND pg) {
    g_tiles = Panel(pg); g_lobbyPanel = Panel(pg); HWND t = g_tiles;
    th::MarkMuted(Make(t, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, 0, 0, 10, 10, ID_UP_BANNER)); Make(t, L"BUTTON", L"Update", BS_PUSHBUTTON, 0, 0, 10, 10, ID_UP_BANNER_APPLY); Make(t, L"BUTTON", L"What's new", BS_PUSHBUTTON, 0, 0, 10, 10, ID_UP_WHATSNEW); Make(t, L"BUTTON", L"Later", BS_PUSHBUTTON, 0, 0, 10, 10, ID_UP_BANNER_LATER);
    for (int id : {ID_UP_BANNER, ID_UP_BANNER_APPLY, ID_UP_WHATSNEW, ID_UP_BANNER_LATER}) ShowId(id, false);
    Make(t, L"STATIC", L"Your name", SS_LEFT, 0, 0, 10, 10, ID_FOLDER_LABEL + 100); Make(t, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 0, 0, 10, 10, ID_NAME);
    Make(t, L"BUTTON", L"Host a game", BS_AUTORADIOBUTTON | WS_GROUP, 0, 0, 10, 10, ID_TILE_HOST); Make(t, L"BUTTON", L"Join a friend", BS_AUTORADIOBUTTON, 0, 0, 10, 10, ID_TILE_JOIN); Make(t, L"BUTTON", L"Play solo", BS_AUTORADIOBUTTON, 0, 0, 10, 10, ID_TILE_SOLO);
    th::MarkMuted(Make(t, L"STATIC", L"Open a lobby; friends join you there, you choose what to play and start.", SS_LEFT, 0, 0, 10, 10, ID_TILE_HOST_TXT));
    th::MarkMuted(Make(t, L"STATIC", L"Accept a Steam invite, or paste the invite or the Steam lobby number a friend sent.", SS_LEFT, 0, 0, 10, 10, ID_TILE_JOIN_TXT));
    th::MarkMuted(Make(t, L"STATIC", L"The game with co-op ready, on your latest save.", SS_LEFT, 0, 0, 10, 10, ID_TILE_SOLO_TXT));
    Make(t, L"BUTTON", L"Through Steam (friends; no port forwarding)", BS_AUTORADIOBUTTON | WS_GROUP, 0, 0, 10, 10, ID_HOST_STEAM); Make(t, L"BUTTON", L"Direct (same network, VPN or a forwarded port)", BS_AUTORADIOBUTTON, 0, 0, 10, 10, ID_HOST_DIRECT);
    Make(t, L"STATIC", L"Invite or Steam lobby number", SS_LEFT, 0, 0, 10, 10, ID_JOIN_LABEL); Make(t, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 0, 0, 10, 10, ID_LB_INVITE);
    th::MarkMuted(Make(t, L"STATIC", L"Continue your latest save, or start a new game with chosen parts of the map done (also on the Saves tab).", SS_LEFT, 0, 0, 10, 10, ID_SOLO_NOTE)); Make(t, L"BUTTON", L"New game...", BS_PUSHBUTTON, 0, 0, 10, 10, ID_SOLO_NEW);
    Make(t, L"BUTTON", L"Open lobby", BS_DEFPUSHBUTTON, 0, 0, 10, 10, ID_PRIMARY);
    BuildLobbyPanel(g_lobbyPanel);
}
void LayoutPlay(int W, int H) {
    Place(g_tiles, 0, 0, W, H); Place(g_lobbyPanel, 0, 0, W, H);
    int m = 24, y = 12;
    PlaceId(ID_UP_BANNER, m, y + 6, W - 2 * m - 390, 22); PlaceId(ID_UP_BANNER_APPLY, W - m - 380, y, 120, 32); PlaceId(ID_UP_WHATSNEW, W - m - 250, y, 130, 32); PlaceId(ID_UP_BANNER_LATER, W - m - 110, y, 110, 32);
    y += 48; PlaceId(ID_FOLDER_LABEL + 100, m, y + 6, 120, 22); PlaceId(ID_NAME, m + 130, y + 2, 320, 30);
    y += 56; int tw = (W - 2 * m - 2 * 20) / 3;
    int ids[3] = {ID_TILE_HOST, ID_TILE_JOIN, ID_TILE_SOLO}, txt[3] = {ID_TILE_HOST_TXT, ID_TILE_JOIN_TXT, ID_TILE_SOLO_TXT};
    for (int i = 0; i < 3; ++i) { PlaceId(ids[i], m + i * (tw + 20), y, tw, 34); PlaceId(txt[i], m + i * (tw + 20) + 26, y + 38, tw - 26, 44); }
    y += 100; PlaceId(ID_HOST_STEAM, m, y, std::min(W - 2 * m, 520), 30); PlaceId(ID_HOST_DIRECT, m, y + 34, std::min(W - 2 * m, 520), 30);
    PlaceId(ID_JOIN_LABEL, m, y + 6, 240, 22); PlaceId(ID_LB_INVITE, m + 250, y + 2, std::min(W - 2 * m - 250, 520), 30);
    PlaceId(ID_SOLO_NOTE, m, y + 4, std::min(W - 2 * m - 200, 760), 44); PlaceId(ID_SOLO_NEW, m, y + 50, 160, 34);
    y += 104; PlaceId(ID_PRIMARY, m, y, 300, 48);
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
    int y = y0; PlaceId(ID_LB_H_INVITE, c3, y, c3w, 20); y += 24;
    PlaceId(ID_LB_INVITE_SHOW, c3, y, c3w, 30); y += 38; PlaceId(ID_LB_COPY, c3, y, (c3w - 8) / 2, 34); PlaceId(ID_LB_STEAM_INVITE, c3 + (c3w + 8) / 2, y, (c3w - 8) / 2, 34); y += 42;
    PlaceId(ID_LB_STEAM_FRIENDS, c3, y + 2, c3w - 130, 300); PlaceId(ID_LB_STEAM_SEND, c3 + c3w - 122, y, 122, 34); y += 40; PlaceId(ID_LB_H_STEAMHINT, c3, y, c3w, 20); y += 34;
    PlaceId(ID_LB_H_PLAY, c3, y, c3w, 20); y += 26; PlaceId(ID_LB_H_GAME, c3, y + 6, 90, 22); PlaceId(ID_LB_MODE, c3 + 96, y + 2, c3w - 96, 200); y += 38;
    PlaceId(ID_LB_H_GOAL, c3, y + 6, 90, 22); PlaceId(ID_LB_GOAL, c3 + 96, y + 2, c3w - 96, 30); y += 34; PlaceId(ID_LB_H_GOALHINT, c3 + 96, y, c3w - 96, 20); y += 26;
    PlaceId(ID_LB_NEWGAME, c3, y, c3w, 28); y += 34; PlaceId(ID_LB_PARTS, c3, y, c3w, 34); y += 40; PlaceId(ID_LB_PARTSINFO, c3, y, c3w, 20); y += 26; PlaceId(ID_LB_RESULT, c3, y, c3w, std::max(20, H - y - 8));
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
    head(L"INVITE", ID_LB_H_INVITE); mk(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | ES_READONLY, ID_LB_INVITE_SHOW);
    mk(L"BUTTON", L"Copy invite", BS_PUSHBUTTON, ID_LB_COPY); mk(L"BUTTON", L"Steam overlay", BS_PUSHBUTTON, ID_LB_STEAM_INVITE);
    { HWND fc = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, ID_LB_STEAM_FRIENDS); SendMessageW(fc, CB_ADDSTRING, 0, (LPARAM)L"(Steam lobby only)"); SendMessageW(fc, CB_SETCURSEL, 0, 0); }
    mk(L"BUTTON", L"Invite friend", BS_PUSHBUTTON, ID_LB_STEAM_SEND); head(L"Friends join by the invite, or through Steam.", ID_LB_H_STEAMHINT);
    head(L"WHAT TO PLAY  (the host chooses)", ID_LB_H_PLAY); mk(L"STATIC", L"Game", SS_LEFT, ID_LB_H_GAME);
    { HWND mc = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, ID_LB_MODE); SendMessageW(mc, CB_ADDSTRING, 0, (LPARAM)L"Co-op"); SendMessageW(mc, CB_ADDSTRING, 0, (LPARAM)L"Race"); SendMessageW(mc, CB_SETCURSEL, 0, 0); }
    mk(L"STATIC", L"Race goal", SS_LEFT, ID_LB_H_GOAL); mk(L"EDIT", L"panel:9F7F", WS_BORDER | ES_AUTOHSCROLL, ID_LB_GOAL); head(L"panel:ID or count:N", ID_LB_H_GOALHINT);
    HWND ng = mk(L"BUTTON", L"New game", BS_AUTOCHECKBOX, ID_LB_NEWGAME); SendMessageW(ng, BM_SETCHECK, BST_CHECKED, 0); mk(L"BUTTON", L"Parts of the map done...", BS_PUSHBUTTON, ID_LB_PARTS); head(L"", ID_LB_PARTSINFO);
    head(L"", ID_LB_RESULT); PartsInfo(); RefreshLobbyUi();
}

// ---- Saves and About
void BuildSaves(HWND sp) {
    g_saveList = Make(sp, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 0, 0, 10, 10, ID_SAVELIST, WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(g_saveList, LVS_EX_FULLROWSELECT); th::StyleList(g_saveList);
    const wchar_t* cols[] = {L"Saved", L"Players", L"Progress", L"Lasers", L"Time played"}; int cw[] = {150, 330, 150, 90, 130};
    for (int i = 0; i < 5; ++i) { LVCOLUMNW c = {}; c.mask = LVCF_TEXT | LVCF_WIDTH; c.pszText = (LPWSTR)cols[i]; c.cx = cw[i]; ListView_InsertColumn(g_saveList, i, &c); }
    g_saveInfo = Make(sp, L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL, 0, 0, 10, 10, ID_SAVEINFO);
    Make(sp, L"BUTTON", L"New game...", BS_DEFPUSHBUTTON, 0, 0, 10, 10, ID_NEWGAME); Make(sp, L"BUTTON", L"Back up", BS_PUSHBUTTON, 0, 0, 10, 10, ID_BACKUP); Make(sp, L"BUTTON", L"Move to backup folder", BS_PUSHBUTTON, 0, 0, 10, 10, ID_TRASH);
    Make(sp, L"BUTTON", L"Open folder", BS_PUSHBUTTON, 0, 0, 10, 10, ID_OPENSAVES); Make(sp, L"BUTTON", L"Refresh", BS_PUSHBUTTON, 0, 0, 10, 10, ID_REFRESH);
}
void LayoutSaves(int W, int H) {
    int m = 16; Place(g_saveList, m, m, W - 2 * m, (H - 80) * 45 / 100); int y = m + (H - 80) * 45 / 100 + 10; Place(g_saveInfo, m, y, W - 2 * m, H - y - 60);
    int by = H - 48, x = m; for (auto p : {std::make_pair(ID_NEWGAME, 160), std::make_pair(ID_BACKUP, 120), std::make_pair(ID_TRASH, 220), std::make_pair(ID_OPENSAVES, 140), std::make_pair(ID_REFRESH, 110)}) { PlaceId(p.first, x, by, p.second, 36); x += p.second + 10; }
}
void BuildAbout(HWND pg) {
    HWND t = Make(pg, L"STATIC", (L"The Witness co-op, version " + W(MOD_VERSION)).c_str(), SS_LEFT, 0, 0, 10, 10, ID_UP_VER); SendMessageW(t, WM_SETFONT, (WPARAM)th::Bold(), TRUE);
    Make(pg, L"BUTTON", L"Check for updates", BS_DEFPUSHBUTTON, 0, 0, 10, 10, ID_UP_CHECK); Make(pg, L"BUTTON", L"Update", BS_PUSHBUTTON, 0, 0, 10, 10, ID_UP_APPLY); Make(pg, L"BUTTON", L"Later", BS_PUSHBUTTON, 0, 0, 10, 10, ID_UP_LATER);
    Make(pg, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, 0, 0, 10, 10, ID_UP_STATUS); Make(pg, L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL, 0, 0, 10, 10, ID_UP_NOTES);
    Make(pg, L"BUTTON", L"Check for updates when the launcher starts (once a day at most; nothing about you is sent)", BS_AUTOCHECKBOX, 0, 0, 10, 10, ID_UP_AUTO);
    Make(pg, L"BUTTON", L"Quick guide", BS_PUSHBUTTON, 0, 0, 10, 10, ID_GUIDE_BTN); th::MarkMuted(Make(pg, L"STATIC", L"Every co-op key and what it does (hold F1 anywhere in the launcher or the game).", SS_LEFT, 0, 0, 10, 10, ID_ABOUT_GUIDE_TXT));
    Make(pg, L"BUTTON", L"Send diagnostics", BS_PUSHBUTTON, 0, 0, 10, 10, ID_DIAG); th::MarkMuted(Make(pg, L"STATIC", L"Report a problem: a zip of logs and settings on your Desktop (names and addresses replaced, nothing uploaded).", SS_LEFT, 0, 0, 10, 10, ID_ABOUT_DIAG_TXT));
    Make(pg, L"BUTTON", L"Project page", BS_PUSHBUTTON, 0, 0, 10, 10, ID_LINK_HOME); Make(pg, L"BUTTON", L"Release notes", BS_PUSHBUTTON, 0, 0, 10, 10, ID_LINK_NOTES);
    th::MarkMuted(Make(pg, L"STATIC", L"Credits: The Witness is by Thekla, Inc. This co-op mod is a fan project (MIT licence), not made or endorsed by Thekla. Steam features go through the game's own Steam library.", SS_LEFT, 0, 0, 10, 10, ID_CREDITS));
    Make(pg, L"STATIC", L"Game folder", SS_LEFT, 0, 0, 10, 10, ID_FOLDER_LABEL); Make(pg, L"EDIT", g_dir.c_str(), WS_BORDER | ES_AUTOHSCROLL | ES_READONLY, 0, 0, 10, 10, ID_GAMEDIR); Make(pg, L"BUTTON", L"Browse...", BS_PUSHBUTTON, 0, 0, 10, 10, ID_BROWSE);
}
void LayoutAbout(int W, int H) {
    (void)H; int m = 24, y = 16, cw = std::min(W - 2 * m, 1000);
    PlaceId(ID_UP_VER, m, y, cw, 28); y += 40; PlaceId(ID_UP_CHECK, m, y, 200, 36); PlaceId(ID_UP_APPLY, m + 210, y, 110, 36); PlaceId(ID_UP_LATER, m + 330, y, 100, 36); y += 44;
    PlaceId(ID_UP_STATUS, m, y, cw, 22); y += 26; PlaceId(ID_UP_NOTES, m, y, cw, 90); y += 98; PlaceId(ID_UP_AUTO, m, y, cw, 26); y += 44;
    PlaceId(ID_GUIDE_BTN, m, y, 200, 36); PlaceId(ID_ABOUT_GUIDE_TXT, m + 214, y + 8, cw - 214, 22); y += 46;
    PlaceId(ID_DIAG, m, y, 200, 36); PlaceId(ID_ABOUT_DIAG_TXT, m + 214, y + 8, cw - 214, 22); y += 46;
    PlaceId(ID_LINK_HOME, m, y, 200, 36); PlaceId(ID_LINK_NOTES, m + 210, y, 200, 36); y += 50;
    PlaceId(ID_FOLDER_LABEL, m, y + 6, 120, 22); PlaceId(ID_GAMEDIR, m + 130, y + 2, cw - 130 - 120, 30); PlaceId(ID_BROWSE, m + cw - 110, y, 110, 34); y += 50;
    PlaceId(ID_CREDITS, m, y, cw, 44);
}

// ---- the update banner (Play) and the About block follow the updater's state
void UpdateUi() {
    static std::string lastMsg; static int lastState = -1; static bool lastRefused, lastLobby;
    wup::Snapshot s = g_upd.Get(); bool refused = g_sess.Active() && !g_sess.IsHost() && g_sess.Refused() == wlobby::kVersionMismatch, lobby = g_sess.Active();
    if (s.message == lastMsg && (int)s.state == lastState && refused == lastRefused && lobby == lastLobby) return; lastMsg = s.message; lastState = (int)s.state; lastRefused = refused; lastLobby = lobby;
    bool avail = s.state == wup::kAvailable;
    SetWindowTextW(C(ID_UP_STATUS), W(s.message).c_str()); SetWindowTextW(C(ID_UP_NOTES), W(avail || s.state == wup::kApplied ? s.tag + "\r\n" + std::string(s.notes) : "").c_str());
    for (int id : {ID_UP_APPLY, ID_UP_LATER}) ShowId(id, avail && !s.managed);      // (a mod manager's install: the manager updates it, the message says so)
    EnableWindow(C(ID_UP_CHECK), !g_upd.Busy()); SendMessageW(C(ID_UP_AUTO), BM_SETCHECK, s.autoCheck ? BST_CHECKED : BST_UNCHECKED, 0);
    if (Field* f = ByKey("upd:auto")) SendMessageW(f->h, BM_SETCHECK, s.autoCheck ? BST_CHECKED : BST_UNCHECKED, 0);
    if (Field* f = ByKey("upd:pre")) SendMessageW(f->h, BM_SETCHECK, s.prereleases == 1 ? BST_CHECKED : BST_UNCHECKED, 0);
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
        else if (cmd == "steamhost") HostSteamLobby();
        else if (cmd == "steamjoin") { uint64_t id = 0; if (steamids::ParseLobby(arg, &id)) JoinSteamLobby(id); else LobbyStatus(L"Not a Steam lobby number."); }
        else if (cmd == "leave") LeaveLobby();
        else if (cmd == "ready") { SendMessageW(LB(ID_LB_READY), BM_SETCHECK, arg == "1" ? BST_CHECKED : BST_UNCHECKED, 0); g_sess.SetReady(arg == "1"); }
        else if (cmd == "chat") { SetWindowTextW(LB(ID_LB_CHAT), W(arg).c_str()); SendChatLine(); }
        else if (cmd == "mode") { SendMessageW(LB(ID_LB_MODE), CB_SETCURSEL, arg == "race" ? 1 : 0, 0); PushSettings(); }
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
        else if (cmd == "section") ShowSection(atoi(arg.c_str()));
        else if (cmd == "choice") ChooseTile(ls::ParseChoice(arg));
        else if (cmd == "primary") SendMessageW(g_main, WM_COMMAND, ID_PRIMARY, 0);
        else if (cmd == "pad") { unsigned b = 0; int lx = 0, ly = 0; if (sscanf(arg.c_str(), "%x %d %d", &b, &lx, &ly) >= 1) { g_injectOn = true; g_injected.connected = true; g_injected.buttons = (uint16_t)b; g_injected.lx = (int16_t)lx; g_injected.ly = (int16_t)ly; PadPoll(); } }
        else if (cmd == "padoff") { g_injectOn = false; }
        else if (cmd == "focus") { if (Field* f = ByKey(arg)) SetFocus(f->h); else if (HWND h = C(atoi(arg.c_str()))) SetFocus(h); }
        else if (cmd == "setting") { std::string k = arg.substr(0, arg.find(' ')), v = arg.find(' ') == std::string::npos ? "" : arg.substr(arg.find(' ') + 1); if (Field* f = ByKey(k)) { SetVal(*f, v); OnFieldChanged(*f); } }
        else if (cmd == "capture") { std::string k = arg.substr(0, arg.find(' ')); unsigned vk = (unsigned)strtoul(arg.substr(arg.find(' ') + 1).c_str(), nullptr, 0); if (Field* f = ByKey(k)) { StartCapture(*f); EndCapture(vk); } }
        else if (cmd == "size") { int w = 0, h = 0; if (sscanf(arg.c_str(), "%d %d", &w, &h) == 2) { ShowWindow(g_main, SW_RESTORE); RECT r = {0, 0, w, h}; AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE); SetWindowPos(g_main, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE); } }
        else if (cmd == "quit") PostMessageW(g_main, WM_CLOSE, 0, 0);
    }
}
void WriteStat() {      // tests: what the lobby is, as text lines, rewritten every 250 ms
    if (g_statFile.empty() || GetTickCount64() - g_statAt < 250) return; g_statAt = GetTickCount64(); char b[600]; std::string o;
    wlobby::State st = g_sess.Active() ? g_sess.State() : wlobby::State(); lw::VoiceStats v = g_voice.Stats();
    snprintf(b, sizeof b, "tabpaints %u\nlobby active %d host %d joined %d id %u phase %d start_in %d refused %d\n", th::TabPaintCount(), g_sess.Active() ? 1 : 0, g_sess.IsHost() ? 1 : 0, g_sess.Joined() ? 1 : 0, g_sess.MyId(), (int)st.phase, st.startInMs, (int)g_sess.Refused()); o += b;
    { wup::Snapshot us = g_upd.Get(); snprintf(b, sizeof b, "version mod %s host %s\nupdate state %d tag %s busy %d auto %d\n", MOD_VERSION, g_sess.HostVersion().empty() ? "-" : g_sess.HostVersion().c_str(), (int)us.state, us.tag.empty() ? "-" : us.tag.c_str(), g_upd.Busy() ? 1 : 0, us.autoCheck ? 1 : 0); o += b; }
    { wchar_t sb[400] = {}; GetWindowTextW(LB(ID_LB_STATUS), sb, 400); o += "lobbystatus " + U(sb) + "\n"; }
    o += std::string("guide ") + (g_guideHold.Open() ? "1\n" : "0\n");
    snprintf(b, sizeof b, "invite %s\n", g_sess.Invite().c_str()); o += b;
    { uint64_t lid = g_sess.SteamLobbyId(); std::string mod = lid ? g_sess.SteamReadData(lsteam::kModKey) : "", proto = lid ? g_sess.SteamReadData(lsteam::kProtocolKey) : "", code = lid ? g_sess.SteamReadData(lsteam::kTokenKey) : "";
      snprintf(b, sizeof b, "steam mode %d lobby %llu members %u limit %u mod %s protocol %s code_set %d failed %d status %s\n", g_sess.SteamMode() ? 1 : 0, (unsigned long long)lid, g_sess.SteamMemberCount(), lid ? g_sess.SteamMemberLimit() : 0u, mod.c_str(), proto.c_str(), code.empty() ? 0 : 1, g_sess.SteamFailed() ? 1 : 0, g_sess.SteamStatus().c_str()); o += b; }
    for (const auto& m : st.members) { snprintf(b, sizeof b, "member %u name %s host %d ready %d ingame %d speaking %d playing %d port %u ip %s\n", m.id, m.name.c_str(), m.host, m.ready, m.inGame, m.speaking, m.playing, m.gamePort, wlobby::IpText(m.ip).c_str()); o += b; snprintf(b, sizeof b, "member_version %u %s\n", m.id, m.version.c_str()); o += b; }
    snprintf(b, sizeof b, "settings mode %d goal %s newgame %d progress %s\n", (int)st.settings.mode, st.settings.raceGoal.c_str(), st.settings.newGame ? 1 : 0, st.settings.progress.c_str()); o += b;
    { std::string chat = U(g_chatLog); for (size_t i; (i = chat.find("\r\n")) != std::string::npos;) chat.replace(i, 2, " | "); o += "result " + st.lastResult + "\n"; o += "chat " + chat + "\n"; }
    snprintf(b, sizeof b, "voice running %d sent %u received %u level %u speaking %d muted_ui %d\n", g_voice.Running() ? 1 : 0, v.sent, v.received, v.level, v.speaking ? 1 : 0, (int)SendMessageW(LB(ID_LB_MUTE), BM_GETCHECK, 0, 0)); o += b;
    for (const auto& s : v.speakers) { snprintf(b, sizeof b, "voice_speaker %u talking %d rms %.4f tone %.4f %.4f %.4f packets %u\n", s.id, s.talking ? 1 : 0, s.rms, s.tone[0], s.tone[1], s.tone[2], s.packets); o += b; }
    snprintf(b, sizeof b, "game pid %lu running %d launches %u\n", (unsigned long)g_gamePid, g_game ? 1 : 0, g_launches); o += b;
    { wchar_t sb[400] = {}, hb[400] = {}; GetWindowTextW(g_status, sb, 400); GetWindowTextW(g_hints, hb, 400); HWND f = GetFocus(); Field* ff = f ? ByHwnd(f) : nullptr; RECT cr; GetClientRect(g_main, &cr); std::string fk = ff ? ff->key : (f ? std::to_string(GetDlgCtrlID(f)) : std::string("-"));
      snprintf(b, sizeof b, "ui tab %d label %s lobbyview %d section %d choice %s padlast %d capture %d focus %s client %ld %ld\n", CurTab(), ls::TabLabel(CurTab(), g_sess.Active()), IsWindowVisible(g_lobbyPanel) ? 1 : 0, g_section, ls::ChoiceKey(g_choice), g_padLast ? 1 : 0, g_capture, fk.c_str(), cr.right, cr.bottom); o += b;
      o += "uistatus " + U(sb) + "\n" + "hints " + U(hb) + "\n" + "steamatstart " + g_steamAtStart + "\n" + "joinbox " + EditText(LB(ID_LB_INVITE)) + "\n"; }
    std::wstring tmp = g_statFile + L".tmp"; if (WriteFile8(tmp, o)) MoveFileExW(tmp.c_str(), g_statFile.c_str(), MOVEFILE_REPLACE_EXISTING);
}


LRESULT CALLBACK GuideProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_ERASEBKGND) return 1;
    if (m != WM_PAINT) return DefWindowProcW(h, m, w, l);
    PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
    FillRect(dc, &rc, th::BgBrush()); SetBkMode(dc, TRANSPARENT);
    auto cols = guide::Wrap(guide::Content(GuideSettings(true)));
    auto layout = guide::Fit(float(rc.right), float(rc.bottom), cols);
    int cw = int(layout.columnWidth), fh = std::max(1, int(layout.rowHeight));
    HFONT font = CreateFontW(-std::max(1, int(20 * layout.scale) - 2), std::max(1, int(10 * layout.scale)), 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
    HGDIOBJ old = SelectObject(dc, th::Bold()); SetTextColor(dc, th::kWarm);
    const wchar_t* title = L"THE WITNESS CO-OP - QUICK GUIDE";
    TextOutW(dc, 16, 12, title, (int)wcslen(title));
    SelectObject(dc, th::Small()); SetTextColor(dc, th::kAccent);
    const wchar_t* hint = L"Hold F1 to read / release to return. Keys reflect current launcher settings.";
    TextOutW(dc, 16, 40, hint, (int)wcslen(hint)); SelectObject(dc, font);
    for (int c = 0; c < 3; ++c) {
        int y = 70;
        for (const auto& line : cols[c]) {
            bool heading = !line.empty() && line.find_first_of("abcdefghijklmnopqrstuvwxyz") == std::string::npos;
            SetTextColor(dc, heading ? th::kWarm : th::kText);
            std::wstring text = W(line); TextOutW(dc, 16 + c * cw, y, text.c_str(), (int)text.size()); y += fh;
        }
    }
    SelectObject(dc, old); DeleteObject(font); EndPaint(h, &ps); return 0;
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
    SetWindowPos(g_guide, HWND_TOP, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
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
    int header = h >= 700 ? 84 : 64, foot = 64;
    Place(g_tab, 8, header, w - 16, h - header - foot);
    RECT r; GetClientRect(g_tab, &r); TabCtrl_AdjustRect(g_tab, FALSE, &r); int pw = r.right - r.left, ph = r.bottom - r.top;
    for (int i = 0; i < g_npages; ++i) Place(g_pages[i], r.left, r.top, pw, ph);
    LayoutPlay(pw, ph); LayoutLobby(pw, ph); LayoutSaves(pw, ph); LayoutAbout(pw, ph);
    Place(g_secList, 12, 12, 240, ph - 24);
    for (int s = 0; s < ls::kSectionCount; ++s) { Place(g_secPanel[s], 270, 8, pw - 282, ph - 16); LayoutSection(g_secPanel[s], s, pw - 282, ph - 16); }
    Place(g_status, 16, h - foot + 8, w - 300, 24); Place(C(ID_FOOT_ACTION), w - 270, h - foot + 4, 254, 30); Place(g_hints, 16, h - 28, w - 32, 22);
    InvalidateRect(g_main, nullptr, TRUE);
}
void OnFieldChanged(Field& f) {
    if (g_loading) return;
    if (f.key == "upd:auto") { g_upd.SetAutoCheck(SendMessageW(f.h, BM_GETCHECK, 0, 0) == BST_CHECKED); return; }
    if (f.key == "upd:pre") { g_upd.SetPrereleases(SendMessageW(f.h, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : -1); return; }
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
    g_padLast = true; HWND f = GetFocus(); std::wstring cls = ClassName(f); Field* fld = f ? ByHwnd(f) : nullptr;
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
    if (sel) { HBRUSH a = CreateSolidBrush(th::kAccent); RECT ar = {r.left, r.top + 6, r.left + 4, r.bottom - 6}; FillRect(dc, &ar, a); DeleteObject(a); }
    SetBkMode(dc, TRANSPARENT); SelectObject(dc, sel ? th::Bold() : th::Body()); SetTextColor(dc, sel ? th::kText : (focus ? th::kLine : th::kMuted)); RECT tr = r; tr.left += 16;
    DrawTextW(dc, W(ls::SectionName((int)d->itemID)).c_str(), -1, &tr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}
LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ACTIVATEAPP: if (!w) { g_guideHold.Reset(); ShowGuide(false); } break;
    case WM_ERASEBKGND: { RECT rc; GetClientRect(h, &rc); FillRect((HDC)w, &rc, th::BgBrush()); return 1; }
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc); th::PaintHeader(dc, rc, rc.bottom >= 700 ? 84 : 64, L"Co-op for The Witness   |   hold F1: quick guide"); EndPaint(h, &ps); return 0; }
    case WM_SIZE: LayoutAll(); return 0;
    case WM_GETMINMAXINFO: { MINMAXINFO* mm = (MINMAXINFO*)l; mm->ptMinTrackSize.x = 1024; mm->ptMinTrackSize.y = 640; return 0; }
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN: return th::CtlColor(m, (HDC)w, (HWND)l);
    case WM_DRAWITEM: { DRAWITEMSTRUCT* d = (DRAWITEMSTRUCT*)l; if (d->CtlID == ID_LB_MEMBERS) DrawMember(d); else if (d->CtlID == ID_LB_LEVEL) DrawLevel(d); else if (d->CtlID == ID_SECTIONS) DrawSectionItem(d); return TRUE; }
    case WM_HSCROLL: { Field* f = ByHwnd((HWND)l); if (f && f->kind == kSlider) { SliderText(*f); if (LOWORD(w) == TB_ENDTRACK) OnFieldChanged(*f); } return 0; }
    case WM_TIMER:
        if (w == 1) { PollCommands(); LobbyTick(); WriteStat(); DiagPoll(); UpdateUi(); UpdateTabs(); if (g_updFromLobby && g_upd.Available()) { g_updFromLobby = false; g_upd.StartApply(); } }
        else if (w == 2) PadPoll();
        return 0;
    case WM_CLOSE: g_voice.Stop(); g_sess.Stop(); DestroyWindow(h); return 0;
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
            else if (g_choice == ls::kJoin) { if (EditText(LB(ID_LB_INVITE)).empty()) { Status(L"Paste the invite or the Steam lobby number your friend sent, then Join."); SetFocus(LB(ID_LB_INVITE)); } else JoinLobby(); }
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
        else if (id == ID_LB_LEAVE) { LeaveLobby(); UpdateTabs(); FocusFirst(g_tiles); }
        else if (id == ID_LB_COPY) { std::wstring inv = W(g_sess.Invite()); if (!inv.empty() && OpenClipboard(h)) { EmptyClipboard(); HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (inv.size() + 1) * 2); if (g) { memcpy(GlobalLock(g), inv.c_str(), (inv.size() + 1) * 2); GlobalUnlock(g); SetClipboardData(CF_UNICODETEXT, g); } CloseClipboard(); LobbyStatus(L"The invite is on the clipboard."); } }
        else if (id == ID_LB_READY && code == BN_CLICKED) g_sess.SetReady(SendMessageW(LB(ID_LB_READY), BM_GETCHECK, 0, 0) == BST_CHECKED);
        else if (id == ID_LB_START) { PushSettings(); if (!g_sess.Start()) LobbyStatus(L"Everybody has to be ready first: " + W(WaitingFor()) + L"."); }
        else if (id == ID_LB_SEND) SendChatLine();
        else if (id == ID_LB_MODE && code == CBN_SELCHANGE) PushSettings();
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
    th::Init(); AddFields();
    WNDCLASSW wc = {}; wc.lpfnWndProc = MainProc; wc.hInstance = hi; wc.lpszClassName = L"WitnessLauncher"; wc.hbrBackground = th::BgBrush(); wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION); RegisterClassW(&wc);
    WNDCLASSW pc = {}; pc.lpfnWndProc = PageProc; pc.hInstance = hi; pc.lpszClassName = L"WitnessLauncherPage"; pc.hbrBackground = th::BgBrush(); pc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&pc);
    // 1280x800 (a Steam Deck's screen) or the work area if that is smaller; resizable (K8)
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0); int ww = sizeW ? sizeW : std::min(1280, (int)(wa.right - wa.left)), wh = sizeH ? sizeH : std::min(800, (int)(wa.bottom - wa.top));
    if (!sizeW && (wa.right - wa.left <= 1280 || wa.bottom - wa.top <= 800)) fill = true;
    RECT want = {0, 0, ww, wh}; if (sizeW) AdjustWindowRect(&want, WS_OVERLAPPEDWINDOW, FALSE);
    g_main = CreateWindowExW(WS_EX_CONTROLPARENT, L"WitnessLauncher", (std::wstring(L"The Witness co-op: launcher ") + W(MOD_VERSION)).c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, want.right - want.left, want.bottom - want.top, nullptr, nullptr, hi, nullptr);
    g_tab = CreateWindowExW(WS_EX_CONTROLPARENT, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 8, 88, 600, 400, g_main, (HMENU)ID_TAB, hi, nullptr); SendMessageW(g_tab, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    static TCITEMW items[ls::kTabCount]; static std::wstring names[ls::kTabCount];
    for (int i = 0; i < ls::kTabCount; ++i) { names[i] = W(ls::TabLabel(i, false)); items[i] = {}; items[i].mask = TCIF_TEXT; items[i].pszText = &names[i][0]; TabCtrl_InsertItem(g_tab, i, &items[i]); }
    th::StyleTab(g_tab);
    for (int i = 0; i < g_npages; ++i) g_pages[i] = Panel(g_tab);
    BuildPlay(g_pages[ls::kPlay]); BuildSaves(g_pages[ls::kSaves]); BuildSettings(g_pages[ls::kSettings]); BuildAbout(g_pages[ls::kAbout]);
    g_status = Make(g_main, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, 0, 0, 10, 10, ID_STATUS); Make(g_main, L"BUTTON", L"", BS_PUSHBUTTON, 0, 0, 10, 10, ID_FOOT_ACTION); ShowId(ID_FOOT_ACTION, false);
    g_hints = Make(g_main, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, 0, 0, 10, 10, ID_HINTS); th::MarkMuted(g_hints);
    g_sess.SetModVersion(MOD_VERSION);
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
    if (!g_inviteLink.empty()) { g_choice = ls::kJoin; ShowChoiceDetails(); SetWindowTextW(LB(ID_LB_INVITE), W(g_inviteLink).c_str()); LobbyStatus(L"An invite came from a link. Check it, then press Join."); startPage = ls::kPlay; }      // a witness-coop:// link: into the join box, never joined without a click (the choice is not remembered)
    if (g_autoJoin) { JoinSteamLobby(g_autoJoin); startPage = 0; } if (startPage < 0 || startPage >= g_npages) startPage = 0;
    ShowSection(startSection); SelectTab(startPage); UpdateTabs(); if (openNew) OpenNewGame();
    if (!dumpTo.empty()) { std::string t = std::to_string(ID_STATUS) + " status\n"; for (auto& f : g_fields) if (f.h) t += std::to_string(f.id) + " " + f.key + "\n"; t += std::to_string(ID_APPLY) + " apply\n" + std::to_string(ID_LAUNCH) + " launch\n" + std::to_string(ID_DIAG) + " diag\n" + std::to_string(ID_SAVEPICK) + " savepick\n" + std::to_string(ID_PRIMARY) + " primary\n" + std::to_string(ID_TILE_SOLO) + " tile:solo\n";
        const std::pair<int, const char*> lb[] = {{ID_LB_INVITE, "lobby:invite"}, {ID_LB_COPY, "lobby:copy"}, {ID_LB_LEAVE, "lobby:leave"}, {ID_LB_READY, "lobby:ready"}, {ID_LB_START, "lobby:start"}, {ID_LB_MEMBERS, "lobby:members"}, {ID_LB_LOG, "lobby:log"}, {ID_LB_CHAT, "lobby:chat"}, {ID_LB_SEND, "lobby:send"}, {ID_LB_MODE, "lobby:mode"}, {ID_LB_GOAL, "lobby:goal"}, {ID_LB_NEWGAME, "lobby:newgame"}, {ID_LB_PARTS, "lobby:parts"}, {ID_LB_STATUS, "lobby:status"}, {ID_LB_MUTE, "lobby:mute"}, {ID_LB_DEAF, "lobby:deaf"}, {ID_LB_RESULT, "lobby:result"}, {ID_LB_STEAM_INVITE, "lobby:steaminvite"}, {ID_LB_STEAM_FRIENDS, "lobby:steamfriends"}, {ID_LB_STEAM_SEND, "lobby:steamsend"}};
        for (const auto& k : lb) t += std::to_string(k.first) + " " + k.second + "\n";
        WriteFile8(dumpTo, t); }
    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (CaptureMessage(msg)) continue;
        if ((msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN || msg.message == WM_KEYUP || msg.message == WM_SYSKEYUP) &&
            g_guideHold.Key((unsigned)msg.wParam, msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN)) {
            ShowGuide(g_guideHold.Open()); continue;
        }
        if (msg.message == WM_LBUTTONDOWN || msg.message == WM_MOUSEMOVE) g_padLast = false;
        if ((!g_ng || GetForegroundWindow() != g_ng) && KeyMessage(msg)) continue;
        if (!IsDialogMessageW(g_ng ? g_ng : g_main, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    return 0;
}
