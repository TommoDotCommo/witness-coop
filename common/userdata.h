#pragma once
// Where the mod's own files live (co-op mod standard J4): nothing is written into the game folder at run time.
//   %LOCALAPPDATA%\witness-coop\   witness_coop.cfg (settings), witness_proxy.log, witness_launcher_update.cfg, witness_race_result.txt, ...
// One exception, for test tools and the lab's per-instance copies: a witness_coop.cfg BESIDE the exe that carries one of the instance keys
// (save_dir, force_active, run_unfocused, diag_dir) is an explicit per-instance override; then every one of those files stays beside the exe,
// exactly as before, so two copies on one PC (or a copy next to the player's own) get separate settings. The mod never creates that file.
// A player's own witness_coop.cfg found beside the exe (written by an earlier version) is moved to the per-user folder once.
// The same header serves the DLL, the launcher and the game-free test (common/userdata_test.cpp).
#include <cstdio>
#include <cstring>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif
namespace userdata {

constexpr const char* kCfgName = "witness_coop.cfg";
constexpr const char* kInstanceKeys[] = { "save_dir", "force_active", "run_unfocused", "diag_dir" };

// True when this cfg text is an explicit per-instance override (an active line whose key is one of the instance keys).
inline bool IsInstanceCfg(const std::string& text) {
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at); if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at); at = end + 1;
        size_t s = 0; while (s < line.size() && (line[s] == ' ' || line[s] == '\t')) s++;
        if (s >= line.size() || line[s] == '#' || line[s] == ';') continue;
        size_t eq = line.find('=', s); if (eq == std::string::npos) continue;
        std::string key = line.substr(s, eq - s); while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
#ifdef USERDATA_MUT_ANY_CFG_IS_INSTANCE
        return true;      // mutant: every cfg beside the exe counts as an override, so a player's old cfg is never moved
#endif
        for (const char* k : kInstanceKeys) if (key == k) return true;
    }
    return false;
}

#ifdef _WIN32
inline std::wstring WithSlash(std::wstring d) { if (!d.empty() && d.back() != L'\\' && d.back() != L'/') d += L'\\'; return d; }
inline bool ReadAll(const std::wstring& path, std::string* out) {
    FILE* f = _wfopen(path.c_str(), L"rb"); if (!f) return false;
    char b[4096]; size_t n; out->clear();
    while ((n = fread(b, 1, sizeof b, f)) > 0) out->append(b, n);
    fclose(f); return true;
}
inline bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
// %LOCALAPPDATA%\witness-coop\ (created), or empty when Windows reports no profile folder.
inline std::wstring PerUserDir() {
    wchar_t la[MAX_PATH]; DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH);
    if (!n || n >= MAX_PATH - 20) return L"";
    std::wstring d = std::wstring(la) + L"\\witness-coop"; CreateDirectoryW(d.c_str(), nullptr);
    return d + L"\\";
}
// Moves a player's cfg from the game folder to `perUser` once: copied, read back and compared, then the old file is removed (a read-only game
// folder keeps it; the per-user copy wins from then on). Returns true when the per-user cfg was created by this call.
inline bool MigrateCfg(const std::wstring& exeDir, const std::wstring& perUser) {
    std::wstring from = exeDir + L"witness_coop.cfg", to = perUser + L"witness_coop.cfg";
    if (perUser.empty() || !Exists(from) || Exists(to)) return false;
    std::string text; if (!ReadAll(from, &text) || IsInstanceCfg(text)) return false;
    FILE* f = _wfopen(to.c_str(), L"wb"); if (!f) return false;
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size(); ok = (fclose(f) == 0) && ok;
    std::string back; if (!ok || !ReadAll(to, &back) || back != text) { DeleteFileW(to.c_str()); return false; }
#ifndef USERDATA_MUT_KEEP_OLD
    DeleteFileW(from.c_str());
#endif
    return true;
}
// The folder (trailing backslash) for settings and logs. `exeDir` is the game folder (the exe's, or this DLL's, which is the same folder).
inline std::wstring DataDir(std::wstring exeDir) {
    exeDir = WithSlash(exeDir);
    std::string text;
    if (ReadAll(exeDir + L"witness_coop.cfg", &text) && IsInstanceCfg(text)) return exeDir;
    std::wstring user = PerUserDir(); if (user.empty()) return exeDir;
    MigrateCfg(exeDir, user);
    return user;
}
#endif
}  // namespace userdata
