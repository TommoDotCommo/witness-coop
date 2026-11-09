#pragma once
// A line puzzle of The Witness as the launcher's puzzle editor makes it and the game shows it: the model, the rules, a solver, and the text of a
// puzzle set. Game-free and header-only: the launcher, the DLL (proxy/randomizer/variants/custom.cpp) and the tests share it (common/wpuzzle_test.cpp).
//
// The model and the rules are a port of jbzdarkid's engine for witnesspuzzles.com (https://github.com/jbzdarkid/jbzdarkid.github.io, engine/puzzle.js,
// validate.js, polyominos.js, solve.js; BSD 3-Clause, copyright 2017 jbzdarkid; dist/THIRD-PARTY-NOTICES.txt), with its default settings (the game's
// rules): negations cancel negations, precise polyominoes, no custom mechanics, no pillars. The port keeps his structure and names so the two can be
// compared; tools/wpuzzle_diff.py checks it against his JavaScript on the same puzzles.
//
// A puzzle of W x H cells is a grid of (2W+1) x (2H+1) positions, as in his engine: corners (both even), edges (one odd), cells (both odd);
// y = 0 is the top. Lines (corners and edges) carry dots, gaps, start points and end points; cells carry the symbols.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace wpz {

enum Kind : uint8_t { kEmpty = 0, kLine, kSquare, kStar, kNega, kTriangle, kPoly, kYlop, kNonce };      // kNonce: a symbol switched off while negations are tried (keeps its colour)
enum Dot : uint8_t { kDotNone = 0, kDotBlack, kDotBlue, kDotYellow, kDotInvisible };
enum Gap : uint8_t { kGapNone = 0, kGapBreak, kGapFull };      // a break in the line; no line there at all
enum Line : uint8_t { kLineNone = 0, kLineBlack, kLineBlue, kLineYellow };
enum End : uint8_t { kEndNone = 0, kEndLeft, kEndRight, kEndTop, kEndBottom };
enum Sym : uint8_t { kSymNone = 0, kSymX, kSymY, kSymXY };      // the second line mirrors the first left-right (x), top-bottom (y), or both (rotated half a turn)

// The colours the game draws symbols in. The number is the game's own colour index (the low bits of a decoration): symbols of one number are one
// colour for the rules. The RGB is what the game shows (the generator's table, third_party/witness-randomizer/src/Panel.h get_color_rgb).
struct Colour { uint8_t index; const char* name; uint8_t r, g, b; };
inline const std::vector<Colour>& Colours() {
    static const std::vector<Colour> c = {{1, "black", 0, 0, 0}, {2, "white", 255, 255, 255}, {3, "red", 255, 0, 0}, {4, "purple", 128, 0, 255}, {5, "green", 0, 255, 0},
                                          {6, "cyan", 0, 255, 255}, {7, "magenta", 255, 0, 255}, {8, "yellow", 255, 255, 0}, {9, "blue", 0, 0, 255}, {10, "orange", 255, 128, 0}};
    return c;
}
inline const Colour* ColourByIndex(int i) { for (auto& c : Colours()) if (c.index == i) return &c; return nullptr; }
inline const Colour* ColourByName(const std::string& n) { for (auto& c : Colours()) if (n == c.name) return &c; return nullptr; }

// A polyomino: 4 x 4 bits, bit (x * 4 + y); kRotBit: it may be placed turned.
constexpr uint32_t kRotBit = 1u << 20;
inline uint32_t PolyMask(int x, int y) { return 1u << (x * 4 + y); }
inline bool PolySet(uint32_t s, int x, int y) { return x >= 0 && y >= 0 && x < 4 && y < 4 && (s & PolyMask(x, y)); }
inline int PolySize(uint32_t s) { int n = 0; for (int x = 0; x < 4; x++) for (int y = 0; y < 4; y++) n += PolySet(s, x, y); return n; }

struct Cell {
    uint8_t kind = kEmpty;
    uint8_t colour = 0;          // symbols: the game's colour index
    uint8_t count = 0;           // triangles: 1 to 3
    uint32_t shape = 0;          // polyominoes: the 4 x 4 bits and kRotBit
    uint8_t dot = kDotNone, gap = kGapNone, end = kEndNone;      // lines
    bool start = false;          // lines
    uint8_t line = kLineNone;    // lines: drawn by a solution (not part of the puzzle)
    bool operator==(const Cell& o) const { return kind == o.kind && colour == o.colour && count == o.count && shape == o.shape && dot == o.dot && gap == o.gap && end == o.end && start == o.start; }
    bool symbol() const { return kind >= kSquare && kind <= kYlop; }
};

constexpr int kMinSize = 1, kMaxSize = 7;      // cells across and down; 7 still reads well on the game's panels

struct Puzzle {
    std::string name;
    unsigned panel = 0;          // the game's panel this puzzle replaces (entity id), 0: the next free one in the order of the game
    int W = 0, H = 0;            // cells
    Sym sym = kSymNone;
    std::vector<Cell> g;         // (2W+1) * (2H+1), index x * height() + y
    int width() const { return 2 * W + 1; }
    int height() const { return 2 * H + 1; }
    bool in(int x, int y) const { return x >= 0 && y >= 0 && x < width() && y < height(); }
    Cell* get(int x, int y) { return in(x, y) ? &g[x * height() + y] : nullptr; }
    const Cell* get(int x, int y) const { return in(x, y) ? &g[x * height() + y] : nullptr; }
    Cell& at(int x, int y) { return g[x * height() + y]; }
    const Cell& at(int x, int y) const { return g[x * height() + y]; }
    static bool isCell(int x, int y) { return x % 2 == 1 && y % 2 == 1; }
    // a line value, or -1 where there is no line (a cell, outside), as his getLine
    int lineAt(int x, int y) const { const Cell* c = get(x, y); return (c && c->kind == kLine) ? c->line : -1; }
    void clearLines() { for (auto& c : g) c.line = kLineNone; }
    bool operator==(const Puzzle& o) const { return name == o.name && panel == o.panel && W == o.W && H == o.H && sym == o.sym && g == o.g; }

