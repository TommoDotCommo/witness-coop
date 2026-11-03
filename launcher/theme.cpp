// See theme.h.
#include "theme.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>
#include "../common/gamefont.h"

namespace th {
COLORREF kBg = RGB(13, 19, 22), kSurface = RGB(21, 31, 36), kSurface2 = RGB(32, 46, 53), kLine = RGB(207, 233, 241), kAccent = RGB(78, 224, 255), kWarm = RGB(255, 200, 87), kText = RGB(230, 238, 240), kMuted = RGB(126, 147, 155), kDanger = RGB(255, 107, 107);
static HFONT g_body, g_small, g_bold, g_title, g_tile; static HBRUSH g_bg, g_surface; static std::wstring g_face;      // g_face: the game's face when it was read, else empty
static HFONT Font(int px, int weight, const wchar_t* face) { return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face); }
// The game's menu face for the headings: read from the player's own archive into memory (common/gamefont.h) and added as a private font; nothing is written.
// Body text stays Segoe UI (Karmina is a bold display serif: fine for a label, tiring for a paragraph). Without the game files: Segoe UI throughout, today's look.
static std::vector<uint8_t> g_faceBytes; static std::string g_fontState = "Segoe UI (no game folder)";
static std::wstring GameFace(const std::wstring& dir) {
    if (dir.empty()) return L"";
    gamefont::Result r = gamefont::FromZipFile((dir + L"\\data-pc.zip").c_str(), "KarminaBold.mod_otf.raw");
    if (!r.ok()) { g_fontState = "Segoe UI (" + r.error + ")"; return L""; }
    DWORD n = 0; g_faceBytes.swap(r.bytes);
    if (!AddFontMemResourceEx(g_faceBytes.data(), (DWORD)g_faceBytes.size(), nullptr, &n) || !n) { g_fontState = "Segoe UI (AddFontMemResourceEx failed)"; return L""; }
    std::wstring fam(r.family.begin(), r.family.end()); HFONT f = Font(16, FW_BOLD, fam.c_str()); HDC dc = CreateCompatibleDC(nullptr); HGDIOBJ o = SelectObject(dc, f);
    wchar_t got[64] = {}; GetTextFaceW(dc, 64, got); SelectObject(dc, o); DeleteDC(dc); DeleteObject(f);
    if (fam != got) { g_fontState = "Segoe UI (GDI chose another face)"; return L""; }
    g_fontState = r.family + " (game)"; return fam;
}
const std::string& FontState() { return g_fontState; }
void Init(const std::wstring& gameDir) {
    if (g_body) return;
    g_body = Font(15, FW_NORMAL, L"Segoe UI"); g_small = Font(13, FW_NORMAL, L"Segoe UI");
    std::wstring face = GameFace(gameDir); g_face = face;
    if (!face.empty()) { g_bold = Font(16, FW_BOLD, face.c_str()); g_title = Font(30, FW_BOLD, face.c_str()); g_tile = Font(20, FW_BOLD, face.c_str()); }      // (Karmina's capitals are a little smaller than Segoe's at one size: 16 px matches the 15 px Semibold)
    else { g_bold = Font(15, FW_SEMIBOLD, L"Segoe UI Semibold"); g_title = Font(30, FW_LIGHT, L"Segoe UI Light"); g_tile = Font(18, FW_SEMIBOLD, L"Segoe UI Semibold"); }
    g_bg = CreateSolidBrush(kBg); g_surface = CreateSolidBrush(kSurface);
}
HFONT Body() { return g_body; } HFONT Small() { return g_small; } HFONT Bold() { return g_bold; } HFONT Title() { return g_title; }
HBRUSH BgBrush() { return g_bg; } HBRUSH SurfaceBrush() { return g_surface; }
static std::wstring ClassOf(HWND h) { wchar_t c[40] = {}; GetClassNameW(h, c, 40); return c; }
void MarkMuted(HWND h) { SetPropW(h, L"muted", (HANDLE)1); SendMessageW(h, WM_SETFONT, (WPARAM)g_small, TRUE); }

