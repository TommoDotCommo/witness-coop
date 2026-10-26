// See theme.h.
#include "theme.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <cmath>
#include <string>

namespace th {
COLORREF kBg = RGB(13, 19, 22), kSurface = RGB(21, 31, 36), kSurface2 = RGB(32, 46, 53), kLine = RGB(207, 233, 241), kAccent = RGB(78, 224, 255), kWarm = RGB(255, 200, 87), kText = RGB(230, 238, 240), kMuted = RGB(126, 147, 155), kDanger = RGB(255, 107, 107);
static HFONT g_body, g_small, g_bold, g_title; static HBRUSH g_bg, g_surface;
static HFONT Font(int px, int weight, const wchar_t* face) { return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face); }
void Init() {
    if (g_body) return;
    g_body = Font(15, FW_NORMAL, L"Segoe UI"); g_small = Font(13, FW_NORMAL, L"Segoe UI"); g_bold = Font(15, FW_SEMIBOLD, L"Segoe UI Semibold"); g_title = Font(30, FW_LIGHT, L"Segoe UI Light");
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
        if (type == BS_AUTOCHECKBOX || type == BS_CHECKBOX || type == BS_AUTORADIOBUTTON || type == BS_RADIOBUTTON) {
            int r = 8, cy = rc.bottom / 2, cx = r + 1; COLORREF edge = dis ? kSurface2 : (hot ? kAccent : kMuted);
            DrawDot(mem, cx, cy, r, edge, false); if (checked) DrawDot(mem, cx, cy, r - 4, dis ? kMuted : kAccent, true);
            RECT tr = rc; tr.left = 2 * r + 10; SetTextColor(mem, dis ? kMuted : kText); DrawTextW(mem, text, -1, &tr, DT_VCENTER | DT_SINGLELINE | DT_LEFT | DT_END_ELLIPSIS);
            if (GetFocus() == h) { HPEN pen = CreatePen(PS_DOT, 1, kMuted); HGDIOBJ op = SelectObject(mem, pen); SelectObject(mem, GetStockObject(NULL_BRUSH)); Rectangle(mem, rc.left, rc.top, rc.right, rc.bottom); SelectObject(mem, op); DeleteObject(pen); }
        } else {
            bool primary = type == BS_DEFPUSHBUTTON; COLORREF fill = primary ? (dis ? kSurface2 : (down ? kLine : kAccent)) : (down ? kSurface : (hot ? kSurface2 : kSurface)), edge = primary ? fill : (dis ? kSurface2 : (hot ? kAccent : kMuted));
            HBRUSH fb = CreateSolidBrush(fill); HPEN pen = CreatePen(PS_SOLID, 1, edge); HGDIOBJ op = SelectObject(mem, pen), obr = SelectObject(mem, fb); RoundRect(mem, rc.left, rc.top, rc.right, rc.bottom, 6, 6); SelectObject(mem, op); SelectObject(mem, obr); DeleteObject(pen); DeleteObject(fb);
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
            if (i == sel) { HBRUSH ab = CreateSolidBrush(kAccent); RECT ar = {r.left + 8, stripBottom - 3, r.right - 8, stripBottom}; FillRect(dc, &ar, ab); DeleteObject(ab); }
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

// The banner: the name, in light capitals, and a small puzzle panel: a pale line from a start circle to the notch at the end, the first part of it with power.
void PaintHeader(HDC dc, const RECT& client, int height, const wchar_t* subtitle) {
    RECT band = {0, 0, client.right, height}; HBRUSH b = CreateSolidBrush(kBg); FillRect(dc, &band, b); DeleteObject(b);
    SetBkMode(dc, TRANSPARENT); SelectObject(dc, g_title); SetTextColor(dc, kText); const wchar_t* name = L"T H E   W I T N E S S"; RECT tr = {24, 10, client.right - 220, 56}; DrawTextW(dc, name, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SelectObject(dc, g_body); SetTextColor(dc, kAccent); RECT sr = {26, 52, client.right - 220, height - 4}; DrawTextW(dc, subtitle, -1, &sr, DT_LEFT | DT_SINGLELINE);
    int px = client.right - 196, py = 12, pw = 172, ph = height - 28;      // the panel
    HBRUSH pb = CreateSolidBrush(kSurface); HPEN pe = CreatePen(PS_SOLID, 1, kSurface2); HGDIOBJ op = SelectObject(dc, pe), ob = SelectObject(dc, pb); RoundRect(dc, px, py, px + pw, py + ph, 8, 8); SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(pe); DeleteObject(pb);
    POINT pts[] = {{px + 22, py + ph - 16}, {px + 22, py + 16}, {px + 66, py + 16}, {px + 66, py + ph - 20}, {px + 108, py + ph - 20}, {px + 108, py + 16}, {px + pw - 14, py + 16}};
    LOGBRUSH lb = {BS_SOLID, kLine, 0}; HPEN lineP = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND, 5, &lb, 0, nullptr); op = SelectObject(dc, lineP); Polyline(dc, pts, 7); SelectObject(dc, op); DeleteObject(lineP);
    LOGBRUSH lb2 = {BS_SOLID, kAccent, 0}; HPEN litP = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND, 5, &lb2, 0, nullptr); op = SelectObject(dc, litP); Polyline(dc, pts, 4); SelectObject(dc, op); DeleteObject(litP);
    DrawDot(dc, pts[0].x, pts[0].y, 7, kAccent, true);      // the start circle, lit
}
}  // namespace th
