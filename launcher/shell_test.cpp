// T0 test of the launcher's shape (shell.h) and of the Steam start decision (common/handover.h), no window. Mutants (Makefile SHELL_MUTANTS) must fail it:
// SHELL_MUT_ALLOW_F1 (F1 can be bound), SHELL_MUT_PAD_LEVEL (a held pad button acts every frame), HANDOVER_MUT_IGNORE_SESSION (the launcher's own game hands over again: a loop),
// SHELL_MUT_TYPED_VOLUME (a volume is a typed number again), SHELL_MUT_PLAYER_NUMBER (the player number field is back).
#include <cstdio>
#include <set>
#include <string>
#include "shell.h"
#include "../common/handover.h"
#include "../common/guide.h"

static int g_fail;
static void Check(bool ok, const char* what, const std::string& detail = "") { printf("%s %s%s%s\n", ok ? "ok  " : "FAIL", what, detail.empty() ? "" : "  | ", detail.c_str()); if (!ok) ++g_fail; }

#ifdef SHELL_MUT_TYPED_VOLUME
static bool Typed(const ls::Row& r) { return ls::IsTyped(r) || r.key == "cfg:voice_volume"; }
#else
static bool Typed(const ls::Row& r) { return ls::IsTyped(r); }
#endif

int main() {
    // A1, A3: four tabs in this order; Play reads Lobby in a lobby
    std::string tabs; for (int t = 0; t < ls::kTabCount; ++t) tabs += std::string(t ? "," : "") + ls::TabLabel(t, false);
    Check(tabs == "Play,Saves,Settings,About", "four tabs: Play, Saves, Settings, About", tabs);
    Check(std::string(ls::TabLabel(ls::kPlay, true)) == "Lobby" && std::string(ls::TabLabel(ls::kSaves, true)) == "Saves", "hosting or joining turns Play into Lobby, the others stay");
    // A2: the primary button says what it does; the choice round-trips through the cfg
    Check(ls::PrimaryLabel(ls::kHost, "") == "Open lobby" && ls::PrimaryLabel(ls::kJoin, "Fox") == "Join Fox" && ls::PrimaryLabel(ls::kSolo, "") == "Play solo", "primary button: Open lobby / Join <name> / Play solo");
    bool rt = true; for (auto c : {ls::kHost, ls::kJoin, ls::kSolo}) rt = rt && ls::ParseChoice(ls::ChoiceKey(c)) == c; Check(rt && ls::ParseChoice("") == ls::kHost, "last choice remembered (cfg round trip)");

    // E: sections in the standard order
    std::string secs; for (int s = 0; s < ls::kSectionCount; ++s) secs += std::string(s ? "|" : "") + ls::SectionName(s);
    Check(secs == "Co-op rules|Players and interface|Voice and sound|Controls|Display|Network|Launcher", "settings sections in the standard order", secs);
    // every field of the old nine pages is a row in exactly one section (the player number is gone: the lobby assigns ids)
    const char* old[] = {"vid:renderProfile", "vid:fullscreen", "vid:vsync", "vid:highdpi", "vid:resolution", "vid:multisampling", "vid:shading", "vid:reflection", "vid:texture", "vid:textureDetail", "vid:geometry",
        "cfg:cap_fps", "cfg:launcher_no_sound", "cfg:game_volume", "cfg:voice_mode", "cfg:key_voice", "cfg:voice_channel", "cfg:voice_volume", "cfg:mic_volume", "cfg:voice_range", "cfg:voice_radio_range", "cfg:voice_radio",
        "cfg:radio_enabled", "cfg:name", "cfg:listen", "cfgmulti:peer", "cfg:dev_menu", "cfg:see_through", "cfg:run_unfocused", "cfg:lobby_port", "cfg:lobby_advertise", "upd:auto", "upd:pre",
        "opt:mouse_speed_3d", "opt:mouse_speed_2d", "opt:invert_y_axis", "opt:invert_x_axis", "opt:mouse_smoothing", "opt:user_brightness", "opt:user_fov_vertical", "opt:user_reticle", "opt:user_vignetting", "opt:look_magnetism_enabled",
        "cfg:appearance_skin"};
    std::string missing; for (const char* k : old) if (!ls::FindRow(k)) missing += std::string(" ") + k;
    for (const auto& b : guide::bindings) if (!ls::FindRow(std::string("cfg:") + b.config)) missing += std::string(" cfg:") + b.config;
    Check(missing.empty(), "every old field and every key of the binding table has a row", missing);
    std::set<std::string> seen; std::string dup; for (const auto& r : ls::Rows()) if (!seen.insert(r.key).second) dup += " " + r.key; Check(dup.empty(), "no setting appears twice", dup);
#ifdef SHELL_MUT_PLAYER_NUMBER
    bool idRow = true;
#else
    bool idRow = ls::FindRow("cfg:id") != nullptr;
#endif
    Check(!idRow, "the player number field is removed");
    // the standard's rows sit in their sections
    auto in = [](const char* k, ls::Section s) { const ls::Row* r = ls::FindRow(k); return r && r->section == s; };
    Check(in("cfg:voice_mode", ls::kVoice) && in("cfg:key_voice", ls::kVoice) && in("cfg:voice_range", ls::kVoice) && in("cfg:key_chat", ls::kControls) && in("vid:resolution", ls::kDisplay) && in("cfg:cap_fps", ls::kDisplay)
          && in("cfg:lobby_port", ls::kNetwork) && in("cfg:show_launcher", ls::kLauncher) && in("upd:auto", ls::kLauncher) && in("cfg:name", ls::kPlayers), "rows are in the standard's sections");
    for (int s = 0; s < ls::kSectionCount; ++s) Check(!ls::RowsOf((ls::Section)s).empty(), "section has rows", ls::SectionName(s));
    // E3: sliders with units for volumes, ranges, FOV, brightness and the frame limit; no typed numbers except the port
    std::string notSlider; for (const char* k : {"cfg:voice_volume", "cfg:mic_volume", "cfg:game_volume", "cfg:voice_range", "cfg:voice_radio_range", "cfg:cap_fps", "opt:user_fov_vertical", "opt:user_brightness", "opt:mouse_speed_3d"}) {
        const ls::Row* r = ls::FindRow(k); if (!r || r->kind != ls::kSlider || (r->unit.empty() && std::string(k).rfind("opt:mouse", 0) != 0) || Typed(*r)) notSlider += std::string(" ") + k; }
    Check(notSlider.empty(), "volumes, ranges, FOV, brightness and frame limit are sliders with units, not typed", notSlider);
    std::string typed; for (const auto& r : ls::Rows()) if (Typed(r) && r.kind != ls::kPort && r.key != "cfg:name" && r.key != "cfg:lobby_advertise" && r.key != "cfgmulti:peer") typed += " " + r.key;
    Check(typed.empty(), "the only typed fields are the ports, the name, the address and the direct-play list", typed);
    // slider values and texts
    const ls::Row* vol = ls::FindRow("cfg:voice_volume"); const ls::Row* cap = ls::FindRow("cfg:cap_fps"); const ls::Row* br = ls::FindRow("opt:user_brightness"); const ls::Row* rr = ls::FindRow("cfg:voice_radio_range");
    Check(ls::ValueText(*vol, 75) == "75 %" && ls::ValueText(*cap, 0) == "none" && ls::ValueText(*cap, 60) == "60 fps" && ls::ValueText(*rr, 0) == "unlimited" && ls::ValueText(*br, 1.25) == "1.25x",
          "slider texts carry units", ls::ValueText(*vol, 75) + "/" + ls::ValueText(*cap, 0) + "/" + ls::ValueText(*br, 1.25));
    Check(ls::SliderValue(*br, ls::SliderPos(*br, 1.35)) > 1.349 && ls::SliderValue(*br, ls::SliderPos(*br, 1.35)) < 1.351 && ls::SliderPos(*vol, 500) == ls::SliderSteps(*vol) && ls::SliderPos(*vol, -3) == 0, "slider positions round-trip and clamp");
    // E1: Reset section gives the defaults of that section, never the player's name
    auto d = ls::SectionDefaults(ls::kVoice); bool hasVol = false, hasName = false; for (auto& p : d) { if (p.first == "voice_volume" && p.second == "100") hasVol = true; if (p.first == "name") hasName = true; }
    Check(hasVol && !hasName && ls::SectionDefaults(ls::kPlayers).size() >= 2, "Reset section: the section's defaults, not the name");
    // E2: save-internal options say so
    Check(ls::FindRow("opt:user_fov_vertical")->inSave && !ls::FindRow("cfg:voice_volume")->inSave, "options inside a save are marked (save to change)");

    // press-a-key capture (E3, H4): F1 refused, duplicates refused, a free key taken
    Cfg c; Cfg out = c; std::string e;
    e = ls::TryBind(c, "key_chat", 0x70, &out); Check(e.find("F1 is the quick guide") == 0 && out.keyChat == c.keyChat, "F1 is refused, in plain words", e);
    e = ls::TryBind(c, "key_chat", 'R', &out); Check(!e.empty() && e.find("Radio") != std::string::npos, "a key in use is refused and named", e);
    e = ls::TryBind(c, "key_draw", 'V', &out); Check(!e.empty(), "a shared co-op key is refused for another action", e);
    out = c; e = ls::TryBind(c, "key_chat", 'U', &out); Check(e.empty() && out.keyChat == 'U' && out.keyPing == c.keyPing, "a free key is taken", e);
    e = ls::TryBind(c, "key_chat", 0x1B, &out); Check(!e.empty(), "Esc is not a key a binding can take (it cancels the capture)");

    // pad (K3): recorded XInput states
    {
        ls::PadNav nav; std::vector<std::string> got; uint32_t t = 0;
        auto step = [&](uint16_t buttons, int16_t lx, int16_t ly, uint32_t dt) { t += dt; ls::PadState s; s.connected = true; s.buttons = buttons; s.lx = lx; s.ly = ly; for (auto a : nav.Update(s, t)) got.push_back(ls::PadActionName(a)); };
        step(0, 0, 0, 16); step(ls::kA, 0, 0, 16); step(ls::kA, 0, 0, 16); step(ls::kA, 0, 0, 16); step(0, 0, 0, 16);      // A held for three frames: one confirm
        step(ls::kRB, 0, 0, 16); step(0, 0, 0, 16); step(ls::kLB, 0, 0, 16); step(0, 0, 0, 16); step(ls::kStart, 0, 0, 16); step(ls::kB, 0, 0, 16); step(ls::kX | ls::kB, 0, 0, 16); step(ls::kY, 0, 0, 16); step(0, 0, 0, 16);
        step(0, 3000, -9000, 16);                      // a resting stick: nothing
        step(0, 0, -30000, 16); step(0, 0, -30000, 100); step(0, 0, -30000, 100);      // stick down: one move, no repeat before 400 ms
        step(0, 0, -30000, 250); step(0, 0, -30000, 120); step(0, 0, 0, 16);           // held past 400 ms: repeats every 120 ms
        step(ls::kDRight, 0, 0, 16); step(0, 0, 0, 16);
        std::string s; for (auto& g : got) s += g + " ";
        Check(s == "confirm nexttab prevtab primary back x y down down down right ", "pad: presses act once, the stick has a dead zone, a held direction repeats", s);
        ls::PadState off; got.clear(); nav.Update(off, t + 16); Check(got.empty(), "pad: a disconnected pad does nothing");
    }
    // footer hints (K7): pad buttons after pad input, keys after keyboard input
    ls::PageActions la = ls::ActionsFor(ls::kPlay, true, true);
    Check(la.primary == "Start game" && la.y == "Leave" && ls::FooterText(la, true).find("Start Start game") != std::string::npos && ls::FooterText(la, true).find("Y Leave") != std::string::npos
          && ls::FooterText(la, false).find("Esc Back") != std::string::npos && ls::FooterText(la, false).find("   Y Leave") == std::string::npos && ls::FooterText(la, false).find("Ctrl+Y Leave") != std::string::npos, "footer: X/Y context actions shown as pad buttons or keys");

    // the Steam start decision (K1, A5)
    using handover::Inputs; using handover::Decide;
    Inputs steam; steam.launcherExists = true; steam.steamLaunch = true;
    Check(Decide(steam) == handover::kToLauncher, "Play in Steam (no launcher session) hands over to the launcher");
    Inputs own = steam; own.launcherSession = true; Check(Decide(own) == handover::kStay, "the launcher's own game stays (no loop), even from Steam");
    Inputs ownJoin = own; ownJoin.connectLobby = true; Check(Decide(ownJoin) == handover::kStay, "the launcher's own game with +connect_lobby stays");
    Inputs tool; tool.launcherExists = true; Check(Decide(tool) == handover::kStay, "a game started without Steam (test tools, the exe) stays");
    Inputs invite = tool; invite.connectLobby = true; Check(Decide(invite) == handover::kToLauncherJoin, "a Steam invite goes to the launcher to join");
    Inputs skip = steam; skip.showLauncher = false; Check(Decide(skip) == handover::kStay, "Show launcher at start off: Play starts the game");
    skip.shiftHeld = true; Check(Decide(skip) == handover::kToLauncher, "... and holding Shift brings the launcher back");
    Inputs none = steam; none.launcherExists = false; Check(Decide(none) == handover::kStay, "no launcher beside the game: the game starts");

    printf(g_fail ? "FAILED: %d\n" : "ALL OK\n", g_fail);
    return g_fail ? 1 : 0;
}
