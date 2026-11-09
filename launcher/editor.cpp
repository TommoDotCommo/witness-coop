// The launcher's puzzle editor: a window with the player's puzzle sets (left), the puzzle drawn as a panel (middle: click to place, right-click
// to clear, or draw a line to try it), and the tools (right). The rules, the solver and the texts are common/wpuzzle.h (a port of jbzdarkid's
// witnesspuzzles.com engine); what a click does is editor_core.h (tested game-free: editor_test.cpp). The game puts the puzzles on its panels
// (proxy/randomizer/variants/nsv.cpp).
#include "editor.h"
#include <commctrl.h>
#include <shellapi.h>
#include <wininet.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "editor_core.h"
#include "theme.h"
#include "../common/wpuzzle_json.h"
#include "../common/wpuzzle_panels.h"

namespace ped {
namespace {

std::wstring Wd(const std::string& s) { if (s.empty()) return L""; int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0); std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n); w.resize(n - 1); return w; }
std::string Nd(const std::wstring& w) { if (w.empty()) return ""; int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr); std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr); s.resize(n - 1); return s; }
bool ReadText(const std::wstring& p, std::string* out) {
    FILE* f = _wfopen(p.c_str(), L"rb"); if (!f) return false;
    out->clear(); char b[8192]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0 && out->size() < (4u << 20)) out->append(b, n);
    fclose(f); return true;
}
bool WriteText(const std::wstring& p, const std::string& t) {
    std::wstring tmp = p + L".tmp"; FILE* f = _wfopen(tmp.c_str(), L"wb"); if (!f) return false;
    bool ok = fwrite(t.data(), 1, t.size(), f) == t.size(); ok = fclose(f) == 0 && ok;
    return ok && MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING);
}

enum { ID_SETS = 3000, ID_SET_NEW, ID_SET_RENAME, ID_SET_DELETE, ID_LIST, ID_ADD, ID_COPY, ID_REMOVE, ID_UP, ID_DOWN,
       ID_NAME, ID_W, ID_H, ID_SYM, ID_PANEL, ID_DOTKIND, ID_ROTATE, ID_NEGATIVE, ID_TRY, ID_SOLVE, ID_CHECK, ID_UNDO, ID_IMPORT, ID_EXPORT, ID_SITE,
       ID_STATUS, ID_CREDIT, ID_LABEL, ID_TOOL0 = 3100, ID_COLOUR0 = 3200, ID_SHAPE0 = 3300 };

struct Ed {
    HWND wnd = nullptr, canvas = nullptr; HFONT font = nullptr;
    std::wstring data; std::string setName; wpz::Set set; int cur = 0;
    Brush brush; Trace trace; bool trying = false; std::vector<wpz::Pos> shown; bool haveHover = false; wpz::Pos hover{0, 0};
    std::vector<wpz::Puzzle> undo; std::string status; bool statusGood = false; bool loading = false;
    void (*changed)() = nullptr;
} E;

HWND Ctl(int id) { return GetDlgItem(E.wnd, id); }
wpz::Puzzle* Cur() { return E.cur >= 0 && E.cur < (int)E.set.puzzles.size() ? &E.set.puzzles[E.cur] : nullptr; }
void Say(const std::string& s, bool good = false) { E.status = s; E.statusGood = good; SetWindowTextW(Ctl(ID_STATUS), Wd(s).c_str()); }
void Redraw() { if (E.canvas) InvalidateRect(E.canvas, nullptr, FALSE); }
void SetTrying(bool on) { E.trying = on; E.trace.path.clear(); if (HWND b = Ctl(ID_TRY)) SetWindowTextW(b, on ? L"Stop trying" : L"Try it"); }

void Save() {
    if (E.setName.empty()) return;
    SaveSet(E.data, E.setName, E.set);
    if (E.changed) E.changed();
}
std::string PuzzleLabel(size_t k) {
    const wpz::Puzzle& p = E.set.puzzles[k]; std::vector<unsigned> where = wpz::Placement(E.set);
    std::string name = p.name.empty() ? "Puzzle " + std::to_string(k + 1) : p.name;
    std::string at = where[k] ? wpz::SlotName(where[k]) : "no panel left";
    return name + "  (" + std::to_string(p.W) + "x" + std::to_string(p.H) + ", " + at + ")" + (wpz::Problems(p).empty() ? "" : "  !");
}
void FillList() {
    HWND l = Ctl(ID_LIST); SendMessageW(l, WM_SETREDRAW, FALSE, 0); SendMessageW(l, LB_RESETCONTENT, 0, 0);
    for (size_t k = 0; k < E.set.puzzles.size(); k++) SendMessageW(l, LB_ADDSTRING, 0, (LPARAM)Wd(PuzzleLabel(k)).c_str());
    SendMessageW(l, LB_SETCURSEL, E.cur, 0); SendMessageW(l, WM_SETREDRAW, TRUE, 0); InvalidateRect(l, nullptr, TRUE);
}
wpz::Look CurLook() {      // how the panel the current puzzle goes on looks in the game
    if (!Cur()) return wpz::Look();
    std::vector<unsigned> where = wpz::Placement(E.set); return wpz::PanelLook(where[E.cur]);
}
void ShowProblems() {
    wpz::Puzzle* p = Cur(); if (!p) { Say("This set has no puzzles yet: Add makes one."); return; }
    auto pr = wpz::Problems(*p);
    if (!pr.empty()) { Say("Not ready for the game: " + pr[0] + "."); return; }
    auto hard = wpz::HardToSee(*p, CurLook());
    if (!hard.empty()) {
        std::string names; for (size_t i = 0; i < hard.size(); i++) names += (i ? (i + 1 == hard.size() ? " and " : ", ") : "") + hard[i];
        Say("Ready, but " + names + " is hard to see on this panel's colour.", true); return;
    }
    Say("Ready. Click to place the tool, right-click to clear; Try it to draw a line.", true);
}
void LoadControls(bool resetUndo = true) {      // the current puzzle into the right-hand controls
    wpz::Puzzle* p = Cur(); E.loading = true;
    bool on = p != nullptr;
    for (int id : {ID_NAME, ID_W, ID_H, ID_SYM, ID_PANEL, ID_TRY, ID_SOLVE, ID_CHECK, ID_EXPORT, ID_COPY, ID_REMOVE, ID_UP, ID_DOWN}) EnableWindow(Ctl(id), on);
    if (p) {
        SetWindowTextW(Ctl(ID_NAME), Wd(p->name).c_str());
        SendMessageW(Ctl(ID_W), CB_SETCURSEL, p->W - 1, 0); SendMessageW(Ctl(ID_H), CB_SETCURSEL, p->H - 1, 0);
        SendMessageW(Ctl(ID_SYM), CB_SETCURSEL, p->sym, 0);
        int k = 0; for (size_t i = 0; i < wpz::Slots().size(); i++) if (wpz::Slots()[i].id == p->panel) k = (int)i + 1;
        SendMessageW(Ctl(ID_PANEL), CB_SETCURSEL, k, 0);
    }
    E.loading = false; SetTrying(false); E.shown.clear(); if (resetUndo) E.undo.clear();
    ShowProblems(); Redraw();
}
void Changed(bool relist = true) { Save(); if (relist) FillList(); ShowProblems(); Redraw(); }
void Remember() { if (Cur()) { E.undo.push_back(*Cur()); if (E.undo.size() > 200) E.undo.erase(E.undo.begin()); } }

