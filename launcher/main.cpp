#include "../common/guide.h"
// The Witness co-op launcher: every option outside the game. Video (the game's Launcher.variables), the mod's settings (witness_coop.cfg: name, keys, connection, sound and voice),
// the game's own options inside a save (mouse speed, brightness, field of view, ...), the save files with who was in the game and how far it got, and a new game that starts with
// chosen parts of the map completed (the order the game itself needs: progress/progress.h). The logic is in core.cpp (tested without a window); this file is the window.
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
#include "../proxy/core/appearance.h"
#include "theme.h"
#include "lobby.h"
#include "lobby_win.h"
#include "../proxy/progress/progress.h"

namespace {
std::wstring W(const std::string& s) { if (s.empty()) return L""; int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0); std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n); w.resize(n - 1); return w; }
std::string U(const std::wstring& w) { if (w.empty()) return ""; int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr); std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr); s.resize(n - 1); return s; }
std::string ReadFile8(const std::wstring& p) { std::string t; FILE* f = _wfopen(p.c_str(), L"rb"); if (f) { char b[4096]; size_t k; while ((k = fread(b, 1, sizeof b, f)) > 0) t.append(b, k); fclose(f); } return t; }
bool WriteFile8(const std::wstring& p, const std::string& t) { FILE* f = _wfopen(p.c_str(), L"wb"); if (!f) return false; fwrite(t.data(), 1, t.size(), f); fclose(f); return true; }
std::wstring Dir(const std::wstring& p) { size_t s = p.find_last_of(L"\\/"); return s == std::wstring::npos ? L"" : p.substr(0, s); }
bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

enum { ID_TAB = 100, ID_APPLY, ID_LAUNCH, ID_NEWGAME, ID_BROWSE, ID_GAMEDIR, ID_STATUS, ID_REFRESH, ID_OPENSAVES, ID_BACKUP, ID_TRASH, ID_SAVELIST, ID_SAVEINFO, ID_SAVEPICK, ID_NG_OK = 300, ID_NG_LASERS, ID_NG_PRESET, ID_NG_INFO, ID_NG_AREAS = 400, ID_FIELD = 1000 };
enum Kind { kCheck, kCombo, kEdit, kMulti };
struct Field { int id; int page; std::string label, key; Kind kind; std::vector<std::string> choices; std::string def, hint; HWND h = nullptr; };
guide::Hold g_guideHold;
HWND g_guide;
std::vector<Field> g_fields; HWND g_main, g_tab, g_pages[9], g_status; int g_npages;
std::wstring g_dir;                       // the game folder
bool g_noActivate;                        // --no-activate (tests): the game's window is shown without taking the focus
const wchar_t* kPageNames[] = {L"Lobby", L"Play", L"Video", L"Sound and voice", L"Controls", L"Game options", L"Co-op", L"Saves", L"Appearance"};

std::wstring CfgPath() { return g_dir + L"\\witness_coop.cfg"; }
std::wstring SaveFolder(const wl::CfgFile& c) {      // where the game keeps its saves, as the mod redirects them (save_dir=instance|<path>)
    std::string sd = c.Get("save_dir");
    if (sd == "instance") return g_dir + L"\\appdata\\The Witness";
    if (!sd.empty()) return W(sd) + L"\\The Witness";
    wchar_t p[MAX_PATH]; if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, p))) return std::wstring(p) + L"\\The Witness"; return L"";
}
wl::CfgFile LoadCfg() { return wl::CfgFile::Parse(ReadFile8(CfgPath())); }
void Status(const std::wstring& s) { SetWindowTextW(g_status, s.c_str()); }
void LobbyCfgSave(wl::CfgFile* c); void LobbyCfgLoad(const wl::CfgFile& c); void LobbyPartsChosen(const std::string& spec);

