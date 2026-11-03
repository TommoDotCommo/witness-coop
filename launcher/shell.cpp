// See shell.h.
#include "shell.h"
#include <cmath>
#include <cstdio>
#include "../common/guide.h"
#include "../proxy/core/appearance.h"
#include "core.h"

namespace ls {
const char* TabLabel(int tab, bool inLobby) {
    static const char* names[] = {"Play", "Saves", "Settings", "About"};
    if (tab == kPlay && inLobby) return "Lobby";
    return tab >= 0 && tab < kTabCount ? names[tab] : "";
}
const char* ChoiceKey(Choice c) { return c == kJoin ? "join" : c == kSolo ? "solo" : "host"; }
Choice ParseChoice(const std::string& s) { return s == "join" ? kJoin : s == "solo" ? kSolo : kHost; }
std::string PrimaryLabel(Choice c, const std::string& joinName) {
    if (c == kJoin) return joinName.empty() ? "Join" : "Join " + joinName;
    if (c == kSolo) return "Play solo";
    return "Open lobby";
}

const char* SectionName(int s) {
    static const char* n[] = {"Co-op rules", "Players and interface", "Voice and sound", "Controls", "Display", "Network", "Launcher"};
    return s >= 0 && s < kSectionCount ? n[s] : "";
}

static Row R(const std::string& key, const std::string& label, Section s, RowKind k) { Row r; r.key = key; r.label = label; r.section = s; r.kind = k; return r; }
static Row Choice_(const std::string& key, const std::string& label, Section s, std::vector<std::string> ch, const std::string& def = "") { Row r = R(key, label, s, kChoice); r.choices = std::move(ch); r.def = def; return r; }
static Row Toggle(const std::string& key, const std::string& label, Section s, const std::string& def = "0", const std::string& hint = "") { Row r = R(key, label, s, kToggle); r.def = def; r.hint = hint; return r; }
static Row Slider(const std::string& key, const std::string& label, Section s, double mn, double mx, double step, double coarse, const std::string& unit, const std::string& def, const std::string& zero = "") {
    Row r = R(key, label, s, kSlider); r.min = mn; r.max = mx; r.step = step; r.coarse = coarse; r.unit = unit; r.def = def; r.zeroText = zero; return r;
}
static std::vector<Row> Build() {
    std::vector<Row> v;
    const std::vector<std::string> q = {"Low", "Medium", "High"};
    // Co-op rules (host): who can see the lobby and the player limit come with the Join list; what to play is chosen in the lobby
    { Row n = R("note:rules", "Who can see your lobby and the player limit come with the Join list. What to play (co-op or race, new game, parts done) is chosen in the lobby by the host.", kRules, kNote); v.push_back(n); }
    // Players and interface
    { Row r = R("cfg:name", "Your name", kPlayers, kText); r.hint = "shown to the others"; v.push_back(r); }
    {
        const std::vector<std::vector<std::string>> palettes = {
            {"Original", "Tan", "Deep", "Light"}, {"Original", "Black", "Auburn", "Silver"}, {"Original", "Blue", "Green", "Amber"}, {"Original", "Rust", "Blue", "Olive"},
            {"Original", "Blue", "Red", "Green"}, {"Original", "Charcoal", "Blue", "Olive"}, {"Original", "Black", "Tan", "Olive"}, {"Original", "Charcoal", "Tan", "Olive"}};
        for (size_t i = 0; i < witness::kAppearanceFeatures; ++i) v.push_back(Choice_(std::string("cfg:appearance_") + witness::kAppearanceNames[i], std::string("Your ") + witness::kAppearanceNames[i] + " colour", kPlayers, palettes[i], "0"));
    }
    v.push_back(Toggle("cfg:see_through", "See through players in the way of a puzzle", kPlayers, "1"));
    v.push_back(Toggle("cfg:emote_third_person", "Third-person view when emoting", kPlayers, "1"));
    v.push_back(Toggle("cfg:dev_menu", "Developer menu (F8)", kPlayers, "0"));
    // Voice and sound
    v.push_back(Choice_("cfg:voice_mode", "Voice chat", kVoice, {"Off", "Open mic (voice gate)", "Push to talk"}, "ptt")); v.back().values = {"off", "open", "ptt"};
    { Row r = R("cfg:key_voice", "Push-to-talk key", kVoice, kKey); r.def = guide::KeyName('B'); v.push_back(r); }
    v.push_back(Choice_("cfg:voice_channel", "Who hears you", kVoice, {"Players nearby", "Radio", "Everyone"}, "proximity")); v.back().values = {"proximity", "radio", "global"};
    v.push_back(Slider("cfg:voice_volume", "Voice volume", kVoice, 0, 100, 1, 5, "%", "100"));
    v.push_back(Slider("cfg:mic_volume", "Microphone volume", kVoice, 0, 100, 1, 5, "%", "100"));
    v.push_back(Slider("cfg:voice_range", "Hearing range", kVoice, 1, 200, 1, 5, "m", "25"));
    v.back().hint = "beyond it the voice turns into radio";
    v.push_back(Slider("cfg:voice_radio_range", "Radio range", kVoice, 0, 1000, 10, 50, "m", "0", "unlimited"));
    v.push_back(Toggle("cfg:voice_radio", "Radio sound when far away", kVoice, "1"));
    v.push_back(Toggle("cfg:radio_enabled", "Handheld radio", kVoice, "1", "hold the radio key to transmit"));
    v.push_back(Slider("cfg:game_volume", "Game volume", kVoice, 0, 100, 1, 5, "%", "100"));
#ifdef WL_MUT_LONG_LABELS
    v.push_back(Toggle("cfg:launcher_no_sound", "Game sound off (starts the game with -no_sound, the game's own sound switched off entirely)", kVoice, "0", "starts the game with -no_sound"));      // (mutant: a label longer than its box)
#else
    v.push_back(Toggle("cfg:launcher_no_sound", "Game sound off", kVoice, "0", "starts the game with -no_sound"));
#endif
    // Controls: every key of the one binding table (push to talk is in Voice and sound), then the game's own mouse options (inside a save)
    for (const auto& b : guide::bindings) {
        if (std::string(b.config) == "key_voice") continue;
        Row r = R(std::string("cfg:") + b.config, b.label, kControls, kKey); r.def = guide::KeyName(b.initial); v.push_back(r);
    }
    for (const auto& d : wl::SaveOptionDefs()) {
        std::string n = d.name; bool display = n == "user_brightness" || n == "user_fov_vertical" || n == "user_reticle" || n == "user_vignetting";
        Section s = display ? kDisplay : kControls; Row r;
        if (d.kind == wl::OptKind::Bool) r = Toggle("opt:" + n, d.label, s, "0");
        else if (n == "user_reticle") r = Choice_("opt:" + n, d.label, s, {"0", "1", "2", "3"});
        else {
            std::string unit = n == "user_fov_vertical" ? "degrees" : n == "user_brightness" ? "x" : "";
            r = Slider("opt:" + n, n == "user_fov_vertical" ? "Field of view (vertical)" : d.label, s, d.min, d.max, d.step, n == "user_fov_vertical" ? 5 : d.step * 2, unit, "");
        }
        r.inSave = true; v.push_back(r);
    }
    // Display
    v.push_back(Toggle("vid:fullscreen", "Full screen", kDisplay));
    v.push_back(Choice_("vid:resolution", "Resolution", kDisplay, {"Display resolution", "1080p", "720p", "Dynamic resolution", "Custom size"}));
    v.push_back(Choice_("vid:customSize", "Custom size", kDisplay, {"1280 x 720", "1280 x 800", "1366 x 768", "1600 x 900", "1920 x 1080", "1920 x 1200", "2560 x 1440", "3840 x 2160"}));
    v.back().hint = "with Resolution: Custom size";
    v.push_back(Slider("cfg:cap_fps", "Frame limit", kDisplay, 0, 240, 1, 10, "fps", "0", "none"));
    v.push_back(Toggle("vid:vsync", "Wait for vertical sync", kDisplay));
    v.push_back(Choice_("vid:renderProfile", "Render quality profile", kDisplay, q));
    v.push_back(Choice_("vid:multisampling", "Anti-aliasing", kDisplay, {"Off", "2x MSAA", "4x MSAA", "8x MSAA", "16x MSAA"}));
    for (auto p : {std::make_pair("vid:shading", "Shading quality"), std::make_pair("vid:reflection", "Reflection quality"), std::make_pair("vid:texture", "Texture quality"),
                   std::make_pair("vid:textureDetail", "Texture detail"), std::make_pair("vid:geometry", "Geometry detail")}) v.push_back(Choice_(p.first, p.second, kDisplay, q));
    v.push_back(Toggle("vid:highdpi", "High DPI", kDisplay));
    v.push_back(Toggle("cfg:run_unfocused", "Keep running in the background", kDisplay, "0", "when the window is not active (two windows on one PC)"));
    // Network
    { Row r = R("cfg:lobby_port", "Lobby port", kNetwork, kPort); r.def = "47100"; v.push_back(r); }
    { Row r = R("cfg:listen", "Game port", kNetwork, kPort); r.def = "47001"; v.push_back(r); }
    { Row r = R("cfg:lobby_advertise", "Friends reach this PC at", kNetwork, kText); r.hint = "blank: automatic"; v.push_back(r); }
    { Row r = R("cfgmulti:peer", "Direct play without a lobby: number,address,port per line", kNetwork, kPeers); r.hint = "advanced; the lobby fills this in itself"; v.push_back(r); }
    // Launcher
    v.push_back(Toggle("cfg:show_launcher", "Show the launcher at start", kLauncher, "1", "off: Play in Steam starts the game directly; hold Shift while it starts to see the launcher"));
    v.push_back(Toggle("upd:auto", "Check for updates when the launcher starts", kLauncher, "1", "once a day at most; nothing about you is sent"));
    v.push_back(Toggle("upd:pre", "Offer pre-releases", kLauncher, "0"));
    return v;
}
const std::vector<Row>& Rows() {
    static const std::vector<Row> rows = [] {
        std::vector<Row> b = Build(), out;      // in section order, the order inside a section as built
        for (int s = 0; s < kSectionCount; ++s) for (const auto& r : b) if (r.section == s) out.push_back(r);
        return out;
    }();
    return rows;
}
std::vector<const Row*> RowsOf(Section s) { std::vector<const Row*> o; for (const auto& r : Rows()) if (r.section == s) o.push_back(&r); return o; }
const Row* FindRow(const std::string& key) { for (const auto& r : Rows()) if (r.key == key) return &r; return nullptr; }
bool IsTyped(const Row& r) { return r.kind == kText || r.kind == kPort || r.kind == kPeers; }

int SliderSteps(const Row& r) { return r.step > 0 ? (int)std::lround((r.max - r.min) / r.step) : 0; }
int SliderPos(const Row& r, double value) {
    if (r.step <= 0) return 0; int p = (int)std::lround((value - r.min) / r.step); int n = SliderSteps(r);
    return p < 0 ? 0 : p > n ? n : p;
}
double SliderValue(const Row& r, int pos) { int n = SliderSteps(r); if (pos < 0) pos = 0; if (pos > n) pos = n; return r.min + pos * r.step; }
std::string ValueText(const Row& r, double value) {
    if (value == 0 && !r.zeroText.empty()) return r.zeroText;
    char b[40]; double rounded = std::round(value);
    if (std::fabs(value - rounded) < 1e-6) snprintf(b, sizeof b, "%.0f", value); else snprintf(b, sizeof b, "%.2f", value);
    std::string t = b; while (t.find('.') != std::string::npos && (t.back() == '0' || t.back() == '.')) { bool dot = t.back() == '.'; t.pop_back(); if (dot) break; }
    if (r.unit.empty()) return t;
    if (r.unit == "x") return t + "x";
    return t + " " + r.unit;
}
std::vector<std::pair<std::string, std::string>> SectionDefaults(Section s) {
    std::vector<std::pair<std::string, std::string>> o;
    for (const auto* r : RowsOf(s)) {
        if (r->key.rfind("cfg:", 0) != 0 || r->kind == kText || r->kind == kNote) continue;      // (a name or an address is the player's, not a setting to reset)
        o.push_back({r->key.substr(4), r->def});
    }
    return o;
}

unsigned KeyFromVk(unsigned vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return vk;
    if (vk >= 0x70 && vk <= 0x7B) return vk;                                  // F1..F12 (F1 refused by TryBind)
    if (vk == guide::MouseMiddle || vk == guide::MouseX1 || vk == guide::MouseX2 || vk == guide::Wheel) return vk;
    return 0;
}
std::string TryBind(const Cfg& cur, const std::string& config, unsigned vk, Cfg* out) {
    unsigned key = KeyFromVk(vk);
    if (!key) return "That key cannot be bound: use a letter, a digit, a function key or a mouse button.";
#ifndef SHELL_MUT_ALLOW_F1
    if (key == guide::Help) return "F1 is the quick guide: choose another key.";
#endif
    const guide::Binding* me = nullptr; for (const auto& b : guide::bindings) if (config == b.config) me = &b;
    if (!me) return "Unknown key setting " + config + ".";
    for (const auto& b : guide::bindings) if (&b != me && cur.*b.member == key) return guide::KeyName(key) + " is already used for " + b.label + ".";
    Cfg c = cur; c.*me->member = key;
    std::string err = guide::Validate(c);
    if (!err.empty()) {
        for (const auto& s : guide::shared) if (s.key == key && !guide::OwnsShared(*me, s)) { const char* lab = s.action; for (const auto& b : guide::bindings) if (std::string(b.config) == s.action) lab = b.label; return guide::KeyName(key) + " is kept for " + lab + " in every co-op mod."; }
        if (guide::Reserved(key)) return guide::KeyName(key) + " is taken by the game or a fixed co-op key.";
        return err;
    }
    *out = c; return "";
}

const char* PadActionName(PadAction a) {
    static const char* n[] = {"up", "down", "left", "right", "confirm", "back", "prevtab", "nexttab", "primary", "x", "y"};
    return n[a];
}
std::vector<PadAction> PadNav::Update(const PadState& s, uint32_t now) {
    std::vector<PadAction> out;
    if (!s.connected) { held_ = 0; dir_ = -1; return out; }
    uint16_t b = s.buttons;
    // the stick as four directions (a dead zone of about half its travel, so a resting stick never moves the focus)
    const int dz = 16000;
    if (s.ly > dz) b |= kDUp; if (s.ly < -dz) b |= kDDown; if (s.lx < -dz) b |= kDLeft; if (s.lx > dz) b |= kDRight;
#ifdef SHELL_MUT_PAD_LEVEL
    uint16_t pressed = b;      // (mutant) a held button acts again every frame
#else
    uint16_t pressed = b & ~held_;
#endif
    held_ = b;
    const std::pair<uint16_t, PadAction> map[] = {{kA, kConfirm}, {kB, kBack}, {kLB, kPrevTab}, {kRB, kNextTab}, {kStart, kPrimary}, {kX, kActionX}, {kY, kActionY}};
    for (const auto& m : map) if (pressed & m.first) out.push_back(m.second);
    // directions: one at a time (the first one held), repeating while held
    const std::pair<uint16_t, PadAction> dirs[] = {{kDUp, kUp}, {kDDown, kDown}, {kDLeft, kLeft}, {kDRight, kRight}};
    int now_dir = -1; for (int i = 0; i < 4; ++i) if (b & dirs[i].first) { if (i == dir_) { now_dir = i; break; } if (now_dir < 0) now_dir = i; }
    if (now_dir < 0) dir_ = -1;
    else if (now_dir != dir_) { dir_ = now_dir; dirSince_ = now; lastRepeat_ = now; out.push_back(dirs[now_dir].second); }
    else if (now - dirSince_ >= 400 && now - lastRepeat_ >= 120) { lastRepeat_ = now; out.push_back(dirs[now_dir].second); }
    return out;
}

PageActions ActionsFor(Tab t, bool inLobby, bool isHost) {
    switch (t) {
    case kPlay: if (!inLobby) return {"the primary button", "", "Quick guide"};
                return {isHost ? "Start game" : "Ready", isHost ? "Copy invite" : "", "Leave"};
    case kSaves: return {"New game", "Back up", "Refresh"};
    case kSettings: return {"", "Reset section", ""};
    case kAbout: return {"Check for updates", "Send diagnostics", "Quick guide"};
    default: return {};
    }
}
std::string FooterText(const PageActions& a, bool pad) {
    std::string t = pad ? "A Select   B Back   LB/RB Tabs" : "Enter Select   Esc Back   Ctrl+Tab Tabs   Hold F1 Quick guide";
    if (!a.primary.empty()) t += std::string(pad ? "   Start " : "   Ctrl+Enter ") + a.primary;
    if (!a.x.empty()) t += std::string(pad ? "   X " : "   Ctrl+X ") + a.x;
    if (!a.y.empty()) t += std::string(pad ? "   Y " : "   Ctrl+Y ") + a.y;
    return t;
}
}  // namespace ls