void FillSets() {
    HWND c = Ctl(ID_SETS); SendMessageW(c, CB_RESETCONTENT, 0, 0);
    auto names = ListSets(E.data); int k = 0;
    for (size_t i = 0; i < names.size(); i++) { SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)Wd(names[i]).c_str()); if (names[i] == E.setName) k = (int)i; }
    SendMessageW(c, CB_SETCURSEL, k, 0);
}
void OpenSet(const std::string& name) {
    wpz::Set s; std::string err;
    if (!LoadSet(E.data, name, &s, &err)) { s = wpz::Set(); s.name = name; s.puzzles.push_back(wpz::NewPuzzle(4, 4, "")); SaveSet(E.data, name, s); }
    E.setName = name; E.set = s; E.cur = 0; FillSets(); FillList(); LoadControls();
    std::wstring title = L"Puzzle editor: " + Wd(name); SetWindowTextW(E.wnd, title.c_str());
}
std::string Ask(const wchar_t* title, const wchar_t* prompt, const std::string& start);      // (below)
void NewSet() {
    std::string n = Ask(L"New puzzle set", L"Name of the new set:", "My puzzles");
    if (n.empty()) return;
    n = SafeName(n); auto names = ListSets(E.data);
    std::string base = n; for (int i = 2; std::find(names.begin(), names.end(), n) != names.end(); i++) n = base + " " + std::to_string(i);
    wpz::Set s; s.name = n; s.puzzles.push_back(wpz::NewPuzzle(4, 4, "")); SaveSet(E.data, n, s); OpenSet(n); if (E.changed) E.changed();
}
void RenameSet() {
    std::string n = Ask(L"Rename the set", L"New name:", E.setName); if (n.empty()) return;
    n = SafeName(n); if (n == E.setName) return;
    std::wstring from = PuzzlesDir(E.data) + L"sets\\" + Wd(SafeName(E.setName)) + L".txt", to = PuzzlesDir(E.data) + L"sets\\" + Wd(n) + L".txt";
    if (GetFileAttributesW(to.c_str()) != INVALID_FILE_ATTRIBUTES) { Say("A set of that name exists already."); return; }
    E.set.name = n; if (!MoveFileW(from.c_str(), to.c_str())) { Say("The set could not be renamed."); return; }
    E.setName = n; SaveSet(E.data, n, E.set); FillSets(); if (E.changed) E.changed();
    SetWindowTextW(E.wnd, (L"Puzzle editor: " + Wd(n)).c_str());
}
void DeleteSet() {
    std::wstring q = L"Delete the set \"" + Wd(E.setName) + L"\" and its " + std::to_wstring(E.set.puzzles.size()) + L" puzzles? Games already played with it keep their copy.";
    if (MessageBoxW(E.wnd, q.c_str(), L"Delete set", MB_OKCANCEL | MB_ICONWARNING) != IDOK) return;
    DeleteFileW((PuzzlesDir(E.data) + L"sets\\" + Wd(SafeName(E.setName)) + L".txt").c_str());
    auto names = ListSets(E.data);
    if (names.empty()) { wpz::Set s; s.name = "My puzzles"; s.puzzles.push_back(wpz::NewPuzzle(4, 4, "")); SaveSet(E.data, s.name, s); names = ListSets(E.data); }
    OpenSet(names[0]); if (E.changed) E.changed();
}

// ---- drawing ------------------------------------------------------------------------------------------------------------------------------
COLORREF Rgb(uint8_t index) { const wpz::Colour* c = wpz::ColourByIndex(index); return c ? RGB(c->r, c->g, c->b) : RGB(0, 0, 0); }
const COLORREF kBack = RGB(28, 30, 36), kPanel = RGB(118, 122, 132), kLine = RGB(58, 60, 70), kTrace = RGB(250, 250, 250), kTraceBlue = RGB(90, 200, 255),
               kTraceYellow = RGB(255, 220, 70), kHover = RGB(255, 204, 0), kWrong = RGB(230, 60, 50);
