#pragma once
// Portable guide model: both renderers use these bindings, text and word wrapping.
#include "../proxy/config.h"
#include "padlayer.h"
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
inline bool IsTvBinding(const Binding& b) { return std::string(b.config).compare(0, 7, "key_tv_") == 0; }      // the experimental TV features (experimental_tv)
inline bool IsRadioBinding(const Binding& b) { return std::string(b.config) == "key_radio"; }      // the experimental handheld radio (experimental_radio)
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
// What a key does for the TV features: nothing at all while experimental_tv is off (the key then belongs to the game).
enum class TvKeyAction { None, View, Consent, Deploy, Seat };
inline TvKeyAction TvKey(const Cfg& c, unsigned vk) {
#ifdef GUIDE_MUT_TV_KEY_WHILE_OFF
    if (false)
#else
    if (!c.experimentalTv)
#endif
        return TvKeyAction::None;
    if (vk == TvView) return TvKeyAction::View;
    if (vk == TvConsent) return TvKeyAction::Consent;
    if (vk == c.keyTvDeploy) return TvKeyAction::Deploy;
    if (vk == c.keyTvSeat) return TvKeyAction::Seat;
    return TvKeyAction::None;
}
// What the radio key does: nothing at all while experimental_radio is off (the key then belongs to the game).
inline bool RadioKeyActs(const Cfg& c, unsigned vk) {
#ifdef GUIDE_MUT_RADIO_KEY_WHILE_OFF
    return vk == c.keyRadio;
#else
    return c.RadioOn() && vk == c.keyRadio;
#endif
}
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
// A key as the player reads it (KeyName is the spelling in witness_coop.cfg: MMB, XBUTTON1, WHEEL).
inline std::string DisplayKey(unsigned k) {
#ifdef GUIDE_MUT_RAW_KEY_NAMES
    return KeyName(k);
#endif
    if (k == MouseMiddle) return "Middle mouse";
    if (k == MouseX1) return "Mouse 4";
    if (k == MouseX2) return "Mouse 5";
    if (k == Wheel) return "Mouse wheel";
    return KeyName(k);
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
        std::string t = std::string(b.label) + ": " + DisplayKey(m.from) + " is now " + DisplayKey(b.initial);
        for (const auto& a : bindings) if (std::string(a.config) == std::string(m.config) + "_alt") t += " or " + DisplayKey(a.initial);
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
    // F1 is a hold, not a toggle: the card is open exactly while the key is down. `changed` (optional) says whether this event changed
    // that state: the first press and the release do, the autorepeat presses while it is held (30 a second) do not, so the caller
    // shows and hides its card on a change only and never redraws or repositions it on a repeat.
    bool Key(unsigned vk, bool down, bool* changed = nullptr) {
        if (vk != Help) return false;
        bool was = held.load();
#ifdef GUIDE_MUT_TOGGLE
        if (down) held = !was;
#else
        held = down;
#endif
        if (changed) {
#ifdef GUIDE_MUT_REARM
            *changed = down || was;      // twin: every press (autorepeat too) re-arms the card
#else
            *changed = was != held.load();
#endif
        }
        return true;
    }
    void Reset() { held = false; }
    bool Open() const { return held.load(); }
};
using Columns = std::array<std::vector<std::string>, 3>;
// Guide lines come in three kinds: a heading (no lower-case letter: "KEYS"), a table row ("key\tdescription": the key column, then what it
// does; both renderers align the descriptions) and a paragraph (anything else). Every renderer puts a blank line before a heading that is
// not the first line of its column, between a table row and a paragraph, and after a paragraph.
inline bool IsHeading(const std::string& l) { return !l.empty() && l.find('\t') == std::string::npos && l.find_first_of("abcdefghijklmnopqrstuvwxyz") == std::string::npos; }
// One row of the key table: "Hold: emote wheel" becomes "Hold H" / "Emote wheel".
inline std::string Row(const std::string& keys, std::string text) {
    std::string key = keys;
    if (text.rfind("Hold: ", 0) == 0) { key = "Hold " + keys; text = text.substr(6); if (!text.empty()) text[0] = char(std::toupper((unsigned char)text[0])); }
    return key + "\t" + text;
}
// The pad layer's lines (padlayer.h, the one table the launcher's Controls page lists too), with the push-to-talk stick click the player chose.
inline std::vector<std::string> PadLines(const Cfg& c) {
    std::vector<std::string> out{"CONTROLLER"};
    for (const auto& l : padlayer::kLines) {
        if (std::string(l.id) == "voice") { if (c.padVoice) out.push_back(std::string(c.padVoice == 1 ? "Hold left stick click" : "Hold right stick click") + "\tPush to talk"); continue; }
        out.push_back(std::string(l.buttons) + "\t" + l.text);
    }
    out.push_back(std::string("Voice on a controller: ") + (c.voiceMode == 3 ? (c.padVoice ? "push to talk on keys and on the stick click" : "open mic after controller input, push to talk after keys") : c.voiceMode == 1 ? "open mic" : c.voiceMode == 2 ? "push to talk" : "off") + ". Drawing stays on the keyboard.");
    return out;
}
// The keyboard and mouse keys: the player's bindings (a second key on the same row as the first), then the fixed keys.
inline std::vector<std::string> KeyLines(const Cfg& c) {
    std::vector<std::string> out{"KEYS"};
    for (const auto& b : bindings) {
#ifdef GUIDE_MUT_MISSING_KEY
        if (b.member == &Cfg::keyChat) continue;
#endif
#ifndef GUIDE_MUT_TV_IN_GUIDE
        if (IsTvBinding(b) && !c.experimentalTv) continue;      // experimental TV features are not in the guide while they are off
#endif
#ifndef GUIDE_MUT_RADIO_IN_GUIDE
        if (IsRadioBinding(b) && !c.RadioOn()) continue;      // nor the handheld radio
#endif
        if (IsAlt(b)) continue;      // listed with its first key
        std::string keys = c.*b.member ? DisplayKey(c.*b.member) : "";
        for (const auto& a : bindings) if (std::string(a.config) == std::string(b.config) + "_alt" && c.*a.member) keys += (keys.empty() ? "" : " / ") + DisplayKey(c.*a.member);
        if (keys.empty()) keys = "unbound";
        out.push_back(Row(keys, b.text));
    }
    out.push_back(Row(KeyName(Roster), "Hold: player panel (watch, mute, leave)"));
    out.push_back(Row(KeyName(PrevView) + " / " + KeyName(NextView), "Watch the previous / next player"));
    out.push_back(Row(KeyName(Video), "Partner picture: small, large, off"));
    out.push_back(Row(KeyName(Boost), "Faster mouse while tracing (on / off)"));
#ifndef GUIDE_MUT_TV_IN_GUIDE
    if (c.experimentalTv)
#endif
    { out.push_back(Row(KeyName(TvView), "Show the back TV of the nearest player (on / off)")); out.push_back(Row(KeyName(TvConsent), "TV camera: share your view (on / off)")); }
    if (unsigned m = MarkerKey(c)) out.push_back(Row(KeyName(m), "Mark a problem for a bug report"));
    out.push_back(Row(KeyName(Help), "Hold: this guide"));
#ifdef GUIDE_MUT_DEV_IN_GUIDE
    if (true)
#else
    if (c.devMenu)      // developer keys only with dev_menu=1 (off in a normal install)
#endif
    { out.push_back("DEVELOPER"); out.push_back(Row(KeyName(DevMenu), "Developer menu (Tab: page)")); out.push_back(Row(KeyName(Nearby), "Nearby puzzles"));
      out.push_back("Dev map: wheel zooms, right-drag pans, click a dot to select it."); }
    return out;
}
// `pad`: the last input was a pad (K7): the controller's lines come first.
inline Columns Content(const Cfg& c, bool pad = false) {
    Columns out;
#ifdef GUIDE_MUT_NO_PAD
    pad = false;
#endif
    auto add = [](std::vector<std::string>& to, const std::vector<std::string>& from) { to.insert(to.end(), from.begin(), from.end()); };
    add(out[0], pad ? PadLines(c) : KeyLines(c));
    out[1] = {
        "PLAYING TOGETHER",
        "Everyone plays their own game of the same island. Solved panels, lines as they are traced, doors, power, cables and lasers are shared, and a player who joins late catches up. Bridges and moving platforms are not shared yet.",
        c.RadioOn() ? "Voice: players nearby sound where they stand, far away as if over a radio. Channels: proximity, radio, open line, or one player."
                    : "Voice: players nearby sound where they stand, far away as if over a radio. Channels: proximity, open line, or one player.",
        "Watch a player's view to compare clues. Draw on the world, or use the canvas for precise lines on a panel.",
        "Race: nothing is shared, but you still see, chat and talk. Everyone starts together; first to the goal wins.",
        "HOSTING AND JOINING",
        "Over the internet: Host a game, Through Steam, then invite friends in Steam. No ports to open.",
        "Same network or Tailscale: Host a game, Direct. Friends find it under Join a friend, or paste the invite you copy for them.",
        "The host picks co-op or race and the save. Everyone ticks Ready, the host starts, and after the game everyone is back in the lobby.",
    };
    add(out[2], pad ? KeyLines(c) : PadLines(c));
    add(out[2], {
        "TROUBLESHOOTING",
        "Can't connect? Use a Steam lobby, or Tailscale for Direct. In the lobby, Friends can join by says what to fix.",
        "Can't join? Everyone needs the same mod version; the launcher offers updates.",
        "Progress missing? Give a freshly loaded world a few seconds, and check everyone is in co-op, not a race.",
        "No voice? Check the voice mode, microphone, volumes, mutes and your channel.",
        "Something wrong? Press " + KeyName(MarkerKey(c) ? MarkerKey(c) : 0x71) + " right away, then Send diagnostics on the launcher's About tab.",
        "Settings changed in the launcher apply the next time the game starts.",
    });
    return out;
}
// Character-cell wrapping for the launcher's monospace guide. The game uses measured glyph advances instead. No elision: every word
// remains on screen. Table rows become a padded key column; their wrapped descriptions stay in the description column.
inline std::vector<std::string> WrapWords(const std::string& text, size_t width) {
    std::vector<std::string> out; std::string line; size_t at = 0;
    while (at <= text.size()) {
        size_t end = text.find(' ', at); if (end == std::string::npos) end = text.size();
        std::string word = text.substr(at, end - at);
        while (word.size() > width) { if (!line.empty()) { out.push_back(line); line.clear(); } out.push_back(word.substr(0, width)); word.erase(0, width); }
        if (!line.empty() && line.size() + 1 + word.size() > width) { out.push_back(line); line.clear(); }
        if (!word.empty()) { if (!line.empty()) line += ' '; line += word; }
        at = end + 1;
    }
    out.push_back(line); return out;
}
inline Columns Wrap(const Columns& input, size_t width = 48) {
    Columns out;
    for (size_t c = 0; c < input.size(); ++c) {
        size_t keyW = 0;
        for (const auto& l : input[c]) { auto t = l.find('\t'); if (t != std::string::npos) keyW = std::max(keyW, t + 2); }
        keyW = std::min(keyW, width / 2);
        bool afterParagraph = false;
        for (const auto& l : input[c]) {
            if (IsHeading(l)) { if (!out[c].empty() && !afterParagraph) out[c].push_back(""); out[c].push_back(l); afterParagraph = false; continue; }
            auto t = l.find('\t');
            if (t == std::string::npos) { if (!out[c].empty() && !out[c].back().empty() && !IsHeading(out[c].back())) out[c].push_back(""); for (auto& w : WrapWords(l, width)) out[c].push_back(w); out[c].push_back(""); afterParagraph = true; continue; }
            auto keys = WrapWords(l.substr(0, t), keyW - 2), desc = WrapWords(l.substr(t + 1), width - keyW);
            for (size_t i = 0; i < std::max(keys.size(), desc.size()); ++i) {
                std::string k = i < keys.size() ? keys[i] : "";
                out[c].push_back(k + std::string(keyW - k.size(), ' ') + (i < desc.size() ? desc[i] : ""));
            }
            afterParagraph = false;
        }
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