    // the mirrored position of the second line (his getSymmetricalPos, no pillars)
    void symPos(int x, int y, int* sx, int* sy) const {
        *sx = x; *sy = y;
        if (sym == kSymX || sym == kSymXY) *sx = width() - 1 - x;
        if (sym == kSymY || sym == kSymXY) *sy = height() - 1 - y;
    }
    uint8_t symEnd(uint8_t e) const {
        if (sym == kSymX || sym == kSymXY) { if (e == kEndLeft) return kEndRight; if (e == kEndRight) return kEndLeft; }
        if (sym == kSymY || sym == kSymXY) { if (e == kEndTop) return kEndBottom; if (e == kEndBottom) return kEndTop; }
        return e;
    }
    // end directions an end point at (x, y) may point (the outside, or a full gap), his getValidEndDirs
    std::vector<uint8_t> validEndDirs(int x, int y) const {
        std::vector<uint8_t> d; auto open = [&](int a, int b) { const Cell* c = get(a, b); return !c || c->gap == kGapFull; };
        if (open(x - 1, y)) d.push_back(kEndLeft);
        if (open(x, y - 1)) d.push_back(kEndTop);
        if (open(x + 1, y)) d.push_back(kEndRight);
        if (open(x, y + 1)) d.push_back(kEndBottom);
        return d;
    }
};

// An empty puzzle of W x H cells: a start at the bottom left, an end at the top right (as his editor's new puzzle).
inline Puzzle NewPuzzle(int W, int H, const std::string& name = "") {
    Puzzle p; p.W = W; p.H = H; p.name = name; p.g.assign(p.width() * p.height(), Cell());
    for (int x = 0; x < p.width(); x++) for (int y = 0; y < p.height(); y++) if (!Puzzle::isCell(x, y)) p.at(x, y).kind = kLine;
    p.at(0, p.height() - 1).start = true; p.at(p.width() - 1, 0).end = kEndRight;
    return p;
}

// Resize, keeping what still fits (corners stay corners). Ends left inside the grid lose their end.
inline Puzzle Resized(const Puzzle& p, int W, int H) {
    Puzzle q = NewPuzzle(W, H, p.name); q.panel = p.panel; q.sym = p.sym;
    for (auto& c : q.g) { c.start = false; c.end = kEndNone; }
    for (int x = 0; x < q.width() && x < p.width(); x++)
        for (int y = 0; y < q.height() && y < p.height(); y++) q.at(x, y) = p.at(x, y);
    for (int x = 0; x < q.width(); x++) for (int y = 0; y < q.height(); y++) {
        Cell& c = q.at(x, y); if (!c.end) continue;
        auto d = q.validEndDirs(x, y);
        if (d.empty()) c.end = kEndNone; else if (std::find(d.begin(), d.end(), c.end) == d.end()) c.end = d[0];
    }
    return q;
}

// ---- the rules (validate.js) --------------------------------------------------------------------------------------------------------------
struct Pos { int x, y; bool operator==(const Pos& o) const { return x == o.x && y == o.y; } };
struct RegionData {
    std::vector<Pos> invalid, veryInvalid;
    std::vector<std::pair<Pos, Pos>> negations;      // eliminator, what it removed
    bool valid() const { return invalid.empty() && veryInvalid.empty(); }
};

class Rules {
public:
    explicit Rules(Puzzle* p) : p_(p) {}
    Pos startPoint{-1, -1}, endPoint{-1, -1};      // of the line being checked (a start or end in the middle of an edge joins the regions on both sides)

    // the regions the line cuts the puzzle into (his getRegions / getRegion); each region lists its cells, edges and corners
    std::vector<std::vector<Pos>> regions() {
        mask(); std::vector<std::vector<Pos>> out;
        for (int x = 0; x < p_->width(); x++) for (int y = 0; y < p_->height(); y++) {
            if (m_[x * p_->height() + y] == kProcessed) continue;
            out.emplace_back(); fill(x, y, out.back());
        }
        return out;
    }
    bool region(int x, int y, std::vector<Pos>* out) {
        if (!p_->in(x, y)) return false;
        mask(); if (m_[x * p_->height() + y] == kProcessed) return false;
        out->clear(); fill(x, y, *out); return true;
    }

    // what the puzzle holds that the region checks need to know (his solve() and validate() both note it first)
    void scan() {
        hasNegations_ = hasPolys_ = false;
        for (auto& c : p_->g) { if (c.kind == kNega) hasNegations_ = true; if (c.kind == kPoly || c.kind == kYlop) hasPolys_ = true; }
    }