void Fill(HDC dc, const POINT* pts, int n, COLORREF c, bool outline = false, int width = 2) {
    HBRUSH br = outline ? (HBRUSH)GetStockObject(NULL_BRUSH) : CreateSolidBrush(c); HPEN pen = CreatePen(PS_SOLID, outline ? width : 1, c);
    HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, pen); Polygon(dc, pts, n); SelectObject(dc, ob); SelectObject(dc, op);
    if (!outline) DeleteObject(br); DeleteObject(pen);
}
void Disc(HDC dc, int x, int y, int r, COLORREF c) { HBRUSH br = CreateSolidBrush(c); HPEN pen = CreatePen(PS_SOLID, 1, c); HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, pen); Ellipse(dc, x - r, y - r, x + r + 1, y + r + 1); SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(br); DeleteObject(pen); }
void Segment(HDC dc, int x1, int y1, int x2, int y2, int w, COLORREF c) {
    LOGBRUSH lb = {BS_SOLID, c, 0}; HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND, std::max(1, w), &lb, 0, nullptr);
    HGDIOBJ op = SelectObject(dc, pen); MoveToEx(dc, x1, y1, nullptr); LineTo(dc, x2, y2); SelectObject(dc, op); DeleteObject(pen);
}
void Square(HDC dc, double cx, double cy, double half, double angle, COLORREF c, bool outline = false) {
    POINT pts[4]; for (int k = 0; k < 4; k++) { double a = angle + 3.14159265 / 4 + k * 3.14159265 / 2; pts[k] = {(LONG)lround(cx + half * 1.41421356 * cos(a)), (LONG)lround(cy + half * 1.41421356 * sin(a))}; }
    Fill(dc, pts, 4, c, outline);
}
void Symbol(HDC dc, const wpz::Cell& c, int cx, int cy, int cell) {
    COLORREF col = Rgb(c.colour); double s = cell;
    switch (c.kind) {
    case wpz::kSquare: { HBRUSH br = CreateSolidBrush(col); HPEN pen = CreatePen(PS_SOLID, 1, col); HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, pen);
        int h = (int)(s * 0.18); RoundRect(dc, cx - h, cy - h, cx + h, cy + h, h, h); SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(br); DeleteObject(pen); break; }
    case wpz::kStar: Square(dc, cx, cy, s * 0.16, 0, col); Square(dc, cx, cy, s * 0.16, 3.14159265 / 4, col); break;
    case wpz::kNega: { int w = std::max(2, (int)(s * 0.07)); double r = s * 0.2;
        for (int k = 0; k < 3; k++) { double a = -3.14159265 / 2 + k * 2 * 3.14159265 / 3; Segment(dc, cx, cy, (int)(cx + r * cos(a)), (int)(cy - r * sin(a)), w, col); } break; }
    case wpz::kTriangle: { double t = s * 0.11; double total = c.count * t * 2.2; double x0 = cx - total / 2 + t * 1.1;
        for (int k = 0; k < c.count; k++) { double x = x0 + k * t * 2.2; POINT pts[3] = {{(LONG)x, (LONG)(cy - t)}, {(LONG)(x - t), (LONG)(cy + t * 0.8)}, {(LONG)(x + t), (LONG)(cy + t * 0.8)}}; Fill(dc, pts, 3, col); } break; }
    case wpz::kPoly: case wpz::kYlop: {
        int minx = 4, maxx = -1, miny = 4, maxy = -1;
        for (int x = 0; x < 4; x++) for (int y = 0; y < 4; y++) if (wpz::PolySet(c.shape, x, y)) { minx = std::min(minx, x); maxx = std::max(maxx, x); miny = std::min(miny, y); maxy = std::max(maxy, y); }
        if (maxx < 0) break;
        double b = s * 0.13, gap = b * 0.25, step = b + gap; double angle = (c.shape & wpz::kRotBit) ? 0.26 : 0;
        double w = (maxx - minx + 1) * step - gap, h = (maxy - miny + 1) * step - gap;
        for (int x = minx; x <= maxx; x++) for (int y = miny; y <= maxy; y++) {
            if (!wpz::PolySet(c.shape, x, y)) continue;
            double bx = -w / 2 + (x - minx) * step + b / 2, by = -h / 2 + (y - miny) * step + b / 2;
            double rx = bx * cos(angle) - by * sin(angle), ry = bx * sin(angle) + by * cos(angle);
            Square(dc, cx + rx, cy + ry, b / 2 - (c.kind == wpz::kYlop ? 1 : 0), angle, col, c.kind == wpz::kYlop);
        }
        break; }
    default: break;
    }
}
void Paint(HDC dc, const RECT& rc) {
    HBRUSH bg = CreateSolidBrush(kBack); FillRect(dc, &rc, bg); DeleteObject(bg);
    wpz::Puzzle* pp = Cur(); if (!pp) return;
    const wpz::Puzzle& p = *pp; Geometry g = Geometry::Fit(p, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, 16);
    int lw = std::max(3, g.step / 3), cell = g.step * 2;
    RECT pr = {g.px(0) - g.step, g.py(0) - g.step, g.px(p.width() - 1) + g.step + 1, g.py(p.height() - 1) + g.step + 1};
    wpz::Look look = CurLook();      // (the panel's own colours, as in the game)
    COLORREF kPanel = look.known ? RGB((look.surface >> 16) & 255, (look.surface >> 8) & 255, look.surface & 255) : ped::kPanel;
    COLORREF kLine = look.known ? RGB((look.line >> 16) & 255, (look.line >> 8) & 255, look.line & 255) : ped::kLine;
    HBRUSH pb = CreateSolidBrush(kPanel); FillRect(dc, &pr, pb); DeleteObject(pb);
    // the lines (edges), with their gaps
    for (int x = 0; x < p.width(); x++) for (int y = 0; y < p.height(); y++) {
        if (wpz::Puzzle::isCell(x, y) || (x % 2 == 0 && y % 2 == 0)) continue;
        const wpz::Cell& c = p.at(x, y); if (c.gap == wpz::kGapFull) continue;
        int ax = x % 2 ? x - 1 : x, ay = y % 2 ? y - 1 : y, bx = x % 2 ? x + 1 : x, by = y % 2 ? y + 1 : y;
        if (c.gap == wpz::kGapBreak) {
            int mx = (g.px(ax) + g.px(bx)) / 2, my = (g.py(ay) + g.py(by)) / 2, dx = (g.px(bx) - g.px(ax)) / 6, dy = (g.py(by) - g.py(ay)) / 6;
            Segment(dc, g.px(ax), g.py(ay), mx - dx, my - dy, lw, kLine); Segment(dc, mx + dx, my + dy, g.px(bx), g.py(by), lw, kLine);
        } else Segment(dc, g.px(ax), g.py(ay), g.px(bx), g.py(by), lw, kLine);
    }
    // ends and starts
    for (int x = 0; x < p.width(); x++) for (int y = 0; y < p.height(); y++) {
        const wpz::Cell& c = p.at(x, y);
        if (c.end) { int dx = c.end == wpz::kEndLeft ? -1 : c.end == wpz::kEndRight ? 1 : 0, dy = c.end == wpz::kEndTop ? -1 : c.end == wpz::kEndBottom ? 1 : 0;
            Segment(dc, g.px(x), g.py(y), g.px(x) + dx * g.step * 2 / 3, g.py(y) + dy * g.step * 2 / 3, lw, kLine); }
        if (c.start) Disc(dc, g.px(x), g.py(y), (int)(lw * 1.25), kLine);
    }
    // the line being tried (or a solution shown), and its mirror
    const std::vector<wpz::Pos>& path = E.trying ? E.trace.path : E.shown;
    if (!path.empty()) {
        bool sym = p.sym != wpz::kSymNone; COLORREF a = sym ? kTraceBlue : kTrace;
        auto draw = [&](bool mirror, COLORREF col) {
            auto at = [&](wpz::Pos q) { if (mirror) { int sx, sy; p.symPos(q.x, q.y, &sx, &sy); q = {sx, sy}; } return q; };
            wpz::Pos s0 = at(path[0]); Disc(dc, g.px(s0.x), g.py(s0.y), (int)(lw * 1.25), col);
            for (size_t i = 1; i < path.size(); i++) { wpz::Pos u = at(path[i - 1]), v = at(path[i]); Segment(dc, g.px(u.x), g.py(u.y), g.px(v.x), g.py(v.y), lw, col); }
            wpz::Pos e = at(path.back()); const wpz::Cell& ec = p.at(e.x, e.y);
            if (ec.end && (!E.trying || E.trace.atEnd(p))) { int dx = ec.end == wpz::kEndLeft ? -1 : ec.end == wpz::kEndRight ? 1 : 0, dy = ec.end == wpz::kEndTop ? -1 : ec.end == wpz::kEndBottom ? 1 : 0;
                Segment(dc, g.px(e.x), g.py(e.y), g.px(e.x) + dx * g.step * 2 / 3, g.py(e.y) + dy * g.step * 2 / 3, lw, col); }
        };
        draw(false, a); if (sym) draw(true, kTraceYellow);
    }
    // dots (over the line), symbols
    for (int x = 0; x < p.width(); x++) for (int y = 0; y < p.height(); y++) {
        const wpz::Cell& c = p.at(x, y);
        if (c.dot) {
            int r = std::max(3, lw * 45 / 100); POINT hex[6]; for (int k = 0; k < 6; k++) hex[k] = {(LONG)lround(g.px(x) + r * cos(k * 3.14159265 / 3)), (LONG)lround(g.py(y) + r * sin(k * 3.14159265 / 3))};
            COLORREF col = c.dot == wpz::kDotBlue ? RGB(60, 160, 255) : c.dot == wpz::kDotYellow ? RGB(255, 210, 40) : c.dot == wpz::kDotInvisible ? RGB(170, 170, 170) : RGB(10, 10, 10);
            Fill(dc, hex, 6, col, c.dot == wpz::kDotInvisible, 1);
        }
        if (wpz::Puzzle::isCell(x, y) && c.symbol()) Symbol(dc, c, g.px(x), g.py(y), cell);
    }
    if (E.haveHover && !E.trying && p.in(E.hover.x, E.hover.y)) {
        int r = wpz::Puzzle::isCell(E.hover.x, E.hover.y) ? (int)(g.step * 0.8) : lw + 4;
        HPEN pen = CreatePen(PS_DOT, 1, kHover); HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, g.px(E.hover.x) - r, g.py(E.hover.y) - r, g.px(E.hover.x) + r, g.py(E.hover.y) + r); SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(pen);
    }
}
Geometry CanvasGeometry() { RECT rc; GetClientRect(E.canvas, &rc); return Geometry::Fit(*Cur(), 0, 0, rc.right, rc.bottom, 16); }
void FinishTry() {
    wpz::Puzzle* p = Cur(); if (!p || !E.trace.atEnd(*p)) return;
    wpz::Verdict v = wpz::Check(*p, E.trace.path);
    Say(v.ok ? "Solved: this line works. (Try it again: click the start.)" : "Not solved: " + v.why + ". Click the start to draw again.", v.ok);
}
LRESULT CALLBACK CanvasProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc); HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom); HGDIOBJ ob = SelectObject(mem, bmp);
        Paint(mem, rc); BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem); EndPaint(h, &ps); return 0; }
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: {
        wpz::Puzzle* p = Cur(); if (!p) return 0;
        SetFocus(h); int mx = (short)LOWORD(l), my = (short)HIWORD(l); Geometry g = CanvasGeometry();
        if (E.trying) {
            wpz::Pos s; if (Pick(g, *p, kStart, mx, my, &s) && p->at(s.x, s.y).start) { E.trace.begin(*p, s); SetCapture(h); Say("Draw the line to an end."); }
            Redraw(); return 0;
        }
        E.shown.clear();
        wpz::Pos at; if (!Pick(g, *p, m == WM_RBUTTONDOWN ? kClear : E.brush.tool, mx, my, &at)) {
            if (m == WM_RBUTTONDOWN) { for (Tool t : {kSquare, kDot}) if (Pick(g, *p, t, mx, my, &at)) { Remember(); if (Apply(*p, E.brush, at, true)) Changed(); else E.undo.pop_back(); break; } }
            return 0;
        }
        Remember();
        if (Apply(*p, E.brush, at, m == WM_RBUTTONDOWN)) Changed(); else E.undo.pop_back();
        return 0; }
    case WM_MOUSEMOVE: {
        wpz::Puzzle* p = Cur(); if (!p) return 0;
        int mx = (short)LOWORD(l), my = (short)HIWORD(l); Geometry g = CanvasGeometry();
        if (E.trying) { if (E.trace.active() && GetCapture() == h) { E.trace.pull(*p, NearestLine(g, *p, mx, my)); Redraw(); } return 0; }
        wpz::Pos at; bool have = Pick(g, *p, E.brush.tool, mx, my, &at);
        if (have != E.haveHover || (have && !(at == E.hover))) { E.haveHover = have; E.hover = at; Redraw(); }
        TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE, h, 0}; TrackMouseEvent(&t);
        return 0; }
    case WM_MOUSELEAVE: if (E.haveHover) { E.haveHover = false; Redraw(); } return 0;
    case WM_LBUTTONUP: if (GetCapture() == h) { ReleaseCapture(); if (E.trying) FinishTry(); Redraw(); } return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ---- the window ---------------------------------------------------------------------------------------------------------------------------
