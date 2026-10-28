#include "guide.h"
#include <cstdlib>
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
        check(std::count(cols[0].begin(), cols[0].end(), guide::KeyName(c.*b.member) + "  " + b.text) == 1,
              "guide lists actual configured action exactly once");
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
    guide::Hold h;
    check(!h.Open() && !h.Key('T', true), "only F1 opens guide");
    check(h.Key(guide::Help, true) && h.Open(), "F1 down opens guide");
    h.Key(guide::Help, true); check(h.Open(), "autorepeat keeps guide open");
    h.Key(guide::Help, false); check(!h.Open(), "release hides guide");
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
    check(c.keyEmote == 'G', "emote binding key is G");
    check(c.keyRadio == 'R', "radio binding key is R");
    check(guide::Help == 0x70, "F1 guide key is F1");
    check(guide::TvView == 0x79, "TV view key is F10");
    check(guide::TvConsent == 0x7A, "TV consent key is F11");
    check(guide::Watch == 'V', "POV watch key is V");
    check(guide::PrevView == 0xDB, "POV prev view key is [");
    check(guide::NextView == 0xDD, "POV next view key is ]");

    // Verify the guide lists every new feature's binding and is generated from the table
    auto defaultCols = guide::Content(Cfg());
    std::string col0Text;
    for (const auto& line : defaultCols[0]) col0Text += line + "\n";
    check(col0Text.find("G  Hold: emote wheel") != std::string::npos, "guide lists emote binding");
    check(col0Text.find("R  Hold: radio channel") != std::string::npos, "guide lists radio binding");
    check(col0Text.find("F1  Hold: quick guide") != std::string::npos, "guide lists F1 quick guide");
    check(col0Text.find("F10  Toggle back TV viewer") != std::string::npos, "guide lists TV view key");
    check(col0Text.find("F11  Toggle back TV camera broadcast consent") != std::string::npos, "guide lists TV consent key");
    check(col0Text.find("V  Watch / clear a player's screen") != std::string::npos, "guide lists POV watch key");

    std::cout << (failures ? "FAILED" : "ALL OK") << '\n';
    return failures ? 1 : 0;
}