void DrawDot(HDC dc, int cx, int cy, int r, COLORREF c, bool filled) {
    HPEN pen = CreatePen(PS_SOLID, 2, c); HBRUSH br = filled ? CreateSolidBrush(c) : (HBRUSH)GetStockObject(NULL_BRUSH); HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, br);
    Ellipse(dc, cx - r, cy - r, cx + r + 1, cy + r + 1); SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(pen); if (filled) DeleteObject(br);
}

static COLORREF Mix(COLORREF a, COLORREF b, float t) { return RGB(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t, GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t, GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t); }
// A Play choice as a tile: a card with the puzzle's start circle (lit and a first stroke of line when chosen), the name in the game's face, the description under it.
static void DrawTile(HWND h, HDC dc, const RECT& rc, const wchar_t* name, bool checked, bool hot, bool dis) {
    COLORREF fill = checked ? Mix(kSurface, kWarm, 0.10f) : (hot ? kSurface2 : kSurface), edge = checked ? kWarm : (hot ? kMuted : kSurface2);
    HBRUSH fb = CreateSolidBrush(fill); HPEN pen = CreatePen(PS_SOLID, checked ? 2 : 1, edge); HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, fb);
    RoundRect(dc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1, 10, 10); SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(pen); DeleteObject(fb);
    int r = 9, cx = 18 + r, cy = 16 + r;
    if (checked) { LOGBRUSH lb = {BS_SOLID, kWarm, 0}; HPEN lp = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND, 5, &lb, 0, nullptr); HGDIOBJ o = SelectObject(dc, lp); MoveToEx(dc, cx, cy, nullptr); LineTo(dc, cx, rc.bottom - 14); SelectObject(dc, o); DeleteObject(lp); }
    DrawDot(dc, cx, cy, r, dis ? kSurface2 : (checked ? kWarm : (hot ? kLine : kMuted)), checked);
    SetBkMode(dc, TRANSPARENT); SelectObject(dc, g_tile); SetTextColor(dc, dis ? kMuted : (checked ? kWarm : kText));
    RECT nr = {cx + r + 14, rc.top + 10, rc.right - 12, rc.top + 42}; DrawTextW(dc, name, -1, &nr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    wchar_t desc[300] = {}; if (HWND d = (HWND)GetPropW(h, L"desc")) GetWindowTextW(d, desc, 300);
    SelectObject(dc, g_small); SetTextColor(dc, checked ? kText : kMuted); RECT dr = {nr.left, rc.top + 44, rc.right - 12, rc.bottom - 8}; DrawTextW(dc, desc, -1, &dr, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
    if (GetFocus() == h && !dis) { HPEN fp = CreatePen(PS_SOLID, 2, kLine); HGDIOBJ o1 = SelectObject(dc, fp), o2 = SelectObject(dc, GetStockObject(NULL_BRUSH)); RoundRect(dc, rc.left + 4, rc.top + 4, rc.right - 4, rc.bottom - 4, 8, 8); SelectObject(dc, o1); SelectObject(dc, o2); DeleteObject(fp); }
}
void MarkTile(HWND radio, HWND desc) { SetPropW(radio, L"tile", (HANDLE)1); SetPropW(radio, L"desc", desc); if (desc) ShowWindow(desc, SW_HIDE); }

static LRESULT CALLBACK ButtonProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE: if (!GetPropW(h, L"hot")) { SetPropW(h, L"hot", (HANDLE)1); TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE, h, 0}; TrackMouseEvent(&t); InvalidateRect(h, nullptr, FALSE); } break;
    case WM_MOUSELEAVE: SetPropW(h, L"hot", nullptr); InvalidateRect(h, nullptr, FALSE); break;
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case BM_SETCHECK: case WM_ENABLE: case WM_SETFOCUS: case WM_KILLFOCUS: { LRESULT r = DefSubclassProc(h, m, w, l); InvalidateRect(h, nullptr, FALSE); return r; }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc); HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom); HGDIOBJ ob = SelectObject(mem, bmp);
        HBRUSH bg = CreateSolidBrush(kBg); FillRect(mem, &rc, bg); DeleteObject(bg);
        wchar_t text[200] = {}; GetWindowTextW(h, text, 200); LONG style = GetWindowLongW(h, GWL_STYLE); int type = style & BS_TYPEMASK;
        bool dis = !IsWindowEnabled(h), hot = GetPropW(h, L"hot") != nullptr, down = (SendMessageW(h, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0, checked = SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
        SelectObject(mem, g_body); SetBkMode(mem, TRANSPARENT);
        if (GetPropW(h, L"tile")) DrawTile(h, mem, rc, text, checked, hot, dis);
        else if (type == BS_AUTOCHECKBOX || type == BS_CHECKBOX || type == BS_AUTORADIOBUTTON || type == BS_RADIOBUTTON) {
            int r = 8, cy = rc.bottom / 2, cx = r + 1; COLORREF edge = dis ? kSurface2 : (hot ? kAccent : kMuted);
            bool radio = type == BS_AUTORADIOBUTTON || type == BS_RADIOBUTTON; COLORREF lit = radio ? kWarm : kAccent;      // a choice lights in the game's yellow, an on/off switch in the cyan of a powered line
            DrawDot(mem, cx, cy, r, edge, false); if (checked) DrawDot(mem, cx, cy, r - 4, dis ? kMuted : lit, true);
            RECT tr = rc; tr.left = 2 * r + 10; SetTextColor(mem, dis ? kMuted : kText); DrawTextW(mem, text, -1, &tr, DT_VCENTER | DT_SINGLELINE | DT_LEFT | DT_END_ELLIPSIS);
            if (GetFocus() == h) { HPEN pen = CreatePen(PS_DOT, 1, kMuted); HGDIOBJ op = SelectObject(mem, pen); SelectObject(mem, GetStockObject(NULL_BRUSH)); Rectangle(mem, rc.left, rc.top, rc.right, rc.bottom); SelectObject(mem, op); DeleteObject(pen); }
        } else {
            bool primary = type == BS_DEFPUSHBUTTON; COLORREF fill = primary ? (dis ? kSurface2 : (down ? kLine : (hot ? Mix(kWarm, kLine, 0.25f) : kWarm))) : (down ? kSurface : (hot ? kSurface2 : kSurface)), edge = primary ? fill : (dis ? kSurface2 : (hot ? kAccent : kMuted));
            HBRUSH fb = CreateSolidBrush(fill); HPEN pen = CreatePen(PS_SOLID, 1, edge); HGDIOBJ op = SelectObject(mem, pen), obr = SelectObject(mem, fb); RoundRect(mem, rc.left, rc.top, rc.right, rc.bottom, 6, 6); SelectObject(mem, op); SelectObject(mem, obr); DeleteObject(pen); DeleteObject(fb);
            if (GetFocus() == h && !dis) { HPEN fp = CreatePen(PS_SOLID, 2, primary ? kLine : kAccent); HGDIOBJ o1 = SelectObject(mem, fp), o2 = SelectObject(mem, GetStockObject(NULL_BRUSH)); RoundRect(mem, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1, 6, 6); SelectObject(mem, o1); SelectObject(mem, o2); DeleteObject(fp); }      // the focus (keyboard and pad users see where they are)
            SelectObject(mem, g_bold); SetTextColor(mem, primary ? (dis ? kMuted : kBg) : (dis ? kMuted : kText)); DrawTextW(mem, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY); SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem); EndPaint(h, &ps); return 0;
    }
    case WM_NCDESTROY: RemoveWindowSubclass(h, ButtonProc, 1); break;
    }
    return DefSubclassProc(h, m, w, l);
}