HWND Mk(const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD ex = 0) {
    HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, E.wnd, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)E.font, TRUE); if (!(wcscmp(cls, L"BUTTON") == 0 && (style & BS_TYPEMASK) == BS_OWNERDRAW)) th::Style(c); return c;
}
void Put(int id, int x, int y, int w, int h) { if (HWND c = Ctl(id)) SetWindowPos(c, nullptr, x, y, std::max(1, w), std::max(1, h), SWP_NOZORDER | SWP_NOACTIVATE); }
void Layout() {
    RECT rc; GetClientRect(E.wnd, &rc); int W = rc.right, H = rc.bottom, m = 12, lw = 240, rw = 290;
    int y = m;
    Put(ID_LABEL, m, y, lw, 20); y += 22; Put(ID_SETS, m, y, lw, 300); y += 34;
    int bw = (lw - 12) / 3; Put(ID_SET_NEW, m, y, bw, 30); Put(ID_SET_RENAME, m + bw + 6, y, bw, 30); Put(ID_SET_DELETE, m + 2 * (bw + 6), y, bw, 30); y += 40;
    Put(ID_LIST, m, y, lw, H - y - 100);
    int by = H - 92; int b5 = (lw - 8) / 3; Put(ID_ADD, m, by, b5, 30); Put(ID_COPY, m + b5 + 4, by, b5, 30); Put(ID_REMOVE, m + 2 * (b5 + 4), by, b5, 30);
    by += 34; Put(ID_UP, m, by, (lw - 4) / 2, 30); Put(ID_DOWN, m + (lw - 4) / 2 + 4, by, (lw - 4) / 2, 30);
    int cx = m + lw + m, cw = W - cx - rw - 2 * m;
    SetWindowPos(E.canvas, nullptr, cx, m, std::max(50, cw), std::max(50, H - m - 140), SWP_NOZORDER | SWP_NOACTIVATE);
    int bottom = H - 128; int bb = (cw - 2 * 6) / 3;
    Put(ID_TRY, cx, bottom, bb, 32); Put(ID_SOLVE, cx + (bb + 6), bottom, bb, 32); Put(ID_CHECK, cx + 2 * (bb + 6), bottom, bb, 32);
    Put(ID_UNDO, cx, bottom + 36, bb, 32); Put(ID_IMPORT, cx + (bb + 6), bottom + 36, bb, 32); Put(ID_EXPORT, cx + 2 * (bb + 6), bottom + 36, bb, 32);
    Put(ID_STATUS, cx, bottom + 74, cw, 36); Put(ID_CREDIT, cx, H - 20, cw + rw + m, 18);
    int rx = W - rw - m; y = m;
    Put(ID_NAME, rx, y, rw, 28); y += 36;
    Put(ID_W, rx, y, (rw - 8) / 2, 300); Put(ID_H, rx + (rw - 8) / 2 + 8, y, (rw - 8) / 2, 300); y += 36;
    Put(ID_SYM, rx, y, rw, 300); y += 36;
    Put(ID_PANEL, rx, y, rw, 400); y += 40;
    int tw = (rw - 8) / 3;
    for (int t = 0; t < kToolCount; t++) Put(ID_TOOL0 + t, rx + (t % 3) * (tw + 4), y + (t / 3) * 34, tw, 30);
    y += ((kToolCount + 2) / 3) * 34 + 6;
    int sw = (rw - 9 * 4) / 10; for (size_t k = 0; k < wpz::Colours().size(); k++) Put(ID_COLOUR0 + (int)k, rx + (int)k * (sw + 4), y, sw, sw); y += sw + 10;
    int cs = 22; for (int k = 0; k < 16; k++) Put(ID_SHAPE0 + k, rx + (k % 4) * (cs + 3), y + (k / 4) * (cs + 3), cs, cs);
    Put(ID_ROTATE, rx + 4 * (cs + 3) + 10, y, rw - 4 * (cs + 3) - 10, 24); Put(ID_NEGATIVE, rx + 4 * (cs + 3) + 10, y + 28, rw - 4 * (cs + 3) - 10, 24);
    Put(ID_DOTKIND, rx + 4 * (cs + 3) + 10, y + 60, rw - 4 * (cs + 3) - 10, 200); y += 4 * (cs + 3) + 10;
    Put(ID_SITE, rx, y, rw, 30);
    Redraw();
}
void DrawOwner(const DRAWITEMSTRUCT* d) {
    int id = (int)d->CtlID; HDC dc = d->hDC; RECT r = d->rcItem;
    HBRUSH bg = th::SurfaceBrush(); FillRect(dc, &r, bg);
    bool sel = false;
    if (id >= ID_COLOUR0 && id < ID_COLOUR0 + 16) {
        const wpz::Colour& c = wpz::Colours()[id - ID_COLOUR0]; sel = E.brush.colour == c.index;
        RECT in = {r.left + 3, r.top + 3, r.right - 3, r.bottom - 3}; HBRUSH b = CreateSolidBrush(RGB(c.r, c.g, c.b)); FillRect(dc, &in, b); DeleteObject(b);
    } else if (id >= ID_SHAPE0 && id < ID_SHAPE0 + 16) {
        int k = id - ID_SHAPE0; sel = wpz::PolySet(E.brush.shape, k % 4, k / 4);
        if (sel) { RECT in = {r.left + 3, r.top + 3, r.right - 3, r.bottom - 3}; HBRUSH b = CreateSolidBrush(Rgb(E.brush.colour == 1 ? 8 : E.brush.colour)); FillRect(dc, &in, b); DeleteObject(b); }
        sel = false;
    } else if (id >= ID_TOOL0 && id < ID_TOOL0 + kToolCount) {
        sel = E.brush.tool == id - ID_TOOL0; SetBkMode(dc, TRANSPARENT); SetTextColor(dc, sel ? RGB(255, 214, 40) : RGB(225, 228, 235));
        HGDIOBJ of = SelectObject(dc, sel ? th::Bold() : E.font); std::wstring t = Wd(ToolName((Tool)(id - ID_TOOL0)));
        DrawTextW(dc, t.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE); SelectObject(dc, of);
    }
    HPEN pen = CreatePen(PS_SOLID, sel ? 2 : 1, sel ? RGB(255, 214, 40) : RGB(90, 95, 108)); HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, r.left, r.top, r.right, r.bottom); SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(pen);
}
void RedrawOwners() { for (int id = ID_TOOL0; id < ID_TOOL0 + kToolCount; id++) InvalidateRect(Ctl(id), nullptr, FALSE); for (int id = ID_COLOUR0; id < ID_COLOUR0 + 16; id++) if (Ctl(id)) InvalidateRect(Ctl(id), nullptr, FALSE); for (int id = ID_SHAPE0; id < ID_SHAPE0 + 16; id++) InvalidateRect(Ctl(id), nullptr, FALSE); }

