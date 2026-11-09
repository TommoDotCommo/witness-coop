#pragma once
// witnesspuzzles.com's puzzle text (the JSON of jbzdarkid's Puzzle.serialize, what his editor's Import and Export show and what each published
// puzzle's page holds) to and from the model of common/wpuzzle.h. Import takes his old layouts too (legacy keys, as his deserialize does) and says
// what the game cannot show; Export writes what his editor's Import reads.
#include "wpuzzle.h"
#include <cmath>
#include <map>
#include <memory>

namespace wpz {

// ---- a small JSON reader (objects, arrays, strings, numbers, true, false, null) ---------------------------------------------------------------
struct Json {
    enum T { kNull, kBool, kNum, kStr, kArr, kObj } t = kNull;
    bool b = false; double n = 0; std::string s; std::vector<Json> a; std::vector<std::pair<std::string, Json>> o;
    const Json* get(const char* k) const { if (t != kObj) return nullptr; for (auto& p : o) if (p.first == k) return &p.second; return nullptr; }
    bool truthy() const { return t == kBool ? b : t == kNum ? n != 0 : t == kStr ? !s.empty() : t == kArr || t == kObj; }
    double num(double d = 0) const { return t == kNum ? n : t == kBool ? (b ? 1 : 0) : d; }
};
class JsonReader {
public:
    explicit JsonReader(const std::string& s) : s_(s) {}
    bool read(Json* out) { i_ = 0; depth_ = 0; if (!value(out)) return false; ws(); return i_ == s_.size(); }
private:
    const std::string& s_; size_t i_ = 0; int depth_ = 0;
    void ws() { while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\n' || s_[i_] == '\r' || s_[i_] == '\t')) i_++; }
    bool lit(const char* w) { size_t n = strlen(w); if (s_.compare(i_, n, w) != 0) return false; i_ += n; return true; }
    bool str(std::string* out) {
        if (s_[i_] != '"') return false;
        i_++;
        while (i_ < s_.size() && s_[i_] != '"') {
            char c = s_[i_++];
            if (c != '\\') { *out += c; continue; }
            if (i_ >= s_.size()) return false;
            char e = s_[i_++];
            if (e == 'n') *out += '\n'; else if (e == 't') *out += '\t'; else if (e == 'r') *out += '\r'; else if (e == 'b') *out += '\b'; else if (e == 'f') *out += '\f';
            else if (e == 'u') {
                if (i_ + 4 > s_.size()) return false;
                unsigned v = (unsigned)strtoul(s_.substr(i_, 4).c_str(), nullptr, 16); i_ += 4;
                if (v < 0x80) *out += (char)v; else if (v < 0x800) { *out += (char)(0xC0 | v >> 6); *out += (char)(0x80 | (v & 63)); }
                else { *out += (char)(0xE0 | v >> 12); *out += (char)(0x80 | ((v >> 6) & 63)); *out += (char)(0x80 | (v & 63)); }
            } else *out += e;
        }
        if (i_ >= s_.size()) return false;
        i_++; return true;
    }
    bool value(Json* v) {
        ws(); if (i_ >= s_.size() || ++depth_ > 64) return false;
        char c = s_[i_]; bool ok = false;
        if (c == '{') {
            v->t = Json::kObj; i_++; ws();
            if (i_ < s_.size() && s_[i_] == '}') { i_++; ok = true; }
            else for (;;) {
                ws(); std::string k; if (i_ >= s_.size() || !str(&k)) break;
                ws(); if (i_ >= s_.size() || s_[i_] != ':') break; i_++;
                Json x; if (!value(&x)) break; v->o.push_back({k, std::move(x)});
                ws(); if (i_ < s_.size() && s_[i_] == ',') { i_++; continue; }
                if (i_ < s_.size() && s_[i_] == '}') { i_++; ok = true; }
                break;
            }
        } else if (c == '[') {
            v->t = Json::kArr; i_++; ws();
            if (i_ < s_.size() && s_[i_] == ']') { i_++; ok = true; }
            else for (;;) {
                Json x; if (!value(&x)) break; v->a.push_back(std::move(x));
                ws(); if (i_ < s_.size() && s_[i_] == ',') { i_++; continue; }
                if (i_ < s_.size() && s_[i_] == ']') { i_++; ok = true; }
                break;
            }
        } else if (c == '"') { v->t = Json::kStr; ok = str(&v->s); }
        else if (lit("true")) { v->t = Json::kBool; v->b = true; ok = true; }
        else if (lit("false")) { v->t = Json::kBool; ok = true; }
        else if (lit("null")) { v->t = Json::kNull; ok = true; }
        else { char* end = nullptr; v->n = strtod(s_.c_str() + i_, &end); size_t used = end - (s_.c_str() + i_); if (used) { v->t = Json::kNum; i_ += used; ok = true; } }
        depth_--; return ok;
    }
};
inline std::string JsonQuote(const std::string& s) {
    std::string o = "\""; char b[8];
    for (unsigned char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += c; } else if (c < 32) { snprintf(b, sizeof b, "\\u%04x", c); o += b; } else o += c; }
    return o + "\"";
}

// ---- colours -------------------------------------------------------------------------------------------------------------------------------
// His colours are CSS: a name, #rgb, #rrggbb, rgb(...) or rgba(...). The nearest of the game's colours is used.
inline bool CssRgb(const std::string& in, int* r, int* g, int* b) {
    std::string s; for (char c : in) if (c != ' ') s += (char)tolower((unsigned char)c);
    static const std::map<std::string, uint32_t> names = {{"black", 0x000000}, {"white", 0xFFFFFF}, {"red", 0xFF0000}, {"orange", 0xFFA500}, {"yellow", 0xFFFF00},
        {"green", 0x008000}, {"lime", 0x00FF00}, {"blue", 0x0000FF}, {"purple", 0x800080}, {"cyan", 0x00FFFF}, {"aqua", 0x00FFFF}, {"magenta", 0xFF00FF},
        {"fuchsia", 0xFF00FF}, {"gray", 0x808080}, {"grey", 0x808080}, {"pink", 0xFFC0CB}, {"brown", 0xA52A2A}, {"violet", 0xEE82EE}, {"navy", 0x000080},
        {"teal", 0x008080}, {"olive", 0x808000}, {"maroon", 0x800000}, {"gold", 0xFFD700}, {"silver", 0xC0C0C0}};
    auto it = names.find(s);
    if (it != names.end()) { *r = it->second >> 16; *g = (it->second >> 8) & 255; *b = it->second & 255; return true; }
    if (!s.empty() && s[0] == '#') {
        std::string h = s.substr(1);
        if (h.size() == 3 || h.size() == 4) { auto d = [&](char c) { int v = (int)strtol(std::string(1, c).c_str(), nullptr, 16); return v * 17; }; *r = d(h[0]); *g = d(h[1]); *b = d(h[2]); return true; }
        if (h.size() == 6 || h.size() == 8) { unsigned long v = strtoul(h.substr(0, 6).c_str(), nullptr, 16); *r = (int)(v >> 16); *g = (int)((v >> 8) & 255); *b = (int)(v & 255); return true; }
        return false;
    }
    if (s.rfind("rgb", 0) == 0) {      // rgb(r,g,b) or rgba(r,g,b,a), decimal
        size_t o = s.find('('); if (o == std::string::npos) return false;
        int v[3]; const char* p = s.c_str() + o + 1;
        for (int k = 0; k < 3; k++) { char* e; v[k] = (int)strtol(p, &e, 10); if (e == p) return false; p = e; while (*p == ',') p++; }
        *r = v[0]; *g = v[1]; *b = v[2]; return true;
    }
    return false;
}
inline const Colour* NearestColour(const std::string& css) {
    for (auto& c : Colours()) if (css == c.name) return &c;      // the game's own names first (what Export writes)
    int r, g, b; if (!CssRgb(css, &r, &g, &b)) return nullptr;
    const Colour* best = nullptr; long bd = 0;
    for (auto& c : Colours()) { long d = (long)(r - c.r) * (r - c.r) + (long)(g - c.g) * (g - c.g) + (long)(b - c.b) * (b - c.b); if (!best || d < bd) { best = &c; bd = d; } }
    return best;
}

// ---- import --------------------------------------------------------------------------------------------------------------------------------
struct Imported { bool ok = false; Puzzle puzzle; std::vector<std::string> problems; std::string error; };
inline Imported ImportJson(const std::string& text) {
    Imported r; Json j;
    std::string t = text; while (!t.empty() && (t.back() == ' ' || t.back() == '\n' || t.back() == '\r')) t.pop_back();
    if (!JsonReader(t).read(&j) || j.t != Json::kObj) { r.error = "this is not a puzzle from witnesspuzzles.com (its Export text)"; return r; }
    const Json* grid = j.get("grid");
    if (!grid || grid->t != Json::kArr || grid->a.empty() || grid->a[0].t != Json::kArr) { r.error = "the puzzle has no grid"; return r; }
    int gw = (int)grid->a.size(), gh = (int)grid->a[0].a.size();
    if (j.get("pillar") && j.get("pillar")->truthy()) { r.error = "pillar puzzles (that wrap around) cannot be put on a flat panel"; return r; }
    if (gw % 2 == 0 || gh % 2 == 0 || gw < 3 || gh < 3) { r.error = "the grid has an odd shape"; return r; }
    int W = (gw - 1) / 2, H = (gh - 1) / 2;
    if (W > 10 || H > 10) { r.error = "the grid is larger than 10 cells"; return r; }
    Puzzle p = NewPuzzle(W, H); for (auto& c : p.g) { c.start = false; c.end = kEndNone; }
    if (const Json* n = j.get("name")) if (n->t == Json::kStr) p.name = n->s;
    std::map<std::string, int> colourOf; std::map<int, std::string> sourceOf; bool unknownSymbol = false, unknownColour = false, merged = false;
    auto colour = [&](const Json* c) -> uint8_t {
        std::string css = (c && c->t == Json::kStr) ? c->s : "black";
        const Colour* col = NearestColour(css);
        if (!col) { unknownColour = true; return 1; }
        auto it = sourceOf.find(col->index);
        if (it != sourceOf.end() && it->second != css) merged = true;
        sourceOf[col->index] = css; return col->index;
    };
    for (int x = 0; x < gw; x++) {
        const Json& colm = grid->a[x];
        if (colm.t != Json::kArr || (int)colm.a.size() != gh) { r.error = "the grid's columns differ in length"; return r; }
        for (int y = 0; y < gh; y++) {
            const Json& c = colm.a[y]; Cell& out = p.at(x, y);
            if (c.t != Json::kObj) continue;      // null, false: empty
            const Json* type = c.get("type");
            std::string ty = (type && type->t == Json::kStr) ? type->s : "";
            if (Puzzle::isCell(x, y)) {
                if (ty.empty() || ty == "nonce") continue;
                if (ty == "square") out.kind = kSquare; else if (ty == "star") out.kind = kStar; else if (ty == "nega") out.kind = kNega;
                else if (ty == "triangle") { out.kind = kTriangle; out.count = (uint8_t)(c.get("count") ? c.get("count")->num(1) : 1); }
                else if (ty == "poly" || ty == "ylop") {
                    out.kind = ty == "poly" ? kPoly : kYlop;
                    double s = c.get("polyshape") ? c.get("polyshape")->num(0) : 0; out.shape = (uint32_t)(int64_t)s & (0xFFFF | kRotBit);
                    if (const Json* rot = c.get("rot")) if (rot->t == Json::kStr && rot->s == "all") out.shape |= kRotBit;
                    if (!(out.shape & 0xFFFF)) { out = Cell(); continue; }
                } else { unknownSymbol = true; continue; }
                out.colour = colour(c.get("color"));
            } else {
                // a line: dot, gap, start, end (legacy: a gap was true)
                if (const Json* d = c.get("dot")) { int v = (int)d->num(0); if (v >= kDotBlack && v <= kDotInvisible) out.dot = (uint8_t)v; }
                if (const Json* g = c.get("gap")) { if (g->t == Json::kBool) out.gap = g->b ? kGapBreak : 0; else { int v = (int)g->num(0); if (v == 1 || v == 2) out.gap = (uint8_t)v; } }
                if (const Json* s = c.get("start")) out.start = s->truthy();
                if (const Json* e = c.get("end")) if (e->t == Json::kStr) out.end = e->s == "left" ? kEndLeft : e->s == "right" ? kEndRight : e->s == "top" ? kEndTop : e->s == "bottom" ? kEndBottom : 0;
                if (!ty.empty() && ty != "line" && ty != "nonce") unknownSymbol = true;
            }
        }
    }
    auto points = [&](const char* one, const char* many, auto apply) {
        if (const Json* s = j.get(one)) if (s->t == Json::kObj) apply(*s);
        if (const Json* s = j.get(many)) if (s->t == Json::kArr) for (auto& e : s->a) if (e.t == Json::kObj) apply(e);
    };
    auto xy = [&](const Json& e, int* x, int* y) { *x = (int)(e.get("x") ? e.get("x")->num(-1) : -1); *y = (int)(e.get("y") ? e.get("y")->num(-1) : -1); return p.in(*x, *y) && !Puzzle::isCell(*x, *y); };
    points("start", "startPoints", [&](const Json& e) { int x, y; if (xy(e, &x, &y)) p.at(x, y).start = true; });
    points("end", "endPoints", [&](const Json& e) { int x, y; if (!xy(e, &x, &y)) return; const Json* d = e.get("dir"); std::string s = (d && d->t == Json::kStr) ? d->s : "";
        p.at(x, y).end = s == "left" ? kEndLeft : s == "right" ? kEndRight : s == "top" ? kEndTop : s == "bottom" ? kEndBottom : 0; });
    if (const Json* d = j.get("dots")) if (d->t == Json::kArr) for (auto& e : d->a) { int x, y; if (e.t == Json::kObj && xy(e, &x, &y)) p.at(x, y).dot = kDotBlack; }
    if (const Json* d = j.get("gaps")) if (d->t == Json::kArr) for (auto& e : d->a) { int x, y; if (e.t == Json::kObj && xy(e, &x, &y)) p.at(x, y).gap = kGapBreak; }
    if (const Json* s = j.get("symmetry")) if (s->t == Json::kObj) {
        bool sx = s->get("x") && s->get("x")->truthy(), sy = s->get("y") && s->get("y")->truthy();
        p.sym = sx && sy ? kSymXY : sx ? kSymX : sy ? kSymY : kSymNone;
    }
    if (const Json* st = j.get("settings")) if (st->t == Json::kObj) {
        auto is = [&](const char* k, bool def) { const Json* v = st->get(k); return v ? v->truthy() : def; };
        if (is("CUSTOM_MECHANICS", false)) r.problems.push_back("it uses witnesspuzzles.com's custom mechanics, which the game does not have");
        if (!is("NEGATIONS_CANCEL_NEGATIONS", true) || is("SHAPELESS_ZERO_POLY", false) || !is("PRECISE_POLYOMINOS", true) || is("FAT_STARTPOINTS", false))
            r.problems.push_back("it changes the rules (witnesspuzzles.com's settings); the game plays it with its own");
    }
    if (unknownSymbol) r.problems.push_back("it has symbols the game does not have; they were left out");
    if (unknownColour) r.problems.push_back("a colour was not understood; black was used");
    if (merged) r.problems.push_back("two of its colours became one colour of the game, which changes the puzzle");
    for (auto& s : Problems(p)) r.problems.push_back(s);
    r.puzzle = p; r.ok = true; return r;
}

// ---- export --------------------------------------------------------------------------------------------------------------------------------
inline std::string ExportJson(const Puzzle& p) {
    std::string o = "{\"name\":" + JsonQuote(p.name) + ",\"grid\":["; char b[96];
    for (int x = 0; x < p.width(); x++) {
        if (x) o += ",";
        o += "[";
        for (int y = 0; y < p.height(); y++) {
            if (y) o += ",";
            const Cell& c = p.at(x, y);
            if (Puzzle::isCell(x, y)) {
                if (!c.symbol()) { o += "null"; continue; }
                const Colour* col = ColourByIndex(c.colour);
                std::string t = c.kind == kSquare ? "square" : c.kind == kStar ? "star" : c.kind == kNega ? "nega" : c.kind == kTriangle ? "triangle" : c.kind == kPoly ? "poly" : "ylop";
                o += "{\"type\":\"" + t + "\",\"color\":\"" + (col ? col->name : "black") + "\"";
                if (c.kind == kTriangle) { snprintf(b, sizeof b, ",\"count\":%d", c.count); o += b; }
                if (c.kind == kPoly || c.kind == kYlop) { snprintf(b, sizeof b, ",\"polyshape\":%u", c.shape); o += b; }
                o += "}";
            } else {
                o += "{\"type\":\"line\",\"line\":0";
                if (c.dot) { snprintf(b, sizeof b, ",\"dot\":%d", c.dot); o += b; }
                if (c.gap) { snprintf(b, sizeof b, ",\"gap\":%d", c.gap); o += b; }
                if (c.start) o += ",\"start\":true";
                if (c.end) { o += ",\"end\":\""; o += EndName(c.end); o += "\""; }
                o += "}";
            }
        }
        o += "]";
    }
    o += "],\"pillar\":false,\"settings\":{\"NEGATIONS_CANCEL_NEGATIONS\":true,\"SHAPELESS_ZERO_POLY\":false,\"PRECISE_POLYOMINOS\":true,\"FLASH_FOR_ERRORS\":true,\"FAT_STARTPOINTS\":false,\"CUSTOM_MECHANICS\":false}";
    if (p.sym != kSymNone) { o += ",\"symmetry\":{\"x\":"; o += (p.sym == kSymX || p.sym == kSymXY) ? "true" : "false"; o += ",\"y\":"; o += (p.sym == kSymY || p.sym == kSymXY) ? "true" : "false"; o += "}"; }
    return o + "}";
}

// A published puzzle's page (witnesspuzzles.com/play/<id>.html) holds the puzzle as Puzzle.deserialize("<JSON in a JS string>"): its JSON.
inline std::string JsonFromPlayPage(const std::string& html) {
    size_t k = html.find("Puzzle.deserialize(\""); if (k == std::string::npos) return "";
    size_t i = k + strlen("Puzzle.deserialize(\""); std::string out;
    while (i < html.size() && html[i] != '"') {
        char c = html[i++];
        if (c == '\\' && i < html.size()) { char e = html[i++]; out += e == 'n' ? '\n' : e; } else out += c;
    }
    return out;
}

}  // namespace wpz
