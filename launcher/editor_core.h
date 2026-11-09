#pragma once
// The puzzle editor's logic, without a window (launcher/editor.cpp draws it; launcher/editor_test.cpp tests it game-free): where a click lands
// for each tool, what the tool then does to the puzzle (common/wpuzzle.h), and the line a player draws to try the puzzle.
#include <cmath>
#include <string>
#include <vector>
#include "../common/wpuzzle.h"

namespace ped {

enum Tool { kSquare, kStar, kEliminator, kTriangle, kPoly, kDot, kGap, kFullGap, kStart, kEnd, kClear, kToolCount };
inline const char* ToolName(Tool t) {
    static const char* n[] = {"Square", "Star", "Eliminator", "Triangle", "Polyomino", "Dot", "Gap", "No line", "Start", "End", "Clear"};
    return t < kToolCount ? n[t] : "";
}
inline bool OnCells(Tool t) { return t == kSquare || t == kStar || t == kEliminator || t == kTriangle || t == kPoly; }

struct Brush {
    Tool tool = kSquare;
    uint8_t colour = 1;           // the game's colour index (wpz::Colours)
    uint32_t shape = 0x13;        // polyomino: 4 x 4 bits (wpz::PolyMask)
    bool rotate = false, negative = false;
    uint8_t dot = wpz::kDotBlack;
};

// Where the puzzle is drawn: grid position (x, y) is at pixel (ox + x * step, oy + y * step); a cell is two steps wide.
struct Geometry {
    int ox = 0, oy = 0, step = 1;
    static Geometry Fit(const wpz::Puzzle& p, int left, int top, int width, int height, int margin) {
        Geometry g; int gw = p.width() - 1, gh = p.height() - 1;      // (a step and a bit of panel around the grid, for the ends)
        g.step = std::max(4, std::min((width - 2 * margin) * 10 / std::max(1, gw * 10 + 24), (height - 2 * margin) * 10 / std::max(1, gh * 10 + 24)));
        g.ox = left + (width - g.step * gw) / 2; g.oy = top + (height - g.step * gh) / 2;
        return g;
    }
    int px(int x) const { return ox + x * step; }
    int py(int y) const { return oy + y * step; }
};

// The position a click at (mx, my) means for this tool: a cell for symbols, a corner for a start, a corner or an edge for the rest.
// False when the click is too far from any such position.
inline bool Pick(const Geometry& g, const wpz::Puzzle& p, Tool t, int mx, int my, wpz::Pos* out) {
    double fx = (mx - g.ox) / (double)g.step, fy = (my - g.oy) / (double)g.step;
    int best = -1; double bd = 1e9; wpz::Pos bp{0, 0};
    for (int x = 0; x < p.width(); x++) for (int y = 0; y < p.height(); y++) {
        bool cell = wpz::Puzzle::isCell(x, y), corner = x % 2 == 0 && y % 2 == 0;
        if (OnCells(t) ? !cell : cell) continue;
        if (t == kStart && !corner) continue;
        if ((t == kGap || t == kFullGap) && corner) continue;
        double d = (fx - x) * (fx - x) + (fy - y) * (fy - y);
        if (d < bd) { bd = d; bp = {x, y}; best = 1; }
    }
    if (best < 0 || bd > (OnCells(t) || t == kStart ? 1.0 : 0.6)) return false;      // (a corner-only tool may reach further)
    *out = bp; return true;
}

inline void ClearAt(wpz::Puzzle& p, wpz::Pos a) {
    wpz::Cell& c = p.at(a.x, a.y);
    if (wpz::Puzzle::isCell(a.x, a.y)) { c = wpz::Cell(); return; }
    c.dot = wpz::kDotNone; c.gap = wpz::kGapNone; c.start = false; c.end = wpz::kEndNone;
}

// What the tool does at a position (already picked for it). The same symbol again takes it away; a triangle again counts one more (1, 2, 3, then
// gone); an end again points the next way out (then goes); on a symmetry puzzle a start or an end is mirrored too. True when the puzzle changed.
inline bool Apply(wpz::Puzzle& p, const Brush& b, wpz::Pos a, bool erase = false) {
    if (!p.in(a.x, a.y)) return false;
    wpz::Puzzle before = p; wpz::Cell& c = p.at(a.x, a.y); bool cell = wpz::Puzzle::isCell(a.x, a.y);
    if (erase || b.tool == kClear) { ClearAt(p, a); return !(before == p); }
    if (OnCells(b.tool)) {
        if (!cell) return false;
        uint8_t kind = b.tool == kSquare ? wpz::kSquare : b.tool == kStar ? wpz::kStar : b.tool == kEliminator ? wpz::kNega : b.tool == kTriangle ? wpz::kTriangle
                     : b.negative ? wpz::kYlop : wpz::kPoly;
        uint32_t shape = (b.shape & 0xFFFF) | (b.rotate ? wpz::kRotBit : 0);
        if (b.tool == kTriangle && c.kind == wpz::kTriangle && c.colour == b.colour) { if (c.count >= 3) c = wpz::Cell(); else c.count++; return true; }
        if (c.kind == kind && c.colour == b.colour && (b.tool != kPoly || c.shape == shape)) { c = wpz::Cell(); return true; }
        c = wpz::Cell(); c.kind = kind; c.colour = b.colour;
        if (b.tool == kTriangle) c.count = 1;
        if (b.tool == kPoly) { if (!(shape & 0xFFFF)) return false; c.shape = shape; }
        return true;
    }
    if (cell) return false;
    bool corner = a.x % 2 == 0 && a.y % 2 == 0;
    int sx = a.x, sy = a.y; if (p.sym != wpz::kSymNone) p.symPos(a.x, a.y, &sx, &sy);
    bool mirror = p.sym != wpz::kSymNone && !(sx == a.x && sy == a.y);
    switch (b.tool) {
    case kDot: c.gap = wpz::kGapNone; c.dot = c.dot == b.dot ? (uint8_t)wpz::kDotNone : b.dot; break;
    case kGap: if (corner) return false; c.dot = wpz::kDotNone; c.gap = c.gap == wpz::kGapBreak ? wpz::kGapNone : wpz::kGapBreak; break;
    case kFullGap: if (corner) return false; c.dot = wpz::kDotNone; c.start = false; c.end = wpz::kEndNone; c.gap = c.gap == wpz::kGapFull ? wpz::kGapNone : wpz::kGapFull; break;
    case kStart:
        if (!corner) return false;
        c.start = !c.start;
#ifndef ED_MUT_NO_MIRROR
        if (mirror) p.at(sx, sy).start = c.start;
#endif
        break;
    case kEnd: {
        auto dirs = p.validEndDirs(a.x, a.y);
        if (dirs.empty()) return false;
        size_t k = 0; while (k < dirs.size() && dirs[k] != c.end) k++;
        c.end = !c.end ? dirs[0] : (k + 1 < dirs.size() ? dirs[k + 1] : (uint8_t)wpz::kEndNone);
        if (mirror) p.at(sx, sy).end = c.end ? p.symEnd(c.end) : (uint8_t)wpz::kEndNone;
        break;
    }
    default: return false;
    }
    return !(before == p);
}

// Symmetry chosen or changed: every start and end gets its mirrored one (the old mirrors of another symmetry are left; the player removes them).
inline void MirrorStartsAndEnds(wpz::Puzzle& p) {
    if (p.sym == wpz::kSymNone) return;
    for (int x = 0; x < p.width(); x++) for (int y = 0; y < p.height(); y++) {
        const wpz::Cell& c = p.at(x, y); if (!c.start && !c.end) continue;
        int sx, sy; p.symPos(x, y, &sx, &sy); wpz::Cell& m = p.at(sx, sy);
        if (c.start) m.start = true;
        if (c.end && !m.end) m.end = p.symEnd(c.end);
    }
}

// The line a player draws to try the puzzle: begun at a start point, then pulled along the grid toward the mouse one step at a time; going
// back over the last step takes it back. Never through a gap or a missing line, never over itself (or its mirror on symmetry puzzles).
struct Trace {
    std::vector<wpz::Pos> path;
    bool active() const { return !path.empty(); }
    bool begin(const wpz::Puzzle& p, wpz::Pos s) { path.clear(); if (!p.in(s.x, s.y) || !p.at(s.x, s.y).start) return false; path.push_back(s); return true; }
    bool used(const wpz::Puzzle& p, wpz::Pos q) const {
        for (auto& a : path) {
            if (a == q) return true;
            if (p.sym != wpz::kSymNone) { int sx, sy; p.symPos(a.x, a.y, &sx, &sy); if (sx == q.x && sy == q.y) return true; }
        }
        return false;
    }
    bool canStep(const wpz::Puzzle& p, wpz::Pos q) const {
        const wpz::Cell* c = p.get(q.x, q.y);
#ifndef ED_MUT_TRACE_THROUGH_GAP
        if (!c || c->kind != wpz::kLine || c->gap != wpz::kGapNone || used(p, q)) return false;
#else
        if (!c || c->kind != wpz::kLine || used(p, q)) return false;      // mutant: the line crosses gaps
#endif
        if (p.sym != wpz::kSymNone) {
            int sx, sy; p.symPos(q.x, q.y, &sx, &sy);
            if (sx == q.x && sy == q.y) return false;
            if (p.at(sx, sy).gap != wpz::kGapNone) return false;
        }
        return true;
    }
    // one step toward the target position (on the grid), or back; true when the line moved
    bool toward(const wpz::Puzzle& p, wpz::Pos t) {
        if (path.empty() || path.back() == t) return false;
        if (path.size() >= 2 && path[path.size() - 2] == t) { path.pop_back(); return true; }
        wpz::Pos h = path.back(); int dx = t.x - h.x, dy = t.y - h.y;
        wpz::Pos tries[2]; int n = 0;
        if (std::abs(dx) >= std::abs(dy)) { if (dx && h.y % 2 == 0) tries[n++] = {h.x + (dx > 0 ? 1 : -1), h.y}; if (dy && h.x % 2 == 0) tries[n++] = {h.x, h.y + (dy > 0 ? 1 : -1)}; }
        else { if (dy && h.x % 2 == 0) tries[n++] = {h.x, h.y + (dy > 0 ? 1 : -1)}; if (dx && h.y % 2 == 0) tries[n++] = {h.x + (dx > 0 ? 1 : -1), h.y}; }
        for (int i = 0; i < n; i++) {
            if (path.size() >= 2 && path[path.size() - 2] == tries[i]) { path.pop_back(); return true; }
            if (canStep(p, tries[i])) { path.push_back(tries[i]); return true; }
        }
        return false;
    }
    void pull(const wpz::Puzzle& p, wpz::Pos t) { for (int i = 0; i < 64 && toward(p, t); i++) {} }
    bool atEnd(const wpz::Puzzle& p) const { return !path.empty() && p.at(path.back().x, path.back().y).end != wpz::kEndNone; }
};

// The nearest corner or edge to a pixel (for the line being drawn).
inline wpz::Pos NearestLine(const Geometry& g, const wpz::Puzzle& p, int mx, int my) {
    wpz::Pos best{0, 0}; double bd = 1e18; double fx = (mx - g.ox) / (double)g.step, fy = (my - g.oy) / (double)g.step;
    for (int x = 0; x < p.width(); x++) for (int y = 0; y < p.height(); y++) {
        if (wpz::Puzzle::isCell(x, y)) continue;
        double d = (fx - x) * (fx - x) + (fy - y) * (fy - y);
        if (d < bd) { bd = d; best = {x, y}; }
    }
    return best;
}

// A short line about how many ways the puzzle can be solved (the editor's Check).
inline std::string CountText(const wpz::Solutions& s, size_t max) {
    if (s.lines.empty()) return s.complete ? "No solution: nobody can solve this puzzle." : "No solution found yet (the search was stopped: the puzzle is very open).";
    if (s.lines.size() >= max) return "More than " + std::to_string(max - 1) + " solutions.";
    std::string n = std::to_string(s.lines.size()) + (s.lines.size() == 1 ? " solution" : " solutions");
    return s.complete ? n + "." : "At least " + n + " (the search was stopped).";
}

}  // namespace ped