std::string FetchUrl(const std::string& url) {
    std::string out; HINTERNET net = InternetOpenW(L"WitnessCoopLauncher", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0); if (!net) return out;
    DWORD ms = 8000; InternetSetOptionW(net, INTERNET_OPTION_CONNECT_TIMEOUT, &ms, sizeof ms); InternetSetOptionW(net, INTERNET_OPTION_RECEIVE_TIMEOUT, &ms, sizeof ms);
    HINTERNET u = InternetOpenUrlW(net, Wd(url).c_str(), nullptr, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_COOKIES | INTERNET_FLAG_SECURE, 0);
    if (u) { char b[8192]; DWORD n = 0; while (InternetReadFile(u, b, sizeof b, &n) && n && out.size() < (2u << 20)) out.append(b, n); InternetCloseHandle(u); }
    InternetCloseHandle(net); return out;
}
// A published puzzle's address or its id (witnesspuzzles.com/play/ABCD1234): the page's JSON. Anything else is taken as the JSON itself.
std::string ImportText(const std::string& in, std::string* from) {
    std::string t = in; while (!t.empty() && (t.back() == ' ' || t.back() == '\n' || t.back() == '\r')) t.pop_back(); while (!t.empty() && (t[0] == ' ' || t[0] == '\n')) t.erase(0, 1);
    if (!t.empty() && t[0] == '{') return t;
    std::string id; size_t k = t.find("/play/");
    if (k != std::string::npos) { id = t.substr(k + 6); size_t e = id.find_first_of(".?#/ "); if (e != std::string::npos) id = id.substr(0, e); }
    else if (t.size() == 8 && t.find_first_not_of("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ") == std::string::npos) id = t;
    if (id.empty() || id.find_first_not_of("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ") != std::string::npos) return "";
    *from = "https://witnesspuzzles.com/play/" + id + ".html";
    return wpz::JsonFromPlayPage(FetchUrl(*from));
}
void Import() {
    std::string in = Ask(L"Import a puzzle", L"Paste a puzzle from witnesspuzzles.com: its Export text from the editor there, or the address of a published puzzle (witnesspuzzles.com/play/...).", "");
    if (in.empty()) return;
    std::string from; HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT)); std::string json = ImportText(in, &from); SetCursor(old);
    if (json.empty()) { Say(from.empty() ? "That is neither a puzzle's text nor the address of a published puzzle." : "The puzzle could not be fetched from " + from + "."); return; }
    wpz::Imported im = wpz::ImportJson(json);
    if (!im.ok) { Say("Not imported: " + im.error + "."); return; }
    if (im.puzzle.name.empty() || im.puzzle.name == "Unnamed Puzzle") im.puzzle.name = from.empty() ? "Imported" : "witnesspuzzles.com " + from.substr(from.rfind('/') + 1, 8);
    if (im.puzzle.W > wpz::kMaxSize || im.puzzle.H > wpz::kMaxSize) { Say("Not imported: the game's panels take at most 7x7 cells."); return; }
    E.set.puzzles.insert(E.set.puzzles.begin() + (E.cur + 1), im.puzzle); E.cur++; Changed(); LoadControls();
    std::string note; for (auto& p : im.problems) note += (note.empty() ? "" : "; ") + p;
    Say(note.empty() ? "Imported." : "Imported, but: " + note + ".", note.empty());
}
void Export() {
    wpz::Puzzle* p = Cur(); if (!p) return;
    std::wstring t = Wd(wpz::ExportJson(*p));
    if (OpenClipboard(E.wnd)) {
        EmptyClipboard(); HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (t.size() + 1) * sizeof(wchar_t));
        if (g) { memcpy(GlobalLock(g), t.c_str(), (t.size() + 1) * sizeof(wchar_t)); GlobalUnlock(g); SetClipboardData(CF_UNICODETEXT, g); }
        CloseClipboard();
        Say("Copied. On witnesspuzzles.com's editor, Import takes it (paste it there).", true);
    }
}
void Solve(bool count) {
    wpz::Puzzle* p = Cur(); if (!p) return;
    SetTrying(false);
    HCURSOR old = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    size_t max = count ? 1000 : 1; wpz::Solutions s = wpz::Solve(*p, max, 0, count ? 4000 : 3000);
    SetCursor(old);
    if (count) Say(CountText(s, max), !s.lines.empty());
    else if (s.lines.empty()) Say(s.complete ? "No solution: nobody can solve this puzzle." : "No solution found in 3 seconds (the puzzle is very open).");
    else Say("One way to solve it is shown. (It is not drawn in the game.)", true);
    E.shown = s.lines.empty() ? std::vector<wpz::Pos>() : s.lines[0]; Redraw();
}