void Style(HWND h) {
    std::wstring c = ClassOf(h);
    if (c == L"Button") { SetWindowSubclass(h, ButtonProc, 1, 0); SendMessageW(h, WM_SETFONT, (WPARAM)g_body, TRUE); }
    else if (c == L"Edit") { SetWindowTheme(h, (GetWindowLongW(h, GWL_STYLE) & ES_MULTILINE) ? L"DarkMode_Explorer" : L"", (GetWindowLongW(h, GWL_STYLE) & ES_MULTILINE) ? nullptr : L""); SendMessageW(h, WM_SETFONT, (WPARAM)g_body, TRUE); }
    else if (c == L"ComboBox") { SetWindowTheme(h, L"DarkMode_CFD", nullptr); SendMessageW(h, WM_SETFONT, (WPARAM)g_body, TRUE); }
    else if (c == L"ListBox") { SetWindowTheme(h, L"DarkMode_Explorer", nullptr); SendMessageW(h, WM_SETFONT, (WPARAM)g_body, TRUE); }
    else if (c == L"Static") SendMessageW(h, WM_SETFONT, (WPARAM)g_body, TRUE);
}
LRESULT CtlColor(UINT msg, HDC dc, HWND ctl) {
    std::wstring c = ClassOf(ctl);
    if (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX || c == L"Edit" || c == L"ListBox") { SetTextColor(dc, kText); SetBkColor(dc, kSurface); return (LRESULT)g_surface; }
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, GetPropW(ctl, L"muted") ? kMuted : kText); SetBkColor(dc, kBg); return (LRESULT)g_bg;
}

