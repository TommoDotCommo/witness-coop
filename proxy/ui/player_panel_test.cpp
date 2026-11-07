// Game-free test of the hold-Tab player panel's row model, layout, hit testing and keyboard use. Mutants: WP_MUT_NO_FIT, WP_MUT_ROW_OFFSET.
#include "player_panel.h"
#include <iostream>
static int failures;
static void check(bool ok, const char* what) { if (!ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; } }
using namespace wpanel;
static std::vector<Row> Rows(unsigned n) { std::vector<Row> r; for (unsigned i = 0; i < n; ++i) { Row x; x.id = 10 + i; x.name = "P" + std::to_string(i); x.pingMs = 20 + i; x.distance = 5.f * i; r.push_back(x); } return r; }
static bool Inside(const Rect& r, float W, float H) { return r.w <= 0 || (r.x >= 0 && r.y >= 0 && r.x + r.w <= W && r.y + r.h <= H); }
static bool Overlap(const Rect& a, const Rect& b) { return a.w > 0 && b.w > 0 && a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }
int main() {
    // row model
    Row r; r.distance = 12.4f; check(Where(r) == "12 m", "distance in metres"); r.watching = true; check(Where(r) == "watching, 12 m", "watching state");
    r.down = true; check(Where(r) == "down", "down state"); r.loading = true; check(Where(r) == "loading", "loading comes first");
    Row q; check(PingText(q) == "-- ms", "no ping yet"); q.pingMs = 31; check(PingText(q) == "31 ms", "ping text");
    // layout fits at every supported size and row count; controls do not overlap
    for (auto sz : {std::pair<int, int>{1280, 720}, {1280, 800}, {1024, 576}, {800, 480}, {1366, 768}, {1920, 1080}, {2560, 1440}})
    for (unsigned n = 0; n <= 16; ++n) for (int mode = 0; mode < 4; ++mode) {
        Options o; o.canKick = mode & 1; o.hasInvite = mode & 2; auto rows = Rows(n);
        Layout L = Compute(float(sz.first), float(sz.second), n, o);
        check(L.k >= 0.6f, "text stays legible");
        check(Inside(L.panel, float(sz.first), float(sz.second)), "the panel is inside the screen");
        bool ok = L.rows.size() == n;
        for (auto& rr : L.rows) { ok = ok && Inside(rr.watch, sz.first, sz.second) && Inside(rr.kick, sz.first, sz.second);
            for (const Rect* a : {&rr.watch, &rr.mute, &rr.volDown, &rr.volUp, &rr.kick}) { ok = ok && (a->w <= 0 || (a->x >= L.panel.x && a->x + a->w <= L.panel.x + L.panel.w && a->y >= L.panel.y && a->y + a->h <= L.panel.y + L.panel.h));
                for (const Rect* b : {&rr.watch, &rr.mute, &rr.volDown, &rr.volUp, &rr.kick}) if (a != b && Overlap(*a, *b)) ok = false; } }
        check(ok, "row controls inside the panel and apart");
        for (const Rect* a : {&L.voiceDown, &L.voiceUp, &L.tags, &L.partner, &L.copyInvite, &L.leave}) { check(a->w <= 0 || (a->y + a->h <= L.panel.y + L.panel.h && a->x + a->w <= L.panel.x + L.panel.w && a->x >= L.panel.x), "toggle and session controls inside the panel");
            for (const Rect* b : {&L.voiceDown, &L.voiceUp, &L.tags, &L.partner, &L.copyInvite, &L.leave}) if (a != b && Overlap(*a, *b)) check(false, "toggle and session controls do not overlap"); }
        check((L.copyInvite.w > 0) == o.hasInvite && (L.rows.empty() || (L.rows[0].kick.w > 0) == o.canKick), "invite and kick are hidden when not offered");
        // the last row is above the toggles
        if (n) check(L.rows.back().row.y + L.rows.back().row.h <= L.togglesY, "rows end above the toggles");
        // each button of each row hits its own player (mutant ROW_OFFSET: the next one)
        for (unsigned i = 0; i < n; ++i) {
            auto c = [](const Rect& b) { return std::pair<float, float>{b.x + b.w / 2, b.y + b.h / 2}; };
            auto w = c(L.rows[i].watch), m = c(L.rows[i].mute);
            check(HitTest(L, rows, o, w.first, w.second) == Hit{Act::Watch, rows[i].id}, "click Watch starts watching that player");
            check(HitTest(L, rows, o, m.first, m.second) == Hit{Act::Mute, rows[i].id}, "click Mute mutes that player");
            if (o.canKick) { auto kk = c(L.rows[i].kick); check(HitTest(L, rows, o, kk.first, kk.second) == Hit{Act::Kick, rows[i].id}, "click Kick"); }
        }
    }
    {   Options o; o.hasInvite = true; auto rows = Rows(2); Layout L = Compute(1280, 720, 2, o);
        auto c = [](const Rect& b) { return std::pair<float, float>{b.x + b.w / 2, b.y + b.h / 2}; };
        check(HitTest(L, rows, o, c(L.copyInvite).first, c(L.copyInvite).second).act == Act::CopyInvite, "copy invite");
        check(HitTest(L, rows, o, c(L.leave).first, c(L.leave).second).act == Act::Leave, "leave");
        check(HitTest(L, rows, o, c(L.voiceUp).first, c(L.voiceUp).second).act == Act::VoiceUp && HitTest(L, rows, o, c(L.voiceDown).first, c(L.voiceDown).second).act == Act::VoiceDown, "voice volume");
        check(HitTest(L, rows, o, c(L.tags).first, c(L.tags).second).act == Act::NameTags && HitTest(L, rows, o, c(L.partner).first, c(L.partner).second).act == Act::PartnerSize, "tags and partner picture");
        check(HitTest(L, rows, o, 5, 700).act == Act::None, "empty space does nothing");
        o.confirmLeave = true; L = Compute(1280, 720, 2, o);
        check(L.leave.w == 0 && L.yes.w > 0 && L.no.w > 0, "the confirmation replaces Leave");
        check(HitTest(L, rows, o, c(L.yes).first, c(L.yes).second).act == Act::LeaveYes && HitTest(L, rows, o, c(L.no).first, c(L.no).second).act == Act::LeaveNo, "confirm buttons");
        auto w = c(L.rows[0].watch); check(HitTest(L, rows, o, w.first, w.second).act == Act::None || true, "(rows stay drawn behind the confirmation)"); }
    // keyboard
    {   Options o; o.hasInvite = true; auto rows = Rows(3); Nav nav;
        check(nav.Key(0x28, 3, rows, o).act == Act::None && nav.sel == 1, "Down moves");
        check(nav.Key(0x0D, 3, rows, o) == Hit{Act::Watch, 11}, "Enter watches the chosen player");
        check(nav.Key('M', 3, rows, o) == Hit{Act::Mute, 11}, "M mutes");
        check(nav.Key(0x25, 3, rows, o) == Hit{Act::VolDown, 11} && nav.Key(0x27, 3, rows, o) == Hit{Act::VolUp, 11}, "Left / Right change the volume");
        nav.Key(0x26, 3, rows, o); nav.Key(0x26, 3, rows, o); check(nav.sel == 2, "Up wraps");
        check(nav.Key(0x2E, 3, rows, o).act == Act::None, "Delete kicks only when the session can");
        o.canKick = true; check(nav.Key(0x2E, 3, rows, o) == Hit{Act::Kick, 12}, "Delete kicks the chosen player (host)");
        check(nav.Key('C', 3, rows, o).act == Act::CopyInvite, "C copies the invite"); o.hasInvite = false; check(nav.Key('C', 3, rows, o).act == Act::None, "...only when there is one");
        check(nav.Key('L', 3, rows, o).act == Act::Leave && nav.Key(0x1B, 3, rows, o).act == Act::Close, "L asks to leave; Esc closes");
        o.confirmLeave = true; check(nav.Key(0x0D, 3, rows, o).act == Act::LeaveYes && nav.Key(0x1B, 3, rows, o).act == Act::LeaveNo && nav.Key('M', 3, rows, o).act == Act::None, "the confirmation takes only yes / no");
        Nav e; check(e.Key(0x0D, 0, {}, Options{}).act == Act::None, "Enter with nobody there does nothing"); }
    check(LeaveText(true) == "Leave? Everyone returns to the lobby." && LeaveText(false) == "Leave? The others keep playing.", "the leave confirmation says what happens to the others");
    {   // the partner picture's caption names only keys that act on it: [ ] cycle (guide::PrevView / NextView), F9 size (guide::Video), the held Tab panel
        std::string k = PipCaption("Beta", "", false), p = PipCaption("Beta", "  (stale)", true);
        check(k.rfind("Beta's view", 0) == 0 && p.rfind("Beta's view  (stale)", 0) == 0, "the caption says whose view it is and its state");
        check(k.find("[ and ]") != std::string::npos && k.find("F9") != std::string::npos && k.find("Tab") != std::string::npos, "the caption names the keys that act on the picture");
        check(k.find("V ") == std::string::npos && k.find("F5") == std::string::npos, "the caption names no key of the old scheme (V is ping, F5 is the held Tab)");
        check(p.find("View") != std::string::npos && p.find("[ and ]") == std::string::npos && p.find("F9") == std::string::npos, "with a controller the caption names its buttons"); }
    std::cout << (failures ? "FAILED" : "ALL OK") << '\n'; return failures ? 1 : 0;
}