    RegionData validate(bool quick) {
        RegionData data; bool needsRegions = false; hasNegations_ = hasPolys_ = false;
        for (int x = 0; x < p_->width(); x++) for (int y = 0; y < p_->height(); y++) {
            const Cell& c = p_->at(x, y);
            if (c.kind == kEmpty) continue;
            if (!needsRegions && c.kind != kLine && c.kind != kTriangle) needsRegions = true;
            if (c.kind == kNega) hasNegations_ = true;
            if (c.kind == kPoly || c.kind == kYlop) hasPolys_ = true;
            if (c.kind == kLine && c.line > kLineNone) {
#ifndef WPZ_MUT_GAP_IGNORED
                if (c.gap > kGapNone) { data.invalid.push_back({x, y}); if (quick) return data; }
#endif
                if ((c.dot == kDotBlue && c.line == kLineYellow) || (c.dot == kDotYellow && c.line == kLineBlue)) { data.invalid.push_back({x, y}); if (quick) return data; }
            }
        }
        std::vector<std::vector<Pos>> rs;
        if (needsRegions) rs = regions();
        else {
            rs.emplace_back();
            for (int x = 0; x < p_->width(); x++) for (int y = 0; y < p_->height(); y++)
                if (Puzzle::isCell(x, y) || p_->at(x, y).line == kLineNone) rs.back().push_back({x, y});
        }
        for (auto& r : rs) {
            RegionData d = validateRegion(r, quick);
            data.negations.insert(data.negations.end(), d.negations.begin(), d.negations.end());
            data.invalid.insert(data.invalid.end(), d.invalid.begin(), d.invalid.end());
            data.invalid.insert(data.invalid.end(), d.veryInvalid.begin(), d.veryInvalid.end());
            if (quick && !data.valid()) break;
        }
        return data;
    }

    // a region with its eliminators (his validateRegion): eliminators first remove what nothing else could make right, then every pairing is tried
    RegionData validateRegion(const std::vector<Pos>& region, bool quick) {
        if (!hasNegations_) return regionCheck(region, quick);
        std::vector<Neg> negs;
        for (auto& pos : region) { Cell* c = p_->get(pos.x, pos.y); if (c && c->kind == kNega) { negs.push_back({pos, *c}); c->kind = kNonce; } }
        if (negs.empty()) return regionCheck(region, quick);
        RegionData d0 = regionCheck(region, false);
        for (auto& n : negs) p_->at(n.pos.x, n.pos.y).kind = kNega;
        std::vector<Neg> invalid, very;
        for (auto& e : d0.invalid) invalid.push_back({e, p_->at(e.x, e.y)});
        for (auto& e : d0.veryInvalid) very.push_back({e, p_->at(e.x, e.y)});
        std::vector<std::pair<Neg, Neg>> base;
        while (!negs.empty() && !very.empty()) {
            Neg s = negs.back(); negs.pop_back(); Neg t = very.back(); very.pop_back();
            clear(s.pos); clear(t.pos); base.push_back({s, t});
        }
        RegionData d = negations2(region, negs, invalid, 0, 0);
        for (auto& b : base) { p_->at(b.first.pos.x, b.first.pos.y) = b.first.cell; p_->at(b.second.pos.x, b.second.pos.y) = b.second.cell; d.negations.push_back({b.first.pos, b.second.pos}); }
        return d;
    }

private:
    enum M : uint8_t { kProcessed = 0, kNoncount, kCount, kGap2, kDotM };
    Puzzle* p_;
    std::vector<uint8_t> m_;
    bool hasNegations_ = false, hasPolys_ = false;
    struct Neg { Pos pos; Cell cell; };

    void clear(Pos p) { Cell& c = p_->at(p.x, p.y); c = Cell(); if (!Puzzle::isCell(p.x, p.y)) c.kind = kLine; }      // (his setCell(null); only symbols are ever removed)

    void mask() {
        const int W = p_->width(), H = p_->height(); m_.assign(W * H, kCount);
        for (int x = 0; x < W; x++) for (int y = 0; y < H; y++) {
            if (Puzzle::isCell(x, y)) continue;
            const Cell& c = p_->at(x, y); uint8_t& v = m_[x * H + y];
            if (c.line > kLineNone) v = kProcessed; else if (c.gap == kGapFull) v = kGap2; else if (c.dot > kDotNone) v = kDotM; else v = kCount;
        }
        if (p_->in(startPoint.x, startPoint.y) && startPoint.x % 2 != startPoint.y % 2) m_[startPoint.x * H + startPoint.y] = kNoncount;      // (not fat start points)
        if (p_->in(endPoint.x, endPoint.y) && endPoint.x % 2 != endPoint.y % 2) m_[endPoint.x * H + endPoint.y] = kNoncount;
        for (int x = 1; x < W; x += 2) { fillOutside(x, 0); fillOutside(x, H - 1); }
        for (int y = 1; y < H; y += 2) { fillOutside(0, y); fillOutside(W - 1, y); }
    }
    void fillOutside(int x, int y) {      // cells that reach the outside through a full gap belong to no region (his _floodFillOutside)
        const int H = p_->height(); uint8_t& v = m_[x * H + y];
        if (v == kProcessed) return;
        if (x % 2 != y % 2 && v != kGap2) return;
        if (x % 2 == 0 && y % 2 == 0 && v == kDotM) return;
        v = kProcessed;
        if (x % 2 == 0 && y % 2 == 0) return;
        if (y < H - 1) fillOutside(x, y + 1);
        if (y > 0) fillOutside(x, y - 1);
        if (x < p_->width() - 1) fillOutside(x + 1, y);
        if (x > 0) fillOutside(x - 1, y);
    }
    void fill(int x, int y, std::vector<Pos>& region) {
        const int H = p_->height(); uint8_t& v = m_[x * H + y];
        if (v == kProcessed) return;
        if (v != kNoncount) region.push_back({x, y});
        v = kProcessed;
        if (y < H - 1) fill(x, y + 1, region);
        if (y > 0) fill(x, y - 1, region);
        if (x < p_->width() - 1) fill(x + 1, y, region);
        if (x > 0) fill(x - 1, y, region);
    }