void Command(int id, int code) {
    wpz::Puzzle* p = Cur();
    if (id == ID_SETS && code == CBN_SELCHANGE) { int k = (int)SendMessageW(Ctl(ID_SETS), CB_GETCURSEL, 0, 0); auto names = ListSets(E.data); if (k >= 0 && k < (int)names.size()) OpenSet(names[k]); return; }
    if (code != BN_CLICKED && code != CBN_SELCHANGE && code != EN_CHANGE && code != LBN_SELCHANGE) return;
    if (E.loading) return;
    switch (id) {
    case ID_SET_NEW: NewSet(); return;
    case ID_SET_RENAME: RenameSet(); return;
    case ID_SET_DELETE: DeleteSet(); return;
    case ID_LIST: if (code == LBN_SELCHANGE) { E.cur = (int)SendMessageW(Ctl(ID_LIST), LB_GETCURSEL, 0, 0); LoadControls(); } return;
    case ID_ADD: { wpz::Puzzle n = p ? wpz::NewPuzzle(p->W, p->H) : wpz::NewPuzzle(4, 4); E.set.puzzles.insert(E.set.puzzles.begin() + (p ? E.cur + 1 : 0), n); E.cur = p ? E.cur + 1 : 0; Changed(); LoadControls(); return; }
    case ID_COPY: if (p) { wpz::Puzzle c = *p; c.panel = 0; c.name = c.name.empty() ? "" : c.name + " (copy)"; E.set.puzzles.insert(E.set.puzzles.begin() + E.cur + 1, c); E.cur++; Changed(); LoadControls(); } return;
    case ID_REMOVE: if (p) { E.set.puzzles.erase(E.set.puzzles.begin() + E.cur); E.cur = std::max(0, std::min(E.cur, (int)E.set.puzzles.size() - 1)); Changed(); LoadControls(); } return;
    case ID_UP: if (p && E.cur > 0) { std::swap(E.set.puzzles[E.cur], E.set.puzzles[E.cur - 1]); E.cur--; Changed(); } return;
    case ID_DOWN: if (p && E.cur + 1 < (int)E.set.puzzles.size()) { std::swap(E.set.puzzles[E.cur], E.set.puzzles[E.cur + 1]); E.cur++; Changed(); } return;
    case ID_NAME: if (p && code == EN_CHANGE) { wchar_t b[200] = {}; GetWindowTextW(Ctl(ID_NAME), b, 200); p->name = Nd(b); Save(); FillList(); } return;
    case ID_W: case ID_H: if (p && code == CBN_SELCHANGE) {
        int w = (int)SendMessageW(Ctl(ID_W), CB_GETCURSEL, 0, 0) + 1, h = (int)SendMessageW(Ctl(ID_H), CB_GETCURSEL, 0, 0) + 1;
        if (w * h < 2) { Say("A puzzle needs at least 2 cells."); LoadControls(); return; }
        Remember(); *p = wpz::Resized(*p, w, h);
        bool start = false, end = false; for (auto& c : p->g) { start = start || c.start; end = end || c.end; }
        if (!start) p->at(0, p->height() - 1).start = true;
        if (!end) p->at(p->width() - 1, 0).end = wpz::kEndRight;
        if (p->sym != wpz::kSymNone) MirrorStartsAndEnds(*p);
        E.shown.clear(); Changed(); } return;
    case ID_SYM: if (p && code == CBN_SELCHANGE) { Remember(); p->sym = (wpz::Sym)SendMessageW(Ctl(ID_SYM), CB_GETCURSEL, 0, 0); MirrorStartsAndEnds(*p); E.shown.clear(); Changed(); } return;
    case ID_PANEL: if (p && code == CBN_SELCHANGE) { int k = (int)SendMessageW(Ctl(ID_PANEL), CB_GETCURSEL, 0, 0); p->panel = k > 0 ? wpz::Slots()[k - 1].id : 0; Changed(); } return;
    case ID_DOTKIND: if (code == CBN_SELCHANGE) E.brush.dot = (uint8_t)(SendMessageW(Ctl(ID_DOTKIND), CB_GETCURSEL, 0, 0) + 1); return;
    case ID_ROTATE: E.brush.rotate = SendMessageW(Ctl(ID_ROTATE), BM_GETCHECK, 0, 0) == BST_CHECKED; E.brush.tool = kPoly; RedrawOwners(); return;
    case ID_NEGATIVE: E.brush.negative = SendMessageW(Ctl(ID_NEGATIVE), BM_GETCHECK, 0, 0) == BST_CHECKED; E.brush.tool = kPoly; RedrawOwners(); return;
    case ID_TRY: SetTrying(!E.trying); E.shown.clear();
        Say(E.trying ? "Trying: press on a start and draw the line to an end, as on a panel." : "Editing."); Redraw(); return;
    case ID_SOLVE: Solve(false); return;
    case ID_CHECK: Solve(true); return;
    case ID_UNDO: if (p && !E.undo.empty()) { *p = E.undo.back(); E.undo.pop_back(); Changed(); LoadControls(false); } return;
    case ID_IMPORT: Import(); return;
    case ID_EXPORT: Export(); return;
    case ID_SITE: ShellExecuteW(E.wnd, L"open", L"https://witnesspuzzles.com/editor.html", nullptr, nullptr, SW_SHOWNORMAL); return;
    }
    if (id >= ID_TOOL0 && id < ID_TOOL0 + kToolCount) { E.brush.tool = (Tool)(id - ID_TOOL0); if (E.trying) SetTrying(false); RedrawOwners(); return; }
    if (id >= ID_COLOUR0 && id < ID_COLOUR0 + (int)wpz::Colours().size()) { E.brush.colour = wpz::Colours()[id - ID_COLOUR0].index; if (!OnCells(E.brush.tool)) E.brush.tool = kSquare; RedrawOwners(); return; }
    if (id >= ID_SHAPE0 && id < ID_SHAPE0 + 16) { int k = id - ID_SHAPE0; E.brush.shape ^= wpz::PolyMask(k % 4, k / 4); E.brush.tool = kPoly; RedrawOwners(); return; }
}
LRESULT CALLBACK EdProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ERASEBKGND: { RECT rc; GetClientRect(h, &rc); th::FillBg((HDC)w, h, rc); return 1; }
    case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN: {
        if ((HWND)l == Ctl(ID_STATUS)) { LRESULT r = th::CtlColor(m, (HDC)w, (HWND)l); SetTextColor((HDC)w, E.statusGood ? RGB(150, 220, 150) : RGB(255, 200, 120)); return r; }
        return th::CtlColor(m, (HDC)w, (HWND)l); }
    case WM_DRAWITEM: DrawOwner((const DRAWITEMSTRUCT*)l); return TRUE;
    case WM_COMMAND: Command(LOWORD(w), HIWORD(w)); return 0;
    case WM_SIZE: Layout(); return 0;
    case WM_GETMINMAXINFO: { MINMAXINFO* mm = (MINMAXINFO*)l; mm->ptMinTrackSize = {980, 640}; return 0; }
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: E.wnd = nullptr; E.canvas = nullptr; return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// a one-line question (the set's name, a puzzle to import)
struct AskState { std::string answer; bool done = false, ok = false; HWND edit = nullptr; };
AskState* g_ask = nullptr;
LRESULT CALLBACK AskProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_ERASEBKGND) { RECT rc; GetClientRect(h, &rc); th::FillBg((HDC)w, h, rc); return 1; }
    if (m == WM_CTLCOLORSTATIC || m == WM_CTLCOLOREDIT || m == WM_CTLCOLORBTN) return th::CtlColor(m, (HDC)w, (HWND)l);
    if (m == WM_COMMAND && (LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL)) {
        if (g_ask) { g_ask->ok = LOWORD(w) == IDOK; wchar_t b[4096] = {}; GetWindowTextW(g_ask->edit, b, 4096); g_ask->answer = Nd(b); g_ask->done = true; }
        DestroyWindow(h); return 0;
    }
    if (m == WM_CLOSE) { if (g_ask) g_ask->done = true; DestroyWindow(h); return 0; }
    return DefWindowProcW(h, m, w, l);
}
std::string Ask(const wchar_t* title, const wchar_t* prompt, const std::string& start) {
    static bool reg; if (!reg) { WNDCLASSW wc = {}; wc.lpfnWndProc = AskProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"WitnessPuzzleAsk"; wc.hbrBackground = th::BgBrush(); wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); RegisterClassW(&wc); reg = true; }
    RECT pr; GetWindowRect(E.wnd, &pr);
    HWND d = CreateWindowExW(WS_EX_DLGMODALFRAME, L"WitnessPuzzleAsk", title, WS_CAPTION | WS_SYSMENU | WS_VISIBLE, pr.left + 120, pr.top + 120, 560, 210, E.wnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int hh, int id) { HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, hh, d, (HMENU)(INT_PTR)id, nullptr, nullptr); SendMessageW(c, WM_SETFONT, (WPARAM)E.font, TRUE); th::Style(c); return c; };
    mk(L"STATIC", prompt, SS_LEFT, 12, 10, 520, 54, 0);
    AskState st; st.edit = mk(L"EDIT", Wd(start).c_str(), WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 12, 70, 520, 28, 1);
    mk(L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, 312, 116, 106, 32, IDOK); mk(L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, 426, 116, 106, 32, IDCANCEL);
    SendMessageW(st.edit, EM_SETSEL, 0, -1); SetFocus(st.edit);
    g_ask = &st; EnableWindow(E.wnd, FALSE);
    MSG msg; while (!st.done && GetMessageW(&msg, nullptr, 0, 0)) { if (!IsDialogMessageW(d, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
    EnableWindow(E.wnd, TRUE); SetForegroundWindow(E.wnd); g_ask = nullptr;
    return st.ok ? st.answer : std::string();
}

}  // namespace

// ---- the library --------------------------------------------------------------------------------------------------------------------------
std::wstring PuzzlesDir(const std::wstring& dataDir) {
    std::wstring d = dataDir; if (!d.empty() && d.back() != L'\\') d += L'\\';
    d += L"puzzles\\"; CreateDirectoryW(d.c_str(), nullptr); CreateDirectoryW((d + L"sets").c_str(), nullptr);
    return d;
}
std::string SafeName(const std::string& name) {
    std::string s; for (char c : name) { if (strchr("\\/:*?\"<>|", c) || (unsigned char)c < 32) continue; s += c; }
    while (!s.empty() && (s.back() == ' ' || s.back() == '.')) s.pop_back(); while (!s.empty() && s[0] == ' ') s.erase(0, 1);
    if (s.size() > 60) s.resize(60);
    return s.empty() ? "Puzzles" : s;
}
std::vector<std::string> ListSets(const std::wstring& dataDir) {
    std::vector<std::string> out; WIN32_FIND_DATAW f; HANDLE h = FindFirstFileW((PuzzlesDir(dataDir) + L"sets\\*.txt").c_str(), &f);
    if (h != INVALID_HANDLE_VALUE) { do { std::wstring n = f.cFileName; if (n.size() > 4) out.push_back(Nd(n.substr(0, n.size() - 4))); } while (FindNextFileW(h, &f)); FindClose(h); }
    std::sort(out.begin(), out.end()); return out;
}
bool LoadSet(const std::wstring& dataDir, const std::string& name, wpz::Set* out, std::string* err) {
    std::string t; if (!ReadText(PuzzlesDir(dataDir) + L"sets\\" + Wd(SafeName(name)) + L".txt", &t)) { if (err) *err = "no such set"; return false; }
    if (!wpz::Parse(t, out, err)) return false;
    out->name = name; return true;
}
bool SaveSet(const std::wstring& dataDir, const std::string& name, const wpz::Set& set) {
    wpz::Set s = set; s.name = name; return WriteText(PuzzlesDir(dataDir) + L"sets\\" + Wd(SafeName(name)) + L".txt", wpz::Print(s));
}
std::string PublishSet(const std::wstring& dataDir, const wpz::Set& set) {
    std::string text = wpz::Print(set), hash = wpz::Hash(text);
    std::wstring p = PuzzlesDir(dataDir) + Wd(hash) + L".txt";
    if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) WriteText(p, text);
    return hash;
}
bool HaveSet(const std::wstring& dataDir, const std::string& hash) { return !SetText(dataDir, hash).empty(); }
std::string SetText(const std::wstring& dataDir, const std::string& hash) {
    if (hash.size() != 16 || hash.find_first_not_of("0123456789abcdef") != std::string::npos) return "";
    std::string t; if (!ReadText(PuzzlesDir(dataDir) + Wd(hash) + L".txt", &t) || wpz::Hash(t) != hash) return "";
    return t;
}
bool StoreSet(const std::wstring& dataDir, const std::string& hash, const std::string& text) {
    wpz::Set s; if (wpz::Hash(text) != hash || !wpz::Parse(text, &s)) return false;
    return WriteText(PuzzlesDir(dataDir) + Wd(hash) + L".txt", text);
}