static volatile LONG g_tabPaints; unsigned TabPaintCount() { return (unsigned)g_tabPaints; }
static LRESULT CALLBACK TabProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE: {      // repaint only when the tab under the pointer changes (repainting on every move made the row flicker)
        TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE, h, 0}; TrackMouseEvent(&t); TCHITTESTINFO hi = {{(short)LOWORD(l), (short)HIWORD(l)}, 0}; int idx = TabCtrl_HitTest(h, &hi);
#ifdef WL_MUT_TAB_REPAINT_ALWAYS
        SetPropW(h, L"hot", (HANDLE)(INT_PTR)(idx + 1)); InvalidateRect(h, nullptr, FALSE); (void)0;
#endif
        if (idx != (int)(INT_PTR)GetPropW(h, L"hot") - 1) { SetPropW(h, L"hot", (HANDLE)(INT_PTR)(idx + 1)); InvalidateRect(h, nullptr, FALSE); }
        return 0; }      // (not passed on: the tab control's own handler repaints on every move)
    case WM_MOUSELEAVE: SetPropW(h, L"hot", nullptr); InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_PAINT: {
        InterlockedIncrement(&g_tabPaints); PAINTSTRUCT ps; HDC wdc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc); HDC dc = CreateCompatibleDC(wdc); HBITMAP bmp = CreateCompatibleBitmap(wdc, rc.right, rc.bottom); HGDIOBJ obmp = SelectObject(dc, bmp);      // drawn off screen and copied once: no flicker
        HBRUSH bg = CreateSolidBrush(kBg); FillRect(dc, &rc, bg); DeleteObject(bg);
        int hotTab = (int)(INT_PTR)GetPropW(h, L"hot") - 1; int n = TabCtrl_GetItemCount(h), sel = TabCtrl_GetCurSel(h); SetBkMode(dc, TRANSPARENT); SelectObject(dc, g_bold);
        RECT first = {}; if (n) TabCtrl_GetItemRect(h, 0, &first); int stripBottom = first.bottom;
        HPEN lp = CreatePen(PS_SOLID, 1, kSurface2); HGDIOBJ op = SelectObject(dc, lp); MoveToEx(dc, rc.left, stripBottom, nullptr); LineTo(dc, rc.right, stripBottom); SelectObject(dc, op); DeleteObject(lp);
        for (int i = 0; i < n; ++i) {
            RECT r; TabCtrl_GetItemRect(h, i, &r); wchar_t t[80] = {}; TCITEMW it = {}; it.mask = TCIF_TEXT; it.pszText = t; it.cchTextMax = 80; TabCtrl_GetItem(h, i, &it);
            bool hot = i == hotTab; SetTextColor(dc, i == sel ? kText : (hot ? kLine : kMuted)); RECT tr = r; DrawTextW(dc, t, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (i == sel) { HBRUSH ab = CreateSolidBrush(kWarm); RECT ar = {r.left + 8, stripBottom - 3, r.right - 8, stripBottom}; FillRect(dc, &ar, ab); DeleteObject(ab); }
        }
        BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY); SelectObject(dc, obmp); DeleteObject(bmp); DeleteDC(dc); EndPaint(h, &ps); return 0;
    }
    case WM_NCDESTROY: RemoveWindowSubclass(h, TabProc, 2); break;
    }
    return DefSubclassProc(h, m, w, l);
}
void StyleTab(HWND tab) { SetWindowSubclass(tab, TabProc, 2, 0); SendMessageW(tab, WM_SETFONT, (WPARAM)g_bold, TRUE); TabCtrl_SetItemSize(tab, 112, 34); TabCtrl_SetPadding(tab, 12, 6); }
void StyleList(HWND list) {
    SetWindowTheme(list, L"DarkMode_Explorer", nullptr); ListView_SetBkColor(list, kSurface); ListView_SetTextBkColor(list, kSurface); ListView_SetTextColor(list, kText); SendMessageW(list, WM_SETFONT, (WPARAM)g_body, TRUE);
    HWND hdr = ListView_GetHeader(list); if (hdr) SetWindowTheme(hdr, L"DarkMode_ItemsView", nullptr);
}

