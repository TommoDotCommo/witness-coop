#pragma once
// The hold-Tab player panel as a pure model (game-free tests: player_panel_test.cpp): the rows, where each control is on screen, what a click or a key does.
// The renderer (overlay.cpp) draws the rectangles this computes and performs the actions it returns; nothing here touches the game.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace wpanel {
struct Row {
    unsigned id = 0; std::string name; float color[3] = {1, 1, 1};
    bool host = false, speaking = false, muted = false, watching = false, loading = false, down = false;
    int pingMs = -1;          // round trip, -1 unknown
    float distance = -1;      // metres from this player, <0 unknown
    float volume = 1;         // this player's voice volume, 0..1
};
// What the row says about where the player is: "loading" until their first position arrived, "down" when they went quiet, else the distance.
inline std::string Where(const Row& r) {
    if (r.loading) return "loading";
    if (r.down) return "down";
    char b[32]; if (r.distance >= 0) snprintf(b, sizeof b, "%d m", int(r.distance + 0.5f)); else snprintf(b, sizeof b, "-");
    return r.watching ? std::string("watching, ") + b : std::string(b);
}
inline std::string PingText(const Row& r) { char b[16]; if (r.pingMs >= 0) snprintf(b, sizeof b, "%d ms", r.pingMs); else snprintf(b, sizeof b, "-- ms"); return b; }