void AddFields() {
    int id = ID_FIELD; auto add = [&](int page, const char* label, const char* key, Kind k, std::vector<std::string> ch = {}, const char* def = "", const char* hint = "") { g_fields.push_back({id++, page, label, key, k, ch, def, hint}); };
    const std::vector<std::string> q = {"Low", "Medium", "High"}, msaa = {"Off", "2x MSAA", "4x MSAA", "8x MSAA", "16x MSAA"}, res = {"Display resolution", "1080p", "720p", "Dynamic resolution", "Custom size"};
    add(2, "Render quality profile", "vid:renderProfile", kCombo, q); add(2, "Full screen", "vid:fullscreen", kCheck); add(2, "Wait for vertical sync", "vid:vsync", kCheck); add(2, "High DPI", "vid:highdpi", kCheck);
    add(2, "Resolution", "vid:resolution", kCombo, res); add(2, "Custom width", "vid:customW", kEdit, {}, "1920"); add(2, "Custom height", "vid:customH", kEdit, {}, "1080"); add(2, "Anti-aliasing", "vid:multisampling", kCombo, msaa);
    add(2, "Shading quality", "vid:shading", kCombo, q); add(2, "Reflection quality", "vid:reflection", kCombo, q); add(2, "Texture quality", "vid:texture", kCombo, q); add(2, "Texture detail", "vid:textureDetail", kCombo, q); add(2, "Geometry detail", "vid:geometry", kCombo, q);
    add(2, "Frame rate limit (0: none)", "cfg:cap_fps", kEdit, {}, "0");

#ifdef WL_MUT_LONG_LABELS
    add(3, "Game sound off (starts the game with -no_sound)",
#else
    add(3, "Game sound off (-no_sound)",
#endif
 "cfg:launcher_no_sound", kCheck); add(3, "Game volume, percent", "cfg:game_volume", kEdit, {}, "100", "its entry in the Windows volume mixer");
    add(3, "Voice chat", "cfg:voice_mode", kCombo, {"off", "open", "ptt"}, "ptt", "off, open (voice gate) or ptt (push to talk)"); add(3, "Push-to-talk key", "cfg:key_voice", kEdit, {}, guide::KeyName(Cfg().keyVoice).c_str(), "unique letter, digit or function key; F1 is help"); add(3, "Who hears you (voice channel)", "cfg:voice_channel", kCombo, {"proximity", "radio", "global"}, "proximity", "proximity, radio or global (open line)");
    add(3, "Voice volume, percent", "cfg:voice_volume", kEdit, {}, "100"); add(3, "Microphone volume, percent", "cfg:mic_volume", kEdit, {}, "100"); add(3, "Hear players within (metres)", "cfg:voice_range", kEdit, {}, "25", "beyond it the voice turns into radio");
    add(3, "Radio range (metres, 0: unlimited)", "cfg:voice_radio_range", kEdit, {}, "0"); add(3, "Radio sound when far away", "cfg:voice_radio", kCheck, {}, "1");
    add(3, "Handheld radio", "cfg:radio_enabled", kCheck, {}, "1", "hold the radio key to transmit");
    for (const auto& b : guide::bindings) {
        if (std::string(b.config) == "key_voice") continue; // voice tab already has this control
        add(4, b.label, (std::string("cfg:") + b.config).c_str(), kEdit, {}, guide::KeyName(b.initial).c_str(), "unique letter, digit or function key; F1 is help");
    }
    for (const auto& d : wl::SaveOptionDefs()) add(5, d.label, (std::string("opt:") + d.name).c_str(), d.kind == wl::OptKind::Bool ? kCheck : kEdit);
    const std::vector<std::vector<std::string>> palettes = {
        {"Original", "Tan", "Deep", "Light"}, {"Original", "Black", "Auburn", "Silver"},
        {"Original", "Blue", "Green", "Amber"}, {"Original", "Rust", "Blue", "Olive"},
        {"Original", "Blue", "Red", "Green"}, {"Original", "Charcoal", "Blue", "Olive"},
        {"Original", "Black", "Tan", "Olive"}, {"Original", "Charcoal", "Tan", "Olive"}
    };
    for (size_t i = 0; i < witness::kAppearanceFeatures; ++i) {
        const auto* feature = witness::kAppearanceNames[i];
        std::string label = std::string(feature) + " palette";
        std::string key = std::string("cfg:appearance_") + feature;
        add(8, label.c_str(), key.c_str(), kCombo, palettes[i], "0");
    }
    add(6, "Player number (1, 2, ...)", "cfg:id", kEdit, {}, "1"); add(6, "Name shown to the others", "cfg:name", kEdit); add(6, "UDP port to listen on", "cfg:listen", kEdit, {}, "47001");
    add(6, "Other players: number,address,port", "cfgmulti:peer", kMulti);
    add(6, "Developer menu (F8)", "cfg:dev_menu", kCheck); add(6, "See through players in the way of a puzzle", "cfg:see_through", kCheck, {}, "1"); add(6, "Keep running when this window is not active", "cfg:run_unfocused", kCheck, {}, "0", "two windows on one PC: stays smooth");
}
Field* ByKey(const std::string& k) { for (auto& f : g_fields) if (f.key == k) return &f; return nullptr; }
std::string GetVal(Field& f) {
    wchar_t b[2000] = {};
    if (f.kind == kCheck) return SendMessageW(f.h, BM_GETCHECK, 0, 0) == BST_CHECKED ? "1" : "0";
    if (f.kind == kCombo) { int i = (int)SendMessageW(f.h, CB_GETCURSEL, 0, 0); return i < 0 ? "" : ((f.key.rfind("vid:", 0) == 0 || f.key.rfind("cfg:appearance_", 0) == 0) ? std::to_string(i) : f.choices[i]); }
    GetWindowTextW(f.h, b, 2000); return U(b);
}
void SetVal(Field& f, const std::string& v) {
    if (f.kind == kCheck) SendMessageW(f.h, BM_SETCHECK, (v == "1" || v == "true") ? BST_CHECKED : BST_UNCHECKED, 0);
    else if (f.kind == kCombo) { int i = -1; if (f.key.rfind("cfg:appearance_", 0) == 0) i = v.size() == 1 && v[0] >= '0' && v[0] <= '3' ? v[0] - '0' : 0; else if (f.key.rfind("vid:", 0) == 0) i = atoi(v.c_str()); else for (size_t k = 0; k < f.choices.size(); ++k) if (f.choices[k] == v) i = (int)k; if ((i < 0 || size_t(i) >= f.choices.size()) && !f.choices.empty()) i = 0; SendMessageW(f.h, CB_SETCURSEL, i, 0); }
    else SetWindowTextW(f.h, W(v).c_str());
}
void FillPeers(Field& f, const wl::CfgFile& c) { std::string t; for (const auto& p : c.GetAll("peer")) t += p + "\r\n"; SetWindowTextW(f.h, W(t).c_str()); }

// ---- the saves page
std::vector<wl::SaveInfo> g_saves; HWND g_saveList, g_saveInfo, g_savePick;
std::wstring Stamp() { SYSTEMTIME st; GetLocalTime(&st); wchar_t b[40]; swprintf(b, 40, L"%04d-%02d-%02d_%02d.%02d.%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond); return b; }
std::wstring When(unsigned long long ft) { FILETIME f; f.dwLowDateTime = (DWORD)ft; f.dwHighDateTime = (DWORD)(ft >> 32); FILETIME l; SYSTEMTIME st; FileTimeToLocalFileTime(&f, &l); FileTimeToSystemTime(&l, &st); wchar_t b[40]; swprintf(b, 40, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute); return b; }
void RefreshSaves() {
    wl::CfgFile c = LoadCfg(); g_saves = wl::ListSaves(SaveFolder(c)); ListView_DeleteAllItems(g_saveList); SendMessageW(g_savePick, CB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < g_saves.size(); ++i) {
        const auto& s = g_saves[i]; LVITEMW it = {}; it.mask = LVIF_TEXT; it.iItem = (int)i; std::wstring w = When(s.when); it.pszText = &w[0]; ListView_InsertItem(g_saveList, &it);
        std::wstring who, prog, las, tm; if (s.hasMeta) { for (size_t k = 0; k < s.meta.players.size(); ++k) who += (k ? L", " : L"") + W(s.meta.players[k].name); wchar_t b[80]; swprintf(b, 80, L"%u%%  (%u of %u)", s.meta.total ? s.meta.solved * 100 / s.meta.total : 0, s.meta.solved, s.meta.total); prog = b; swprintf(b, 80, L"%u of %u", s.meta.lasers, s.meta.lasersTotal); las = b; swprintf(b, 80, L"%u h %02u min", s.meta.totalSeconds / 3600, (s.meta.totalSeconds % 3600) / 60); tm = b; } else who = L"(no co-op record)";
        ListView_SetItemText(g_saveList, (int)i, 1, &who[0]); ListView_SetItemText(g_saveList, (int)i, 2, &prog[0]); ListView_SetItemText(g_saveList, (int)i, 3, &las[0]); ListView_SetItemText(g_saveList, (int)i, 4, &tm[0]);
        SendMessageW(g_savePick, CB_ADDSTRING, 0, (LPARAM)When(s.when).c_str());
    }
    SendMessageW(g_savePick, CB_SETCURSEL, 0, 0);
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
        if (f) keys.*b.member = guide::ParseKey(GetVal(*f).empty() ? guide::KeyName(b.initial) : GetVal(*f));
    }
    if (migrate) guide::MigrateF1(keys);
    return keys;
}
void MigrateGuideKeys() {
    Cfg keys = GuideSettings(true);
    for (const auto& b : guide::bindings) if (auto* f = ByKey(std::string("cfg:") + b.config))
        if (guide::ParseKey(GetVal(*f)) == guide::Help) SetVal(*f, guide::KeyName(keys.*b.member));
}
void LoadAll() {
    wl::CfgFile c = LoadCfg(); wl::Video v; std::wstring sf = SaveFolder(c);
    if (!wl::ParseVideo(ReadFile8(sf + L"\\Launcher.variables"), &v)) v = wl::Video();
    auto vid = [&](const std::string& k) -> std::string { if (k == "renderProfile") return std::to_string(v.renderProfile); if (k == "fullscreen") return v.fullscreen ? "1" : "0"; if (k == "vsync") return v.vsync ? "1" : "0"; if (k == "highdpi") return v.highdpi ? "1" : "0"; if (k == "resolution") return std::to_string(v.resolution);
        if (k == "customW") return std::to_string(v.customW); if (k == "customH") return std::to_string(v.customH); if (k == "multisampling") return std::to_string(v.multisampling); if (k == "shading") return std::to_string(v.shading); if (k == "reflection") return std::to_string(v.reflection); if (k == "texture") return std::to_string(v.texture); if (k == "textureDetail") return std::to_string(v.textureDetail); return std::to_string(v.geometry); };
    for (auto& f : g_fields) {
        if (f.key.rfind("vid:", 0) == 0) SetVal(f, vid(f.key.substr(4)));
        else if (f.key.rfind("cfg:", 0) == 0) SetVal(f, c.Get(f.key.substr(4), f.def));
        else if (f.key == "cfgmulti:peer") FillPeers(f, c);
    }
    MigrateGuideKeys();
    LobbyCfgLoad(c); RefreshSaves();
    // the game's options come from the save chosen in the box (the newest first)
    std::vector<uint8_t> data; if (!g_saves.empty()) { std::string t = ReadFile8(g_saves[0].path); data.assign(t.begin(), t.end()); }
    auto opts = wl::ReadSaveOptions(data);
    for (auto& f : g_fields) if (f.key.rfind("opt:", 0) == 0) { bool found = false; for (const auto& o : opts) if (o.name == f.key.substr(4)) { found = true; char b[40]; snprintf(b, sizeof b, "%g", o.value); SetVal(f, f.kind == kCheck ? (o.value != 0 ? "1" : "0") : b); } EnableWindow(f.h, found); }
    ShowSaveInfo(g_saves.empty() ? -1 : 0);
}
bool ApplyAll(std::wstring* msg) {
    std::string keyError = guide::Validate(GuideSettings(false));
    if (!keyError.empty()) { *msg = W(keyError); return false; }
    wl::CfgFile c = LoadCfg(); wl::Video v; std::wstring sf = SaveFolder(c); wl::ParseVideo(ReadFile8(sf + L"\\Launcher.variables"), &v);
    for (auto& f : g_fields) {
        std::string val = GetVal(f);
        if (f.key.rfind("vid:", 0) == 0) { std::string k = f.key.substr(4); int n = atoi(val.c_str());
            if (k == "renderProfile") v.renderProfile = n; else if (k == "fullscreen") v.fullscreen = n; else if (k == "vsync") v.vsync = n; else if (k == "highdpi") v.highdpi = n; else if (k == "resolution") v.resolution = n; else if (k == "customW") v.customW = n; else if (k == "customH") v.customH = n;
            else if (k == "multisampling") v.multisampling = n; else if (k == "shading") v.shading = n; else if (k == "reflection") v.reflection = n; else if (k == "texture") v.texture = n; else if (k == "textureDetail") v.textureDetail = n; else v.geometry = n; }
        else if (f.key.rfind("cfg:", 0) == 0) {
            std::string k = f.key.substr(4); if (f.kind == kCombo && (val == "(default)" || val.empty())) val = "";
            if (k.rfind("key_", 0) == 0 && !val.empty()) val = guide::KeyName(guide::ParseKey(val));
            if (f.kind == kEdit && (k == "cap_fps" || k == "game_volume" || k == "voice_volume" || k == "mic_volume" || k == "voice_range" || k == "voice_radio_range" || k == "id" || k == "listen")) { char* e; long n = strtol(val.c_str(), &e, 10); if (val.empty() || *e || n < 0 || n > 65535) { *msg = W(f.label + ": a whole number from 0 to 65535"); return false; } if ((k == "game_volume" || k == "voice_volume" || k == "mic_volume") && n > 100) { *msg = W(f.label + ": 0 to 100"); return false; } }
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
    LobbyCfgSave(&c);
#ifdef WL_MUT_CFG_DROP
    { wl::CfgFile only; for (auto& f : g_fields) if (f.key == "cfg:name") only.Set("name", GetVal(f)); c = only; }      // (mutant) the lines the launcher does not know are lost
#endif
    if (!WriteFile8(CfgPath(), c.Text())) { *msg = L"Could not write witness_coop.cfg in " + g_dir; return false; }
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

void LaunchGame(bool newGame) {
    std::wstring exe = g_dir + L"\\witness64_d3d11.exe"; if (!Exists(exe)) exe = g_dir + L"\\witness_d3d11.exe";
    if (!Exists(exe)) { Status(L"The game was not found in " + g_dir); return; }
    std::wstring ApplyMsg; if (!ApplyAll(&ApplyMsg)) { Status(ApplyMsg); return; }
    std::wstring cmd = L"\"" + exe + L"\" " + W(wl::LaunchArgs(LoadCfg(), newGame));
    STARTUPINFOW si = {sizeof si}; PROCESS_INFORMATION pi = {}; if (g_noActivate) { si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_SHOWNOACTIVATE; }
    if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, g_dir.c_str(), &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); Status(L"The game is starting."); } else Status(L"Could not start the game.");
}

// ---- the window
HWND Make(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, DWORD ex = 0) {
    HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE); th::Style(c); return c;
}
void ShowPage(int p) { for (int i = 0; i < g_npages; ++i) ShowWindow(g_pages[i], i == p ? SW_SHOW : SW_HIDE); }
LRESULT CALLBACK PageProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CTLCOLORSTATIC || m == WM_CTLCOLOREDIT || m == WM_CTLCOLORLISTBOX || m == WM_CTLCOLORBTN) return th::CtlColor(m, (HDC)w, (HWND)l);
    if (m == WM_DRAWITEM) return SendMessageW(g_main, WM_DRAWITEM, w, l);
    if (m == WM_COMMAND) { SendMessageW(g_main, WM_COMMAND, w, l); return 0; }
    if (m == WM_NOTIFY) { NMHDR* n = (NMHDR*)l; if (n->idFrom == ID_SAVELIST && n->code == LVN_ITEMCHANGED) { NMLISTVIEW* nl = (NMLISTVIEW*)l; if (nl->uNewState & LVIS_SELECTED) ShowSaveInfo(nl->iItem); } return 0; }
    return DefWindowProcW(h, m, w, l);
}
void BuildLobbyPage();
void BuildPages(HWND parent) {
    WNDCLASSW wc = {}; wc.lpfnWndProc = PageProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"WitnessLauncherPage"; wc.hbrBackground = th::BgBrush(); wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc);
    g_npages = 9; static TCITEMW items[9];
    for (int i = 0; i < g_npages; ++i) { items[i] = {}; items[i].mask = TCIF_TEXT; items[i].pszText = (LPWSTR)kPageNames[i]; TabCtrl_InsertItem(g_tab, i, &items[i]); }
    th::StyleTab(g_tab);
    RECT r; GetClientRect(g_tab, &r); TabCtrl_AdjustRect(g_tab, FALSE, &r);      // the pages go under the row of tabs (measured once the tabs exist)
    for (int i = 0; i < g_npages; ++i) g_pages[i] = CreateWindowExW(0, L"WitnessLauncherPage", L"", WS_CHILD, r.left, r.top, r.right - r.left, r.bottom - r.top, g_tab, nullptr, GetModuleHandleW(nullptr), nullptr);
    // Play
    int y = 16; Make(g_pages[1], L"STATIC", L"Game folder:", SS_LEFT, 16, y + 3, 100, 20, 0); Make(g_pages[1], L"EDIT", g_dir.c_str(), WS_BORDER | ES_AUTOHSCROLL | ES_READONLY, 120, y, 420, 24, ID_GAMEDIR); Make(g_pages[1], L"BUTTON", L"Browse...", BS_PUSHBUTTON, 550, y, 90, 24, ID_BROWSE);
    y += 50; Make(g_pages[1], L"BUTTON", L"Continue (start the game)", BS_DEFPUSHBUTTON, 16, y, 240, 40, ID_LAUNCH); Make(g_pages[1], L"BUTTON", L"New game...", BS_PUSHBUTTON, 270, y, 160, 40, ID_NEWGAME);
    Make(g_pages[1], L"STATIC", L"Continue starts the game with the settings of every page, on your latest save. New game asks what is already completed and starts a fresh world; your saves stay where they are.", SS_LEFT, 16, y + 56, 620, 40, 0);
    // fields
    for (auto& f : g_fields) {
        HWND pg = g_pages[f.page]; static int rowY[9]; if (!rowY[f.page]) rowY[f.page] = 12; int ry = rowY[f.page]; int lw = 300;
        if (f.kind == kMulti) { Make(pg, L"STATIC", W(f.label).c_str(), SS_LEFT, 16, ry + 3, lw, 20, 0); f.h = Make(pg, L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL, 330, ry, 320, 90, f.id); rowY[f.page] += 98; continue; }
        Make(pg, L"STATIC", W(f.label).c_str(), SS_LEFT, 16, ry + 3, lw, 20, 0);
        if (f.kind == kCheck) f.h = Make(pg, L"BUTTON", L"", BS_AUTOCHECKBOX, 330, ry, 24, 22, f.id);
        else if (f.kind == kEdit) f.h = Make(pg, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 330, ry, 120, 22, f.id);
        else {
#ifdef WL_MUT_WIDE_COMBOS
        f.h = Make(pg, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 330, ry, 220, 200, f.id);
#else
        f.h = Make(pg, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 330, ry, 190, 200, f.id);
#endif
        for (auto& c : f.choices) SendMessageW(f.h, CB_ADDSTRING, 0, (LPARAM)W(c).c_str()); }
        if (!f.hint.empty()) th::MarkMuted(Make(pg, L"STATIC", W(f.hint).c_str(), SS_LEFT, 540, ry + 3, 300, 20, 0));
        rowY[f.page] += 28;
    }
    // saves
    HWND sp = g_pages[7]; g_saveList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 12, 12, 650, 190, sp, (HMENU)ID_SAVELIST, nullptr, nullptr);
    ListView_SetExtendedListViewStyle(g_saveList, LVS_EX_FULLROWSELECT); th::StyleList(g_saveList); SendMessageW(g_saveList, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    const wchar_t* cols[] = {L"Saved", L"Players", L"Progress", L"Lasers", L"Time played"}; int cw[] = {120, 230, 120, 60, 100};
    for (int i = 0; i < 5; ++i) { LVCOLUMNW c = {}; c.mask = LVCF_TEXT | LVCF_WIDTH; c.pszText = (LPWSTR)cols[i]; c.cx = cw[i]; ListView_InsertColumn(g_saveList, i, &c); }
    g_saveInfo = Make(sp, L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL, 12, 212, 650, 200, ID_SAVEINFO);
    Make(sp, L"BUTTON", L"Refresh", BS_PUSHBUTTON, 12, 422, 90, 26, ID_REFRESH); Make(sp, L"BUTTON", L"Copy to backup", BS_PUSHBUTTON, 112, 422, 120, 26, ID_BACKUP); Make(sp, L"BUTTON", L"Move to backup folder", BS_PUSHBUTTON, 242, 422, 150, 26, ID_TRASH); Make(sp, L"BUTTON", L"Open folder", BS_PUSHBUTTON, 402, 422, 100, 26, ID_OPENSAVES);
    // the game-options page chooses its save
    Make(g_pages[5], L"STATIC", L"Save to change:", SS_LEFT, 16, 330, 200, 20, 0); g_savePick = Make(g_pages[5], L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 330, 326, 220, 200, ID_SAVEPICK);
    BuildLobbyPage();
    th::MarkMuted(Make(g_pages[5], L"STATIC", L"These are the game's own options; they live inside each save. A copy of the save as it was is kept once (.before-launcher).", SS_LEFT, 16, 362, 640, 36, 0));
}
// ---- the lobby page: host or join, see who is there, chat by text and voice, choose what to play, start; and be back here when the game is over
enum { ID_LB_HOST = 600, ID_LB_JOIN, ID_LB_INVITE, ID_LB_COPY, ID_LB_LEAVE, ID_LB_READY, ID_LB_START, ID_LB_MEMBERS, ID_LB_LOG, ID_LB_CHAT, ID_LB_SEND, ID_LB_MODE, ID_LB_GOAL, ID_LB_NEWGAME, ID_LB_PARTS, ID_LB_STATUS, ID_LB_PORT, ID_LB_ADVERTISE, ID_LB_MUTE, ID_LB_DEAF, ID_LB_RESULT, ID_LB_LEVEL, ID_LB_PARTSINFO, ID_LB_WHO };
lw::Session g_sess; lw::Voice g_voice; wlobby::State g_shown; std::wstring g_chatLog; HANDLE g_game; DWORD g_gamePid; std::string g_lobbySpec, g_cfgBackup, g_lastResult; bool g_gameWasRace;
std::wstring g_statFile, g_cmdFile; ULONGLONG g_statAt, g_launchedAt; unsigned g_launches; std::string g_lastLaunchCfg; bool g_voiceWanted = true; std::set<unsigned> g_everHeard;
HWND LB(int id) { return GetDlgItem(g_pages[0], id); }
std::string EditText(HWND h) { wchar_t b[1000] = {}; GetWindowTextW(h, b, 1000); return U(b); }
void LobbyStatus(const std::wstring& s) { SetWindowTextW(LB(ID_LB_STATUS), s.c_str()); }
std::string CfgGet(const char* k, const char* d = "") { return LoadCfg().Get(k, d); }
lw::VoiceCfg VoiceCfgNow() {
    wl::CfgFile c = LoadCfg(); lw::VoiceCfg v; std::string m = c.Get("voice_mode", "ptt"); v.mode = m == "off" ? 0 : m == "open" ? 1 : 2; Cfg keys;
    for (const auto& b : guide::bindings) {
        unsigned k = guide::ParseKey(c.Get(b.config, guide::KeyName(b.initial)));
        if (k) keys.*b.member = k;
    }
    guide::MigrateF1(keys); v.key = keys.keyVoice;
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
void LobbyCfgSave(wl::CfgFile* c) { std::string port = EditText(LB(ID_LB_PORT)), adv = EditText(LB(ID_LB_ADVERTISE)); if (!port.empty() && (port != "47100" || c->Has("lobby_port"))) c->Set("lobby_port", port); if (adv.empty()) c->Unset("lobby_advertise"); else c->Set("lobby_advertise", adv); }
void LobbyCfgLoad(const wl::CfgFile& c) { if (!g_pages[0] || !LB(ID_LB_PORT)) return; SetWindowTextW(LB(ID_LB_PORT), W(c.Get("lobby_port", "47100")).c_str()); SetWindowTextW(LB(ID_LB_ADVERTISE), W(c.Get("lobby_advertise", "")).c_str()); }
void LobbyPartsChosen(const std::string& spec) { g_lobbySpec = spec; PartsInfo(); PushSettings(); }
void HostLobby() {
    wl::CfgFile c = LoadCfg(); std::string err; unsigned port = (unsigned)atoi(EditText(LB(ID_LB_PORT)).c_str()); if (port < 1 || port > 65535) { LobbyStatus(L"The lobby port is a number from 1 to 65535."); return; }
    uint16_t gp = (uint16_t)atoi(c.Get("listen", "47001").c_str()); std::string name = c.Get("name", "Player");
    if (!g_sess.Host(name, gp, (uint16_t)port, EditText(LB(ID_LB_ADVERTISE)), &err)) { LobbyStatus(W(err)); return; }
    SetWindowTextW(LB(ID_LB_INVITE), W(g_sess.Invite()).c_str()); g_chatLog.clear(); SetWindowTextW(LB(ID_LB_LOG), L""); PushSettings(); StartVoice(); RefreshLobbyUi();
}
void JoinLobby() {
    wl::CfgFile c = LoadCfg(); std::string err; uint16_t gp = (uint16_t)atoi(c.Get("listen", "47001").c_str());
    if (!g_sess.Join(EditText(LB(ID_LB_INVITE)), c.Get("name", "Player"), gp, &err)) { LobbyStatus(W(err)); return; }
    g_chatLog.clear(); SetWindowTextW(LB(ID_LB_LOG), L""); LobbyStatus(L"Joining..."); RefreshLobbyUi();
}
void LeaveLobby() { g_voice.Stop(); g_sess.Stop(); g_shown = wlobby::State(); SendMessageW(LB(ID_LB_MEMBERS), LB_RESETCONTENT, 0, 0); SendMessageW(LB(ID_LB_READY), BM_SETCHECK, BST_UNCHECKED, 0); RefreshLobbyUi(); LobbyStatus(L"You left the lobby."); }
void AppendChat(const std::wstring& line) { g_chatLog += line + L"\r\n"; HWND l = LB(ID_LB_LOG); SetWindowTextW(l, g_chatLog.c_str()); SendMessageW(l, EM_SETSEL, g_chatLog.size(), g_chatLog.size()); SendMessageW(l, EM_SCROLLCARET, 0, 0); }
std::string MemberName(unsigned id) { for (const auto& m : g_shown.members) if (m.id == id) return m.name; return "player " + std::to_string(id); }
std::string ClockText(unsigned ms) { char b[24]; snprintf(b, sizeof b, "%u:%02u.%u", ms / 60000, (ms / 1000) % 60, (ms / 100) % 10); return b; }
std::string RaceResultText() {      // witness_race_result.txt written by the game: goal=, winner=, then place|name|finish ms|phase
    std::string t = ReadFile8(g_dir + L"\\witness_race_result.txt"); if (t.empty()) return ""; std::string out = "Race: "; size_t at = 0; bool any = false;
    while (at < t.size()) { size_t e = t.find('\n', at); if (e == std::string::npos) e = t.size(); std::string line = t.substr(at, e - at); at = e + 1; unsigned place = 0, ms = 0; char name[80] = {}, ph[24] = {};
        if (sscanf(line.c_str(), "%u|%79[^|]|%u|%23s", &place, name, &ms, ph) == 4) { if (any) out += "   "; any = true; out += std::to_string(place) + ". " + name + (!strcmp(ph, "finished") ? " " + ClockText(ms) : std::string(" (not finished)")); } }
    return any ? out : "";
}
void LaunchFromLobby(const wlobby::Launch& l) {
    std::wstring exe = g_dir + L"\\witness64_d3d11.exe"; if (!Exists(exe)) exe = g_dir + L"\\witness_d3d11.exe"; if (!Exists(exe)) { LobbyStatus(L"The game was not found in " + g_dir); g_sess.SetInGame(false); return; }
    std::wstring msg; if (!ApplyAll(&msg)) { LobbyStatus(msg); return; }
    g_cfgBackup = ReadFile8(CfgPath());      // the player's own settings: put back when the game is over
    std::string cfg = wlobby::BuildGameCfg(g_cfgBackup, l, g_sess.HostIp());
#ifndef WL_MUT_LOBBY_NO_CFG
    WriteFile8(CfgPath(), cfg);
#endif
    g_lastLaunchCfg = cfg;
    wl::CfgFile c = LoadCfg(); std::wstring sf = SaveFolder(c); if (l.state.settings.newGame) DeleteFileW((sf + L"\\coop_progress.txt").c_str());
    DeleteFileW((g_dir + L"\\witness_race_result.txt").c_str()); g_gameWasRace = l.state.settings.mode == wlobby::kRace;
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
    EnableWindow(LB(ID_LB_HOST), !act); EnableWindow(LB(ID_LB_JOIN), !act); EnableWindow(LB(ID_LB_LEAVE), act); EnableWindow(LB(ID_LB_COPY), host);
    SendMessageW(LB(ID_LB_INVITE), EM_SETREADONLY, act ? TRUE : FALSE, 0); EnableWindow(LB(ID_LB_PORT), !act); EnableWindow(LB(ID_LB_ADVERTISE), !act);
    EnableWindow(LB(ID_LB_READY), joined && !host && lobby); EnableWindow(LB(ID_LB_START), host && lobby); EnableWindow(LB(ID_LB_SEND), joined); EnableWindow(LB(ID_LB_CHAT), joined);
    EnableWindow(LB(ID_LB_MODE), !act || host); EnableWindow(LB(ID_LB_GOAL), !act || host); EnableWindow(LB(ID_LB_NEWGAME), !act || host); EnableWindow(LB(ID_LB_PARTS), !act || host);
}
void ApplyRemoteSettings(const wlobby::State& st) {      // a guest sees what the host chose
    if (g_sess.IsHost()) return; SendMessageW(LB(ID_LB_MODE), CB_SETCURSEL, st.settings.mode == wlobby::kRace ? 1 : 0, 0); SetWindowTextW(LB(ID_LB_GOAL), W(st.settings.raceGoal).c_str());
    SendMessageW(LB(ID_LB_NEWGAME), BM_SETCHECK, st.settings.newGame ? BST_CHECKED : BST_UNCHECKED, 0); g_lobbySpec = st.settings.progress; PartsInfo();
}
void LobbyTick() {
    if (g_game && WaitForSingleObject(g_game, 0) == WAIT_OBJECT_0) GameEnded();
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
            InvalidateRect(LB(ID_LB_MEMBERS), nullptr, FALSE);
            std::wstring t; if (g_sess.IsHost()) t = L"Hosting.  Send friends the invite above."; else t = L"In the lobby of " + W(MemberName(1)) + L".";
            if (st.phase == wlobby::kStarting) t = L"The game starts in " + std::to_wstring((st.startInMs + 999) / 1000) + L" ...";
            else if (st.phase == wlobby::kInGame) t = g_game ? L"The game is running." : L"A game is running (you were not in it).";
            LobbyStatus(t); StartVoice();
            for (const auto& c : g_sess.TakeChat()) AppendChat(W(MemberName(c.from) + ":  " + c.text));
            wlobby::Launch l; if (g_sess.TakeLaunch(&l)) LaunchFromLobby(l);
        } else {
            wlobby::JoinResult r = g_sess.Refused();
            if (!g_sess.IsHost()) LobbyStatus(r == wlobby::kBadToken ? L"The invite is wrong (the code does not match)." : r == wlobby::kFull ? L"The lobby is full." : r == wlobby::kBadVersion ? L"This launcher is a different version from the host's." : L"Looking for the host ...");
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
    SelectObject(dc, m.host ? th::Bold() : th::Body()); SetTextColor(dc, th::kText); RECT nr = r; nr.left += 34; nr.right -= 120; DrawTextW(dc, W(m.name + (m.id == g_sess.MyId() ? "  (you)" : "")).c_str(), -1, &nr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
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
void BuildLobbyPage() {
    HWND pg = g_pages[0]; wl::CfgFile c = LoadCfg();
    auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) { HWND k = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, pg, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr); th::Style(k); return k; };
    auto label = [&](const wchar_t* t, int x, int y, int w, bool muted = false) { HWND k = mk(L"STATIC", t, SS_LEFT, x, y, w, 20, 0); if (muted) th::MarkMuted(k); return k; };
    mk(L"BUTTON", L"Host a lobby", BS_DEFPUSHBUTTON, 16, 12, 150, 32, ID_LB_HOST); label(L"Lobby port", 184, 19, 80, true); mk(L"EDIT", W(c.Get("lobby_port", "47100")).c_str(), WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER, 266, 16, 66, 24, ID_LB_PORT);
    label(L"Friends reach this PC at", 350, 19, 160, true); mk(L"EDIT", W(c.Get("lobby_advertise", "")).c_str(), WS_BORDER | ES_AUTOHSCROLL, 512, 16, 170, 24, ID_LB_ADVERTISE); label(L"(blank: automatic)", 690, 19, 160, true);
    label(L"Invite", 16, 60, 50, true); mk(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 70, 56, 360, 26, ID_LB_INVITE); mk(L"BUTTON", L"Join", BS_PUSHBUTTON, 442, 54, 84, 30, ID_LB_JOIN); mk(L"BUTTON", L"Copy invite", BS_PUSHBUTTON, 534, 54, 110, 30, ID_LB_COPY); mk(L"BUTTON", L"Leave", BS_PUSHBUTTON, 652, 54, 84, 30, ID_LB_LEAVE);
    HWND st = label(L"Host a lobby and send friends the invite, or paste an invite and join.", 16, 94, 800); SetWindowLongPtrW(st, GWLP_ID, ID_LB_STATUS);
    label(L"IN THE LOBBY", 16, 126, 200, true); mk(L"LISTBOX", L"", LBS_OWNERDRAWFIXED | LBS_NOTIFY | WS_VSCROLL | LBS_HASSTRINGS, 16, 148, 330, 230, ID_LB_MEMBERS); SendMessageW(LB(ID_LB_MEMBERS), LB_SETITEMHEIGHT, 0, 30);
    label(L"CHAT", 362, 126, 200, true); mk(L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL, 362, 148, 462, 190, ID_LB_LOG);
    HWND ce = mk(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 362, 346, 372, 28, ID_LB_CHAT); SetWindowSubclass(ce, ChatEditProc, 3, 0); mk(L"BUTTON", L"Send", BS_PUSHBUTTON, 742, 344, 82, 32, ID_LB_SEND);
    label(L"VOICE", 16, 390, 200, true); mk(L"BUTTON", L"Mute my microphone", BS_AUTOCHECKBOX, 16, 412, 170, 22, ID_LB_MUTE); mk(L"BUTTON", L"Deafen", BS_AUTOCHECKBOX, 196, 412, 100, 22, ID_LB_DEAF);
    mk(L"STATIC", L"", SS_OWNERDRAW, 16, 442, 330, 8, ID_LB_LEVEL); label(L"Voice mode and keys: Sound and voice page.", 16, 456, 330, true);
    label(L"WHAT TO PLAY  (the host chooses)", 362, 390, 300, true); label(L"Game", 362, 416, 50); { HWND mc = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 414, 412, 120, 100, ID_LB_MODE); SendMessageW(mc, CB_ADDSTRING, 0, (LPARAM)L"Co-op"); SendMessageW(mc, CB_ADDSTRING, 0, (LPARAM)L"Race"); SendMessageW(mc, CB_SETCURSEL, 0, 0); }
    label(L"Race goal", 540, 416, 70); mk(L"EDIT", L"panel:9F7F", WS_BORDER | ES_AUTOHSCROLL, 612, 412, 100, 24, ID_LB_GOAL); label(L"panel:ID or count:N", 718, 416, 140, true);
    HWND ng = mk(L"BUTTON", L"New game", BS_AUTOCHECKBOX, 362, 448, 110, 22, ID_LB_NEWGAME); SendMessageW(ng, BM_SETCHECK, BST_CHECKED, 0); mk(L"BUTTON", L"Parts of the map done...", BS_PUSHBUTTON, 480, 444, 190, 30, ID_LB_PARTS); { HWND pi = label(L"", 680, 450, 150, true); SetWindowLongPtrW(pi, GWLP_ID, ID_LB_PARTSINFO); }
    mk(L"BUTTON", L"I am ready", BS_AUTOCHECKBOX, 16, 494, 130, 28, ID_LB_READY); mk(L"BUTTON", L"Start the game", BS_DEFPUSHBUTTON, 154, 488, 200, 38, ID_LB_START);
    HWND rs = label(L"", 362, 494, 462); th::MarkMuted(rs); SetWindowLongPtrW(rs, GWLP_ID, ID_LB_RESULT); PartsInfo(); RefreshLobbyUi();
}
void PollCommands() {      // tests: one command per line in launcher_cmd.txt, taken whole
    if (g_cmdFile.empty() || !Exists(g_cmdFile)) return; std::string t = ReadFile8(g_cmdFile); DeleteFileW(g_cmdFile.c_str()); size_t at = 0;
    while (at < t.size()) { size_t e = t.find('\n', at); if (e == std::string::npos) e = t.size(); std::string line = t.substr(at, e - at); at = e + 1; while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back(); if (line.empty()) continue;
        std::string cmd = line.substr(0, line.find(' ')), arg = line.find(' ') == std::string::npos ? "" : line.substr(line.find(' ') + 1);
        if (cmd == "host") { if (!arg.empty()) SetWindowTextW(LB(ID_LB_PORT), W(arg).c_str()); HostLobby(); }
        else if (cmd == "join") { SetWindowTextW(LB(ID_LB_INVITE), W(arg).c_str()); JoinLobby(); }
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
        else if (cmd == "quit") PostMessageW(g_main, WM_CLOSE, 0, 0);
    }
}
void WriteStat() {      // tests: what the lobby is, as text lines, rewritten every 250 ms
    if (g_statFile.empty() || GetTickCount64() - g_statAt < 250) return; g_statAt = GetTickCount64(); char b[600]; std::string o;
    wlobby::State st = g_sess.Active() ? g_sess.State() : wlobby::State(); lw::VoiceStats v = g_voice.Stats();
    snprintf(b, sizeof b, "tabpaints %u\nlobby active %d host %d joined %d id %u phase %d start_in %d refused %d\n", th::TabPaintCount(), g_sess.Active() ? 1 : 0, g_sess.IsHost() ? 1 : 0, g_sess.Joined() ? 1 : 0, g_sess.MyId(), (int)st.phase, st.startInMs, (int)g_sess.Refused()); o += b;
    o += std::string("guide ") + (g_guideHold.Open() ? "1\n" : "0\n");
    snprintf(b, sizeof b, "invite %s\n", g_sess.Invite().c_str()); o += b;
    for (const auto& m : st.members) { snprintf(b, sizeof b, "member %u name %s host %d ready %d ingame %d speaking %d playing %d port %u ip %s\n", m.id, m.name.c_str(), m.host, m.ready, m.inGame, m.speaking, m.playing, m.gamePort, wlobby::IpText(m.ip).c_str()); o += b; }
    snprintf(b, sizeof b, "settings mode %d goal %s newgame %d progress %s\n", (int)st.settings.mode, st.settings.raceGoal.c_str(), st.settings.newGame ? 1 : 0, st.settings.progress.c_str()); o += b;
    { std::string chat = U(g_chatLog); for (size_t i; (i = chat.find("\r\n")) != std::string::npos;) chat.replace(i, 2, " | "); o += "result " + st.lastResult + "\n"; o += "chat " + chat + "\n"; }
    snprintf(b, sizeof b, "voice running %d sent %u received %u level %u speaking %d muted_ui %d\n", g_voice.Running() ? 1 : 0, v.sent, v.received, v.level, v.speaking ? 1 : 0, (int)SendMessageW(LB(ID_LB_MUTE), BM_GETCHECK, 0, 0)); o += b;
    for (const auto& s : v.speakers) { snprintf(b, sizeof b, "voice_speaker %u talking %d rms %.4f tone %.4f %.4f %.4f packets %u\n", s.id, s.talking ? 1 : 0, s.rms, s.tone[0], s.tone[1], s.tone[2], s.packets); o += b; }
    snprintf(b, sizeof b, "game pid %lu running %d launches %u\n", (unsigned long)g_gamePid, g_game ? 1 : 0, g_launches); o += b;
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

LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ACTIVATEAPP: if (!w) { g_guideHold.Reset(); ShowGuide(false); } break;
    case WM_ERASEBKGND: { RECT rc; GetClientRect(h, &rc); FillRect((HDC)w, &rc, th::BgBrush()); return 1; }
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc); th::PaintHeader(dc, rc, 84, L"Hold F1: quick guide   |   lobby, race, voice, saves"); EndPaint(h, &ps); return 0; }
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN: return th::CtlColor(m, (HDC)w, (HWND)l);
    case WM_DRAWITEM: { DRAWITEMSTRUCT* d = (DRAWITEMSTRUCT*)l; if (d->CtlID == ID_LB_MEMBERS) DrawMember(d); else if (d->CtlID == ID_LB_LEVEL) DrawLevel(d); return TRUE; }
    case WM_TIMER: if (w == 1) { PollCommands(); LobbyTick(); WriteStat(); } return 0;
    case WM_CLOSE: g_voice.Stop(); g_sess.Stop(); DestroyWindow(h); return 0;
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_APPLY) { std::wstring msg; ApplyAll(&msg); Status(msg); LoadAll(); }
        else if (id == ID_LB_HOST) HostLobby();
        else if (id == ID_LB_JOIN) JoinLobby();
        else if (id == ID_LB_LEAVE) LeaveLobby();
        else if (id == ID_LB_COPY) { std::wstring inv = W(g_sess.Invite()); if (!inv.empty() && OpenClipboard(h)) { EmptyClipboard(); HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (inv.size() + 1) * 2); if (g) { memcpy(GlobalLock(g), inv.c_str(), (inv.size() + 1) * 2); GlobalUnlock(g); SetClipboardData(CF_UNICODETEXT, g); } CloseClipboard(); LobbyStatus(L"The invite is on the clipboard."); } }
        else if (id == ID_LB_READY && HIWORD(w) == BN_CLICKED) g_sess.SetReady(SendMessageW(LB(ID_LB_READY), BM_GETCHECK, 0, 0) == BST_CHECKED);
        else if (id == ID_LB_START) { PushSettings(); if (!g_sess.Start()) LobbyStatus(L"Everybody has to be ready first."); }
        else if (id == ID_LB_SEND) SendChatLine();
        else if (id == ID_LB_MODE && HIWORD(w) == CBN_SELCHANGE) PushSettings();
        else if (id == ID_LB_GOAL && HIWORD(w) == EN_CHANGE) PushSettings();
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
            if (BackupSave(g_saves[i], id == ID_TRASH, dir)) Status(std::wstring(id == ID_TRASH ? L"Moved to " : L"Copied to ") + dir); else Status(L"Could not do that."); LoadAll();
        }
        else if (id == ID_BROWSE) { BROWSEINFOW bi = {}; bi.hwndOwner = h; bi.lpszTitle = L"The Witness game folder"; PIDLIST_ABSOLUTE p = SHBrowseForFolderW(&bi); if (p) { wchar_t b[MAX_PATH]; if (SHGetPathFromIDListW(p, b)) { g_dir = b; SetDlgItemTextW(g_pages[1], ID_GAMEDIR, b); LoadAll(); } CoTaskMemFree(p); } }
        return 0; }
    case WM_NOTIFY: { NMHDR* n = (NMHDR*)l; if (n->idFrom == ID_TAB && n->code == TCN_SELCHANGE) ShowPage(TabCtrl_GetCurSel(g_tab)); return 0; }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
}  // namespace

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR cmdline, int show) {
    INITCOMMONCONTROLSEX ic = {sizeof ic, ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&ic); CoInitialize(nullptr);
    int argc; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc); std::wstring dumpTo; int startPage = 0; bool openNew = false;
    wchar_t self[MAX_PATH]; GetModuleFileNameW(nullptr, self, MAX_PATH); g_dir = Dir(self);
    for (int i = 1; i < argc; ++i) { std::wstring a = argv[i]; if (a == L"--dir" && i + 1 < argc) g_dir = argv[++i]; else if (a == L"--dump-controls" && i + 1 < argc) dumpTo = argv[++i]; else if (a == L"--no-activate") g_noActivate = true; else if (a == L"--page" && i + 1 < argc) startPage = _wtoi(argv[++i]); else if (a == L"--stat-file" && i + 1 < argc) g_statFile = argv[++i]; else if (a == L"--cmd-file" && i + 1 < argc) g_cmdFile = argv[++i]; else if (a == L"--new-game-window") openNew = true; }
    if (!Exists(g_dir + L"\\witness64_d3d11.exe") && !Exists(g_dir + L"\\witness_d3d11.exe") && Exists(Dir(g_dir) + L"\\witness64_d3d11.exe")) g_dir = Dir(g_dir);
    th::Init(); AddFields();
    WNDCLASSW wc = {}; wc.lpfnWndProc = MainProc; wc.hInstance = hi; wc.lpszClassName = L"WitnessLauncher"; wc.hbrBackground = th::BgBrush(); wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION); RegisterClassW(&wc);
    g_main = CreateWindowExW(0, L"WitnessLauncher", L"The Witness co-op: launcher", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 880, 790, nullptr, nullptr, hi, nullptr);
    RECT rc; GetClientRect(g_main, &rc);
    g_tab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 8, 88, rc.right - 16, rc.bottom - 88 - 64, g_main, (HMENU)ID_TAB, hi, nullptr); SendMessageW(g_tab, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    BuildPages(g_main);
    Make(g_main, L"BUTTON", L"Save settings", BS_PUSHBUTTON, 12, rc.bottom - 52, 150, 34, ID_APPLY); g_status = Make(g_main, L"STATIC", L"", SS_LEFT, 176, rc.bottom - 46, rc.right - 190, 24, ID_STATUS); th::MarkMuted(g_status);
    LoadAll(); SetTimer(g_main, 1, 100, nullptr); if (startPage < 0 || startPage >= g_npages) startPage = 0; TabCtrl_SetCurSel(g_tab, startPage); ShowPage(startPage); if (openNew) OpenNewGame();
    if (!dumpTo.empty()) { std::string t = std::to_string(ID_STATUS) + " status\n"; for (auto& f : g_fields) t += std::to_string(f.id) + " " + f.key + "\n"; t += std::to_string(ID_APPLY) + " apply\n" + std::to_string(ID_SAVEPICK) + " savepick\n" + std::to_string(ID_LAUNCH) + " launch\n";
        const std::pair<int, const char*> lb[] = {{ID_LB_HOST, "lobby:host"}, {ID_LB_JOIN, "lobby:join"}, {ID_LB_INVITE, "lobby:invite"}, {ID_LB_COPY, "lobby:copy"}, {ID_LB_LEAVE, "lobby:leave"}, {ID_LB_READY, "lobby:ready"}, {ID_LB_START, "lobby:start"}, {ID_LB_MEMBERS, "lobby:members"}, {ID_LB_LOG, "lobby:log"}, {ID_LB_CHAT, "lobby:chat"}, {ID_LB_SEND, "lobby:send"}, {ID_LB_MODE, "lobby:mode"}, {ID_LB_GOAL, "lobby:goal"}, {ID_LB_NEWGAME, "lobby:newgame"}, {ID_LB_PARTS, "lobby:parts"}, {ID_LB_STATUS, "lobby:status"}, {ID_LB_PORT, "lobby:port"}, {ID_LB_ADVERTISE, "lobby:advertise"}, {ID_LB_MUTE, "lobby:mute"}, {ID_LB_DEAF, "lobby:deaf"}, {ID_LB_RESULT, "lobby:result"}};
        for (const auto& k : lb) t += std::to_string(k.first) + " " + k.second + "\n";
        WriteFile8(dumpTo, t); }
    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if ((msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN || msg.message == WM_KEYUP || msg.message == WM_SYSKEYUP) &&
            g_guideHold.Key((unsigned)msg.wParam, msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN)) {
            ShowGuide(g_guideHold.Open()); continue;
        }
        if (!IsDialogMessageW(g_ng ? g_ng : g_main, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    return 0;
}
