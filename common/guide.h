#pragma once
// Portable guide model: both renderers use these bindings, text and word wrapping.
#include "../proxy/config.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <set>
#include <string>
#include <vector>
namespace guide {
struct Binding { const char* config; unsigned Cfg::*member; unsigned initial; const char* text; const char* label; };
inline const Binding bindings[] = {
#define BIND(name, member, key, text, label) {name, &Cfg::member, key, text, label},
#include "bindings.def"
#undef BIND
};
// Fixed mod keys live here too, and are used by their dispatchers.
constexpr unsigned Help = 0x70, Boost = 0x72, Nearby = 0x73, Roster = 0x74,
    DevMenu = 0x77, Video = 0x78, TvView = 0x79, TvConsent = 0x7A,
    Watch = 'V', PrevView = 0xDB, NextView = 0xDD;
struct Fixed { unsigned key; const char* text; };
inline const Fixed fixed[] = {
    {Help, "Hold: quick guide (game and launcher)"},
    {Boost, "Toggle puzzle mouse speed boost"},
    {Roster, "Player list: arrows / Enter; Back/Del clears"},
    {Watch, "Watch / clear a player's screen"},
    {PrevView, "Watch previous player"}, {NextView, "Watch next player"},
    {Video, "Partner picture: small / large / off"},
    {TvView, "Toggle back TV viewer (on / off)"},
    {TvConsent, "Toggle back TV camera broadcast consent"},
    {DevMenu, "Developer menu (if enabled); Tab: page"},
    {Nearby, "Nearby puzzles (developer mode)"},
};
inline std::string KeyName(unsigned k) {
    if (k >= 0x70 && k <= 0x7B) return "F" + std::to_string(k - 0x6F);
    if (k == PrevView) return "[";
    if (k == NextView) return "]";
    return k ? std::string(1, char(k)) : "unbound";
}
inline unsigned ParseKey(std::string s) {
    for (auto& c : s) c = char(std::toupper((unsigned char)c));
    if (s.size() == 1 && std::isalnum((unsigned char)s[0])) return unsigned(s[0]);
    for (unsigned k = 0x70; k <= 0x7B; ++k) if (s == KeyName(k)) return k;
    return 0;
}
inline bool Reserved(unsigned k) {
    for (const auto& f : fixed) if (k == f.key) return true;
    return k == 'W' || k == 'A' || k == 'S' || k == 'D';
}
inline std::string Validate(const Cfg& c) {
#ifdef GUIDE_MUT_ALLOW_COLLISION
    return {};
#endif
    std::set<unsigned> used;
    for (const auto& b : bindings) {
        unsigned k = c.*b.member;
        if (!k || Reserved(k) || !used.insert(k).second)
            return std::string(b.config) + ": key is reserved or already used";
    }
    return {};
}
inline void MigrateF1(Cfg& c) {
#ifndef GUIDE_MUT_NO_MIGRATE
    std::set<unsigned> used;
    for (const auto& b : bindings) used.insert(c.*b.member);
    for (const auto& b : bindings) if (c.*b.member == Help) {
        // F2 is free in the game and launcher. Keep other custom keys intact.
        for (unsigned k : {0x71u, 0x7Bu, unsigned('0'), unsigned('1'), unsigned('2'), unsigned('3'), unsigned('4'), unsigned('5'), unsigned('6'), unsigned('7'), unsigned('8'), unsigned('9')})
            if (!used.count(k) && !Reserved(k)) { c.*b.member = k; used.insert(k); break; }
    }
#else
    (void)c;
#endif
}
class Hold {
    std::atomic<bool> held{false};
public:
    bool Key(unsigned vk, bool down) {
        if (vk != Help) return false;
#ifdef GUIDE_MUT_TOGGLE
        if (down) held = !held.load();
#else
        held = down;
#endif
        return true;
    }
    void Reset() { held = false; }
    bool Open() const { return held.load(); }
};
using Columns = std::array<std::vector<std::string>, 3>;
inline Columns Content(const Cfg& c) {
    Columns out;
    out[0].push_back("MOD KEYS - your current bindings");
    for (const auto& b : bindings) {
#ifdef GUIDE_MUT_MISSING_KEY
        if (b.member == &Cfg::keyChat) continue;
#endif
        out[0].push_back(KeyName(c.*b.member) + "  " + b.text);
    }
    out[0].push_back("FIXED KEYS");
    for (const auto& b : fixed) out[0].push_back(KeyName(b.key) + "  " + b.text);
    out[0].push_back("GAME / MENU CONTROLS");
    out[0].push_back("Game uses your settings. Defaults: WASD move, mouse look / draw, click puzzles, Esc back.");
    out[0].push_back("Lists: arrows, Enter, Esc. Launcher: Tab / Shift+Tab and Enter.");
    out[0].push_back("Dev map: wheel zoom, right-drag pan; click a dot. Buttons teleport or act on a puzzle.");
    out[1] = {
        "PLAY TOGETHER",
        "Explore independently. See animated teammates, names, typing dots, pings and emotes.",
        "Co-op shares solved panels, traced puzzle lines, opened doors, panel power and cable lighting. Late arrivals recover shared progress.",
        "Players fade when blocking a puzzle. Watch a partner's view to compare clues. Draw on surfaces or use the precise screen canvas.",
        "Voice: proximity is spatial nearby, radio farther away. Channels: proximity, radio, open line (global), or one on one. The mic indicator names yours.",
        "Race mode keeps worlds and puzzle lines separate. Players, chat and voice remain. Wait for all worlds, countdown, GO; first to the chosen goal wins.",
        "HOST / JOIN - in the launcher",
        "Host a lobby, copy the invite and send it to friends. Join with the invite. Check the roster and chat / voice before starting.",
        "Host chooses co-op or race, goal and new-game progress. Guests mark Ready; host starts when everyone is ready. Return to the lobby after the game.",
        "Saves list participants and progress. Back up saves, or start a new game with chosen island areas already completed."
    };
    out[2] = {
        "HOW IT WORKS",
        "Each player runs their own game and mod. A UDP mesh exchanges movement and pictures; reliable events retry shared world changes. Local game functions apply progress between frames.",
        "Everyone needs the same supported game build and mod version. Steam build 11370412 is supported. Use matching model packs to see the intended characters.",
        "Your movement and camera stay yours. Shared solves run their targets, but moving platforms and bridges are not synced. This is experimental co-op.",
        "TROUBLESHOOTING",
        "Can't connect? Check invite, peer address / ports and firewall rules. Across the internet use reachable UDP addresses or a shared VPN; a lobby invite does not bypass NAT.",
        "Missing progress? Wait for both worlds to load and catch up. Match co-op / race mode. Race deliberately does not share world progress.",
        "No voice? Check voice mode, PTT key, microphone, volumes, mute / deafen and channel. One-on-one only reaches its selected recipient.",
        "Slow puzzle cursor? Toggle the puzzle mouse boost. No picture? Choose a player, check partner view is enabled and wait for a frame.",
        "Hold F1 here or in game; release to return. The game keeps running. Settings edits take effect after restarting the game."
    };
    return out;
}
// Fixed-width wrapping matches the game's existing 10-pixel glyph cells and the
// launcher's monospace guide font. No elision: every word remains on screen.
inline Columns Wrap(const Columns& input, size_t width = 48) {
    Columns out;
    for (size_t c = 0; c < input.size(); ++c) for (const auto& paragraph : input[c]) {
        std::string line;
        size_t at = 0;
        while (at < paragraph.size()) {
            size_t end = paragraph.find(' ', at); if (end == std::string::npos) end = paragraph.size();
            std::string word = paragraph.substr(at, end - at);
            if (!line.empty() && line.size() + 1 + word.size() > width) { out[c].push_back(line); line.clear(); }
            if (!line.empty()) line += ' ';
            line += word; at = end + 1;
        }
        out[c].push_back(line);
        if (c != 0) out[c].push_back("");
    }
    return out;
}
struct Layout { float scale, rowHeight, columnWidth; };
inline Layout Fit(float width, float height, const Columns& columns) {
    size_t rows = 1;
    for (const auto& col : columns) rows = std::max(rows, col.size());
    float scale = std::min((width - 48) / 1500.f, (height - 84) / (rows * 21.f));
    return {scale, 21 * scale, (width - 32) / 3};
}
} // namespace guide