enum class Act { None, Watch, Mute, VolDown, VolUp, Kick, CopyInvite, Leave, LeaveYes, LeaveNo, VoiceDown, VoiceUp, NameTags, PartnerSize, Clear, Close };
struct Hit { Act act = Act::None; unsigned id = 0; bool operator==(const Hit& o) const { return act == o.act && id == o.id; } };
struct Rect { float x = 0, y = 0, w = 0, h = 0; bool has(float px, float py) const { return w > 0 && px >= x && px < x + w && py >= y && py < y + h; } };
struct RowRects { Rect row, watch, mute, volDown, volUp, kick; };
struct Options {          // what this session offers (the panel hides what it cannot do)
    bool canKick = false;     // this player is the host and the session can remove a player (TODO: the lobby has no kick yet, so the launcher never sets it)
    bool hasInvite = false;   // the launcher shared an invite with the game
    bool confirmLeave = false;
};
struct Layout {
    float k = 1; Rect panel; std::vector<RowRects> rows;
    Rect voiceDown, voiceUp, tags, partner, copyInvite, leave, yes, no;
    float rowStep = 0, firstRow = 0, togglesY = 0, sessionY = 0, hintY = 0;
};
inline constexpr float kRowH = 26, kButtonH = 22;
inline Layout Compute(float W, float H, size_t n, const Options& o) {
    Layout L;
    // heights at scale 1: title 34, rows, a gap, the toggles line 30, the session line 34, the hint 26
    float total = 34 + kRowH * float(std::max<size_t>(n, 1)) + 12 + 30 + 34 + 26 + 12;
    float k = std::min(std::min(W / 1280.f, H / 720.f), (H - 24) / total);
#ifdef WP_MUT_NO_FIT
    k = 1;
#endif
    L.k = k;
    float x = 24 * k, y = std::min(90 * k, std::max(8.f, H - total * k - 12)), w = 660 * k;
    L.panel = {x, y, w, total * k};
    L.firstRow = y + 34 * k; L.rowStep = kRowH * k;
    float bh = kButtonH * k;
    for (size_t i = 0; i < n; ++i) {
        float ry = L.firstRow + L.rowStep * float(i), by = ry + (kRowH - kButtonH) / 2 * k, right = x + w - 8 * k;
        RowRects r; r.row = {x + 4 * k, ry, w - 8 * k, L.rowStep};
        float bx = right;
        if (o.canKick) { bx -= 48 * k; r.kick = {bx, by, 48 * k, bh}; bx -= 4 * k; }
        bx -= 22 * k; r.volUp = {bx, by, 22 * k, bh}; bx -= 4 * k;
        bx -= 22 * k; r.volDown = {bx, by, 22 * k, bh}; bx -= 4 * k;
        bx -= 52 * k; r.mute = {bx, by, 52 * k, bh}; bx -= 4 * k;
        bx -= 62 * k; r.watch = {bx, by, 62 * k, bh};
        L.rows.push_back(r);
    }
    float rowsEnd = L.firstRow + L.rowStep * float(std::max<size_t>(n, 1));
    L.togglesY = rowsEnd + 12 * k; L.sessionY = L.togglesY + 30 * k; L.hintY = L.sessionY + 34 * k;
    float ty = L.togglesY + 2 * k, right = x + w - 8 * k;
    // toggles line: "Voice volume 80% [-][+]" on the left, [Name tags: on] [Partner picture: large] on the right
    L.voiceDown = {x + 150 * k, ty, 22 * k, bh}; L.voiceUp = {x + 214 * k, ty, 22 * k, bh};
    L.partner = {right - 190 * k, ty, 190 * k, bh}; L.tags = {L.partner.x - 4 * k - 120 * k, ty, 120 * k, bh};
    float sy = L.sessionY + 4 * k;
    if (o.confirmLeave) { L.yes = {right - 200 * k, sy, 96 * k, bh}; L.no = {right - 100 * k, sy, 100 * k, bh}; }
    else {
        L.leave = {right - 100 * k, sy, 100 * k, bh};
        if (o.hasInvite) L.copyInvite = {L.leave.x - 4 * k - 130 * k, sy, 130 * k, bh};
    }
    return L;
}
inline Hit HitTest(const Layout& L, const std::vector<Row>& rows, const Options& o, float px, float py) {
    if (o.confirmLeave) {
        if (L.yes.has(px, py)) return {Act::LeaveYes, 0};
        if (L.no.has(px, py)) return {Act::LeaveNo, 0};
    } else {
        if (L.leave.has(px, py)) return {Act::Leave, 0};
        if (L.copyInvite.has(px, py)) return {Act::CopyInvite, 0};
    }
    if (L.voiceDown.has(px, py)) return {Act::VoiceDown, 0};
    if (L.voiceUp.has(px, py)) return {Act::VoiceUp, 0};
    if (L.tags.has(px, py)) return {Act::NameTags, 0};
    if (L.partner.has(px, py)) return {Act::PartnerSize, 0};
    for (size_t i = 0; i < rows.size() && i < L.rows.size(); ++i) {
        const RowRects& r = L.rows[i]; unsigned id = rows[i].id;
#ifdef WP_MUT_ROW_OFFSET
        id = i + 1 < rows.size() ? rows[i + 1].id : id;      // mutant: an action lands on the next player
#endif
        if (r.watch.has(px, py)) return {Act::Watch, id};
        if (r.mute.has(px, py)) return {Act::Mute, id};
        if (r.volDown.has(px, py)) return {Act::VolDown, id};
        if (r.volUp.has(px, py)) return {Act::VolUp, id};
        if (o.canKick && r.kick.has(px, py)) return {Act::Kick, id};
    }
    return {};
}
// Keyboard use without a mouse: Up / Down choose a player, Enter watches, M mutes, Left / Right change the voice volume, Backspace stops watching,
// 1 / 2 the voice volume of everybody, 3 name tags, 4 partner picture, C copies the invite, L leaves (then Enter / Y confirms, Esc / N stays), Esc closes.
struct Nav {
    int sel = 0;
    Hit Key(unsigned vk, size_t n, const std::vector<Row>& rows, const Options& o) {
        const unsigned UP = 0x26, DOWN = 0x28, LEFT = 0x25, RIGHT = 0x27, ENTER = 0x0D, ESC = 0x1B, BACK = 0x08, DEL = 0x2E;
        if (o.confirmLeave) {
            if (vk == ENTER || vk == 'Y') return {Act::LeaveYes, 0};
            if (vk == ESC || vk == 'N') return {Act::LeaveNo, 0};
            return {};
        }
        if (n && sel >= int(n)) sel = int(n) - 1;
        unsigned id = n && sel < int(rows.size()) ? rows[sel].id : 0;
        if (vk == UP && n) { sel = (sel + int(n) - 1) % int(n); return {}; }
        if (vk == DOWN && n) { sel = (sel + 1) % int(n); return {}; }
        if (vk == ENTER && id) return {Act::Watch, id};
        if (vk == 'M' && id) return {Act::Mute, id};
        if (vk == LEFT && id) return {Act::VolDown, id};
        if (vk == RIGHT && id) return {Act::VolUp, id};
        if (vk == BACK) return {Act::Clear, 0};
        if (vk == DEL && id && o.canKick) return {Act::Kick, id};
        if (vk == '1') return {Act::VoiceDown, 0};
        if (vk == '2') return {Act::VoiceUp, 0};
        if (vk == '3') return {Act::NameTags, 0};
        if (vk == '4') return {Act::PartnerSize, 0};
        if (vk == 'C' && o.hasInvite) return {Act::CopyInvite, 0};
        if (vk == 'L') return {Act::Leave, 0};
        if (vk == ESC) return {Act::Close, 0};
        return {};
    }
};
inline constexpr const char* kHint = "Up/Down: choose  Enter: watch  M: mute  Left/Right: volume  L: leave  Esc: close";
// The confirmation text before leaving (the standard's I7): what happens to the others.
inline std::string LeaveText(bool host) { return host ? "Leave? Everyone returns to the lobby." : "Leave? The others keep playing."; }
}  // namespace wpanel
