#pragma once
// Steam ids as text, game-free (tested by launcher/lobby_test.cpp and proxy/core/tests/peers.cpp).
// A SteamID64 packs universe (8 bits), account type (4 bits), instance (20 bits) and account number (32 bits). The two kinds used here:
// an individual (a player: universe 1, type 1) and a chat id (a lobby: universe 1, type 8). Lobby ids are typed by players (the fallback when the
// overlay is off) and come from Steam on the command line ("+connect_lobby <id>" when a friend accepts an invite and the game was not running).
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>

namespace steamids {
inline bool ParseU64(const char* p, uint64_t* out) {
    uint64_t n = 0; if (!p || !*p) return false;
    for (; *p; ++p) { if (*p < '0' || *p > '9') return false; unsigned d = unsigned(*p - '0'); if (n > (UINT64_MAX - d) / 10) return false; n = n * 10 + d; }
    *out = n; return true;
}
inline bool IsLobby(uint64_t id) { return (id >> 56) == 1 && ((id >> 52) & 15) == 8 && (id & 0xffffffffull) != 0; }
inline bool IsIndividual(uint64_t id) { return (id >> 56) == 1 && ((id >> 52) & 15) == 1 && (id & 0xffffffffull) != 0; }
// A lobby id typed by a player (spaces around it are fine).
inline bool ParseLobby(const std::string& text, uint64_t* id) {
    std::string t; for (char c : text) if (!isspace((unsigned char)c)) t += c;
    uint64_t n; if (!ParseU64(t.c_str(), &n) || !IsLobby(n)) return false; *id = n; return true;
}
// "+connect_lobby <id>" anywhere in a command line, as its own word (Steam's form; MDK2 handles the same case).
inline bool ParseConnect(const std::string& cmd, uint64_t* id) {
    static const char kTok[] = "+connect_lobby"; size_t at = 0;
    while ((at = cmd.find(kTok, at)) != std::string::npos) {
        bool startOk = at == 0 || isspace((unsigned char)cmd[at - 1]) || cmd[at - 1] == '"';
        size_t p = at + sizeof kTok - 1; at = p;
        if (!startOk || p >= cmd.size() || !isspace((unsigned char)cmd[p])) continue;
        while (p < cmd.size() && isspace((unsigned char)cmd[p])) ++p;
        size_t e = p; while (e < cmd.size() && cmd[e] >= '0' && cmd[e] <= '9') ++e;
        if (e < cmd.size() && !isspace((unsigned char)cmd[e]) && cmd[e] != '"') continue;
        uint64_t n; if (ParseU64(cmd.substr(p, e - p).c_str(), &n) && IsLobby(n)) { *id = n; return true; }
    }
    return false;
}
inline std::string Text(uint64_t id) { return std::to_string((unsigned long long)id); }
}  // namespace steamids
