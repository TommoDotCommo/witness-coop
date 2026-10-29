#pragma once
// The dev menu's frame and the selected panel's detail pane, as pure layout: the renderer draws the boxes this computes, and
// detail_test.cpp checks at every supported screen size, without the game, that every line and button fits inside its pane.
#include <algorithm>
#include <string>
#include <vector>
#include "text.h"

namespace uidetail {
constexpr float kLine = 24;      // the renderer's line height at scale 1 (ui.cpp kLineHeight)
constexpr float kMinScale = 0.5f;      // 10 px text: the smallest the layout checks accept

struct Box { float x, y, w, h; };
enum Kind { Title, Info, Targets, Action, RevTitle, Rev, ChainTitle, ChainItem };
struct Item { Kind kind; int index; int chain; Box box; float scale; std::string text; };
struct Content {
    std::string title, info, targets;
    std::vector<std::string> actions;      // buttons, in order
    std::string revTitle; std::vector<std::string> rev;      // "Targeted by N:" and its panel buttons
    std::string chainTitle[2]; std::vector<std::string> chain[2];      // "Powers on from here:" and "Fed by:" with their panel buttons
    bool legacy = false;      // test mutant (no_detail_fit): stacked titles and chains laid out 520 px wide whatever the pane's width
};

// The selected panel's details, top-down in a pane `w` wide starting at (x, y). Text wraps at the pane's width, every row of buttons
// wraps instead of dropping what does not fit, and a button wider than the pane is narrowed (the renderer shrinks its label to fit).
// measure(text, scale) is the rendered width. Returns the height used.
template <typename Measure>
float Layout(const Content& c, float x, float y, float w, float sc, Measure measure, std::vector<Item>* out) {
    const float top = y, lh = kLine * sc;
    auto text = [&](Kind kind, const std::string& s, float scale) {
        std::string t = c.legacy ? s : uitext::Wrap(s.c_str(), w, [&](const char* q) { return measure(q, scale); });
        float lines = 1 + (float)std::count(t.begin(), t.end(), '\n');
        out->push_back({kind, 0, 0, {x, y, measure(t.c_str(), scale), lines * kLine * scale}, scale, t});
        y += lines * kLine * scale + 0.3f * lh;
    };
    struct Flow {      // left to right, wrapping at the right edge
        float x0, right, x, h, gap, vgap; float* y;
        Box Place(float bw, bool wrap = true) {
#ifdef UI_MUT_FLOW
            wrap = false;
#endif
            if (wrap) bw = std::min(bw, right - x0);
            if (wrap && x > x0 && x + bw > right + .01f) { x = x0; *y += h + vgap; }
            Box b{x, *y, bw, h}; x += bw + gap; return b;
        }
    };
    text(Title, c.title, sc * 1.15f);
    text(Info, c.info, sc);
    text(Targets, c.targets, sc);
    float bh = lh + 12;
    Flow acts{x, x + w, x, bh, 8, 8, &y};
    for (size_t k = 0; k < c.actions.size(); k++) out->push_back({Action, (int)k, 0, acts.Place(measure(c.actions[k].c_str(), sc) + 24), sc, c.actions[k]});
    y += bh + 12;
    auto row = [&](Kind titleKind, Kind itemKind, int chain, const std::string& title, const std::vector<std::string>& items, float width, float after) {
        float tw = measure(title.c_str(), sc);
        out->push_back({titleKind, 0, chain, {x, y, tw, lh + 4}, sc, title});
        if (c.legacy) y += lh * 1.2f;
        Flow f{x, x + width, c.legacy ? x : x + tw + 10, lh + 4, 6, 4, &y};
        for (size_t k = 0; k < items.size(); k++) {
            float bw = measure(items[k].c_str(), sc) + 12;
            if (c.legacy && f.x + bw > f.right) break;      // the old chains dropped what did not fit
            out->push_back({itemKind, (int)k, chain, f.Place(bw, !c.legacy), sc, items[k]});
        }
        y += lh + after;
    };
    row(RevTitle, Rev, 0, c.revTitle, c.rev, w, 12);
    for (int k = 0; k < 2; k++) row(ChainTitle, ChainItem, k, c.chainTitle[k], c.chain[k], c.legacy ? 520 : w, 10);
    return y - top;
}

template <typename Measure>
float Height(const Content& c, float w, float sc, Measure measure) { std::vector<Item> items; return Layout(c, 0, 0, w, sc, measure, &items); }

// The largest text scale, at most `want` and at least kMinScale, at which the details fit `maxH`.
template <typename Measure>
float FitScale(const Content& c, float w, float maxH, float want, Measure measure) {
#ifdef UI_MUT_NOFIT
    return want;
#endif
    if (c.legacy || Height(c, w, want, measure) <= maxH) return want;
    float lo = kMinScale, hi = want;
    for (int i = 0; i < 18; i++) { float mid = (lo + hi) / 2; if (Height(c, w, mid, measure) <= maxH) lo = mid; else hi = mid; }
    return lo;
}

// The dev menu's frame at a screen size (DevMenuDraw): scale, the content area below the tab and TV rows, the left column and the right pane.
struct Frame { float sc, x0, y0, w, h, th, tvY, cy0, ch, lw, gap, rx, rw; };
inline Frame MenuFrame(float W, float H) {
    Frame f; f.sc = std::min(W / 1280.f, H / 900.f); float pad = 18 * f.sc;
    f.x0 = pad; f.y0 = pad; f.w = W - 2 * pad; f.h = H - 2 * pad;
    f.th = kLine * f.sc + 10 * f.sc; f.tvY = f.y0 + 68 * f.sc;
    f.cy0 = f.tvY + f.th + 12 * f.sc; f.ch = f.h - (f.cy0 - f.y0) - kLine * f.sc - 18 * f.sc;
    f.lw = std::max(300 * f.sc, std::min(380 * f.sc, f.w * 0.29f)); f.gap = 12 * f.sc;
    f.rx = f.x0 + 20 * f.sc + f.lw + f.gap; f.rw = f.w - 40 * f.sc - f.lw - f.gap;
    return f;
}

// The map page: the spot list on top of the left column, the selected panel's details under it. The detail pane grows (the list
// shrinks, it scrolls) up to two thirds of the column; if the details still do not fit, their text shrinks, down to kMinScale.
struct MapDetail { Box pane, content; float scale; };
template <typename Measure>
MapDetail FitMapDetail(const Frame& f, const Content& c, Measure measure) {
    const float want = f.sc * 0.9f, inset = 10 * f.sc, contentW = f.lw - 20 * f.sc;
    float dh = std::min(f.ch * 0.46f, 440.f * f.sc);
    if (!c.legacy) {
        float need = Height(c, contentW, want, measure) + 2 * inset;
        if (need > dh) dh = std::min(f.ch * 0.66f, need);
    }
    MapDetail d; d.pane = {f.x0 + 20 * f.sc, f.cy0 + f.ch - dh, f.lw, dh};
    d.content = {d.pane.x + inset, d.pane.y + inset, contentW, dh - 2 * inset};
    d.scale = FitScale(c, contentW, d.content.h, want, measure);
    return d;
}
} // namespace uidetail