    RegionData negations2(const std::vector<Pos>& region, std::vector<Neg>& negs, std::vector<Neg>& invalid, size_t index, size_t index2) {
        if (index2 >= negs.size()) return regionCheck(region, false);
        if (index >= invalid.size()) {
            size_t i = index2;
            for (; i + 1 < negs.size(); i += 2) { clear(negs[i].pos); clear(negs[i + 1].pos); }      // negations cancel negations
            for (; i < negs.size(); i++) p_->at(negs[i].pos.x, negs[i].pos.y).kind = kNonce;
            RegionData d = regionCheck(region, false);
            i = index2;
            for (; i + 1 < negs.size(); i += 2) {
                p_->at(negs[i].pos.x, negs[i].pos.y) = negs[i].cell; p_->at(negs[i + 1].pos.x, negs[i + 1].pos.y) = negs[i + 1].cell;
                d.negations.push_back({negs[i].pos, negs[i + 1].pos});
            }
            for (; i < negs.size(); i++) { p_->at(negs[i].pos.x, negs[i].pos.y).kind = kNega; d.invalid.push_back(negs[i].pos); }
            return d;
        }
        Neg source = negs[index2++]; clear(source.pos);
        RegionData first, d; bool haveFirst = false;
        for (size_t i = index; i < invalid.size(); i++) {
            Neg& target = invalid[i];
            clear(target.pos);
            d = negations2(region, negs, invalid, i + 1, index2);
            p_->at(target.pos.x, target.pos.y) = target.cell;
            if (!haveFirst) { first = d; first.negations.push_back({source.pos, target.pos}); haveFirst = true; }
            if (d.valid()) { d.negations.push_back({source.pos, target.pos}); break; }
        }
        p_->at(source.pos.x, source.pos.y) = source.cell;
        return d.valid() ? d : first;
    }

    // one region without eliminators (his regionCheck)
    RegionData regionCheck(const std::vector<Pos>& region, bool quick) {
        RegionData d; std::vector<Pos> squares; std::vector<std::pair<Pos, int>> stars; int counts[16] = {0}; int squareColour = 0;
        for (auto& pos : region) {
            const Cell& c = p_->at(pos.x, pos.y);
            if (c.kind == kEmpty) continue;
            if (c.kind == kLine && c.dot > kDotNone) { d.veryInvalid.push_back(pos); if (quick) return d; }
            if (c.kind == kTriangle) {
                int n = (p_->lineAt(pos.x - 1, pos.y) > 0) + (p_->lineAt(pos.x + 1, pos.y) > 0) + (p_->lineAt(pos.x, pos.y - 1) > 0) + (p_->lineAt(pos.x, pos.y + 1) > 0);
#ifndef WPZ_MUT_TRIANGLES_IGNORED
                if (n != c.count) { d.veryInvalid.push_back(pos); if (quick) return d; }
#endif
            }
            if (c.kind != kLine && c.colour) {
                counts[c.colour & 15]++;
                if (c.kind == kSquare) { squares.push_back(pos); if (!squareColour) squareColour = c.colour; else if (squareColour != c.colour) squareColour = -1; }
                if (c.kind == kStar) stars.push_back({pos, c.colour & 15});
            }
        }
#ifndef WPZ_MUT_SQUARES_IGNORED
        if (squareColour == -1) { d.invalid.insert(d.invalid.end(), squares.begin(), squares.end()); if (quick) return d; }
#endif
        for (auto& s : stars) {
#ifndef WPZ_MUT_STARS_IGNORED
            if (counts[s.second] == 1) { d.veryInvalid.push_back(s.first); if (quick) return d; }
            else if (counts[s.second] > 2) { d.invalid.push_back(s.first); if (quick) return d; }
#endif
        }
        if (hasPolys_ && !polyFit(region)) {
            for (auto& pos : region) { const Cell& c = p_->at(pos.x, pos.y); if (c.kind == kPoly || c.kind == kYlop) { d.invalid.push_back(pos); if (quick) return d; } }
        }
        return d;
    }