// ---- the banner's art, drawn in software into a 32-bit buffer (anti-aliased shapes; GDI's own circles and lines have no edge smoothing)
struct Canvas { int w, h; uint32_t* px; };
static inline void Blend(Canvas& c, int x, int y, COLORREF col, float a) {
    if (x < 0 || y < 0 || x >= c.w || y >= c.h || a <= 0) return; if (a > 1) a = 1;
    uint32_t& d = c.px[(size_t)y * c.w + x]; float db = float(d & 255), dg = float((d >> 8) & 255), dr = float((d >> 16) & 255);
    dr += (GetRValue(col) - dr) * a; dg += (GetGValue(col) - dg) * a; db += (GetBValue(col) - db) * a;
    d = 0xFF000000u | (uint32_t(dr + .5f) << 16) | (uint32_t(dg + .5f) << 8) | uint32_t(db + .5f);
}
static float SegDist(float px, float py, float ax, float ay, float bx, float by) {
    float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay, l = vx * vx + vy * vy, t = l > 0 ? (wx * vx + wy * vy) / l : 0; t = t < 0 ? 0 : t > 1 ? 1 : t;
    float dx = wx - t * vx, dy = wy - t * vy; return sqrtf(dx * dx + dy * dy);
}
static float RoundRectDist(float px, float py, float x, float y, float w, float h, float r) {
    float qx = fabsf(px - (x + w / 2)) - (w / 2 - r), qy = fabsf(py - (y + h / 2)) - (h / 2 - r), ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    return sqrtf(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.f) - r;
}
// fill where the signed distance is below zero, with a one-pixel soft edge; soft > 0: a glow that fades out over `soft` pixels instead
template <class F> static void Shape(Canvas& c, float x0, float y0, float x1, float y1, COLORREF col, float alpha, float soft, F sd) {
    int ya = std::max(0, int(y0 - soft - 1)), yb = std::min(c.h, int(y1 + soft + 2)), xa = std::max(0, int(x0 - soft - 1)), xb = std::min(c.w, int(x1 + soft + 2));
    for (int y = ya; y < yb; ++y) for (int x = xa; x < xb; ++x) {
        float d = sd(x + .5f, y + .5f), a = soft > 0 ? (d <= 0 ? 1.f : d >= soft ? 0.f : (1 - d / soft) * (1 - d / soft)) : (d <= -.5f ? 1.f : d >= .5f ? 0.f : .5f - d);
        if (a > 0) Blend(c, x, y, col, a * alpha);
    }
}
static void Line(Canvas& c, const float* p, int n, float half, COLORREF col, float alpha, float soft = 0) {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f; for (int i = 0; i < n; ++i) { x0 = std::min(x0, p[2 * i]); x1 = std::max(x1, p[2 * i]); y0 = std::min(y0, p[2 * i + 1]); y1 = std::max(y1, p[2 * i + 1]); }
    Shape(c, x0 - half, y0 - half, x1 + half, y1 + half, col, alpha, soft, [&](float x, float y) { float d = 1e9f; for (int i = 0; i + 1 < n; ++i) d = std::min(d, SegDist(x, y, p[2 * i], p[2 * i + 1], p[2 * i + 2], p[2 * i + 3])); return d - half; });
}
static void Disc(Canvas& c, float cx, float cy, float r, COLORREF col, float alpha, float soft = 0) { Shape(c, cx - r, cy - r, cx + r, cy + r, col, alpha, soft, [&](float x, float y) { return sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy)) - r; }); }
// The picture scaled to cover the canvas (bilinear up, area average down), `focusY` of the spare height above the crop (0 top, 1 bottom).
static void Cover(Canvas& c, const uint8_t* rgba, int w, int h, float focusY) {
    float k = std::max(float(c.w) / w, float(c.h) / h), ox = (w - c.w / k) / 2, oy = (h - c.h / k) * focusY;
    for (int y = 0; y < c.h; ++y) for (int x = 0; x < c.w; ++x) {
        float r = 0, g = 0, b = 0;
        if (k >= 1) {
            float u = (x + .5f) / k + ox - .5f, v = (y + .5f) / k + oy - .5f; int u0 = std::max(0, std::min(w - 1, int(floorf(u)))), v0 = std::max(0, std::min(h - 1, int(floorf(v)))), u1 = std::min(w - 1, u0 + 1), v1 = std::min(h - 1, v0 + 1);
            float fu = std::max(0.f, std::min(1.f, u - u0)), fv = std::max(0.f, std::min(1.f, v - v0));
            const uint8_t *a = rgba + ((size_t)v0 * w + u0) * 4, *bq = rgba + ((size_t)v0 * w + u1) * 4, *cq = rgba + ((size_t)v1 * w + u0) * 4, *dq = rgba + ((size_t)v1 * w + u1) * 4;
            for (int ch = 0; ch < 3; ++ch) { float t = (a[ch] * (1 - fu) + bq[ch] * fu) * (1 - fv) + (cq[ch] * (1 - fu) + dq[ch] * fu) * fv; (ch == 0 ? r : ch == 1 ? g : b) = t; }
        } else {
            int u0 = int(x / k + ox), u1 = std::max(u0 + 1, int((x + 1) / k + ox)), v0 = int(y / k + oy), v1 = std::max(v0 + 1, int((y + 1) / k + oy)), n = 0;
            for (int v = v0; v < v1 && v < h; ++v) for (int u = u0; u < u1 && u < w; ++u) { const uint8_t* q = rgba + ((size_t)v * w + u) * 4; r += q[0]; g += q[1]; b += q[2]; ++n; }
            if (n) { r /= n; g /= n; b /= n; }
        }
        c.px[(size_t)y * c.w + x] = 0xFF000000u | (uint32_t(r + .5f) << 16) | (uint32_t(g + .5f) << 8) | uint32_t(b + .5f);
    }
}
// A Witness puzzle panel: a yellow panel with its darker grid (4 x 3 cells), the start circle at the bottom left, the end cap past the top right corner, and a solved
// line traced in white along the grid from the start to the end, with the soft glow of a lit line.
static void Puzzle(Canvas& c, float x, float y, float w, float h) {
    const COLORREF body = RGB(240, 186, 56), body2 = RGB(222, 158, 38), grid = RGB(126, 90, 30), line = RGB(255, 251, 236);
    float r = std::min(w, h) * .07f;
    Shape(c, x, y, x + w, y + h, RGB(0, 0, 0), .45f, std::min(w, h) * .08f, [&](float px, float py) { return RoundRectDist(px, py, x + 3, y + 7, w, h, r); });      // its shadow
    Shape(c, x, y, x + w, y + h, body, 1, 0, [&](float px, float py) { return RoundRectDist(px, py, x, y, w, h, r); });
    Shape(c, x, y + h * .45f, x + w, y + h, body2, .55f, 0, [&](float px, float py) { return std::max(RoundRectDist(px, py, x, y, w, h, r), (y + h * .45f) - py); });      // the lower part a little deeper
    const int cols = 4, rows = 3; float m = std::min(w, h) * .15f, cell = std::min((w - 2 * m) / cols, (h - 2 * m) / rows), gx = x + (w - cols * cell) / 2, gy = y + (h - rows * cell) / 2, half = cell * .12f;
    auto nx = [&](float i) { return gx + i * cell; }; auto ny = [&](float j) { return gy + j * cell; };
    for (int j = 0; j <= rows; ++j) { float p[] = {nx(0), ny(j), nx(cols), ny(j)}; Line(c, p, 2, half, grid, 1); }
    for (int i = 0; i <= cols; ++i) { float p[] = {nx(i), ny(0), nx(i), ny(rows)}; Line(c, p, 2, half, grid, 1); }
    { float p[] = {nx(cols), ny(0), nx(cols) + cell * .34f, ny(0)}; Line(c, p, 2, half, grid, 1); }      // the end cap
    Disc(c, nx(0), ny(rows), half * 2.5f, grid, 1);                                                      // the start circle
    const float path[] = {nx(0), ny(3), nx(0), ny(1), nx(1), ny(1), nx(1), ny(2), nx(3), ny(2), nx(3), ny(0), nx(4), ny(0), nx(4) + cell * .34f, ny(0)};
    Line(c, path, 8, half, line, .35f, half * 3.5f); Disc(c, nx(0), ny(rows), half * 2.5f, line, .35f, half * 3.5f);      // the glow
    Line(c, path, 8, half, line, 1); Disc(c, nx(0), ny(rows), half * 2.5f, line, 1);
}
// A soft blur (two box passes each way): the game's save pictures are 320 x 180, so the banner shows them 4 to 6 times larger; blurred they read as a calm backdrop, not as blocks.
static void Blur(Canvas& c, int r) {
    if (r < 1) return; std::vector<uint32_t> tmp((size_t)c.w * c.h);
    auto pass = [&](const uint32_t* src, uint32_t* dst, int n, int lines, size_t step, size_t lineStep) {
        for (int l = 0; l < lines; ++l) {
            const uint32_t* s0 = src + l * lineStep; uint32_t* d0 = dst + l * lineStep; int sum[3] = {0, 0, 0};
            auto at = [&](int i) { i = i < 0 ? 0 : i >= n ? n - 1 : i; return s0[i * step]; };
            for (int i = -r; i <= r; ++i) { uint32_t v = at(i); sum[0] += v & 255; sum[1] += (v >> 8) & 255; sum[2] += (v >> 16) & 255; }
            for (int i = 0; i < n; ++i) {
                int k = 2 * r + 1; d0[i * step] = 0xFF000000u | (uint32_t(sum[2] / k) << 16) | (uint32_t(sum[1] / k) << 8) | uint32_t(sum[0] / k);
                uint32_t a = at(i + r + 1), b = at(i - r); sum[0] += int(a & 255) - int(b & 255); sum[1] += int((a >> 8) & 255) - int((b >> 8) & 255); sum[2] += int((a >> 16) & 255) - int((b >> 16) & 255);
            }
        }
    };
    for (int it = 0; it < 2; ++it) { pass(c.px, tmp.data(), c.w, c.h, 1, c.w); pass(tmp.data(), c.px, c.h, c.w, c.w, 1); }
}
static void Shade(Canvas& c, COLORREF col, const std::function<float(int, int)>& alpha) { for (int y = 0; y < c.h; ++y) for (int x = 0; x < c.w; ++x) Blend(c, x, y, col, alpha(x, y)); }

