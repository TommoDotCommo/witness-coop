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
// Mouse inputs are key codes below 0x08 (the Windows VK_ values for the buttons; 7 is the wheel, which has no VK).
constexpr unsigned MouseMiddle = 0x04, MouseX1 = 0x05, MouseX2 = 0x06, Wheel = 0x07;
constexpr unsigned Help = 0x70, Boost = 0x72, Nearby = 0x73, Roster = 0x09 /* hold Tab */,
    DevMenu = 0x77, Video = 0x78, TvView = 0x79, TvConsent = 0x7A,
    PrevView = 0xDB, NextView = 0xDD;
struct Fixed { unsigned key; const char* text; };
inline const Fixed fixed[] = {
    {Help, "Hold: quick guide (game and launcher)"},
    {Boost, "Toggle puzzle mouse speed boost"},
    {Roster, "Hold: player list (mouse or arrows / Enter: watch, mute, volume)"},
    {PrevView, "Watch previous player"}, {NextView, "Watch next player"},
    {Video, "Partner picture: small / large / off"},
    {TvView, "Toggle back TV viewer (on / off)"},
    {TvConsent, "Toggle back TV camera broadcast consent"},
    {DevMenu, "Developer menu (if enabled); Tab: page"},
    {Nearby, "Nearby puzzles (developer mode)"},
};
// The diagnostics marker: F2, unless a binding already uses it (the F1 migration may have put one there), then F12, else none.
inline unsigned MarkerKey(const Cfg& c) {
#ifdef GUIDE_MUT_MARKER_BOUND
    (void)c; return 0x71;
#else
    for (unsigned k : {0x71u, 0x7Bu}) {
        bool used = false;
        for (const auto& b : bindings) if (c.*b.member == k) used = true;
        if (!used && k != Help && k != Boost && k != Nearby && k != Roster && k != DevMenu && k != Video && k != TvView && k != TvConsent) return k;
    }
    return 0;
#endif
}
inline std::string KeyName(unsigned k) {
    if (k >= 0x70 && k <= 0x7B) return "F" + std::to_string(k - 0x6F);
    if (k == MouseMiddle) return "MMB";
    if (k == MouseX1) return "XBUTTON1";
    if (k == MouseX2) return "XBUTTON2";
    if (k == Wheel) return "WHEEL";
    if (k == Roster) return "Tab";
    if (k == PrevView) return "[";
    if (k == NextView) return "]";
    return k ? std::string(1, char(k)) : "unbound";
}
inline unsigned ParseKey(std::string s) {
    for (auto& c : s) c = char(std::toupper((unsigned char)c));
    if (s.size() == 1 && std::isalnum((unsigned char)s[0])) return unsigned(s[0]);
    if (s == "MMB" || s == "MIDDLE" || s == "MOUSE3") return MouseMiddle;
    if (s == "XBUTTON1" || s == "MOUSE4") return MouseX1;
    if (s == "XBUTTON2" || s == "MOUSE5") return MouseX2;
    if (s == "WHEEL") return Wheel;
    for (unsigned k = 0x70; k <= 0x7B; ++k) if (s == KeyName(k)) return k;
    return 0;
}
inline bool Reserved(unsigned k) {
    for (const auto& f : fixed) if (k == f.key) return true;
    return k == 'W' || k == 'A' || k == 'S' || k == 'D';
}
// The shared co-op keys (the same default in every co-op mod). A shared key belongs to its shared action: the primary binding and its "_alt" second key.
struct Shared { unsigned key; const char* action; };
inline const Shared shared[] = {
    {'T', "key_chat"}, {'V', "key_ping"}, {MouseMiddle, "key_ping"}, {'B', "key_voice"}, {MouseX1, "key_voice"}, {'H', "key_emote"},
};
inline bool OwnsShared(const Binding& b, const Shared& s) {
    std::string n = b.config, a = s.action;
    return n == a || n == a + "_alt";
}
inline bool IsAlt(const Binding& b) { std::string n = b.config; return n.size() > 4 && n.compare(n.size() - 4, 4, "_alt") == 0; }
inline bool HasLiveAlt(const Cfg& c, const Binding& b) {
    for (const auto& a : bindings) if (std::string(a.config) == std::string(b.config) + "_alt") return c.*a.member != 0;
    return false;
}
inline std::string Validate(const Cfg& c) {
#ifdef GUIDE_MUT_ALLOW_COLLISION
    return {};
#endif
    std::set<unsigned> used;
    for (const auto& b : bindings) {
        unsigned k = c.*b.member;
        if (!k && (IsAlt(b) || HasLiveAlt(c, b))) continue;      // a second key may stay unbound; so may a key whose action has its second key
        if (!k || Reserved(k) || !used.insert(k).second)
            return std::string(b.config) + ": key is reserved or already used";
#ifndef GUIDE_MUT_SHARED_FREE
        for (const auto& s : shared) if (k == s.key && !OwnsShared(b, s)) return std::string(b.config) + ": that key is shared for " + s.action;
#endif
    }
    return {};
}
// Defaults that moved (2026-11 key scheme): a cfg that holds the OLD default gets the new one; a key the player changed stays.
struct Moved { const char* config; unsigned from; };
inline const Moved moved[] = {{"key_ping", 'P'}, {"key_voice", 'N'}, {"key_emote", 'G'}, {"key_voice_channel", 'H'}, {"key_color", 'B'}};
inline unsigned MigrateDefault(const std::string& config, unsigned key) {
    (void)config;
#ifdef GUIDE_MUT_OVERWRITE_CUSTOM
    for (const auto& m : moved) if (config == m.config) for (const auto& b : bindings) if (config == b.config) return b.initial;      // mutant: a player's own key is replaced too
#endif
#ifndef GUIDE_MUT_KEEP_OLD
    for (const auto& m : moved) if (config == m.config && key == m.from) for (const auto& b : bindings) if (config == b.config) return b.initial;
#endif
    return key;
}
// After the cfg is read: a shared key that a player's own binding for another action holds stays with the player; the shared action is left on its
// second key (or unbound) and the change is said in `notes`.
inline void ResolveShared(Cfg& c, std::vector<std::string>* notes = nullptr) {
#ifdef GUIDE_MUT_NO_RESOLVE
    (void)c; (void)notes; return;
#endif
    for (const auto& s : shared) {
        const Binding* holder = nullptr;
        for (const auto& b : bindings) if (c.*b.member == s.key && !OwnsShared(b, s)) holder = &b;
        if (!holder) continue;
        for (const auto& o : bindings) if (OwnsShared(o, s) && c.*o.member == s.key) {
            c.*o.member = 0;
            if (notes) notes->push_back(std::string(o.label) + ": " + KeyName(s.key) + " is your key for " + holder->label + ", so " + o.label + " is unbound or on its second key");
        }
    }
}
// The one-time "moved keys" list for the launcher's What's new: every moved action the player has left on its default, plus the two fixed moves.
inline std::vector<std::string> MovedNotes(const Cfg& c) {
    std::vector<std::string> out;
    for (const auto& m : moved) for (const auto& b : bindings) if (std::string(b.config) == m.config && c.*b.member == b.initial) {
        std::string t = std::string(b.label) + ": " + KeyName(m.from) + " is now " + KeyName(b.initial);
        for (const auto& a : bindings) if (std::string(a.config) == std::string(m.config) + "_alt") t += " or " + KeyName(a.initial);
        out.push_back(t);
    }
    out.push_back("Player list: F5 is now hold Tab");
    out.push_back("Watch a player: V is now in the player list (hold Tab); [ and ] still cycle");
    return out;
}
inline void MigrateF1(Cfg& c) {
#ifndef GUIDE_MUT_NO_MIGRATE
    std::set<unsigned> used;
    for (const auto& b : bindings) used.insert(c.*b.member);
    for (const auto& b : bindings) if (c.*b.member == Help) {
        // F2 is free in the game and launcher. Keep other custom keys intact.
        for (unsigned k : {0x71u, 0x7Bu, unsigned('0'), unsigned('1'), unsigned('2'), unsigned('3'), unsigned('4'), unsigned('5'), unsigned('6'), unsigned('7'), unsigned('8'), unsigned('9'), unsigned('Q'), unsigned('U'), unsigned('I'), unsigned('O')})
            if (!used.count(k) && !Reserved(k)) { c.*b.member = k; used.insert(k); break; }
    }
#else
    (void)c;
#endif
}
inline void MigrateAll(Cfg& c, std::vector<std::string>* notes = nullptr) { MigrateF1(c); ResolveShared(c, notes); }
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
    out[0].push_back(KeyName(MarkerKey(c)) + "  Mark a problem: saves the last minute for a bug report (every player's game marks it)");
    out[0].push_back("GAME / MENU CONTROLS");
    out[0].push_back("Game uses your settings. Defaults: WASD move, mouse look / draw, click puzzles, Esc back.");
    out[0].push_back("Lists: arrows, Enter, Esc. Launcher: Tab / Shift+Tab and Enter.");
    out[0].push_back("Dev map: wheel zoom, right-drag pan; click a dot. Buttons teleport or act on a puzzle.");
    out[1] = {
        "PLAY TOGETHER",
        "Explore independently. See animated teammates, names, typing dots, pings and emotes.",
        "Co-op shares solved panels, traced puzzle lines, opened doors, panel power and cable lighting. Late arrivals recover shared progress.",
        "Players fade during close body overlap. Watch a partner's view to compare clues. Draw on surfaces or use the precise screen canvas.",
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
// Character-cell wrapping for the launcher's monospace guide. The game uses
// measured glyph advances instead. No elision: every word remains on screen.
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
