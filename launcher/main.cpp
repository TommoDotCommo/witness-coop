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
#include "core.h"
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
std::vector<Field> g_fields; HWND g_main, g_tab, g_pages[8], g_status; int g_npages;
std::wstring g_dir;                       // the game folder
bool g_noActivate;                        // --no-activate (tests): the game's window is shown without taking the focus
const wchar_t* kPageNames[] = {L"Play", L"Video", L"Sound and voice", L"Controls", L"Game options", L"Co-op", L"Saves"};

std::wstring CfgPath() { return g_dir + L"\\witness_coop.cfg"; }
std::wstring SaveFolder(const wl::CfgFile& c) {      // where the game keeps its saves, as the mod redirects them (save_dir=instance|<path>)
    std::string sd = c.Get("save_dir");
    if (sd == "instance") return g_dir + L"\\appdata\\The Witness";
    if (!sd.empty()) return W(sd) + L"\\The Witness";
    wchar_t p[MAX_PATH]; if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, p))) return std::wstring(p) + L"\\The Witness"; return L"";
}
wl::CfgFile LoadCfg() { return wl::CfgFile::Parse(ReadFile8(CfgPath())); }
void Status(const std::wstring& s) { SetWindowTextW(g_status, s.c_str()); }
std::vector<std::string> Models() { std::vector<std::string> r; WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((g_dir + L"\\witness_models\\*.glb").c_str(), &fd); if (h == INVALID_HANDLE_VALUE) return r; do { std::string n = U(fd.cFileName); r.push_back(n.substr(0, n.size() - 4)); } while (FindNextFileW(h, &fd)); FindClose(h); std::sort(r.begin(), r.end()); return r; }

