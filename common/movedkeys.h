#pragma once
// The one-time "moved keys" list: written once per user when the key scheme changes, read (and removed) by the launcher's What's new.
// One helper for both: the proxy writes it at start, the launcher can write it too. Where the run-time files live is decided here only.
//   <dir>moved_keys.txt   one line per moved key
//   <dir>keys_scheme      the scheme number the list was written for; with it present (>= kScheme) nothing is written again
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
namespace movedkeys {
constexpr int kScheme = 2;      // 1: the keys before 2026-11 (P ping, N talk, G emote, H channel, B ink, V watch, F5 list); 2: the shared co-op keys
// `cfgDir`: the folder of witness_coop.cfg with a trailing separator. The per-user folder (%LOCALAPPDATA%\witness-coop\, where the logs live) is used unless
// `instance` is true; lab copies (`instance`: a per-instance diag_dir) and installs without one keep the files beside the cfg.
inline std::string Dir(const std::string& cfgDir, bool instance, const char* localAppData = nullptr) {
    if (!instance) {
        const char* la = localAppData ? localAppData : std::getenv("LOCALAPPDATA");
        if (la && *la) return std::string(la) + "\\witness-coop\\";      // (the caller creates it)
    }
    return cfgDir;
}
inline int SchemeOf(const std::string& dir) {
    FILE* f = std::fopen((dir + "keys_scheme").c_str(), "r"); if (!f) return 0;
    int v = 0; if (std::fscanf(f, "%d", &v) != 1) v = 0; std::fclose(f); return v;
}
// Writes the list unless this scheme was already handled. Returns true when it wrote one.
inline bool WriteOnce(const std::string& dir, const std::vector<std::string>& notes) {
#ifdef GUIDE_MUT_WRITE_ALWAYS
    if (false)
#else
    if (SchemeOf(dir) >= kScheme)
#endif
        return false;
    FILE* f = std::fopen((dir + "moved_keys.txt").c_str(), "w"); if (!f) return false;
    for (const auto& n : notes) std::fprintf(f, "%s\n", n.c_str());
    std::fclose(f);
    FILE* g = std::fopen((dir + "keys_scheme").c_str(), "w"); if (g) { std::fprintf(g, "%d\n", kScheme); std::fclose(g); }
    return true;
}
}  // namespace movedkeys