bool EditorOpen() { return E.wnd != nullptr; }
void EditorTest(const std::string& c) {      // tests and pictures: "select <n>", "solve", "count", "try", "tool <n>"
    if (!E.wnd) return;
    std::string w = c.substr(0, c.find(' ')), a = c.find(' ') == std::string::npos ? "" : c.substr(c.find(' ') + 1);
    if (w == "select") { E.cur = std::max(0, std::min(atoi(a.c_str()), (int)E.set.puzzles.size() - 1)); FillList(); LoadControls(); }
    else if (w == "solve") Solve(false);
    else if (w == "count") Solve(true);
    else if (w == "try") { SetTrying(true); Redraw(); }
    else if (w == "tool") { E.brush.tool = (Tool)std::max(0, std::min(atoi(a.c_str()), (int)kToolCount - 1)); RedrawOwners(); }
    UpdateWindow(E.wnd);
}
std::string EditorStat() {
    return std::string("editor open ") + (E.wnd ? "1" : "0") + " status \"" + E.status + "\" set " + (E.setName.empty() ? "-" : SafeName(E.setName)) + " puzzles " + std::to_string(E.set.puzzles.size()) +
           " tool " + ToolName(E.brush.tool) + " trying " + (E.trying ? "1" : "0");
}

void OpenEditor(HWND owner, const std::wstring& dataDir, const std::string& setName, void (*changed)()) {
    if (E.wnd) { ShowWindow(E.wnd, SW_RESTORE); SetForegroundWindow(E.wnd); if (!setName.empty() && setName != E.setName) OpenSet(setName); return; }
    E.data = dataDir; E.changed = changed;
    static bool reg; if (!reg) {
        WNDCLASSW wc = {}; wc.lpfnWndProc = EdProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"WitnessPuzzleEditor"; wc.hbrBackground = th::BgBrush(); wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = (HICON)SendMessageW(owner, WM_GETICON, ICON_BIG, 0); RegisterClassW(&wc);
        WNDCLASSW cc = {}; cc.lpfnWndProc = CanvasProc; cc.hInstance = GetModuleHandleW(nullptr); cc.lpszClassName = L"WitnessPuzzleCanvas"; cc.hCursor = LoadCursorW(nullptr, IDC_HAND); RegisterClassW(&cc);
        reg = true;
    }
    E.font = (HFONT)SendMessageW(owner, WM_GETFONT, 0, 0); if (!E.font) E.font = th::Body();
    E.wnd = CreateWindowExW(0, L"WitnessPuzzleEditor", L"Puzzle editor", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1240, 800, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    E.canvas = CreateWindowExW(0, L"WitnessPuzzleCanvas", L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, E.wnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    Mk(L"STATIC", L"Puzzle set", SS_LEFT, ID_LABEL);
    Mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_SETS);
    Mk(L"BUTTON", L"New set", BS_PUSHBUTTON | WS_TABSTOP, ID_SET_NEW); Mk(L"BUTTON", L"Rename", BS_PUSHBUTTON | WS_TABSTOP, ID_SET_RENAME); Mk(L"BUTTON", L"Delete", BS_PUSHBUTTON | WS_TABSTOP, ID_SET_DELETE);
    Mk(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_TABSTOP, ID_LIST);
    Mk(L"BUTTON", L"Add", BS_PUSHBUTTON | WS_TABSTOP, ID_ADD); Mk(L"BUTTON", L"Copy", BS_PUSHBUTTON | WS_TABSTOP, ID_COPY); Mk(L"BUTTON", L"Remove", BS_PUSHBUTTON | WS_TABSTOP, ID_REMOVE);
    Mk(L"BUTTON", L"Move up", BS_PUSHBUTTON | WS_TABSTOP, ID_UP); Mk(L"BUTTON", L"Move down", BS_PUSHBUTTON | WS_TABSTOP, ID_DOWN);
    Mk(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, ID_NAME); SendMessageW(Ctl(ID_NAME), EM_SETCUEBANNER, TRUE, (LPARAM)L"Name of this puzzle");
    for (int id : {ID_W, ID_H}) { HWND c = Mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, id); for (int k = wpz::kMinSize; k <= wpz::kMaxSize; k++) SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)(std::to_wstring(k) + (id == ID_W ? L" across" : L" down")).c_str()); }
    HWND sym = Mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, ID_SYM);
    for (const wchar_t* t : {L"No symmetry", L"Mirrored left-right", L"Mirrored top-bottom", L"Turned half round"}) SendMessageW(sym, CB_ADDSTRING, 0, (LPARAM)t);
    HWND pan = Mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_PANEL);
    SendMessageW(pan, CB_ADDSTRING, 0, (LPARAM)L"Panel: the next free one");
    for (auto& s : wpz::Slots()) SendMessageW(pan, CB_ADDSTRING, 0, (LPARAM)Wd(std::string("Panel: ") + s.area + ", " + s.name).c_str());
    for (int t = 0; t < kToolCount; t++) Mk(L"BUTTON", Wd(ToolName((Tool)t)).c_str(), BS_OWNERDRAW | WS_TABSTOP, ID_TOOL0 + t);
    for (size_t k = 0; k < wpz::Colours().size(); k++) { HWND c = Mk(L"BUTTON", Wd(wpz::Colours()[k].name).c_str(), BS_OWNERDRAW | WS_TABSTOP, ID_COLOUR0 + (int)k); (void)c; }
    for (int k = 0; k < 16; k++) Mk(L"BUTTON", L"", BS_OWNERDRAW, ID_SHAPE0 + k);
    Mk(L"BUTTON", L"May be turned", BS_AUTOCHECKBOX | WS_TABSTOP, ID_ROTATE); Mk(L"BUTTON", L"Negative (blue)", BS_AUTOCHECKBOX | WS_TABSTOP, ID_NEGATIVE);
    HWND dk = Mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, ID_DOTKIND);
    for (const wchar_t* t : {L"Black dots", L"Blue dots", L"Yellow dots", L"Invisible dots"}) SendMessageW(dk, CB_ADDSTRING, 0, (LPARAM)t); SendMessageW(dk, CB_SETCURSEL, 0, 0);
    Mk(L"BUTTON", L"Open witnesspuzzles.com's editor", BS_PUSHBUTTON | WS_TABSTOP, ID_SITE);
    Mk(L"BUTTON", L"Try it", BS_PUSHBUTTON | WS_TABSTOP, ID_TRY); Mk(L"BUTTON", L"Show a solution", BS_PUSHBUTTON | WS_TABSTOP, ID_SOLVE);
    Mk(L"BUTTON", L"Count solutions", BS_PUSHBUTTON | WS_TABSTOP, ID_CHECK); Mk(L"BUTTON", L"Undo", BS_PUSHBUTTON | WS_TABSTOP, ID_UNDO);
    Mk(L"BUTTON", L"Import...", BS_PUSHBUTTON | WS_TABSTOP, ID_IMPORT); Mk(L"BUTTON", L"Copy for the web", BS_PUSHBUTTON | WS_TABSTOP, ID_EXPORT);
    Mk(L"STATIC", L"", SS_LEFT, ID_STATUS);
    th::MarkMuted(Mk(L"STATIC", L"The rules, the solver and the puzzle text are from jbzdarkid's witnesspuzzles.com (BSD licence); the game draws and checks the puzzles itself.", SS_LEFT, ID_CREDIT));
    std::string name = setName; auto names = ListSets(E.data);
    if (name.empty()) name = names.empty() ? "My puzzles" : names[0];
    OpenSet(name);
    Layout(); ShowWindow(E.wnd, SW_SHOW); SetForegroundWindow(E.wnd);
}

}  // namespace ped