    // ---- polyominos.js ---------------------------------------------------------------------------------------------------------------------
    std::vector<int> pg_;      // the region marked up for placing: -1 must be covered once, 0 is done, 1 covered twice
    static std::vector<uint32_t> rotations(uint32_t s) {
#ifndef WPZ_MUT_NO_ROTATION
        if (!(s & kRotBit)) return {s};
#else
        return {s & ~kRotBit};
#endif
        uint32_t r[4] = {0, 0, 0, 0};
        for (int x = 0; x < 4; x++) for (int y = 0; y < 4; y++) if (PolySet(s, x, y)) { r[0] ^= PolyMask(x, y); r[1] ^= PolyMask(y, 3 - x); r[2] ^= PolyMask(3 - x, 3 - y); r[3] ^= PolyMask(3 - y, x); }
        return {r[0], r[1], r[2], r[3]};
    }
    static std::vector<Pos> cellsOf(uint32_t s, bool ylop) {      // his polyominoFromPolyshape, precise
        int tx = -1, ty = -1;
        for (int y = 0; y < 4 && tx < 0; y++) for (int x = 0; x < 4; x++) if (PolySet(s, x, y)) { tx = x; ty = y; break; }
        std::vector<Pos> out; if (tx < 0) return out;
        for (int x = 0; x < 4; x++) for (int y = 0; y < 4; y++) {
            if (!PolySet(s, x, y)) continue;
            int bx = 2 * (x - tx), by = 2 * (y - ty); out.push_back({bx, by});
            if (ylop) {
                if (!PolySet(s, x - 1, y)) out.push_back({bx - 1, by});
                if (!PolySet(s, x, y - 1)) out.push_back({bx, by - 1});
                out.push_back({bx + 1, by}); out.push_back({bx, by + 1});
            } else {
                if (PolySet(s, x + 1, y)) out.push_back({bx + 1, by});
                if (PolySet(s, x, y + 1)) out.push_back({bx, by + 1});
            }
        }
        return out;
    }
    bool place(const std::vector<Pos>& cells, int x, int y, int sign) {
        const int H = p_->height();
        for (auto& c : cells) if (!p_->in(c.x + x, c.y + y)) return false;
        for (auto& c : cells) pg_[(c.x + x) * H + c.y + y] += sign;
        return true;
    }
    bool polyFit(const std::vector<Pos>& region) {
        std::vector<uint32_t> polys, ylops; int polyCount = 0, regionSize = 0;
        for (auto& pos : region) {
            if (Puzzle::isCell(pos.x, pos.y)) regionSize++;
            const Cell& c = p_->at(pos.x, pos.y);
            if ((c.kind != kPoly && c.kind != kYlop) || !(c.shape & ~kRotBit)) continue;
            if (c.kind == kPoly) { polys.push_back(c.shape); polyCount += PolySize(c.shape); }
            else { ylops.push_back(c.shape); polyCount -= PolySize(c.shape); }
        }
        if (polys.empty() && ylops.empty()) return true;
        if (polyCount > 0 && polyCount != regionSize) return false;
        if (polyCount < 0) return false;
        pg_.assign(p_->width() * p_->height(), 0);
        if (polyCount > 0) for (auto& pos : region) pg_[pos.x * p_->height() + pos.y] = -1;
        return placeYlops(ylops, 0, polys);
    }
    bool placeYlops(const std::vector<uint32_t>& ylops, size_t i, std::vector<uint32_t>& polys) {
        if (i == ylops.size()) return placePolys(polys);
        for (int x = 1; x < p_->width(); x += 2) for (int y = 1; y < p_->height(); y += 2)
            for (uint32_t s : rotations(ylops[i])) {
                auto cells = cellsOf(s, true);
                if (!place(cells, x, y, -1)) continue;
                if (placeYlops(ylops, i + 1, polys)) return true;
                place(cells, x, y, +1);
            }
        return false;
    }
    bool placePolys(std::vector<uint32_t>& polys) {
        const int W = p_->width(), H = p_->height(); bool allPlaced = polys.empty();
        for (int x = 0; x < W; x++) for (int y = 0; y < H; y++) {
            int v = pg_[x * H + y];
            if (v > 0) return false;
            if (allPlaced && v < 0 && Puzzle::isCell(x, y)) return false;
        }
        if (allPlaced) return true;
        int ox = -1, oy = -1;
        for (int y = 1; y < H && ox < 0; y += 2) for (int x = 1; x < W; x += 2) if (pg_[x * H + y] < 0) { ox = x; oy = y; break; }
        if (ox < 0) return false;
        std::vector<uint32_t> tried;
        for (size_t i = 0; i < polys.size(); i++) {
            uint32_t poly = polys[i];
            if (std::find(tried.begin(), tried.end(), poly) != tried.end()) continue;
            tried.push_back(poly); polys.erase(polys.begin() + i);
            for (uint32_t s : rotations(poly)) {
                auto cells = cellsOf(s, false);
                if (!place(cells, ox, oy, +1)) continue;
                if (placePolys(polys)) return true;
                place(cells, ox, oy, -1);
            }
            polys.insert(polys.begin() + i, poly);
        }
        return false;
    }
};

// ---- a line through the puzzle ------------------------------------------------------------------------------------------------------------
// A line is the positions it passes, from a start point to an end point, one step apart (corners and edges). Draw puts it into the puzzle's lines
// (the mirrored line too, on symmetry puzzles); Check says whether the puzzle accepts it.
inline bool Draw(Puzzle& p, const std::vector<Pos>& path) {
    p.clearLines();
    if (path.empty()) return false;
    for (size_t i = 0; i < path.size(); i++) {
        const Pos& a = path[i]; Cell* c = p.get(a.x, a.y);
        if (!c || c->kind != kLine) return false;
        if (i && std::abs(a.x - path[i - 1].x) + std::abs(a.y - path[i - 1].y) != 1) return false;
        if (p.sym == kSymNone) { if (c->line) return false; c->line = kLineBlack; }
        else {
            int sx, sy; p.symPos(a.x, a.y, &sx, &sy);
            if (sx == a.x && sy == a.y) return false;
            if (c->line) return false;
            c->line = kLineBlue; Cell& s = p.at(sx, sy); if (s.line) return false; s.line = kLineYellow;
        }
    }
    return true;
}
struct Verdict { bool ok = false; std::string why; RegionData data; };
inline Verdict Check(const Puzzle& puzzle, const std::vector<Pos>& path) {
    Verdict v; Puzzle p = puzzle;
    if (path.empty()) { v.why = "no line"; return v; }
    const Cell* s = p.get(path.front().x, path.front().y); const Cell* e = p.get(path.back().x, path.back().y);
    if (!s || !s->start) { v.why = "the line does not begin at a start point"; return v; }
    if (!e || !e->end) { v.why = "the line does not end at an end point"; return v; }
    if (!Draw(p, path)) { v.why = "the line leaves the grid, jumps or crosses itself"; return v; }
    if (p.sym != kSymNone) {      // the mirrored line must start and end too
        int sx, sy, ex, ey; p.symPos(path.front().x, path.front().y, &sx, &sy); p.symPos(path.back().x, path.back().y, &ex, &ey);
        if (!p.at(sx, sy).start || !p.at(ex, ey).end) { v.why = "the mirrored line has no start or end there"; return v; }
        for (auto& a : path) { int x, y; p.symPos(a.x, a.y, &x, &y); if (p.at(x, y).gap) { v.why = "the mirrored line crosses a gap"; return v; } }
    }
    Rules r(&p); r.startPoint = path.front(); r.endPoint = path.back();
    v.data = r.validate(false); v.ok = v.data.valid();
    if (!v.ok) v.why = "a rule is broken";
    return v;
}