void AddFields() {
    int id = ID_FIELD; auto add = [&](int page, const char* label, const char* key, Kind k, std::vector<std::string> ch = {}, const char* def = "", const char* hint = "") { g_fields.push_back({id++, page, label, key, k, ch, def, hint}); };
    const std::vector<std::string> q = {"Low", "Medium", "High"}, msaa = {"Off", "2x MSAA", "4x MSAA", "8x MSAA", "16x MSAA"}, res = {"Display resolution", "1080p", "720p", "Dynamic resolution", "Custom size"};
    add(1, "Render quality profile", "vid:renderProfile", kCombo, q); add(1, "Full screen", "vid:fullscreen", kCheck); add(1, "Wait for vertical sync", "vid:vsync", kCheck); add(1, "High DPI", "vid:highdpi", kCheck);
    add(1, "Resolution", "vid:resolution", kCombo, res); add(1, "Custom width", "vid:customW", kEdit, {}, "1920"); add(1, "Custom height", "vid:customH", kEdit, {}, "1080"); add(1, "Anti-aliasing", "vid:multisampling", kCombo, msaa);
    add(1, "Shading quality", "vid:shading", kCombo, q); add(1, "Reflection quality", "vid:reflection", kCombo, q); add(1, "Texture quality", "vid:texture", kCombo, q); add(1, "Texture detail", "vid:textureDetail", kCombo, q); add(1, "Geometry detail", "vid:geometry", kCombo, q);
    add(1, "Frame rate limit (0: none)", "cfg:cap_fps", kEdit, {}, "0");
    add(2, "Game sound off (starts the game with -no_sound)", "cfg:launcher_no_sound", kCheck); add(2, "Game volume, percent", "cfg:game_volume", kEdit, {}, "100", "the game's own sound, set through the Windows volume mixer when it starts");
    add(2, "Voice chat", "cfg:voice_mode", kCombo, {"off", "open microphone", "push to talk"}, "off"); add(2, "Push-to-talk key", "cfg:key_voice", kEdit, {}, "V", "a letter or F1 to F12");
    add(2, "Voice volume, percent", "cfg:voice_volume", kEdit, {}, "100"); add(2, "Microphone volume, percent", "cfg:mic_volume", kEdit, {}, "100"); add(2, "Hear players within (metres)", "cfg:voice_range", kEdit, {}, "25", "beyond it the voice turns into radio");
    add(2, "Radio range (metres, 0: unlimited)", "cfg:voice_radio_range", kEdit, {}, "0"); add(2, "Radio sound when far away", "cfg:voice_radio", kCheck, {}, "1");
    add(3, "Chat", "cfg:key_chat", kEdit, {}, "T", "a letter or F1 to F12"); add(3, "Ping a place", "cfg:key_ping", kEdit, {}, "P"); add(3, "Emote wheel (hold)", "cfg:key_emote", kEdit, {}, "G"); add(3, "Draw (hold)", "cfg:key_draw", kEdit, {}, "X");
    add(3, "Drawing canvas", "cfg:key_canvas", kEdit, {}, "Z"); add(3, "Ink colour", "cfg:key_color", kEdit, {}, "B"); add(3, "Character list", "cfg:key_character", kEdit, {}, "C");
    for (const auto& d : wl::SaveOptionDefs()) add(4, d.label, (std::string("opt:") + d.name).c_str(), d.kind == wl::OptKind::Bool ? kCheck : kEdit);
    add(5, "Player number (1, 2, ...)", "cfg:id", kEdit, {}, "1"); add(5, "Name shown to the others", "cfg:name", kEdit); add(5, "UDP port to listen on", "cfg:listen", kEdit, {}, "47001");
    add(5, "Other players (one per line: number,address,port)", "cfgmulti:peer", kMulti); add(5, "Your character", "cfg:character", kCombo, {}, ""); add(5, "Show other players as different models", "cfg:model_by_player", kCheck);
    add(5, "Developer menu (F8)", "cfg:dev_menu", kCheck);
}
Field* ByKey(const std::string& k) { for (auto& f : g_fields) if (f.key == k) return &f; return nullptr; }
std::string GetVal(Field& f) {
    wchar_t b[2000] = {};
    if (f.kind == kCheck) return SendMessageW(f.h, BM_GETCHECK, 0, 0) == BST_CHECKED ? "1" : "0";
    if (f.kind == kCombo) { int i = (int)SendMessageW(f.h, CB_GETCURSEL, 0, 0); return i < 0 ? "" : (f.key.rfind("vid:", 0) == 0 ? std::to_string(i) : f.choices[i]); }
    GetWindowTextW(f.h, b, 2000); return U(b);
}
void SetVal(Field& f, const std::string& v) {
    if (f.kind == kCheck) SendMessageW(f.h, BM_SETCHECK, (v == "1" || v == "true") ? BST_CHECKED : BST_UNCHECKED, 0);
    else if (f.kind == kCombo) { int i = -1; if (f.key.rfind("vid:", 0) == 0) i = atoi(v.c_str()); else for (size_t k = 0; k < f.choices.size(); ++k) if (f.choices[k] == v) i = (int)k; if (i < 0 && !f.choices.empty()) i = 0; SendMessageW(f.h, CB_SETCURSEL, i, 0); }
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
    RefreshSaves();
    // the game's options come from the save chosen in the box (the newest first)
    std::vector<uint8_t> data; if (!g_saves.empty()) { std::string t = ReadFile8(g_saves[0].path); data.assign(t.begin(), t.end()); }
    auto opts = wl::ReadSaveOptions(data);
    for (auto& f : g_fields) if (f.key.rfind("opt:", 0) == 0) { bool found = false; for (const auto& o : opts) if (o.name == f.key.substr(4)) { found = true; char b[40]; snprintf(b, sizeof b, "%g", o.value); SetVal(f, f.kind == kCheck ? (o.value != 0 ? "1" : "0") : b); } EnableWindow(f.h, found); }
    ShowSaveInfo(g_saves.empty() ? -1 : 0);
}
bool ApplyAll(std::wstring* msg) {
    wl::CfgFile c = LoadCfg(); wl::Video v; std::wstring sf = SaveFolder(c); wl::ParseVideo(ReadFile8(sf + L"\\Launcher.variables"), &v);
    for (auto& f : g_fields) {
        std::string val = GetVal(f);
        if (f.key.rfind("vid:", 0) == 0) { std::string k = f.key.substr(4); int n = atoi(val.c_str());
            if (k == "renderProfile") v.renderProfile = n; else if (k == "fullscreen") v.fullscreen = n; else if (k == "vsync") v.vsync = n; else if (k == "highdpi") v.highdpi = n; else if (k == "resolution") v.resolution = n; else if (k == "customW") v.customW = n; else if (k == "customH") v.customH = n;
            else if (k == "multisampling") v.multisampling = n; else if (k == "shading") v.shading = n; else if (k == "reflection") v.reflection = n; else if (k == "texture") v.texture = n; else if (k == "textureDetail") v.textureDetail = n; else v.geometry = n; }
        else if (f.key.rfind("cfg:", 0) == 0) {
            std::string k = f.key.substr(4); if (f.kind == kCombo && (val == "(default)" || val.empty())) val = "";
            if (k.rfind("key_", 0) == 0 && !val.empty()) { for (auto& ch : val) ch = (char)toupper((unsigned char)ch); bool ok = (val.size() == 1 && isalnum((unsigned char)val[0])) || (val[0] == 'F' && atoi(val.c_str() + 1) >= 1 && atoi(val.c_str() + 1) <= 12); if (!ok) { *msg = W(f.label + ": a letter, a digit or F1 to F12"); return false; } }
            if (f.kind == kEdit && (k == "cap_fps" || k == "game_volume" || k == "voice_volume" || k == "mic_volume" || k == "voice_range" || k == "voice_radio_range" || k == "id" || k == "listen")) { char* e; long n = strtol(val.c_str(), &e, 10); if (val.empty() || *e || n < 0 || n > 65535) { *msg = W(f.label + ": a whole number from 0 to 65535"); return false; } if ((k == "game_volume" || k == "voice_volume" || k == "mic_volume") && n > 100) { *msg = W(f.label + ": 0 to 100"); return false; } }
            if (val.empty() || (val == f.def && k != "id" && k != "listen" && k != "name")) c.Unset(k); else c.Set(k, val);
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
HWND g_ng; std::vector<std::pair<int, std::string>> g_ngAreas;
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
    DeleteFileW((sf + L"\\coop_progress.txt").c_str());      // the forced-progress list of the old world must not be put into this one
    if (spec.empty()) c.Unset("start_progress"); else c.Set("start_progress", spec);
    WriteFile8(CfgPath(), c.Text()); DestroyWindow(g_ng); g_ng = nullptr; LaunchGame(true);
}
LRESULT CALLBACK NgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_COMMAND) { int id = LOWORD(w); if (id == ID_NG_OK) { StartNewGame(); return 0; } if (id == IDCANCEL) { DestroyWindow(h); g_ng = nullptr; return 0; } if (id >= ID_NG_AREAS || id == ID_NG_PRESET || id == ID_NG_LASERS) NgUpdate(); return 0; }
    if (m == WM_CLOSE) { DestroyWindow(h); g_ng = nullptr; return 0; }
    return DefWindowProcW(h, m, w, l);
}
void OpenNewGame() {
    if (g_ng) { SetForegroundWindow(g_ng); return; }
    static bool reg; if (!reg) { WNDCLASSW wc = {}; wc.lpfnWndProc = NgProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"WitnessNewGame"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc); reg = true; }
    g_ng = CreateWindowExW(WS_EX_DLGMODALFRAME, L"WitnessNewGame", L"New game: what is already completed", WS_CAPTION | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 620, 640, g_main, nullptr, GetModuleHandleW(nullptr), nullptr);
    HFONT font = (HFONT)SendMessageW(g_main, WM_GETFONT, 0, 0); auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) { HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, g_ng, (HMENU)(INT_PTR)id, nullptr, nullptr); SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE); return c; };
    mk(L"STATIC", L"Tick the parts of the map that are done when the game starts. Whatever the game needs before them (the gate of the tutorial, the doors on the way) is solved too, in its own order.", SS_LEFT, 12, 8, 590, 36, 0);
    const auto& d = wprog::Builtin(); g_ngAreas.clear(); int y = 50, col = 0, k = 0;
    for (size_t i = 0; i < d.areas.size(); ++i) { if (!d.AreaCount((int)i)) continue; int id = ID_NG_AREAS + k++; g_ngAreas.push_back({id, d.areas[i]}); mk(L"BUTTON", W(d.areas[i] + "  (" + std::to_string(d.AreaCount((int)i)) + ")").c_str(), BS_AUTOCHECKBOX, 16 + col * 290, y, 280, 22, id); if (++col == 2) { col = 0; y += 24; } }
    if (col) y += 24; y += 8;
    mk(L"STATIC", L"Or light this many lasers (the cheapest first):", SS_LEFT, 16, y + 3, 280, 20, 0); mk(L"EDIT", L"0", WS_BORDER | ES_NUMBER, 300, y, 50, 22, ID_NG_LASERS);
    y += 32; mk(L"STATIC", L"Or a ready-made start:", SS_LEFT, 16, y + 3, 280, 20, 0); HWND cb = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 300, y, 200, 200, ID_NG_PRESET);
    SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"(none)"); for (const auto& n : wprog::PresetNames()) SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)W(n).c_str()); SendMessageW(cb, CB_SETCURSEL, 0, 0);
    y += 40; mk(L"STATIC", L"", SS_LEFT, 16, y, 580, 60, ID_NG_INFO); y += 70;
    mk(L"BUTTON", L"Start new game", BS_DEFPUSHBUTTON, 16, y, 160, 30, ID_NG_OK); mk(L"BUTTON", L"Cancel", BS_PUSHBUTTON, 190, y, 100, 30, IDCANCEL);
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
    SendMessageW(c, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE); return c;
}
void ShowPage(int p) { for (int i = 0; i < g_npages; ++i) ShowWindow(g_pages[i], i == p ? SW_SHOW : SW_HIDE); }
LRESULT CALLBACK PageProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_COMMAND) { SendMessageW(g_main, WM_COMMAND, w, l); return 0; }
    if (m == WM_NOTIFY) { NMHDR* n = (NMHDR*)l; if (n->idFrom == ID_SAVELIST && n->code == LVN_ITEMCHANGED) { NMLISTVIEW* nl = (NMLISTVIEW*)l; if (nl->uNewState & LVIS_SELECTED) ShowSaveInfo(nl->iItem); } return 0; }
    return DefWindowProcW(h, m, w, l);
}
void BuildPages(HWND parent) {
    WNDCLASSW wc = {}; wc.lpfnWndProc = PageProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"WitnessLauncherPage"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClassW(&wc);
    RECT r; GetClientRect(g_tab, &r); TabCtrl_AdjustRect(g_tab, FALSE, &r); g_npages = 7;
    for (int i = 0; i < g_npages; ++i) { TabCtrl_InsertItem(g_tab, i, &(TCITEMW&)*[&] { static TCITEMW t[8]; t[i] = {}; t[i].mask = TCIF_TEXT; t[i].pszText = (LPWSTR)kPageNames[i]; return &t[i]; }());
        g_pages[i] = CreateWindowExW(0, L"WitnessLauncherPage", L"", WS_CHILD, r.left, r.top, r.right - r.left, r.bottom - r.top, g_tab, nullptr, GetModuleHandleW(nullptr), nullptr); }
    // Play
    int y = 16; Make(g_pages[0], L"STATIC", L"Game folder:", SS_LEFT, 16, y + 3, 100, 20, 0); Make(g_pages[0], L"EDIT", g_dir.c_str(), WS_BORDER | ES_AUTOHSCROLL | ES_READONLY, 120, y, 420, 24, ID_GAMEDIR); Make(g_pages[0], L"BUTTON", L"Browse...", BS_PUSHBUTTON, 550, y, 90, 24, ID_BROWSE);
    y += 50; Make(g_pages[0], L"BUTTON", L"Continue (start the game)", BS_DEFPUSHBUTTON, 16, y, 240, 40, ID_LAUNCH); Make(g_pages[0], L"BUTTON", L"New game...", BS_PUSHBUTTON, 270, y, 160, 40, ID_NEWGAME);
    Make(g_pages[0], L"STATIC", L"Continue starts the game with the settings of every page, on your latest save. New game asks what is already completed and starts a fresh world; your saves stay where they are.", SS_LEFT, 16, y + 56, 620, 40, 0);
    // fields
    std::vector<std::string> models = Models();
    for (auto& f : g_fields) {
        HWND pg = g_pages[f.page]; static int rowY[8]; if (!rowY[f.page]) rowY[f.page] = 12; int ry = rowY[f.page]; int lw = 300;
        if (f.kind == kMulti) { Make(pg, L"STATIC", W(f.label).c_str(), SS_LEFT, 16, ry + 3, lw, 20, 0); f.h = Make(pg, L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL, 330, ry, 320, 90, f.id); rowY[f.page] += 98; continue; }
        Make(pg, L"STATIC", W(f.label).c_str(), SS_LEFT, 16, ry + 3, lw, 20, 0);
        if (f.kind == kCheck) f.h = Make(pg, L"BUTTON", L"", BS_AUTOCHECKBOX, 330, ry, 24, 22, f.id);
        else if (f.kind == kEdit) f.h = Make(pg, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 330, ry, 120, 22, f.id);
        else { f.h = Make(pg, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 330, ry, 220, 200, f.id); if (f.key == "cfg:character") { f.choices = {"(default)"}; for (auto& m : models) f.choices.push_back(m); } for (auto& c : f.choices) SendMessageW(f.h, CB_ADDSTRING, 0, (LPARAM)W(c).c_str()); }
        if (!f.hint.empty()) Make(pg, L"STATIC", W(f.hint).c_str(), SS_LEFT, 460, ry + 3, 200, 20, 0);
        rowY[f.page] += 28;
    }
    // saves
    HWND sp = g_pages[6]; g_saveList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 12, 12, 650, 190, sp, (HMENU)ID_SAVELIST, nullptr, nullptr);
    ListView_SetExtendedListViewStyle(g_saveList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES); SendMessageW(g_saveList, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    const wchar_t* cols[] = {L"Saved", L"Players", L"Progress", L"Lasers", L"Time played"}; int cw[] = {120, 230, 120, 60, 100};
    for (int i = 0; i < 5; ++i) { LVCOLUMNW c = {}; c.mask = LVCF_TEXT | LVCF_WIDTH; c.pszText = (LPWSTR)cols[i]; c.cx = cw[i]; ListView_InsertColumn(g_saveList, i, &c); }
    g_saveInfo = Make(sp, L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL, 12, 212, 650, 200, ID_SAVEINFO);
    Make(sp, L"BUTTON", L"Refresh", BS_PUSHBUTTON, 12, 422, 90, 26, ID_REFRESH); Make(sp, L"BUTTON", L"Copy to backup", BS_PUSHBUTTON, 112, 422, 120, 26, ID_BACKUP); Make(sp, L"BUTTON", L"Move to backup folder", BS_PUSHBUTTON, 242, 422, 150, 26, ID_TRASH); Make(sp, L"BUTTON", L"Open folder", BS_PUSHBUTTON, 402, 422, 100, 26, ID_OPENSAVES);
    // the game-options page chooses its save
    Make(g_pages[4], L"STATIC", L"Save to change:", SS_LEFT, 16, 330, 200, 20, 0); g_savePick = Make(g_pages[4], L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 330, 326, 220, 200, ID_SAVEPICK);
    Make(g_pages[4], L"STATIC", L"These are the game's own options; they live inside each save. A copy of the save as it was is kept once (.before-launcher).", SS_LEFT, 16, 362, 640, 36, 0);
}
LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_APPLY) { std::wstring msg; ApplyAll(&msg); Status(msg); LoadAll(); }
        else if (id == ID_LAUNCH) LaunchGame(false);
        else if (id == ID_NEWGAME) OpenNewGame();
        else if (id == ID_REFRESH) { LoadAll(); Status(L"Reloaded."); }
        else if (id == ID_OPENSAVES) ShellExecuteW(nullptr, L"open", SaveFolder(LoadCfg()).c_str(), nullptr, nullptr, SW_SHOW);
        else if (id == ID_BACKUP || id == ID_TRASH) {
            int i = ListView_GetNextItem(g_saveList, -1, LVNI_SELECTED); if (i < 0 || i >= (int)g_saves.size()) { Status(L"Select a save first."); break; }
            std::wstring dir = SaveFolder(LoadCfg()) + L"\\launcher-backup\\" + Stamp();
            if (BackupSave(g_saves[i], id == ID_TRASH, dir)) Status(std::wstring(id == ID_TRASH ? L"Moved to " : L"Copied to ") + dir); else Status(L"Could not do that."); LoadAll();
        }
        else if (id == ID_BROWSE) { BROWSEINFOW bi = {}; bi.hwndOwner = h; bi.lpszTitle = L"The Witness game folder"; PIDLIST_ABSOLUTE p = SHBrowseForFolderW(&bi); if (p) { wchar_t b[MAX_PATH]; if (SHGetPathFromIDListW(p, b)) { g_dir = b; SetDlgItemTextW(g_pages[0], ID_GAMEDIR, b); LoadAll(); } CoTaskMemFree(p); } }
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
    for (int i = 1; i < argc; ++i) { std::wstring a = argv[i]; if (a == L"--dir" && i + 1 < argc) g_dir = argv[++i]; else if (a == L"--dump-controls" && i + 1 < argc) dumpTo = argv[++i]; else if (a == L"--no-activate") g_noActivate = true; else if (a == L"--page" && i + 1 < argc) startPage = _wtoi(argv[++i]); else if (a == L"--new-game-window") openNew = true; }
    if (!Exists(g_dir + L"\\witness64_d3d11.exe") && !Exists(g_dir + L"\\witness_d3d11.exe") && Exists(Dir(g_dir) + L"\\witness64_d3d11.exe")) g_dir = Dir(g_dir);
    AddFields();
    WNDCLASSW wc = {}; wc.lpfnWndProc = MainProc; wc.hInstance = hi; wc.lpszClassName = L"WitnessLauncher"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION); RegisterClassW(&wc);
    g_main = CreateWindowExW(0, L"WitnessLauncher", L"The Witness co-op: launcher", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 720, 640, nullptr, nullptr, hi, nullptr);
    RECT rc; GetClientRect(g_main, &rc);
    g_tab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 8, 8, rc.right - 16, rc.bottom - 80, g_main, (HMENU)ID_TAB, hi, nullptr); SendMessageW(g_tab, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    BuildPages(g_main);
    Make(g_main, L"BUTTON", L"Save settings", BS_PUSHBUTTON, 8, rc.bottom - 60, 140, 30, ID_APPLY); g_status = Make(g_main, L"STATIC", L"", SS_LEFT, 160, rc.bottom - 54, rc.right - 170, 24, ID_STATUS);
    LoadAll(); if (startPage < 0 || startPage >= g_npages) startPage = 0; TabCtrl_SetCurSel(g_tab, startPage); ShowPage(startPage); if (openNew) OpenNewGame();
    if (!dumpTo.empty()) { std::string t = std::to_string(ID_STATUS) + " status\n"; for (auto& f : g_fields) t += std::to_string(f.id) + " " + f.key + "\n"; t += std::to_string(ID_APPLY) + " apply\n" + std::to_string(ID_SAVEPICK) + " savepick\n" + std::to_string(ID_LAUNCH) + " launch\n"; WriteFile8(dumpTo, t); }
    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { if (!IsDialogMessageW(g_ng ? g_ng : g_main, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
    return 0;
}
