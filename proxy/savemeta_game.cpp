// Writes <game save>.coop.txt next to the newest save of the game (progress/savemeta.h): who was in the game and how far it got. See savemeta.h.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include "coop.h"
#include "config.h"
#include "savepoints.h"
#include "social.h"
#include "progress/savemeta.h"

void Log(const char* msg);
bool DevMenuSnapshot(std::set<unsigned>* solved, unsigned* doorsOpen, unsigned* doorsTotal);      // devmenu.cpp

namespace {
std::wstring Swap(const std::wstring& p, const wchar_t* ext) { size_t d = p.rfind(L'.'); return (d == std::wstring::npos ? p : p.substr(0, d)) + ext; }
std::string Utf8(const std::wstring& s) { char b[600]; WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, b, 600, nullptr, nullptr); return b; }
std::string ReadAll(const std::wstring& p) { std::string t; FILE* f = _wfopen(p.c_str(), L"rb"); if (f) { char b[4096]; size_t k; while ((k = fread(b, 1, sizeof b, f)) > 0) t.append(b, k); fclose(f); } return t; }
unsigned long long Ft(const FILETIME& f) { return ((unsigned long long)f.dwHighDateTime << 32) | f.dwLowDateTime; }
bool Newest(const std::wstring& dir, const wchar_t* pat, std::wstring* path, unsigned long long* when = nullptr) {
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\" + pat).c_str(), &fd); if (h == INVALID_HANDLE_VALUE) return false; unsigned long long best = 0; bool any = false;
    do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && Ft(fd.ftLastWriteTime) >= best) { best = Ft(fd.ftLastWriteTime); *path = dir + L"\\" + fd.cFileName; any = true; } } while (FindNextFileW(h, &fd));
    FindClose(h); if (when) *when = best; return any;
}
struct Seen { unsigned id = 0; std::string character; double seconds = 0; };
std::map<std::string, Seen> g_roster;      // this session, by name
}  // namespace

void SaveMetaTick() {
    static ULONGLONG start = GetTickCount64(), last = start, lastWrite = 0; static std::wstring lastFile; static unsigned long long lastWhen; static bool loadedEarlier; static wprog::SaveMeta earlier; static bool hadEarlier;
    const Cfg& cfg = GetCfg(); if (cfg.noSaveMeta) return;
    ULONGLONG now = GetTickCount64(); double dt = (now - last) / 1000.0; last = now; if (dt > 5) dt = 5;
    { Seen& me = g_roster[CoopMyName()]; me.id = CoopMyId(); me.character = SocialMyCharacter(); me.seconds += dt; }
    for (const auto& r : CoopGetRemotes()) if (r.name[0] && r.ageMs < 3000) { Seen& s = g_roster[r.name]; s.id = r.id; s.character = r.character; s.seconds += dt; }
    if (now - lastWrite < 3000) return;
    std::wstring dir = SaveFolder(), file; unsigned long long when = 0; if (dir.empty() || !Newest(dir, L"*.witness_campaign", &file, &when)) return;
    if (!loadedEarlier) {      // the sidecar of the newest save written before this session: its players and time are the earlier sessions of this campaign
        loadedEarlier = true; std::wstring prev; if (Newest(dir, L"*.coop.txt", &prev)) { hadEarlier = wprog::ParseMeta(ReadAll(prev), &earlier); }
    }
    if (file == lastFile && when == lastWhen && now - lastWrite < 20000) return;      // nothing new in the game's save; refresh the times now and then
    lastWrite = now; lastFile = file; lastWhen = when;
    wprog::SaveMeta m; std::set<unsigned> solved; unsigned open = 0, doors = 0;
    if (DevMenuSnapshot(&solved, &open, &doors)) wprog::FillProgress(wprog::Builtin(), solved, open, doors, &m);
    SYSTEMTIME st; GetLocalTime(&st); char b[40]; snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond); m.savedAt = b;
    m.sessionSeconds = unsigned((now - start) / 1000); m.totalSeconds = m.sessionSeconds + (hadEarlier ? earlier.totalSeconds : 0);
    for (const auto& kv : g_roster) { wprog::MetaPlayer p; p.id = kv.second.id; p.name = kv.first; p.character = kv.second.character; p.seconds = unsigned(kv.second.seconds); p.sessions = 1; m.players.push_back(p); }
    if (hadEarlier) wprog::MergePlayers(&m.players, earlier.players);
    std::wstring out = Swap(file, L".coop.txt"); std::string text = wprog::FormatMeta(m);
    if (m.total == 0) return;      // no census yet (the world is not up): nothing worth writing
    FILE* f = _wfopen(out.c_str(), L"wb"); if (f) { fwrite(text.data(), 1, text.size(), f); fclose(f); char msg[300]; snprintf(msg, sizeof msg, "savemeta: wrote %s (%u of %u panels, %zu players)", Utf8(out).c_str(), m.solved, m.total, m.players.size()); Log(msg); }
}
