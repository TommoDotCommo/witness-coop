// Game-free check of the dev menu's map page: at every supported screen size the selected panel's details (every information line,
// action button, "Targeted by" button and chain button) lie inside the detail pane's clip rectangle, at a readable size, and the spot
// list above keeps room for rows. Glyph advances are the ones the game's atlas measured (Segoe UI, ui.cpp: "uiglyph" log lines).
#include "detail_layout.h"
#include <cstdio>
#include <string>
static const float kAdvance[95] = {
    5.5f, 5.5f, 8.0f, 12.0f, 11.0f, 16.5f, 16.0f, 4.5f, 6.0f, 6.0f, 8.5f, 13.5f, 4.5f, 8.0f, 4.5f, 8.0f, 11.0f, 11.0f, 11.0f, 11.0f,
    11.0f, 11.0f, 11.0f, 11.0f, 11.0f, 11.0f, 4.5f, 4.5f, 13.5f, 13.5f, 13.5f, 9.0f, 19.0f, 13.0f, 11.5f, 12.5f, 14.0f, 10.0f, 10.0f, 13.5f,
    14.0f, 5.5f, 7.0f, 11.5f, 9.5f, 18.0f, 15.0f, 15.0f, 11.0f, 15.0f, 12.0f, 10.5f, 10.5f, 13.5f, 12.5f, 18.5f, 12.0f, 11.0f, 11.5f, 6.0f,
    7.5f, 6.0f, 13.5f, 8.5f, 5.5f, 10.0f, 12.0f, 9.0f, 12.0f, 10.5f, 6.5f, 12.0f, 11.5f, 5.0f, 5.0f, 10.0f, 5.0f, 17.0f, 11.5f, 11.5f,
    12.0f, 12.0f, 7.0f, 8.5f, 7.0f, 11.5f, 9.5f, 14.5f, 9.0f, 9.5f, 9.0f, 6.0f, 5.0f, 6.0f, 13.5f};
static int failures;
static void check(bool ok, const char* what, float W, float H) { if (!ok) { std::printf("FAILED at %.0fx%.0f: %s\n", W, H, what); ++failures; } }
static float Measure(const char* s, float sc) { uitext::Glyph g[95]; for (int i = 0; i < 95; i++) g[i].advance = kAdvance[i]; return uitext::Width(s, sc, g); }

static uidetail::Content Worst(bool legacy) {      // the most a selected panel can show: every action, eight "Targeted by" buttons, full chains
    uidetail::Content c; c.legacy = legacy;
    c.title = "Panel 0x17cfb   121 nodes"; c.info = "solved: yes    powered: yes\nat -1187.3 -1155.2 113.7";
    c.targets = "Targets: Power_Cable 0x28903 (lit)";
    c.actions = {"Teleport to it", "Solve (and run target)", "Power on", "Run its target", "Open target door", "Light cable", "Cable off", "Select target panel"};
    c.revTitle = "Targeted by 12:"; for (int k = 0; k < 8; k++) c.rev.push_back("0x17cfb");
    c.chainTitle[0] = "Powers on from here:"; c.chainTitle[1] = "Fed by:";
    for (int k = 0; k < 2; k++) for (int i = 0; i < 6; i++) c.chain[k].push_back("0x17cfb ok");
    return c;
}
static uidetail::Content Panel293() {      // the panel the live layout check selects (menusel 293): a maze whose target is a cable
    uidetail::Content c = Worst(false);
    c.title = "Panel 0x293   35 nodes"; c.info = "solved: no    powered: yes\nat -112.4 -124.0 4.5"; c.targets = "Targets: Power_Cable 0x349";
    c.actions = {"Teleport to it", "Solve (and run target)", "Run its target", "Light cable", "Cable off"};
    c.revTitle = "Targeted by 0:"; c.rev.clear(); c.chain[0] = {"0x295"}; c.chain[1].clear();
    return c;
}

// Counts the items outside the pane's clip rectangle (as DevMenuDraw sets it) and checks sizes; returns the number clipped.
static int Clipped(float W, float H, const uidetail::Content& c, bool report) {
    using namespace uidetail;
    Frame f = MenuFrame(W, H); MapDetail d = FitMapDetail(f, c, Measure);
    std::vector<Item> items; Layout(c, d.content.x, d.content.y, d.content.w, d.scale, Measure, &items);
    float l = d.pane.x + f.sc, t = d.pane.y + 1, r = l + f.lw - 2 * f.sc, b = t + d.pane.h - 2 * f.sc;      // UiClip(x0 + 21 sc, dy + 1, lw - 2 sc, dh - 2 sc)
    int n = 0;
    for (const auto& it : items) {
        bool out = it.box.x < l - .5f || it.box.y < t - .5f || it.box.x + it.box.w > r + .5f || it.box.y + it.box.h > b + .5f;
        if (out && report) std::printf("  clipped at %.0fx%.0f: '%s' at %.0f %.0f size %.0f %.0f inside %.0f %.0f %.0f %.0f\n", W, H, it.text.c_str(), it.box.x, it.box.y, it.box.w, it.box.h, l, t, r, b);
        n += out;
        if (report) {
            check(it.scale * 20 >= 10 - .01f, "text at least 10 px", W, H);
            if (it.kind == Action || it.kind == Rev || it.kind == ChainItem)      // Button(): the label shrinks to fit, at most to 70 % of the scale
                check(it.box.h >= 24 * it.scale - .01f && Measure(it.text.c_str(), it.scale * 0.7f) <= it.box.w - 16 * it.scale + .01f, "button large enough for its label", W, H);
        }
    }
    if (report) {
        size_t want = c.actions.size() + c.rev.size() + c.chain[0].size() + c.chain[1].size() + 6;
        check(items.size() == want, "every line and button is laid out (none dropped)", W, H);
        float listH = d.pane.y - f.gap - f.cy0;      // SpotList(x0 + 20 sc, cy0, lw, ch - dh - gap): a scrolling list that must keep a few rows
        check(listH >= 3 * (kLine * f.sc + 6), "the spot list keeps at least three rows", W, H);
    }
    return n;
}

int main() {
    const float sizes[][2] = {{1024, 768}, {1280, 720}, {1280, 800}, {1366, 768}, {1600, 900}, {1920, 1080}, {2560, 1080}, {2560, 1440}, {3440, 1440}, {3840, 1600}, {3840, 2160}};
    for (const auto& s : sizes) {
        for (const auto& c : {Worst(false), Panel293()}) check(Clipped(s[0], s[1], c, true) == 0, "every detail line and button inside the pane", s[0], s[1]);
        // the right-hand pane of the nearby page (DetailPane at full scale, the pane's whole height)
        uidetail::Frame f = uidetail::MenuFrame(s[0], s[1]); uidetail::Content c = Worst(false);
        float w = f.rw - 32 * f.sc, sc = uidetail::FitScale(c, w, f.ch - 24 * f.sc, f.sc, Measure);
        check(uidetail::Height(c, w, sc, Measure) <= f.ch - 24 * f.sc + .01f && sc * 20 >= 10, "nearby page details fit their pane", s[0], s[1]);
    }
    // The no_detail_fit mutant (the old stacked layout with a fixed pane height) must show clipped items at 1920x1080, as the live check expects.
    uidetail::Content old293 = Panel293(); old293.legacy = true;
    check(Clipped(1920, 1080, old293, false) > 0, "the legacy layout of panel 0x293 is reported clipped at 1920x1080", 1920, 1080);
    std::printf("%s\n", failures ? "FAILED" : "ALL OK"); return failures ? 1 : 0;
}