// ---- the solver (solve.js) ----------------------------------------------------------------------------------------------------------------
// Every line that solves the puzzle, up to `max`; `limit` caps the positions tried and `ms` the time (0: no cap), so a huge empty puzzle or a
// heap of polyominoes cannot hang the launcher (complete is then false). `cancel`, when set by another thread, stops it too.
// Prunes as his solver does: when the line runs from one edge of the grid back to an edge, the side it left is closed and is checked at once.
struct Solutions { std::vector<std::vector<Pos>> lines; bool complete = true; long long tried = 0; };
class Solver {
public:
    Solver(const Puzzle& p, size_t max, long long limit, int ms = 0, const volatile bool* cancel = nullptr) : p_(p), r_(&p_), max_(max), limit_(limit), ms_(ms), cancel_(cancel) {}
    Solutions run() {
        std::vector<Pos> starts; numEnds_ = 0;
        for (int x = 0; x < p_.width(); x++) for (int y = 0; y < p_.height(); y++) { const Cell& c = p_.at(x, y); if (c.start) starts.push_back({x, y}); if (c.end) numEnds_++; }
        p_.clearLines();
#ifndef WPZ_MUT_PRUNE_UNSCANNED
        r_.scan();
#endif
        for (auto& s : starts) { if (stop()) break; path_.clear(); r_.startPoint = s; Exit e; loop(s.x, s.y, numEnds_, e); }
        return out_;
    }
private:
    struct Exit { bool leftEdge = false; Pos prev{0, 0}; bool prevEdge = false; Pos cur{0, 0}; bool curEdge = false; };
    Puzzle p_; Rules r_; size_t max_; long long limit_; int ms_; const volatile bool* cancel_; int numEnds_ = 0;
    std::chrono::steady_clock::time_point t0_ = std::chrono::steady_clock::now();
    std::vector<Pos> path_; Solutions out_;
    bool stop() {
        if (out_.lines.size() >= max_) return true;
        if (!out_.complete) return true;
        if ((limit_ && out_.tried >= limit_) || (cancel_ && *cancel_)) { out_.complete = false; return true; }
        if (ms_ && (out_.tried & 255) == 0 && std::chrono::steady_clock::now() - t0_ > std::chrono::milliseconds(ms_)) { out_.complete = false; return true; }
        return false;
    }
    void unset(int x, int y) { p_.at(x, y).line = kLineNone; if (p_.sym != kSymNone) { int sx, sy; p_.symPos(x, y, &sx, &sy); p_.at(sx, sy).line = kLineNone; } }
    void loop(int x, int y, int numEnds, const Exit& ex) {
        if (stop()) return;
        Cell* c = p_.get(x, y);
        if (!c || c->kind != kLine || c->gap > kGapNone || c->line != kLineNone) return;
        if (p_.sym == kSymNone) c->line = kLineBlack;
        else {
            int sx, sy; p_.symPos(x, y, &sx, &sy);
            if (sx == x && sy == y) return;
            Cell& s = p_.at(sx, sy); if (s.gap > kGapNone || s.line != kLineNone) return;      // (his solver does not test the mirror's own line: it cannot be set there without this one)
            c->line = kLineBlue; s.line = kLineYellow;
        }
        out_.tried++; path_.push_back({x, y});
        if (c->end) {
            bool ok = true;
            if (p_.sym != kSymNone) { int sx, sy; p_.symPos(x, y, &sx, &sy); ok = p_.at(sx, sy).end != kEndNone; int ax, ay; p_.symPos(path_[0].x, path_[0].y, &ax, &ay); ok = ok && p_.at(ax, ay).start; }
            r_.endPoint = {x, y};
            if (ok && r_.validate(true).valid()) out_.lines.push_back(path_);
            r_.endPoint = {-1, -1};
            if (--numEnds == 0) { path_.pop_back(); unset(x, y); return; }
        }
        bool isEdge = x <= 0 || y <= 0 || x >= p_.width() - 1 || y >= p_.height() - 1;
        Exit nx; nx.leftEdge = ex.leftEdge || (!isEdge && ex.curEdge); nx.prev = ex.cur; nx.prevEdge = ex.curEdge; nx.cur = {x, y}; nx.curEdge = isEdge;
        if (ex.leftEdge && !ex.prevEdge && ex.curEdge && isEdge && path_.size() > 2) {
            std::vector<Pos> region;
            if (r_.region(ex.cur.x + (ex.prev.x - x), ex.cur.y + (ex.prev.y - y), &region)) {
                if (!r_.validateRegion(region, true).valid()) { path_.pop_back(); unset(x, y); return; }
                for (auto& q : region) if (p_.at(q.x, q.y).end) numEnds--;
                if (numEnds <= 0) { path_.pop_back(); unset(x, y); return; }
            }
        }
        if (y % 2 == 0) { loop(x - 1, y, numEnds, nx); loop(x + 1, y, numEnds, nx); }
        if (x % 2 == 0) { loop(x, y - 1, numEnds, nx); loop(x, y + 1, numEnds, nx); }
        path_.pop_back(); unset(x, y);
    }
};
inline Solutions Solve(const Puzzle& p, size_t max = 1, long long limit = 20000000, int ms = 0) { return Solver(p, max, limit, ms).run(); }

