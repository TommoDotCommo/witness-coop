#include "guide.h"
#include "movedkeys.h"
#include "../proxy/bindings.h"
#include <algorithm>
#include <cstdlib>
#include <unistd.h>
#include <iostream>
static int failures;
static void check(bool ok, const char* what) { if (!ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; } }
int main() {
    Cfg c;
    check(guide::Validate(c).empty(), "default bindings have no collisions or reserved keys");
    for (const auto& b : guide::bindings) {
        check(c.*b.member == b.initial, "config defaults match real binding table");
        c.*b.member = guide::Help;
        check(!guide::Validate(c).empty(), "F1 is reserved");
        guide::MigrateF1(c);
        check(c.*b.member == 0x71, "legacy F1 action moves to F2");
        check(guide::Validate(c).empty(), "moved binding is collision free");
        auto cols = guide::Content(c);
        auto rowsFor = [&](const guide::Columns& cs) { std::string r = guide::Row("", b.text), desc = r.substr(r.find('\t')); size_t n = 0;      // the action's row, which names this key
            for (const auto& l : cs[0]) if (l.size() >= desc.size() && l.compare(l.size() - desc.size(), desc.size(), desc) == 0 && l.find(guide::DisplayKey(c.*b.member)) != std::string::npos) ++n; return n; };
        check(rowsFor(cols) == (guide::IsTvBinding(b) || guide::IsRadioBinding(b) ? 0u : 1u),
              "guide lists actual configured action exactly once (the experimental TV and radio keys not while their options are off)");
        c.experimentalTv = true; c.experimentalRadio = true; cols = guide::Content(c);
        check(rowsFor(cols) == 1, "with experimental_tv on every action is listed exactly once");
        c = Cfg();
    }
    c.keyChat = guide::Help; c.keyPing = 0x71;
    guide::MigrateF1(c);
    check(c.keyChat == 0x7B && c.keyPing == 0x71, "occupied F2 preserved; move to free F12");
    c.keyChat = c.keyPing;
    check(!guide::Validate(c).empty(), "duplicate custom bindings rejected");
    for (const auto& f : guide::fixed) {
        c = Cfg(); c.keyChat = f.key;
        check(!guide::Validate(c).empty(), "fixed binding collision rejected");
    }
    std::set<unsigned> fixed;
    for (const auto& f : guide::fixed) check(fixed.insert(f.key).second, "fixed bindings unique");
    for (unsigned k = 0x70; k <= 0x7B; ++k) check(guide::ParseKey(guide::KeyName(k)) == k, "function keys round trip");
    for (auto s : {"F0", "F13", "F1oops", "F01", "", "Space"}) check(!guide::ParseKey(s), "malformed binding rejected");
    {   // experimental_tv: off (the default) the TV keys are free, out of the guide, and bound all the same
        Cfg off; check(!off.experimentalTv, "experimental_tv is off by default");
        for (unsigned vk : {guide::TvView, guide::TvConsent, (unsigned)off.keyTvDeploy, (unsigned)off.keyTvSeat}) check(guide::TvKey(off, vk) == guide::TvKeyAction::None, "a TV key does nothing while experimental_tv is off");
        Cfg on; on.experimentalTv = true;
        check(guide::TvKey(on, guide::TvView) == guide::TvKeyAction::View && guide::TvKey(on, guide::TvConsent) == guide::TvKeyAction::Consent
              && guide::TvKey(on, on.keyTvDeploy) == guide::TvKeyAction::Deploy && guide::TvKey(on, on.keyTvSeat) == guide::TvKeyAction::Seat && guide::TvKey(on, 'Q') == guide::TvKeyAction::None, "with experimental_tv on the TV keys act as before");
        check(guide::Validate(off).empty() && off.keyTvDeploy == 'J' && off.keyTvSeat == 'K', "the TV keys stay bound while off");
        auto text = [](const guide::Columns& cols) { std::string t; for (const auto& col : cols) for (const auto& l : col) t += l + "\n"; return t; };
        std::string go = text(guide::Content(off)), gn = text(guide::Content(on));
        for (const char* w : {"sky TV", "audience stool", "back TV", "TV camera"}) {
            check(go.find(w) == std::string::npos, "the quick guide leaves out the TV features while off");
            check(gn.find(w) != std::string::npos, "the quick guide lists the TV features while on");
        }
    }
    {   // experimental_radio: off (the default) R does nothing and stays bound, the guide does not mention the radio
        Cfg off; check(!off.experimentalRadio && !off.RadioOn(), "experimental_radio is off by default");
        check(!guide::RadioKeyActs(off, 'R'), "R does nothing while experimental_radio is off");
        Cfg on; on.experimentalRadio = true; check(guide::RadioKeyActs(on, 'R') && !guide::RadioKeyActs(on, 'Q'), "with experimental_radio on R acts");
        on.radioEnabled = false; check(!guide::RadioKeyActs(on, 'R'), "radio_enabled=0 still switches it off");
        check(guide::Validate(off).empty() && off.keyRadio == 'R', "R stays bound while off");
        auto text = [](const guide::Columns& cols) { std::string t; for (const auto& col : cols) for (const auto& l : col) t += l + "\n"; return t; };
        Cfg on2; on2.experimentalRadio = true; std::string go = text(guide::Content(off)), gn = text(guide::Content(on2));
        check(go.find("Channels: proximity, open line") != std::string::npos && go.find("adio (hold)") == std::string::npos && go.find("walkie") == std::string::npos, "the quick guide has no radio channel while off");
        check(gn.find("Channels: proximity, radio, open line") != std::string::npos, "the quick guide names the radio channel while on");
    }
    {   // the developer menu's keys and map help are only in the guide when dev_menu=1 (off in a normal install)
        auto text = [](const guide::Columns& cols) { std::string t; for (const auto& col : cols) for (const auto& l : col) t += l + "\n"; return t; };
        Cfg off; Cfg on; on.devMenu = true; std::string go = text(guide::Content(off)), gn = text(guide::Content(on));
        for (const char* w : {"Developer menu", "Nearby puzzles", "Dev map"}) {
            check(go.find(w) == std::string::npos, "the quick guide leaves out the developer menu while it is off");
            check(gn.find(w) != std::string::npos, "the quick guide lists the developer menu while it is on");
        }
    }
    guide::Hold h;
    check(!h.Open() && !h.Key('T', true), "only F1 opens guide");
    check(h.Key(guide::Help, true) && h.Open(), "F1 down opens guide");
    h.Key(guide::Help, true); check(h.Open(), "autorepeat keeps guide open");
    h.Key(guide::Help, false); check(!h.Open(), "release hides guide");
    {   // a real hold: one press, then autorepeat presses (30 a second) for 3 s, then the release: the card changes state exactly twice and is open on every event in between
        guide::Hold k; int changes = 0, openEvents = 0, events = 0; bool ch = false;
        for (int i = 0; i < 90; ++i) { k.Key(guide::Help, true, &ch); changes += ch; ++events; openEvents += k.Open(); }
        check(changes == 1 && openEvents == events, "autorepeat presses neither toggle nor re-arm the guide (1 change in 90 presses, open on all)");
        k.Key(guide::Help, false, &ch); changes += ch; check(changes == 2 && !k.Open(), "release is the second and last change");
        k.Key(guide::Help, false, &ch); check(!ch && !k.Open(), "a stray release changes nothing");
        k.Key('T', true, &ch); check(!ch && !k.Open(), "other keys neither open nor change the guide");
        k.Key(guide::Help, true, &ch); k.Reset(); k.Key(guide::Help, true, &ch); check(ch && k.Open(), "after a focus loss the key opens the guide again");
    }
    h.Key(guide::Help, true); h.Reset(); check(!h.Open(), "focus loss hides guide");
    auto cols = guide::Wrap(guide::Content(Cfg()));
    size_t rows = 0;
    for (const auto& col : cols) {
        rows = std::max(rows, col.size());
        for (const auto& line : col) check(line.size() <= 48, "guide lines stay inside column");
    }
    std::cout << "guide rows: " << cols[0].size() << ", " << cols[1].size() << ", " << cols[2].size() << '\n';
    check(rows <= 50, "guide line budget");
    for (auto size : {std::pair<int, int>{1280, 720}, {1366, 768}, {1920, 1080}, {2560, 1440}}) {
        auto layout = guide::Fit(float(size.first), float(size.second), cols);
        check(20 * layout.scale >= 12, "legible game font at common resolutions");
        check(72 + rows * layout.rowHeight <= size.second - 10, "last guide row fits vertically");
        for (const auto& col : cols) for (const auto& line : col)
            check(line.size() * 10 * layout.scale <= layout.columnWidth - 10, "rendered text fits horizontally");
    }
    c = Cfg();
    for (const auto& b : guide::bindings) c.*b.member = guide::Help;
    guide::MigrateF1(c);
    check(guide::Validate(c).empty(), "even multiple legacy F1 assignments move to distinct free keys");

    // Combined table collision test across emotes, radio, TV, and F1 guide
    c = Cfg();
    std::set<unsigned> combinedKeys;
    for (const auto& b : guide::bindings) {
        check(combinedKeys.insert(c.*b.member).second, "binding key must be unique in combined table");
    }
    for (const auto& f : guide::fixed) {
        check(combinedKeys.insert(f.key).second, "fixed key must not collide with bindings or other fixed keys");
    }
    for (unsigned wasd : {'W', 'A', 'S', 'D'}) {
        check(!combinedKeys.count(wasd), "no binding or fixed key collides with movement WASD");
    }
    check(c.keyEmote == 'H', "emote binding key is H");
    check(c.keyPing == 'V' && c.keyPingAlt == guide::MouseMiddle, "ping is V and middle mouse");
    check(c.keyVoice == 'B' && c.keyVoiceAlt == guide::MouseX1, "push to talk is B and the mouse side button");
    check(c.keyVoiceChannel == 'Y' && c.keyColor == guide::Wheel && c.keyChat == 'T', "channel Y, ink colour wheel, chat T");
    check(c.keyRadio == 'R', "radio binding key is R");
    check(guide::Help == 0x70, "F1 guide key is F1");
    check(guide::TvView == 0x79, "TV view key is F10");
    check(guide::TvConsent == 0x7A, "TV consent key is F11");
    check(guide::Roster == 0x09, "the player list is Tab");
    check(guide::PrevView == 0xDB, "POV prev view key is [");
    check(guide::NextView == 0xDD, "POV next view key is ]");

    // Verify the guide lists every new feature's binding and is generated from the table
    auto defaultCols = guide::Content(Cfg());
    std::string col0Text;
    for (const auto& line : defaultCols[0]) col0Text += line + "\n";
    check(col0Text.find("Hold H\tEmote wheel") != std::string::npos, "guide lists emote binding");
    check(col0Text.find("Hold R\t") == std::string::npos, "guide leaves out the radio key (experimental_radio off by default)");
    { Cfg on; on.experimentalRadio = true; auto oc = guide::Content(on); std::string t; for (const auto& l : oc[0]) t += l + "\n"; check(t.find("Hold R\tTalk on the radio") != std::string::npos, "guide lists the radio key with experimental_radio on"); }
    check(col0Text.find("Hold F1\tThis guide") != std::string::npos, "guide lists F1 quick guide");
    check(col0Text.find("F10\t") == std::string::npos, "guide leaves out the TV view key (experimental_tv off by default)");
    check(col0Text.find("F11\t") == std::string::npos, "guide leaves out the TV consent key (experimental_tv off by default)");
    check(col0Text.find("Hold Tab\tPlayer panel") != std::string::npos, "guide lists the player list key");
    check(col0Text.find("V / Middle mouse\tPing") != std::string::npos && col0Text.find("Hold B / Mouse 4\tPush to talk") != std::string::npos, "guide lists the mouse keys on the row of their action");
    check(col0Text.find("Watch / clear") == std::string::npos, "V no longer watches");
    std::string allText; for (const auto& col : defaultCols) for (const auto& line : col) allText += line + "\n";
    {   // a guide for players: their key names, one row per action, nothing stale (2026-11-06 clean-up; mutant GUIDE_MUT_RAW_KEY_NAMES)
        for (const char* raw : {"MMB", "XBUTTON", "WHEEL", "F5", "V clear", "(second key)", "when PTT enabled"}) check(allText.find(raw) == std::string::npos, "the guide uses no raw or stale key wording");
        size_t pings = 0, talks = 0; for (const auto& line : defaultCols[0]) { if (line.find("\tPing") != std::string::npos) ++pings; if (line.find("\tPush to talk") != std::string::npos) ++talks; }
        check(pings == 1 && talks == 1, "an action with a second key has one row");
        for (const auto& col : defaultCols) for (const auto& line : col) check(std::count(line.begin(), line.end(), '\t') <= 1, "a table row has one key column");
        std::set<std::string> seen; bool dup = false; for (const auto& col : defaultCols) for (const auto& line : col) if (!line.empty() && !seen.insert(line).second) dup = true;
        check(!dup, "no line is said twice");
        auto wrapped = guide::Wrap(defaultCols); bool tabs = false; for (const auto& col : wrapped) for (const auto& line : col) if (line.find('\t') != std::string::npos) tabs = true;
        check(!tabs, "the launcher's wrapped guide has aligned columns, no tab characters"); }
    // H5/K7: the controller's co-op layer comes from padlayer.h's one table; after pad input it is listed first (mutant GUIDE_MUT_NO_PAD)
    for (const auto& l : padlayer::kLines) if (std::string(l.id) != "voice")
        check(allText.find(std::string(l.buttons) + "\t" + l.text) != std::string::npos, "the guide lists every line of the pad layer");
    {   auto padCols = guide::Content(Cfg(), true);
        check(!padCols[0].empty() && padCols[0][0].rfind("CONTROLLER", 0) == 0, "after pad input the guide starts with the controller's lines");
        check(defaultCols[0][0] == "KEYS", "after keys the guide starts with the keys");
        Cfg rs; rs.padVoice = 2; std::string t; auto rsCols = guide::Content(rs, true); for (const auto& line : rsCols[0]) t += line + "\n";
        check(t.find("Hold right stick click\tPush to talk") != std::string::npos, "a stick click chosen for push to talk is listed");
    }

    // the diagnostics marker is F2 unless a binding took it, never one of the bindings, and the guide names it
    check(guide::MarkerKey(Cfg()) == 0x71, "the marker is F2 by default");
    check(col0Text.find("F2\tMark a problem") != std::string::npos, "guide lists the marker key");
    { Cfg m; m.keyChat = 0x71; check(guide::MarkerKey(m) == 0x7B, "a binding on F2 moves the marker to F12");
      auto cols = guide::Content(m); std::string t; for (const auto& line : cols[0]) t += line + "\n"; check(t.find("F12\tMark a problem") != std::string::npos, "the guide names the moved marker");
      m.keyPing = 0x7B; check(guide::MarkerKey(m) == 0, "no free key: no marker key"); }
    {   // no default or migrated binding may equal the marker key
        Cfg m; for (const auto& b : guide::bindings) m.*b.member = guide::Help; guide::MigrateF1(m);
        unsigned mk = guide::MarkerKey(m); for (const auto& b : guide::bindings) check(!mk || m.*b.member != mk, "the marker never shares a key with a binding");
    }

    // mouse keys round trip; Tab is never a binding
    for (unsigned k : {guide::MouseMiddle, guide::MouseX1, guide::MouseX2, guide::Wheel}) check(guide::ParseKey(guide::KeyName(k)) == k, "mouse keys round trip");
    check(guide::ParseKey("Tab") == 0 && guide::ParseKey("mouse4") == guide::MouseX1, "Tab is not bindable; mouse4 is XBUTTON1");
    // H2: a shared key is reserved for its shared action
    for (auto pr : {std::pair<unsigned, unsigned Cfg::*>{'V', &Cfg::keyChat}, {'B', &Cfg::keyDraw}, {'H', &Cfg::keyRadio}, {'T', &Cfg::keyCanvas}, {guide::MouseMiddle, &Cfg::keyTvSeat}, {guide::MouseX1, &Cfg::keyTvDeploy}}) {
        Cfg m; m.*pr.second = pr.first; check(!guide::Validate(m).empty(), "a shared key refused for another action");
    }
    { Cfg m; m.keyPing = 'B'; check(!guide::Validate(m).empty(), "ping cannot take the push-to-talk key"); }
    { Cfg m; m.keyPingAlt = 0; check(guide::Validate(m).empty(), "a second key may be unbound"); m.keyPing = 0; check(!guide::Validate(m).empty(), "a primary key may not be unbound"); }
    { Cfg m; m.keyPing = 'Q'; m.keyPingAlt = 'V'; check(guide::Validate(m).empty(), "the ping action may use V on its second key"); }

    // migration: old defaults get the new defaults, a key the player changed stays (mutants: GUIDE_MUT_KEEP_OLD, GUIDE_MUT_OVERWRITE_CUSTOM)
    {   Cfg m; const char* oldCfg[] = {"key_ping=P", "key_voice=N", "key_emote=G", "key_voice_channel=H", "key_color=B"};
        for (auto l : oldCfg) SetBindingLine(&m, l);
        check(m.keyPing == 'V' && m.keyVoice == 'B' && m.keyEmote == 'H' && m.keyVoiceChannel == 'Y' && m.keyColor == guide::Wheel, "a cfg with the old defaults gets the new ones");
        guide::MigrateAll(m); check(guide::Validate(m).empty(), "migrated old-default cfg validates"); }
    {   Cfg m; for (auto l : {"key_ping=Q", "key_voice=M", "key_emote=U", "key_voice_channel=L", "key_color=O"}) SetBindingLine(&m, l);
        check(m.keyPing == 'Q' && m.keyVoice == 'M' && m.keyEmote == 'U' && m.keyVoiceChannel == 'L' && m.keyColor == 'O', "a key the player changed stays");
        check(guide::MovedNotes(m).size() == 2, "moved list names only the fixed moves when every moved key is the player's own"); }
    {   Cfg m; SetBindingLine(&m, "key_ping=F7"); check(m.keyPing == 0x76, "a function key the player chose stays"); }
    {   // the old default of one action is where another action's new default lives: cfg with key_emote=G and key_voice_channel=H (both old defaults)
        Cfg m; SetBindingLine(&m, "key_emote=G"); SetBindingLine(&m, "key_voice_channel=H");
        check(m.keyEmote == 'H' && m.keyVoiceChannel == 'Y', "emote G->H and channel H->Y in one cfg"); guide::MigrateAll(m); check(guide::Validate(m).empty(), "...and they do not collide"); }
    // conflict: the player's own chat key is V; ping keeps middle mouse only, the player's key is kept, and it is logged
    {   Cfg m; SetBindingLine(&m, "key_chat=V"); std::vector<std::string> notes; guide::MigrateAll(m, &notes);
        check(m.keyChat == 'V' && m.keyPing == 0 && m.keyPingAlt == guide::MouseMiddle, "a player's own V stays with chat; ping stays on middle mouse");
        check(notes.size() == 1 && notes[0].find("Chat") != std::string::npos, "the conflict is logged");
        std::set<unsigned> seen; bool dup = false; for (const auto& b : guide::bindings) if (m.*b.member && !seen.insert(m.*b.member).second) dup = true;
        check(!dup, "the resolved cfg has no key on two actions");
        check(!guide::Validate(m).empty(), "the launcher still asks for a free key when the player's own binding sits on a shared key"); }
    {   Cfg m; SetBindingLine(&m, "key_radio=H"); guide::MigrateAll(m); check(m.keyRadio == 'H' && m.keyEmote == 0, "a player's own H stays with radio; the emote wheel is unbound"); }
    {   Cfg m; SetBindingLine(&m, "key_draw=B"); guide::MigrateAll(m); check(m.keyDraw == 'B' && m.keyVoice == 0 && m.keyVoiceAlt == guide::MouseX1, "a player's own B stays; push to talk keeps the side button"); }
    {   // the moved-keys list: default players get every line, written once
        Cfg m; auto n = guide::MovedNotes(m); std::string t; for (auto& l : n) t += l + "\n";
        check(t.find("Ping a place: P is now V or Middle mouse") != std::string::npos && t.find("Push to talk: N is now B or Mouse 4") != std::string::npos
              && t.find("Emote wheel (hold): G is now H") != std::string::npos && t.find("Next voice channel: H is now Y") != std::string::npos
              && t.find("Ink colour: B is now Mouse wheel") != std::string::npos && t.find("F5 is now hold Tab") != std::string::npos && t.find("Watch a player: V is now in the player list") != std::string::npos,
              "moved list names every move");
        char tmpl[] = "/tmp/mkXXXXXX"; std::string d = mkdtemp(tmpl) ? std::string(tmpl) + "/" : "";
        check(movedkeys::WriteOnce(d, n), "first start writes the list"); check(!movedkeys::WriteOnce(d, n), "second start writes nothing");
        check(movedkeys::SchemeOf(d) == movedkeys::kScheme, "scheme recorded");
        check(movedkeys::Dir("C:\\g\\", false, "C:\\U") == "C:\\U\\witness-coop\\" && movedkeys::Dir("C:\\g\\", true, "C:\\U") == "C:\\g\\", "per-user folder, or beside the cfg for a test copy");
        std::remove((d + "moved_keys.txt").c_str()); std::remove((d + "keys_scheme").c_str()); rmdir(tmpl); }

    std::cout << (failures ? "FAILED" : "ALL OK") << '\n';
    return failures ? 1 : 0;
}