static std::vector<uint8_t> g_art; static int g_artW, g_artH; static unsigned g_artGen = 1; static std::string g_artState = "panel";
void SetArt(const uint8_t* rgba, int w, int h, const std::string& name) {
    if (rgba && w > 0 && h > 0) { g_art.assign(rgba, rgba + (size_t)w * h * 4); g_artW = w; g_artH = h; g_artState = "picture " + name + " " + std::to_string(w) + "x" + std::to_string(h); }
    else { g_art.clear(); g_artW = g_artH = 0; g_artState = "panel"; }
    ++g_artGen;
}
const std::string& ArtState() { return g_artState; }
int BannerHeight(int clientH) { return clientH < 640 ? 72 : clientH < 760 ? 100 : std::min(200, std::max(128, clientH * 16 / 100)); }      // 128 at 1280 x 800, 172 at 1080
static HBITMAP NewDib(HDC ref, int w, int h, uint32_t** bits) {
    BITMAPINFO bi = {}; bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB, 0, 0, 0, 0, 0}; void* p = nullptr;
    HBITMAP b = CreateDIBSection(ref, &bi, DIB_RGB_COLORS, &p, nullptr, 0); *bits = (uint32_t*)p; return b;
}
static HBITMAP g_banner; static int g_bannerW, g_bannerH; static unsigned g_bannerGen;
static void Compose(HDC ref, int W, int H, const wchar_t* subtitle) {
    uint32_t* bits = nullptr; HBITMAP bmp = NewDib(ref, W, H, &bits); if (!bmp || !bits) return; Canvas c{W, H, bits};
    if (!g_art.empty()) {      // the save's picture, then a calm shade: darker to the left where the title sits, fading into the window at the bottom
        Cover(c, g_art.data(), g_artW, g_artH, .42f); Blur(c, int(std::max(float(W) / g_artW, float(H) / g_artH) * .8f));
        Shade(c, kBg, [&](int x, int y) { float t = std::max(0.f, 1 - x / (W * .6f)), b = y > H * .5f ? (y - H * .5f) / (H * .5f) : 0; return std::max(.12f + .70f * powf(t, 1.3f), powf(b, 1.6f)); });
    } else {      // no picture: an evening sky over the drawn panel
        Shade(c, kBg, [](int, int) { return 1.f; });
        Shade(c, RGB(34, 66, 76), [&](int x, int y) { float t = 1 - float(y) / H; return .85f * t * t * (.55f + .45f * float(x) / W); });
        float ph = H * .72f, pw = ph * 1.3f; Puzzle(c, W - pw - std::max(28.f, W * .05f), (H - ph) / 2 - H * .03f, pw, ph);
    }
    HDC mem = CreateCompatibleDC(ref); HGDIOBJ ob = SelectObject(mem, bmp); SetBkMode(mem, TRANSPARENT);
    int tp = std::max(26, std::min(60, H * 27 / 100)), x0 = std::max(24, W * 3 / 100); bool compact = H < 110;
    HFONT title = Font(tp, FW_BOLD, g_face.empty() ? L"Segoe UI Light" : g_face.c_str()), coop = Font(std::max(14, tp / 2), FW_BOLD, g_face.empty() ? L"Segoe UI Semibold" : g_face.c_str());
    auto text = [&](HFONT f, int extra, const wchar_t* t, int x, int y, COLORREF col) { SelectObject(mem, f); SetTextCharacterExtra(mem, extra); SetTextColor(mem, RGB(0, 0, 0)); TextOutW(mem, x + 2, y + 2, t, (int)wcslen(t)); SetTextColor(mem, col); TextOutW(mem, x, y, t, (int)wcslen(t)); SIZE sz; GetTextExtentPoint32W(mem, t, (int)wcslen(t), &sz); return sz; };
    int ty = compact ? 8 : H * 16 / 100; SIZE ts = text(title, tp / 8, L"THE WITNESS", x0, ty, kText);
    int cy = compact ? ty + (ts.cy - std::max(14, tp / 2)) / 2 + 2 : ty + ts.cy + 2, cx = compact ? x0 + ts.cx + tp / 2 : x0 + 2; SIZE cs = text(coop, tp / 4, L"CO-OP", cx, cy, kWarm);
    SelectObject(mem, g_body); SetTextCharacterExtra(mem, 0); SetTextColor(mem, kLine); RECT sr = {x0 + 2, compact ? ty + ts.cy + 2 : cy + cs.cy + 6, W - 24, H - 4}; DrawTextW(mem, subtitle, -1, &sr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(mem, ob); DeleteDC(mem); DeleteObject(title); DeleteObject(coop);
    if (g_banner) DeleteObject(g_banner); g_banner = bmp; g_bannerW = W; g_bannerH = H; g_bannerGen = g_artGen;
}
void PaintHeader(HDC dc, const RECT& client, int height, const wchar_t* subtitle) {
    int W = client.right; if (W <= 0 || height <= 0) return;
    if (!g_banner || g_bannerW != W || g_bannerH != height || g_bannerGen != g_artGen) Compose(dc, W, height, subtitle);
    if (!g_banner) return; HDC mem = CreateCompatibleDC(dc); HGDIOBJ o = SelectObject(mem, g_banner); BitBlt(dc, 0, 0, W, height, mem, 0, 0, SRCCOPY); SelectObject(mem, o); DeleteDC(mem);
}
HBITMAP Thumb(const uint8_t* rgba, int w, int h, int tw, int th) {
    HDC screen = GetDC(nullptr); uint32_t* bits = nullptr; HBITMAP b = NewDib(screen, tw, th, &bits); ReleaseDC(nullptr, screen); if (!b || !bits) return b; Canvas c{tw, th, bits};
    if (rgba && w > 0 && h > 0) Cover(c, rgba, w, h, .42f);
    else { Shade(c, kSurface, [](int, int) { return 1.f; }); float ph = th * .78f, pw = std::min(tw * .8f, ph * 1.3f); Puzzle(c, (tw - pw) / 2, (th - ph) / 2, pw, ph); }
    return b;
}
}  // namespace th