// ---- what the game can show ---------------------------------------------------------------------------------------------------------------
// Why the puzzle cannot be put on a panel, or empty. (The editor only makes puzzles that can; an imported one may not.)
inline std::vector<std::string> Problems(const Puzzle& p) {
    std::vector<std::string> out; int starts = 0, ends = 0;
    if (p.W < kMinSize || p.H < kMinSize || p.W > kMaxSize || p.H > kMaxSize) out.push_back("the grid must be 1 to 7 cells across and down");
    if ((int)p.g.size() != p.width() * p.height()) { out.push_back("the grid is damaged"); return out; }
    if (p.W * p.H < 2) out.push_back("the grid must have at least 2 cells");
    for (int x = 0; x < p.width(); x++) for (int y = 0; y < p.height(); y++) {
        const Cell& c = p.at(x, y);
        if (Puzzle::isCell(x, y) ? c.kind == kLine : c.kind != kLine) { out.push_back("the grid is damaged"); return out; }
        if (c.symbol() && !ColourByIndex(c.colour)) { out.push_back("a symbol has no colour the game knows"); }
        if (c.kind == kTriangle && (c.count < 1 || c.count > 3)) out.push_back("a triangle must count 1, 2 or 3");
        if ((c.kind == kPoly || c.kind == kYlop) && !(c.shape & 0xFFFF)) out.push_back("a polyomino has no squares");
        if (c.start) { starts++; if (x % 2 || y % 2) out.push_back("a start point must be on a corner of the grid (the game's panels cannot start in the middle of a line)"); }
        if (c.end) { ends++; auto d = p.validEndDirs(x, y); if (std::find(d.begin(), d.end(), c.end) == d.end() || (x % 2 && y % 2)) out.push_back("an end point must be on the edge of the grid and point out of it"); }
        if (p.sym != kSymNone && (c.start || c.end)) {
            int sx, sy; p.symPos(x, y, &sx, &sy);
            if (c.start && !p.at(sx, sy).start) out.push_back("on a symmetry puzzle every start point needs its mirrored one");
            if (c.end && p.at(sx, sy).end != p.symEnd(c.end)) out.push_back("on a symmetry puzzle every end point needs its mirrored one");
        }
        if ((c.dot == kDotBlue || c.dot == kDotYellow) && p.sym == kSymNone) out.push_back("blue and yellow dots need a symmetry puzzle");
#ifndef WPZ_MUT_CORNER_GAP
        if (c.gap && x % 2 == 0 && y % 2 == 0) out.push_back("a gap must be on a line between two corners (the game shows none on a corner)");
#endif
    }
    if (!starts) out.push_back("the puzzle needs a start point");
    if (!ends) out.push_back("the puzzle needs an end point");
    std::sort(out.begin(), out.end()); out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// ---- the text of a puzzle set ------------------------------------------------------------------------------------------------------------
// One file per set, UTF-8 text, written by Print and read by Parse; the same set always prints the same text, and its Hash names it (the lobby sends
// the set by that name, each game keeps it by that name). Every line is a keyword and its values:
//   witness-coop puzzles 1
//   set My puzzles                         the set's name
//   puzzle 4 4 none 0x00064 Tutorial 1     cells across, down, symmetry (none, x, y, xy), the panel (0: the next free one), the puzzle's name
//   at 0 8 start                           a start point on the line at x = 0, y = 8 (positions as in the model above)
//   at 8 0 end right                       an end point
//   at 3 0 dot black | gap break | gap full
//   at 1 1 square white | star orange | eliminator white | triangle orange 2 | poly yellow 0x0033 rotated | ylop blue 0x0001
struct Set { std::string name; std::vector<Puzzle> puzzles; bool operator==(const Set& o) const { return name == o.name && puzzles == o.puzzles; } };

inline const char* SymName(Sym s) { return s == kSymX ? "x" : s == kSymY ? "y" : s == kSymXY ? "xy" : "none"; }
inline const char* EndName(uint8_t e) { return e == kEndLeft ? "left" : e == kEndRight ? "right" : e == kEndTop ? "top" : e == kEndBottom ? "bottom" : ""; }
inline const char* DotName(uint8_t d) { return d == kDotBlack ? "black" : d == kDotBlue ? "blue" : d == kDotYellow ? "yellow" : d == kDotInvisible ? "invisible" : ""; }
inline const char* KindName(uint8_t k) { return k == kSquare ? "square" : k == kStar ? "star" : k == kNega ? "eliminator" : k == kTriangle ? "triangle" : k == kPoly ? "poly" : k == kYlop ? "ylop" : ""; }
inline std::string OneLine(const std::string& s) { std::string r; for (char c : s) r += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c; return r; }

inline std::string Print(const Set& s) {
    std::string out = "witness-coop puzzles 1\nset " + OneLine(s.name) + "\n"; char b[160];
    for (auto& p : s.puzzles) {
        snprintf(b, sizeof b, "puzzle %d %d %s 0x%05X ", p.W, p.H, SymName(p.sym), p.panel); out += b; out += OneLine(p.name); out += "\n";
        for (int x = 0; x < p.width(); x++) for (int y = 0; y < p.height(); y++) {
            const Cell& c = p.at(x, y);
            auto at = [&](const std::string& what) { snprintf(b, sizeof b, "at %d %d ", x, y); out += b; out += what; out += "\n"; };
            if (c.start) at("start");
            if (c.end) at(std::string("end ") + EndName(c.end));
            if (c.dot) at(std::string("dot ") + DotName(c.dot));
            if (c.gap) at(c.gap == kGapFull ? "gap full" : "gap break");
            if (c.symbol()) {
                const Colour* col = ColourByIndex(c.colour); std::string w = std::string(KindName(c.kind)) + " " + (col ? col->name : "black");
                if (c.kind == kTriangle) w += " " + std::to_string(c.count);
                if (c.kind == kPoly || c.kind == kYlop) {
#ifndef WPZ_MUT_PRINT_NO_ROTATION
                    snprintf(b, sizeof b, " 0x%04X%s", c.shape & 0xFFFF, (c.shape & kRotBit) ? " rotated" : "");
#else
                    snprintf(b, sizeof b, " 0x%04X", c.shape & 0xFFFF);
#endif
                    w += b;
                }
                at(w);
            }
        }
    }
    return out;
}

inline bool Parse(const std::string& text, Set* out, std::string* err = nullptr) {
    Set s; Puzzle* cur = nullptr; size_t pos = 0; int lineNo = 0; bool header = false;
    auto fail = [&](const std::string& why) { if (err) *err = "line " + std::to_string(lineNo) + ": " + why; return false; };
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos); if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(pos, nl - pos); pos = nl + 1; lineNo++;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) { if (nl == text.size()) break; continue; }
        std::vector<std::string> t; { size_t i = 0; while (i < line.size()) { while (i < line.size() && line[i] == ' ') i++; size_t j = i; while (j < line.size() && line[j] != ' ') j++; if (j > i) t.push_back(line.substr(i, j - i)); i = j; } }
        auto rest = [&](size_t n) { size_t i = 0; for (size_t k = 0; k < n; k++) { while (i < line.size() && line[i] == ' ') i++; while (i < line.size() && line[i] != ' ') i++; } if (i < line.size()) i++; return i < line.size() ? line.substr(i) : std::string(); };
        if (!header) { if (line != "witness-coop puzzles 1") return fail("not a puzzle set of this version"); header = true; continue; }
        if (t[0] == "set") { s.name = rest(1); continue; }
        if (t[0] == "puzzle") {
            if (t.size() < 5) return fail("puzzle needs: cells across, down, symmetry, panel");
            int W = atoi(t[1].c_str()), H = atoi(t[2].c_str());
            if (W < kMinSize || H < kMinSize || W > kMaxSize || H > kMaxSize) return fail("the grid must be 1 to 7 cells across and down");
            Puzzle p = NewPuzzle(W, H, rest(5)); for (auto& c : p.g) { c.start = false; c.end = kEndNone; }
            p.sym = t[3] == "x" ? kSymX : t[3] == "y" ? kSymY : t[3] == "xy" ? kSymXY : kSymNone;
            if (p.sym == kSymNone && t[3] != "none") return fail("unknown symmetry " + t[3]);
            p.panel = (unsigned)strtoul(t[4].c_str(), nullptr, 16);
            s.puzzles.push_back(p); cur = &s.puzzles.back(); continue;
        }
        if (t[0] == "at") {
            if (!cur) return fail("at before a puzzle");
            if (t.size() < 4) return fail("at needs: x, y, what");
            int x = atoi(t[1].c_str()), y = atoi(t[2].c_str());
            if (!cur->in(x, y)) return fail("outside the grid");
            Cell& c = cur->at(x, y); const std::string& w = t[3]; bool lineCell = !Puzzle::isCell(x, y);
            if (w == "start" && lineCell) c.start = true;
            else if (w == "end" && lineCell && t.size() >= 5) { c.end = t[4] == "left" ? kEndLeft : t[4] == "right" ? kEndRight : t[4] == "top" ? kEndTop : t[4] == "bottom" ? kEndBottom : 0; if (!c.end) return fail("unknown end direction"); }
            else if (w == "dot" && lineCell && t.size() >= 5) { c.dot = t[4] == "black" ? kDotBlack : t[4] == "blue" ? kDotBlue : t[4] == "yellow" ? kDotYellow : t[4] == "invisible" ? kDotInvisible : 0; if (!c.dot) return fail("unknown dot"); }
            else if (w == "gap" && lineCell && t.size() >= 5) { c.gap = t[4] == "full" ? kGapFull : t[4] == "break" ? kGapBreak : 0; if (!c.gap) return fail("unknown gap"); }
            else if (!lineCell && t.size() >= 5 && (w == "square" || w == "star" || w == "eliminator" || w == "triangle" || w == "poly" || w == "ylop")) {
                const Colour* col = ColourByName(t[4]); if (!col) return fail("unknown colour " + t[4]);
                c.kind = w == "square" ? kSquare : w == "star" ? kStar : w == "eliminator" ? kNega : w == "triangle" ? kTriangle : w == "poly" ? kPoly : kYlop; c.colour = col->index;
                if (c.kind == kTriangle) { if (t.size() < 6) return fail("a triangle needs its count"); c.count = (uint8_t)atoi(t[5].c_str()); if (c.count < 1 || c.count > 3) return fail("a triangle counts 1 to 3"); }
                if (c.kind == kPoly || c.kind == kYlop) { if (t.size() < 6) return fail("a polyomino needs its shape"); c.shape = (uint32_t)strtoul(t[5].c_str(), nullptr, 16) & 0xFFFF; if (!c.shape) return fail("an empty polyomino"); if (t.size() >= 7 && t[6] == "rotated") c.shape |= kRotBit; }
            } else return fail("unknown: " + line);
            continue;
        }
        return fail("unknown line: " + t[0]);
    }
    if (!header) return fail("empty");
    *out = s; return true;
}

// FNV-1a 64 of the printed set, as 16 hex digits: the set's name in the lobby, in the cfg (randomizer=custom;set:<hash>) and as its file name.
inline std::string Hash(const std::string& printed) {
    uint64_t h = 1469598103934665603ULL; for (unsigned char c : printed) { h ^= c; h *= 1099511628211ULL; }
    char b[17]; snprintf(b, sizeof b, "%016llx", (unsigned long long)h); return b;
}
inline std::string Hash(const Set& s) { return Hash(Print(s)); }

}  // namespace wpz
