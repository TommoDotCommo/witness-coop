// See theme.h.
#include "theme.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <d2d1.h>
#include <dwrite_3.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>
#include "../common/gamefont.h"
#include "trace.h"

namespace th {
COLORREF kBg = RGB(13, 19, 22), kSurface = RGB(21, 31, 36), kSurface2 = RGB(32, 46, 53), kLine = RGB(207, 233, 241), kAccent = RGB(78, 224, 255), kWarm = RGB(255, 200, 87), kText = RGB(230, 238, 240), kMuted = RGB(126, 147, 155), kDanger = RGB(255, 107, 107);
static HFONT g_body, g_small, g_bold, g_title, g_tile, g_label, g_tabFont; static HBRUSH g_bg, g_surface; static std::wstring g_face;      // g_face: the game's face when it was read, else empty
static HFONT Font(int px, int weight, const wchar_t* face) { return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face); }
// The game's menu face for the headings: read from the player's own archive into memory (common/gamefont.h) and added as a private font; nothing is written.
// Labels (row labels, "Your name", choice and switch text) use it too (owner, 2026-11-04); descriptions, hints, text boxes and lists stay Segoe UI
// (Karmina is a bold display serif: fine for a label, tiring for a paragraph). Without the game files: Segoe UI throughout, today's look.
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

// ---- text in the game's face through DirectWrite. Karmina is a CFF (PostScript-outline) OpenType font, and GDI draws it with coarse grey edges only, never ClearType, in
// every quality setting; at 16-20 px the owner saw that as pixelated (2026-11-06). DirectWrite draws it as smoothly as Segoe UI gets from GDI (ClearType, fractional
// positions, smoothed in both directions). The font goes to DirectWrite from the same bytes in memory (IDWriteFactory5's in-memory loader, Windows 10 1703 and later)
// and the text is drawn by Direct2D into the GDI DC, so the controls and the double buffers stay GDI. Without it (older Windows, a failure) GDI draws as before.
static IDWriteFactory5* g_dw; static IDWriteFontCollection1* g_dwFonts; static ID2D1Factory* g_d2; static ID2D1DCRenderTarget* g_rt; static float g_dwAscent = .8f, g_dwDescent = .2f;
template <class T> static void Rel(T*& p) { if (p) p->Release(); p = nullptr; }
static ID2D1DCRenderTarget* Target() {
    if (!g_rt && g_d2) {      // 96 dpi: one unit is one pixel, as GDI's layout is
        D2D1_RENDER_TARGET_PROPERTIES rp = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.f, 96.f);
        if (FAILED(g_d2->CreateDCRenderTarget(&rp, &g_rt))) g_rt = nullptr;
    }
    return g_rt;
}
// Shapes (the start circles, the line round a button) are drawn by Direct2D too: GDI's Ellipse and Polyline have no edge smoothing and looked pixelated (owner, 2026-11-06).
// Paint2D binds the render target to the box `b` of the DC and calls fn with the DC's own coordinates; false: no Direct2D, the caller draws with GDI.
static ID2D1StrokeStyle* g_round;
template <class F> static bool Paint2D(HDC dc, RECT b, F fn) {
#ifdef WL_MUT_GDI_SHAPES
    return false;      // mutant: shapes stay GDI's (the shape test must see hard, stepped edges)
#endif
    ID2D1DCRenderTarget* rt = Target(); if (!rt) return false; RECT clip; if (GetClipBox(dc, &clip) != ERROR) IntersectRect(&b, &b, &clip); if (b.right <= b.left || b.bottom <= b.top) return true;
    if (!g_round) { D2D1_STROKE_STYLE_PROPERTIES sp = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND); g_d2->CreateStrokeStyle(sp, nullptr, 0, &g_round); }
    if (FAILED(rt->BindDC(dc, &b))) return false;
    rt->BeginDraw(); rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE); rt->SetTransform(D2D1::Matrix3x2F::Translation(-float(b.left), -float(b.top))); fn(rt);
    rt->SetTransform(D2D1::Matrix3x2F::Identity()); if (rt->EndDraw() == (HRESULT)D2DERR_RECREATE_TARGET) Rel(g_rt); return true;
}
static D2D1_COLOR_F Col(COLORREF c, float a = 1) { return D2D1::ColorF(GetRValue(c) / 255.f, GetGValue(c) / 255.f, GetBValue(c) / 255.f, a); }
template <class F> static void WithBrush(ID2D1RenderTarget* rt, COLORREF c, float a, F fn) { ID2D1SolidColorBrush* br = nullptr; if (SUCCEEDED(rt->CreateSolidColorBrush(Col(c, a), &br))) { fn(br); br->Release(); } }
static void Disc2D(ID2D1RenderTarget* rt, float cx, float cy, float r, COLORREF c, float a = 1) { WithBrush(rt, c, a, [&](ID2D1Brush* b) { rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), b); }); }
static void Ring2D(ID2D1RenderTarget* rt, float cx, float cy, float r, float w, COLORREF c, float a = 1) { WithBrush(rt, c, a, [&](ID2D1Brush* b) { rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), b, w); }); }
static void Glow2D(ID2D1RenderTarget* rt, float cx, float cy, float r, COLORREF c, float a) {      // a soft light fading out to radius r
    D2D1_GRADIENT_STOP st[3] = {{0, Col(c, a)}, {.45f, Col(c, a * .55f)}, {1, Col(c, 0)}}; ID2D1GradientStopCollection* sc = nullptr; ID2D1RadialGradientBrush* br = nullptr;
    if (SUCCEEDED(rt->CreateGradientStopCollection(st, 3, &sc)) && SUCCEEDED(rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(cx, cy), D2D1::Point2F(0, 0), r, r), sc, &br))) rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), br);
    if (br) br->Release(); if (sc) sc->Release();
}
static void Poly2D(ID2D1RenderTarget* rt, const std::vector<trace::Pt>& p, float w, COLORREF c, float a = 1) {
    if (p.size() < 2) return; ID2D1PathGeometry* g = nullptr; ID2D1GeometrySink* k = nullptr; if (FAILED(g_d2->CreatePathGeometry(&g))) return;
    if (SUCCEEDED(g->Open(&k))) { k->BeginFigure(D2D1::Point2F(p[0].x, p[0].y), D2D1_FIGURE_BEGIN_HOLLOW); for (size_t i = 1; i < p.size(); ++i) k->AddLine(D2D1::Point2F(p[i].x, p[i].y)); k->EndFigure(D2D1_FIGURE_END_OPEN); k->Close(); k->Release(); }
    WithBrush(rt, c, a, [&](ID2D1Brush* b) { rt->DrawGeometry(g, b, w, g_round); }); g->Release();
}
static bool DwInit(const std::wstring& family) {
#ifdef WL_MUT_GDI_GAME_FACE
    return false;      // mutant: the game's face stays in GDI (the text test must see grey, pixelated edges)
#endif
    if (family.empty() || g_faceBytes.empty()) return false;
    IDWriteInMemoryFontFileLoader* ld = nullptr; IDWriteFontFile* file = nullptr; IDWriteFontSetBuilder1* sb = nullptr; IDWriteFontSet* set = nullptr; bool ok = false;
    if (SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory5), (IUnknown**)&g_dw)) && g_dw && SUCCEEDED(g_dw->CreateInMemoryFontFileLoader(&ld)) && SUCCEEDED(g_dw->RegisterFontFileLoader(ld))
        && SUCCEEDED(ld->CreateInMemoryFontFileReference(g_dw, g_faceBytes.data(), (UINT32)g_faceBytes.size(), nullptr, &file)) && SUCCEEDED(g_dw->CreateFontSetBuilder(&sb)) && SUCCEEDED(sb->AddFontFile(file))
        && SUCCEEDED(sb->CreateFontSet(&set)) && SUCCEEDED(g_dw->CreateFontCollectionFromFontSet(set, &g_dwFonts))) {
        UINT32 i = 0; BOOL found = FALSE; IDWriteFontFamily1* ff = nullptr; IDWriteFont* f = nullptr;
        if (SUCCEEDED(g_dwFonts->FindFamilyName(family.c_str(), &i, &found)) && found && SUCCEEDED(g_dwFonts->GetFontFamily(i, &ff)) && SUCCEEDED(ff->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &f))) {
            DWRITE_FONT_METRICS m; f->GetMetrics(&m); if (m.designUnitsPerEm) { g_dwAscent = float(m.ascent) / m.designUnitsPerEm; g_dwDescent = float(m.descent) / m.designUnitsPerEm; }      // (GDI's line: ascent + descent, no gap)
            ok = Target() != nullptr;
        }
        Rel(f); Rel(ff);
    }
    Rel(set); Rel(sb); Rel(file); Rel(ld);      // (the loader stays registered with the factory, which holds it)
    if (!ok) { Rel(g_dwFonts); Rel(g_dw); }
    return ok;
}
static bool GameFaceIn(HFONT f, LOGFONTW* lf) { return g_dwFonts && f && !g_face.empty() && GetObjectW(f, sizeof *lf, lf) && g_face == lf->lfFaceName; }
const char* TextEngine() { return g_dwFonts ? "directwrite" : "gdi"; }
static unsigned g_screenDraws; unsigned ScreenTextDraws() { return g_screenDraws; }
int Text(HDC dc, const wchar_t* s, int n, RECT* r, UINT fmt, int extra) {
    LOGFONTW lf;
    if (!GameFaceIn((HFONT)GetCurrentObject(dc, OBJ_FONT), &lf)) { int old = extra ? SetTextCharacterExtra(dc, extra) : 0; int h = DrawTextW(dc, s, n, r, fmt); if (extra) SetTextCharacterExtra(dc, old); return h; }
    std::wstring t = n < 0 ? std::wstring(s) : std::wstring(s, (size_t)n);
    if (!(fmt & DT_NOPREFIX)) { std::wstring u; for (size_t i = 0; i < t.size(); ++i) { if (t[i] == L'&' && i + 1 < t.size()) ++i; else if (t[i] == L'&') continue; u += t[i]; } t.swap(u); }      // (an & marks a mnemonic, && is one &)
    float em = float(lf.lfHeight < 0 ? -lf.lfHeight : std::max<LONG>(1, lf.lfHeight)); bool single = (fmt & DT_SINGLELINE) || !(fmt & DT_WORDBREAK), calc = (fmt & DT_CALCRECT) != 0;
    IDWriteTextFormat* tf = nullptr; IDWriteTextLayout* tl = nullptr; IDWriteInlineObject* dots = nullptr; ID2D1SolidColorBrush* br = nullptr; int height = 0;
    if (FAILED(g_dw->CreateTextFormat(g_face.c_str(), g_dwFonts, lf.lfWeight >= FW_SEMIBOLD ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, em, L"en-us", &tf))) return DrawTextW(dc, s, n, r, fmt);
    tf->SetWordWrapping(single ? DWRITE_WORD_WRAPPING_NO_WRAP : DWRITE_WORD_WRAPPING_WRAP);
    tf->SetTextAlignment((fmt & DT_CENTER) ? DWRITE_TEXT_ALIGNMENT_CENTER : (fmt & DT_RIGHT) ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
    tf->SetParagraphAlignment(single && (fmt & DT_VCENTER) ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : single && (fmt & DT_BOTTOM) ? DWRITE_PARAGRAPH_ALIGNMENT_FAR : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    tf->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, std::ceil(em * (g_dwAscent + g_dwDescent)), std::round(em * g_dwAscent));      // lines as tall as GDI's, so DT_VCENTER and wrapped heights match
    if ((fmt & (DT_END_ELLIPSIS | DT_WORD_ELLIPSIS | DT_PATH_ELLIPSIS)) && !calc && SUCCEEDED(g_dw->CreateEllipsisTrimmingSign(tf, &dots))) { DWRITE_TRIMMING tr = {DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0}; tf->SetTrimming(&tr, dots); }
    float w = float(r->right - r->left), h = float(r->bottom - r->top); if (calc) { if (single || w <= 0) w = 1e5f; h = 1e5f; } else if (fmt & DT_NOCLIP) { if (single) w = std::max(w, 1.f); h = std::max(h, 1.f); }
    if (SUCCEEDED(g_dw->CreateTextLayout(t.c_str(), (UINT32)t.size(), tf, std::max(w, 1.f), std::max(h, 1.f), &tl))) {
        if (extra) { IDWriteTextLayout1* l1 = nullptr; if (SUCCEEDED(tl->QueryInterface(__uuidof(IDWriteTextLayout1), (void**)&l1))) { l1->SetCharacterSpacing(0, float(extra), 0, DWRITE_TEXT_RANGE{0, (UINT32)t.size()}); l1->Release(); } }
        DWRITE_TEXT_METRICS m = {}; tl->GetMetrics(&m); height = (int)std::ceil(m.height);
        if (calc) { r->right = r->left + (int)std::ceil(m.widthIncludingTrailingWhitespace); r->bottom = r->top + height; }
        else if (GetObjectType(dc) != OBJ_MEMDC) {      // straight to the screen: Direct2D reads the window's pixels back and writes them again, slower than GDI, so a background
            // drawn just before shows alone for a moment (flicker). Callers draw off screen; any that does not is counted (the stat file's textscreen line) and drawn through a copy.
            ++g_screenDraws; RECT b = *r; if (fmt & DT_NOCLIP) { b.right = std::max<LONG>(b.right, r->left + (LONG)std::ceil(m.left + m.widthIncludingTrailingWhitespace)); b.bottom = std::max<LONG>(b.bottom, r->top + height); }
            InflateRect(&b, 3, 3); RECT clip; if (GetClipBox(dc, &clip) != ERROR) IntersectRect(&b, &b, &clip);
            int bw = b.right - b.left, bh = b.bottom - b.top;
            if (bw > 0 && bh > 0) {
                HDC mem = CreateCompatibleDC(dc); HBITMAP bmp = CreateCompatibleBitmap(dc, bw, bh); HGDIOBJ ob = SelectObject(mem, bmp), of = SelectObject(mem, GetCurrentObject(dc, OBJ_FONT));
                BitBlt(mem, 0, 0, bw, bh, dc, b.left, b.top, SRCCOPY); SetTextColor(mem, GetTextColor(dc)); SetBkMode(mem, TRANSPARENT); RECT rr = *r; OffsetRect(&rr, -b.left, -b.top);
                Rel(tl); Rel(dots); Rel(tf); Text(mem, t.c_str(), (int)t.size(), &rr, fmt | DT_NOPREFIX, extra); BitBlt(dc, b.left, b.top, bw, bh, mem, 0, 0, SRCCOPY);
                SelectObject(mem, of); SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem);
            }
        }
        else if (ID2D1DCRenderTarget* rt = Target()) {
            RECT b = *r, clip; if (fmt & DT_NOCLIP) { b.right = std::max<LONG>(b.right, r->left + (LONG)std::ceil(m.left + m.widthIncludingTrailingWhitespace)); b.bottom = std::max<LONG>(b.bottom, r->top + height); }
            InflateRect(&b, 3, 3); if (GetClipBox(dc, &clip) != ERROR) IntersectRect(&b, &b, &clip);      // (a few pixels round the box for the glyphs' soft edges)
            COLORREF c = GetTextColor(dc);
            if (b.right > b.left && b.bottom > b.top && SUCCEEDED(rt->BindDC(dc, &b)) && (rt->BeginDraw(), SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(GetRValue(c) / 255.f, GetGValue(c) / 255.f, GetBValue(c) / 255.f), &br)))) {
                rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_DEFAULT);      // (Windows' own font smoothing setting: ClearType where it is on)
                rt->DrawTextLayout(D2D1::Point2F(float(r->left - b.left), float(r->top - b.top)), tl, br, (fmt & DT_NOCLIP) ? D2D1_DRAW_TEXT_OPTIONS_NONE : D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            if (b.right > b.left && b.bottom > b.top && rt->EndDraw() == (HRESULT)D2DERR_RECREATE_TARGET) Rel(g_rt);
        }
    }
    Rel(br); Rel(tl); Rel(dots); Rel(tf); return height;
}
// A static text in the game's face is painted here (the control's own painting is GDI's); its background and colour come from the parent's WM_CTLCOLORSTATIC as before.
static bool GameStatic(HWND h, LOGFONTW* lf) {
    int type = GetWindowLongW(h, GWL_STYLE) & SS_TYPEMASK;
    return (type == SS_LEFT || type == SS_CENTER || type == SS_RIGHT || type == SS_LEFTNOWORDWRAP || type == SS_SIMPLE) && GameFaceIn((HFONT)SendMessageW(h, WM_GETFONT, 0, 0), lf);
}
static LRESULT CALLBACK StaticProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    LOGFONTW lf;
    if (m == WM_PAINT && GameStatic(h, &lf)) {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc); int cw = std::max<LONG>(1, rc.right), ch = std::max<LONG>(1, rc.bottom);
        HDC mem = CreateCompatibleDC(dc); HBITMAP bmp = CreateCompatibleBitmap(dc, cw, ch); HGDIOBJ ob = SelectObject(mem, bmp), of = SelectObject(mem, (HFONT)SendMessageW(h, WM_GETFONT, 0, 0));
        HBRUSH br = (HBRUSH)SendMessageW(GetParent(h), WM_CTLCOLORSTATIC, (WPARAM)mem, (LPARAM)h); FillRect(mem, &rc, br ? br : g_bg); SetBkMode(mem, TRANSPARENT); if (!IsWindowEnabled(h)) SetTextColor(mem, kMuted);
        LONG st = GetWindowLongW(h, GWL_STYLE); int type = st & SS_TYPEMASK; UINT fmt = type == SS_CENTER ? DT_CENTER : type == SS_RIGHT ? DT_RIGHT : DT_LEFT;
        fmt |= (type == SS_LEFTNOWORDWRAP || type == SS_SIMPLE || (st & SS_CENTERIMAGE)) ? DT_SINGLELINE : DT_WORDBREAK; if (st & SS_CENTERIMAGE) fmt |= DT_VCENTER; if (st & SS_NOPREFIX) fmt |= DT_NOPREFIX;
        if ((st & SS_ELLIPSISMASK) == SS_ENDELLIPSIS || (st & SS_ELLIPSISMASK) == SS_WORDELLIPSIS) fmt |= DT_END_ELLIPSIS;
        int len = GetWindowTextLengthW(h); std::wstring t(len + 1, L'\0'); t.resize(GetWindowTextW(h, &t[0], len + 1)); Text(mem, t.c_str(), (int)t.size(), &rc, fmt);
        BitBlt(dc, 0, 0, cw, ch, mem, 0, 0, SRCCOPY); SelectObject(mem, of); SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem); EndPaint(h, &ps); return 0;
    }
    if (m == WM_NCDESTROY) RemoveWindowSubclass(h, StaticProc, 3);
    return DefSubclassProc(h, m, w, l);
}
const std::string& FontState() { return g_fontState; }
void Init(const std::wstring& gameDir) {
    if (g_body) return;
    g_body = Font(15, FW_NORMAL, L"Segoe UI"); g_small = Font(13, FW_NORMAL, L"Segoe UI");
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr, (void**)&g_d2);      // (shapes and the game's face; none: GDI as before)
    std::wstring face = GameFace(gameDir); g_face = face; if (!face.empty()) g_fontState += DwInit(face) ? " directwrite" : " gdi";
    if (!face.empty()) { g_bold = Font(16, FW_BOLD, face.c_str()); g_title = Font(30, FW_BOLD, face.c_str()); g_tile = Font(20, FW_BOLD, face.c_str()); g_label = Font(16, FW_BOLD, face.c_str()); g_tabFont = Font(22, FW_BOLD, face.c_str()); }      // (Karmina's capitals are a little smaller than Segoe's at one size: 16 px matches the 15 px Semibold)
    else { g_bold = Font(15, FW_SEMIBOLD, L"Segoe UI Semibold"); g_title = Font(30, FW_LIGHT, L"Segoe UI Light"); g_tile = Font(18, FW_SEMIBOLD, L"Segoe UI Semibold"); g_label = g_body; g_tabFont = Font(20, FW_SEMIBOLD, L"Segoe UI Semibold"); }
    g_bg = CreateSolidBrush(kBg); g_surface = CreateSolidBrush(kSurface);
}
HFONT Body() { return g_body; } HFONT Small() { return g_small; } HFONT Bold() { return g_bold; } HFONT Title() { return g_title; }
HBRUSH BgBrush() { return g_bg; } HBRUSH SurfaceBrush() { return g_surface; }
static std::wstring ClassOf(HWND h) { wchar_t c[40] = {}; GetClassNameW(h, c, 40); return c; }
void MarkMuted(HWND h) { SetPropW(h, L"muted", (HANDLE)1); SendMessageW(h, WM_SETFONT, (WPARAM)g_small, TRUE); }

void DrawDot(HDC dc, int cx, int cy, int r, COLORREF c, bool filled) {
    float fx = cx + .5f, fy = cy + .5f; RECT b = {cx - r - 3, cy - r - 3, cx + r + 4, cy + r + 4};
    if (Paint2D(dc, b, [&](ID2D1RenderTarget* rt) { if (filled) Disc2D(rt, fx, fy, r + .5f, c); else Ring2D(rt, fx, fy, r - .5f, 2, c); })) return;
    HPEN pen = CreatePen(PS_SOLID, 2, c); HBRUSH br = filled ? CreateSolidBrush(c) : (HBRUSH)GetStockObject(NULL_BRUSH); HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, br);
    Ellipse(dc, cx - r, cy - r, cx + r + 1, cy + r + 1); SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(pen); if (filled) DeleteObject(br);
}

static COLORREF Mix(COLORREF a, COLORREF b, float t) { return RGB(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t, GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t, GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t); }
// A Play choice as a tile: a card with the puzzle's start circle (lit and a first stroke of line when chosen), the name in the game's face, the description under it.
static void DrawTile(HWND h, HDC dc, const RECT& rc, const wchar_t* name, bool checked, bool hot, bool dis) {
    COLORREF fill = checked ? Mix(kSurface, kWarm, 0.10f) : (hot ? kSurface2 : kSurface), edge = checked ? kWarm : (hot ? kMuted : kSurface2);
    int r = 9, cx = 18 + r, cy = 16 + r; bool focus = GetFocus() == h && !dis;
    // the card and the puzzle's start circle, smooth: a hollow circle, lit (filled, with the soft light of a powered start) when chosen. (The line that ran down from it
    // looked like a lollipop: owner, 2026-11-06.)
    bool smooth = Paint2D(dc, rc, [&](ID2D1RenderTarget* rt) {
        D2D1_ROUNDED_RECT card = {D2D1::RectF(rc.left + 1.5f, rc.top + 1.5f, rc.right - 1.5f, rc.bottom - 1.5f), 6, 6};
        WithBrush(rt, fill, 1, [&](ID2D1Brush* b) { rt->FillRoundedRectangle(card, b); }); WithBrush(rt, edge, 1, [&](ID2D1Brush* b) { rt->DrawRoundedRectangle(card, b, checked ? 2.f : 1.f); });
        float x = cx + .5f, y = cy + .5f;
        if (checked && !dis) { Glow2D(rt, x, y, r + 12.f, kWarm, .30f); Disc2D(rt, x, y, r + .5f, kWarm); Disc2D(rt, x, y, r * .38f, RGB(255, 251, 236), .85f); }
        else Ring2D(rt, x, y, r - .75f, 2.5f, dis ? kSurface2 : (hot ? kLine : kMuted));
        if (focus) WithBrush(rt, kLine, 1, [&](ID2D1Brush* b) { rt->DrawRoundedRectangle({D2D1::RectF(rc.left + 5.f, rc.top + 5.f, rc.right - 5.f, rc.bottom - 5.f), 4, 4}, b, 1.5f); });
    });
    if (!smooth) {
        HBRUSH fb = CreateSolidBrush(fill); HPEN pen = CreatePen(PS_SOLID, checked ? 2 : 1, edge); HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, fb);
        RoundRect(dc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1, 10, 10); SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(pen); DeleteObject(fb);
        DrawDot(dc, cx, cy, r, dis ? kSurface2 : (checked ? kWarm : (hot ? kLine : kMuted)), checked);
    }
    SetBkMode(dc, TRANSPARENT); SelectObject(dc, g_tile); SetTextColor(dc, dis ? kMuted : (checked ? kWarm : kText));
    RECT nr = {cx + r + 14, rc.top + 10, rc.right - 12, rc.top + 42}; Text(dc, name, -1, &nr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    wchar_t desc[300] = {}; if (HWND d = (HWND)GetPropW(h, L"desc")) GetWindowTextW(d, desc, 300);
    SelectObject(dc, g_small); SetTextColor(dc, checked ? kText : kMuted); RECT dr = {nr.left, rc.top + 44, rc.right - 12, rc.bottom - 8}; Text(dc, desc, -1, &dr, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
    if (focus && !smooth) { HPEN fp = CreatePen(PS_SOLID, 2, kLine); HGDIOBJ o1 = SelectObject(dc, fp), o2 = SelectObject(dc, GetStockObject(NULL_BRUSH)); RoundRect(dc, rc.left + 4, rc.top + 4, rc.right - 4, rc.bottom - 4, 8, 8); SelectObject(dc, o1); SelectObject(dc, o2); DeleteObject(fp); }
}
void MarkTile(HWND radio, HWND desc) { SetPropW(radio, L"tile", (HANDLE)1); SetPropW(radio, L"desc", desc); if (desc) ShowWindow(desc, SW_HIDE); }

// Windows' "Show animations in Windows" (off: every line is shown already drawn and nothing moves)
bool Motion() { BOOL on = TRUE; if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0)) on = TRUE; return on != FALSE; }
// The line around a button: under the pointer (or with the keyboard or pad focus) a line leaves a start dot at the middle of its left side and runs round its edge, as a
// panel's line does; it stays lit while the button is the one you are on.
static const UINT_PTR kTraceTimer = 0x7472; static const float kTraceMs = 600;
static bool Traceable(HWND h) { if (GetPropW(h, L"tile")) return true; int type = GetWindowLongW(h, GWL_STYLE) & BS_TYPEMASK; return type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON; }
static void TraceUpdate(HWND h) {
    bool want = Traceable(h) && IsWindowEnabled(h) && (GetPropW(h, L"hot") || (!GetPropW(h, L"tile") && GetFocus() == h)), had = GetPropW(h, L"tr0") != nullptr;
    if (want && !had) { SetPropW(h, L"tr0", (HANDLE)(ULONG_PTR)(GetTickCount64() | 1)); if (Motion()) SetTimer(h, kTraceTimer, 15, nullptr); InvalidateRect(h, nullptr, FALSE); }
    else if (!want && had) { RemovePropW(h, L"tr0"); KillTimer(h, kTraceTimer); InvalidateRect(h, nullptr, FALSE); }
}
static float TraceProgress(HWND h) { ULONG_PTR t0 = (ULONG_PTR)GetPropW(h, L"tr0"); if (!t0) return 0; if (!Motion()) return 1; return trace::EaseOut(float(GetTickCount64() - t0) / kTraceMs); }
static void DrawTrace(HDC dc, const RECT& rc, int inset, float rad, float progress, COLORREF col, int width, int dot) {
    if (progress <= 0) return;
    { float h = width / 2.f; auto ring = trace::Prefix(trace::Ring(rc.left + inset + h, rc.top + inset + h, rc.right - inset - h, rc.bottom - inset - h, rad, 8), progress);
      if (Paint2D(dc, rc, [&](ID2D1RenderTarget* rt) { Poly2D(rt, ring, float(width), col); if (dot > 0) Disc2D(rt, ring[0].x, ring[0].y, dot + .5f, col); })) return; }
    auto ring = trace::Prefix(trace::Ring(rc.left + inset + .5f, rc.top + inset + .5f, rc.right - 1 - inset - .5f, rc.bottom - 1 - inset - .5f, rad), progress);
    std::vector<POINT> pts; for (auto& q : ring) pts.push_back({LONG(lroundf(q.x)), LONG(lroundf(q.y))});
    LOGBRUSH lb = {BS_SOLID, col, 0}; HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_ROUND | PS_JOIN_ROUND, width, &lb, 0, nullptr); HGDIOBJ op = SelectObject(dc, pen);
    if (pts.size() >= 2) Polyline(dc, pts.data(), (int)pts.size()); SelectObject(dc, op); DeleteObject(pen);
    if (dot > 0) DrawDot(dc, pts[0].x, pts[0].y, dot, col, true);
}

static LRESULT CALLBACK ButtonProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE: if (!GetPropW(h, L"hot")) { SetPropW(h, L"hot", (HANDLE)1); TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE, h, 0}; TrackMouseEvent(&t); InvalidateRect(h, nullptr, FALSE); TraceUpdate(h); } break;
    case WM_MOUSELEAVE: SetPropW(h, L"hot", nullptr); InvalidateRect(h, nullptr, FALSE); TraceUpdate(h); break;
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case BM_SETCHECK: case WM_ENABLE: case WM_SETFOCUS: case WM_KILLFOCUS: { LRESULT r = DefSubclassProc(h, m, w, l); InvalidateRect(h, nullptr, FALSE); TraceUpdate(h); return r; }
    case WM_TIMER: if (w == kTraceTimer) { InvalidateRect(h, nullptr, FALSE); if (TraceProgress(h) >= 1) KillTimer(h, kTraceTimer); return 0; } break;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc); HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom); HGDIOBJ ob = SelectObject(mem, bmp);
        HBRUSH bg = CreateSolidBrush(kBg); FillRect(mem, &rc, bg); DeleteObject(bg);
        wchar_t text[200] = {}; GetWindowTextW(h, text, 200); LONG style = GetWindowLongW(h, GWL_STYLE); int type = style & BS_TYPEMASK;
        bool dis = !IsWindowEnabled(h), hot = GetPropW(h, L"hot") != nullptr, down = (SendMessageW(h, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0, checked = SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
        SelectObject(mem, g_body); SetBkMode(mem, TRANSPARENT);
        if (GetPropW(h, L"tile")) { DrawTile(h, mem, rc, text, checked, hot, dis); if (!dis) DrawTrace(mem, rc, 1, 5, TraceProgress(h), checked ? kWarm : kLine, 2, 0); }
        else if (type == BS_AUTOCHECKBOX || type == BS_CHECKBOX || type == BS_AUTORADIOBUTTON || type == BS_RADIOBUTTON) {
            int r = 8, cy = rc.bottom / 2, cx = r + 1; COLORREF edge = dis ? kSurface2 : (hot ? kAccent : kMuted);
            bool radio = type == BS_AUTORADIOBUTTON || type == BS_RADIOBUTTON; COLORREF lit = radio ? kWarm : kAccent;      // a choice lights in the game's yellow, an on/off switch in the cyan of a powered line
            DrawDot(mem, cx, cy, r, edge, false); if (checked) DrawDot(mem, cx, cy, r - 4, dis ? kMuted : lit, true);
            RECT tr = rc; tr.left = 2 * r + 10; SelectObject(mem, g_label); SetTextColor(mem, dis ? kMuted : kText); Text(mem, text, -1, &tr, DT_VCENTER | DT_SINGLELINE | DT_LEFT | DT_END_ELLIPSIS);
            if (GetFocus() == h) { HPEN pen = CreatePen(PS_DOT, 1, kMuted); HGDIOBJ op = SelectObject(mem, pen); SelectObject(mem, GetStockObject(NULL_BRUSH)); Rectangle(mem, rc.left, rc.top, rc.right, rc.bottom); SelectObject(mem, op); DeleteObject(pen); }
        } else {
            bool primary = type == BS_DEFPUSHBUTTON; COLORREF fill = primary ? (dis ? kSurface2 : (down ? kLine : (hot ? Mix(kWarm, kLine, 0.25f) : kWarm))) : (down ? kSurface : (hot ? kSurface2 : kSurface)), edge = primary ? fill : (dis ? kSurface2 : kMuted);
            if (!Paint2D(mem, rc, [&](ID2D1RenderTarget* rt) { D2D1_ROUNDED_RECT b = {D2D1::RectF(rc.left + .5f, rc.top + .5f, rc.right - .5f, rc.bottom - .5f), 4, 4}; WithBrush(rt, fill, 1, [&](ID2D1Brush* br) { rt->FillRoundedRectangle(b, br); }); WithBrush(rt, edge, 1, [&](ID2D1Brush* br) { rt->DrawRoundedRectangle(b, br, 1); }); })) {
                HBRUSH fb = CreateSolidBrush(fill); HPEN pen = CreatePen(PS_SOLID, 1, edge); HGDIOBJ op = SelectObject(mem, pen), obr = SelectObject(mem, fb); RoundRect(mem, rc.left, rc.top, rc.right, rc.bottom, 6, 6); SelectObject(mem, op); SelectObject(mem, obr); DeleteObject(pen); DeleteObject(fb);
            }
            if (!dis) DrawTrace(mem, rc, primary ? 3 : 2, 3, TraceProgress(h), primary ? RGB(255, 251, 236) : kAccent, 2, 3);      // (near the edge, well clear of the words: owner, 2026-11-06)      // under the pointer or with the focus: the line runs round (keyboard and pad users see where they are)
            SelectObject(mem, g_bold); SetTextColor(mem, primary ? (dis ? kMuted : kBg) : (dis ? kMuted : kText)); Text(mem, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY); SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem); EndPaint(h, &ps); return 0;
    }
    case WM_NCDESTROY: KillTimer(h, kTraceTimer); RemovePropW(h, L"tr0"); RemoveWindowSubclass(h, ButtonProc, 1); break;
    }
    return DefSubclassProc(h, m, w, l);
}

void Style(HWND h) {
    std::wstring c = ClassOf(h);
    if (c == L"Button") { SetWindowSubclass(h, ButtonProc, 1, 0); SendMessageW(h, WM_SETFONT, (WPARAM)g_body, TRUE); }
    else if (c == L"Edit") { SetWindowTheme(h, (GetWindowLongW(h, GWL_STYLE) & ES_MULTILINE) ? L"DarkMode_Explorer" : L"", (GetWindowLongW(h, GWL_STYLE) & ES_MULTILINE) ? nullptr : L""); SendMessageW(h, WM_SETFONT, (WPARAM)g_body, TRUE); }
    else if (c == L"ComboBox") { SetWindowTheme(h, L"DarkMode_CFD", nullptr); SendMessageW(h, WM_SETFONT, (WPARAM)g_body, TRUE); }
    else if (c == L"ListBox") { SetWindowTheme(h, L"DarkMode_Explorer", nullptr); SendMessageW(h, WM_SETFONT, (WPARAM)g_body, TRUE); }
    else if (c == L"Static") { SendMessageW(h, WM_SETFONT, (WPARAM)g_label, TRUE); SetWindowSubclass(h, StaticProc, 3, 0); }      // a label; descriptions and hints switch to the small face in MarkMuted
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
        int hotTab = (int)(INT_PTR)GetPropW(h, L"hot") - 1; int n = TabCtrl_GetItemCount(h), sel = TabCtrl_GetCurSel(h); SetBkMode(dc, TRANSPARENT); SelectObject(dc, g_tabFont);
        RECT first = {}; if (n) TabCtrl_GetItemRect(h, 0, &first); int stripBottom = first.bottom;
        HPEN lp = CreatePen(PS_SOLID, 1, kSurface2); HGDIOBJ op = SelectObject(dc, lp); MoveToEx(dc, rc.left, stripBottom, nullptr); LineTo(dc, rc.right, stripBottom); SelectObject(dc, op); DeleteObject(lp);
        for (int i = 0; i < n; ++i) {
            RECT r; TabCtrl_GetItemRect(h, i, &r); wchar_t t[80] = {}; TCITEMW it = {}; it.mask = TCIF_TEXT; it.pszText = t; it.cchTextMax = 80; TabCtrl_GetItem(h, i, &it);
            bool hot = i == hotTab; SetTextColor(dc, i == sel ? kText : (hot ? kLine : kMuted)); RECT tr = r; Text(dc, t, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (i == sel) { HBRUSH ab = CreateSolidBrush(kWarm); RECT ar = {r.left + 18, stripBottom - 4, r.right - 18, stripBottom}; FillRect(dc, &ar, ab); DeleteObject(ab); }
        }
        BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY); SelectObject(dc, obmp); DeleteObject(bmp); DeleteDC(dc); EndPaint(h, &ps); return 0;
    }
    case WM_NCDESTROY: RemoveWindowSubclass(h, TabProc, 2); break;
    }
    return DefSubclassProc(h, m, w, l);
}
void StyleTab(HWND tab) { SetWindowSubclass(tab, TabProc, 2, 0); SendMessageW(tab, WM_SETFONT, (WPARAM)g_tabFont, TRUE); TabCtrl_SetItemSize(tab, 150, 46); TabCtrl_SetPadding(tab, 16, 8); }      // (bigger words, wider apart: owner, 2026-11-06)
// A list's column headings, painted here: the dark theme's own were dim grey on near black and hard to read (owner, 2026-11-06). Light words in the labels' face on a raised strip.
static LRESULT CALLBACK HeaderProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_PAINT) {
        PAINTSTRUCT ps; HDC wdc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc); int cw = std::max<LONG>(1, rc.right), ch = std::max<LONG>(1, rc.bottom);
        HDC dc = CreateCompatibleDC(wdc); HBITMAP bmp = CreateCompatibleBitmap(wdc, cw, ch); HGDIOBJ ob = SelectObject(dc, bmp), of = SelectObject(dc, g_label);
        HBRUSH bg = CreateSolidBrush(kSurface2); FillRect(dc, &rc, bg); DeleteObject(bg); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, kText);
        HBRUSH sep = CreateSolidBrush(kBg); int n = Header_GetItemCount(h);
        for (int i = 0; i < n; ++i) {
            RECT r; Header_GetItemRect(h, i, &r); wchar_t t[80] = {}; HDITEMW it = {}; it.mask = HDI_TEXT | HDI_FORMAT; it.pszText = t; it.cchTextMax = 80; Header_GetItem(h, i, &it);
            RECT tr = r; tr.left += 8; tr.right -= 8; Text(dc, t, -1, &tr, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX | ((it.fmt & HDF_RIGHT) ? DT_RIGHT : (it.fmt & HDF_CENTER) ? DT_CENTER : DT_LEFT));
            RECT sr = {r.right - 1, r.top + 5, r.right, r.bottom - 5}; FillRect(dc, &sr, sep);
        }
        RECT line = {0, ch - 1, cw, ch}; FillRect(dc, &line, sep); DeleteObject(sep);
        BitBlt(wdc, 0, 0, cw, ch, dc, 0, 0, SRCCOPY); SelectObject(dc, of); SelectObject(dc, ob); DeleteObject(bmp); DeleteDC(dc); EndPaint(h, &ps); return 0;
    }
    if (m == WM_NCDESTROY) RemoveWindowSubclass(h, HeaderProc, 4);
    return DefSubclassProc(h, m, w, l);
}
void StyleList(HWND list) {
    SetWindowTheme(list, L"DarkMode_Explorer", nullptr); ListView_SetBkColor(list, kSurface); ListView_SetTextBkColor(list, kSurface); ListView_SetTextColor(list, kText); SendMessageW(list, WM_SETFONT, (WPARAM)g_body, TRUE);
    HWND hdr = ListView_GetHeader(list); if (hdr) { SetWindowTheme(hdr, L"DarkMode_ItemsView", nullptr); SetWindowSubclass(hdr, HeaderProc, 4, 0); InvalidateRect(hdr, nullptr, TRUE); }
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
struct Grid { float gx, gy, cell, half, cap; int cols, rows; bool up = false; };      // up: the end cap leaves the top right node upwards (else to the right)      // a board's nodes: (gx + i * cell, gy + j * cell), the line half as thick as `half`, its end cap `cap` past the top right node
static Grid PuzzleGrid(float x, float y, float w, float h) { const int cols = 4, rows = 3; float m = std::min(w, h) * .15f, cell = std::min((w - 2 * m) / cols, (h - 2 * m) / rows); return {x + (w - cols * cell) / 2, y + (h - rows * cell) / 2, cell, cell * .12f, cell * .34f, cols, rows}; }
static void Puzzle(Canvas& c, float x, float y, float w, float h, bool solved = true) {
    const COLORREF body = RGB(240, 186, 56), body2 = RGB(222, 158, 38), grid = RGB(126, 90, 30), line = RGB(255, 251, 236);
    float r = std::min(w, h) * .07f;
    Shape(c, x, y, x + w, y + h, RGB(0, 0, 0), .45f, std::min(w, h) * .08f, [&](float px, float py) { return RoundRectDist(px, py, x + 3, y + 7, w, h, r); });      // its shadow
    Shape(c, x, y, x + w, y + h, body, 1, 0, [&](float px, float py) { return RoundRectDist(px, py, x, y, w, h, r); });
    Shape(c, x, y + h * .45f, x + w, y + h, body2, .55f, 0, [&](float px, float py) { return std::max(RoundRectDist(px, py, x, y, w, h, r), (y + h * .45f) - py); });      // the lower part a little deeper
    Grid g = PuzzleGrid(x, y, w, h); const int cols = g.cols, rows = g.rows; float cell = g.cell, gx = g.gx, gy = g.gy, half = g.half;
    auto nx = [&](float i) { return gx + i * cell; }; auto ny = [&](float j) { return gy + j * cell; };
    for (int j = 0; j <= rows; ++j) { float p[] = {nx(0), ny(j), nx(cols), ny(j)}; Line(c, p, 2, half, grid, 1); }
    for (int i = 0; i <= cols; ++i) { float p[] = {nx(i), ny(0), nx(i), ny(rows)}; Line(c, p, 2, half, grid, 1); }
    { float p[] = {nx(cols), ny(0), nx(cols) + cell * .34f, ny(0)}; Line(c, p, 2, half, grid, 1); }      // the end cap
    Disc(c, nx(0), ny(rows), half * 2.5f, grid, 1);                                                      // the start circle
    if (!solved) return;      // (the banner's panel: its line is drawn moving, see the moving line below)
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
static bool g_capOn, g_capMax, g_capActive = true; static int g_capHot, g_capDown; static const int kCapW = 46, kCapH = 32;      // (the caption buttons: see PaintHeader)
static HBITMAP g_banner; static uint32_t* g_bannerBits; static int g_bannerW, g_bannerH; static unsigned g_bannerGen;

// ---- the moving line on the banner: the boards (one drawn yellow panel, or over a save's picture a row of small dark Witness panels) are solved one after another,
// each line from its start circle to its end cap along a random solution (trace.h); a solved line stays lit, the last solve holds, they fade together, a rest, new puzzles.
// The clock runs only while the launcher is the window in use.
struct Board { Grid g; std::vector<trace::Pt> line; double ms; };
static bool g_boardOn, g_boardPanel; static std::vector<Board> g_boards; static double g_clock, g_cycle0;
static uint32_t g_seed = 1; static unsigned g_frames, g_puzzles; static bool g_active, g_motion = true;
static HBITMAP g_frame; static uint32_t* g_frameBits; static int g_frameW, g_frameH; static ULONGLONG g_lastTick, g_lastInval;
constexpr double kNextMs = 700;      // a solved panel's flash before the next one starts
static void NewPuzzle() {
    for (auto& b : g_boards) {
        const Grid& g = b.g; auto nodes = trace::SolvePath(g.cols, g.rows, g_seed++, g.cols + g.rows + (g_boardPanel ? 6 : 4)); b.line.clear();
        for (auto& n : nodes) b.line.push_back({g.gx + n.i * g.cell, g.gy + n.j * g.cell});
        if (!b.line.empty()) b.line.push_back(g.up ? trace::Pt{g.gx + g.cols * g.cell, g.gy - g.cap} : trace::Pt{g.gx + g.cols * g.cell + g.cap, g.gy});
        b.ms = std::max(1100.0, std::min(5000.0, trace::Length(b.line) / (g.cell * 3.4) * 1000.0));
    }
    g_cycle0 = g_clock; ++g_puzzles;
}
static void SetBoards(const std::vector<Grid>& gs, bool panel) {
    bool same = g_boardOn && g_boardPanel == panel && g_boards.size() == gs.size();
    for (size_t i = 0; same && i < gs.size(); ++i) { const Grid &a = g_boards[i].g, &b = gs[i]; same = a.cols == b.cols && a.rows == b.rows && a.up == b.up && fabsf(a.cell - b.cell) < .01f && fabsf(a.gx - b.gx) < .01f && fabsf(a.gy - b.gy) < .01f; }
    if (same) return; g_boards.clear(); for (auto& g : gs) g_boards.push_back({g, {}, 1000}); g_boardPanel = panel; g_boardOn = !gs.empty(); if (g_boardOn) NewPuzzle();
}
static void ClearBoard() { g_boardOn = false; g_boards.clear(); }
// where the sequence is at `ms`: the panel being worked on and its state (trace.h's phases; the last panel's solve, fade and rest end the cycle)
struct Seq { size_t cur; trace::State st; };
static double SeqMs() { double t = 0; for (size_t k = 0; k < g_boards.size(); ++k) t += k + 1 < g_boards.size() ? g_boards[k].ms + kNextMs : trace::CycleMs(g_boards[k].ms); return t; }
static Seq SeqAt(double ms) {
    for (size_t k = 0; k < g_boards.size(); ++k) {
        double tm = g_boards[k].ms; if (k + 1 == g_boards.size()) return {k, trace::At(ms, tm)};
        if (ms < tm) return {k, {trace::kTrace, float(ms / tm), 0, 1}}; ms -= tm;
        if (ms < kNextMs) { float f = ms < trace::kFlashMs ? float(1 - ms / trace::kFlashMs) : 0; return {k, {trace::kSolved, 1, f * f, 1}}; } ms -= kNextMs;
    }
    return {0, {trace::kRest, 0, 0, 0}};
}
// a polyline with each pixel taken by its nearest segment only, so the soft glow does not double up at the corners (and only small boxes are visited)
static void SoftLine(Canvas& c, const std::vector<trace::Pt>& p, float half, COLORREF col, float alpha, float soft) {
    if (p.size() < 2 || alpha <= 0) return; int n = (int)p.size() - 1; float reach = half + soft + 1;
    auto d = [&](int k, float x, float y) { return SegDist(x, y, p[k].x, p[k].y, p[k + 1].x, p[k + 1].y); };
    for (int k = 0; k < n; ++k) {
        int xa = std::max(0, int(std::min(p[k].x, p[k + 1].x) - reach)), xb = std::min(c.w, int(std::max(p[k].x, p[k + 1].x) + reach + 1)), ya = std::max(0, int(std::min(p[k].y, p[k + 1].y) - reach)), yb = std::min(c.h, int(std::max(p[k].y, p[k + 1].y) + reach + 1));
        for (int y = ya; y < yb; ++y) for (int x = xa; x < xb; ++x) {
            float px = x + .5f, py = y + .5f, dk = d(k, px, py); if (dk > reach) continue;
            if ((k > 0 && d(k - 1, px, py) <= dk) || (k + 1 < n && d(k + 1, px, py) < dk)) continue;
            float s = dk - half, a = soft > 0 ? (s <= 0 ? 1.f : s >= soft ? 0.f : (1 - s / soft) * (1 - s / soft)) : (s <= -.5f ? 1.f : s >= .5f ? 0.f : .5f - s);
            if (a > 0) Blend(c, x, y, col, a * alpha);
        }
    }
}
static void DrawOneLine(Canvas& c, const Board& b, const trace::State& st) {
    if (b.line.empty() || st.alpha <= 0) return;
    auto drawn = trace::Prefix(b.line, st.drawn); float half = b.g.half, r0 = half * 2.5f; trace::Pt s0 = b.line.front(); bool solved = st.phase != trace::kTrace;
    // as in the game: a pale line while it is drawn, bright warm white with a soft light once the panel is solved (a flash at the moment of solving)
    COLORREF core = g_boardPanel ? RGB(255, 251, 236) : (solved ? RGB(255, 250, 232) : RGB(226, 236, 240)), glow = g_boardPanel ? RGB(255, 251, 236) : (solved ? kWarm : kLine);
    float ga = (g_boardPanel ? .35f : solved ? .40f : .18f) + .5f * st.flash, soft = half * (3.0f + 5.0f * st.flash);
    SoftLine(c, drawn, half, glow, ga * st.alpha, soft); Disc(c, s0.x, s0.y, r0, glow, ga * st.alpha, soft);
    SoftLine(c, drawn, half, core, st.alpha, 0); Disc(c, s0.x, s0.y, r0, core, st.alpha);
}
static void DrawMovingLine(Canvas& c, double ms, bool still) {
    if (g_boards.empty()) return;
    if (still) { for (auto& b : g_boards) DrawOneLine(c, b, {trace::kSolved, 1, 0, 1}); return; }
    Seq q = SeqAt(ms); bool ending = q.cur + 1 == g_boards.size() && q.st.phase != trace::kTrace;
    for (size_t k = 0; k < q.cur; ++k) DrawOneLine(c, g_boards[k], {trace::kSolved, 1, 0, ending ? q.st.alpha : 1.f});
    DrawOneLine(c, g_boards[q.cur], q.st);
}
// A small dark Witness panel over the picture: a slate square with a soft shadow, its grid as wide grooves with round ends, the start circle at the bottom left and the end
// cap past the top right node (up or to the right). n x n cells.
static Grid DarkPanel(Canvas& c, float x, float y, float ps, int n, bool up) {
    float r = ps * .07f; const COLORREF body = RGB(30, 37, 41), rim = RGB(58, 70, 76), groove = RGB(74, 89, 97);
    Shape(c, x, y, x + ps, y + ps, RGB(0, 0, 0), .55f, ps * .10f, [&](float px, float py) { return RoundRectDist(px, py, x + 2, y + 5, ps, ps, r); });
    Shape(c, x, y, x + ps, y + ps, rim, .95f, 0, [&](float px, float py) { return RoundRectDist(px, py, x, y, ps, ps, r); });
    Shape(c, x, y, x + ps, y + ps, body, 1, 0, [&](float px, float py) { return RoundRectDist(px, py, x + 1.5f, y + 1.5f, ps - 3, ps - 3, r * .85f); });
    float m = ps * .2f, cell = (ps - 2 * m) / n, half = std::max(1.6f, cell * .12f); Grid g{x + m, y + m, cell, half, std::max(cell * .42f, half * 3.2f), n, n, up};
    auto nx = [&](float i) { return g.gx + i * cell; }; auto ny = [&](float j) { return g.gy + j * cell; };
    for (int j = 0; j <= n; ++j) { float p[] = {nx(0), ny(j), nx(n), ny(j)}; Line(c, p, 2, half, groove, 1); }
    for (int i = 0; i <= n; ++i) { float p[] = {nx(i), ny(0), nx(i), ny(n)}; Line(c, p, 2, half, groove, 1); }
    { float p[] = {nx(n), ny(0), up ? nx(n) : nx(n) + g.cap, up ? ny(0) - g.cap : ny(0)}; Line(c, p, 2, half, groove, 1); }
    Disc(c, nx(0), ny(n), half * 2.5f, groove, 1);
    return g;
}
static void Compose(HDC ref, int W, int H, const wchar_t* subtitle) {
    uint32_t* bits = nullptr; HBITMAP bmp = NewDib(ref, W, H, &bits); if (!bmp || !bits) return; Canvas c{W, H, bits};
    if (!g_art.empty()) {      // the save's picture, then a calm shade: darker to the left where the title sits, fading into the window at the bottom
        Cover(c, g_art.data(), g_artW, g_artH, .42f); Blur(c, int(std::max(float(W) / g_artW, float(H) / g_artH) * .8f));
        Shade(c, kBg, [&](int x, int y) { float t = std::max(0.f, 1 - x / (W * .6f)), b = y > H * .5f ? (y - H * .5f) / (H * .5f) : 0; return std::max(.12f + .70f * powf(t, 1.3f), powf(b, 1.6f)); });
    } else {      // no picture: an evening sky over the drawn panel
        Shade(c, kBg, [](int, int) { return 1.f; });
        Shade(c, RGB(34, 66, 76), [&](int x, int y) { float t = 1 - float(y) / H; return .85f * t * t * (.55f + .45f * float(x) / W); });
        float ph = H * .72f, pw = ph * 1.3f, px = W - pw - std::max(28.f, W * .05f), py = (H - ph) / 2 - H * .03f; Puzzle(c, px, py, pw, ph, false); SetBoards({PuzzleGrid(px, py, pw, ph)}, true);
    }
    HDC mem = CreateCompatibleDC(ref); HGDIOBJ ob = SelectObject(mem, bmp); SetBkMode(mem, TRANSPARENT);
    int tp = std::max(26, std::min(60, H * 27 / 100)), x0 = std::max(24, W * 3 / 100); bool compact = H < 110;
    HFONT title = Font(tp, FW_BOLD, g_face.empty() ? L"Segoe UI Light" : g_face.c_str()), coop = Font(std::max(14, tp / 2), FW_BOLD, g_face.empty() ? L"Segoe UI Semibold" : g_face.c_str());
    auto text = [&](HFONT f, int extra, const wchar_t* t, int x, int y, COLORREF col) { SelectObject(mem, f); const UINT k = DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP; RECT sh = {x + 2, y + 2, x + 2, y + 2}, at = {x, y, x, y}, m = at;
        SetTextColor(mem, RGB(0, 0, 0)); Text(mem, t, -1, &sh, k, extra); SetTextColor(mem, col); Text(mem, t, -1, &at, k, extra); Text(mem, t, -1, &m, k | DT_CALCRECT, extra); return SIZE{m.right - m.left, m.bottom - m.top}; };      // (in the game's face through DirectWrite, letter-spaced)
    int ty = compact ? 8 : H * 16 / 100; SIZE ts = text(title, tp / 8, L"THE WITNESS", x0, ty, kText);
    int cy = compact ? ty + (ts.cy - std::max(14, tp / 2)) / 2 + 2 : ty + ts.cy + 2, cx = compact ? x0 + ts.cx + tp / 2 : x0 + 2; SIZE cs = text(coop, tp / 4, L"CO-OP", cx, cy, kWarm);
    if (subtitle && *subtitle) { SelectObject(mem, g_body); SetTextCharacterExtra(mem, 0); SetTextColor(mem, kLine); RECT sr = {x0 + 2, compact ? ty + ts.cy + 2 : cy + cs.cy + 6, W - 24, H - 4}; Text(mem, subtitle, -1, &sr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS); }
    int textRight = compact ? cx + cs.cx : std::max(x0 + (int)ts.cx, cx + (int)cs.cx);
    SelectObject(mem, ob); DeleteDC(mem); DeleteObject(title); DeleteObject(coop); GdiFlush();
    if (!g_art.empty()) {      // over the picture: a row of small Witness panels right of the title, solved one after another (a 14 x 3 grid read as a sheet, not a puzzle: owner, 2026-11-06)
        float ps = std::round(H * .68f), gap = std::round(ps * .32f), xEnd = W - std::max(28.f, W * .03f) - (g_capOn ? 3 * kCapW - 8.f : 0.f), xStart = textRight + std::max(48.f, H * .5f); int n = 3;      // (the row ends clear of the caption buttons)
        while (n > 0 && n * ps + (n - 1) * gap > xEnd - xStart) --n;
        if (n > 0) {
            static const int kCells[3][3] = {{4, 0, 0}, {3, 4, 0}, {3, 4, 4}}; std::vector<Grid> gs; float y = std::round((H - ps) / 2 - H * .02f);
            for (int i = 0; i < n; ++i) gs.push_back(DarkPanel(c, std::round(xEnd - (n - i) * ps - (n - 1 - i) * gap), y, ps, kCells[n - 1][i], i % 2 == 1));
            SetBoards(gs, false);
        } else ClearBoard();
    }
    if (g_banner) DeleteObject(g_banner); g_banner = bmp; g_bannerBits = bits; g_bannerW = W; g_bannerH = H; g_bannerGen = g_artGen;
}
// ---- the window's own caption buttons (minimize, maximize or restore, close) on the banner's top right: the Windows title bar is gone (owner, 2026-11-06)
void SetCaption(int hot, int down, bool maximized, bool active) { g_capOn = true; g_capHot = hot; g_capDown = down; g_capMax = maximized; g_capActive = active; }
int CaptionHit(int x, int y, int clientW) { if (y < 0 || y >= kCapH || x >= clientW || x < clientW - 3 * kCapW) return 0; return 1 + (x - (clientW - 3 * kCapW)) / kCapW; }
RECT CaptionRect(int clientW) { return {clientW - 3 * kCapW, 0, clientW, kCapH}; }
static void DrawCaption(Canvas& c) {
    auto box = [&](float x0, float y0, float x1, float y1, COLORREF col, float a) { for (int y = std::max(0, int(y0)); y < std::min(c.h, int(y1)); ++y) for (int x = std::max(0, int(x0)); x < std::min(c.w, int(x1)); ++x) Blend(c, x, y, col, a); };
    auto seg = [&](float ax, float ay, float bx, float by, COLORREF col) { float p[] = {ax, ay, bx, by}; Line(c, p, 2, .55f, col, 1); };
    for (int k = 1; k <= 3; ++k) {
        float x0 = float(c.w - (4 - k) * kCapW), cx = x0 + kCapW / 2.f, cy = kCapH / 2.f, s = 5; bool hot = g_capHot == k, down = g_capDown == k;
        if (hot || down) box(x0, 0, x0 + kCapW, kCapH, k == 3 ? RGB(196, 43, 28) : RGB(255, 255, 255), k == 3 ? (down ? .78f : 1.f) : (down ? .18f : .11f));
        COLORREF ink = k == 3 && (hot || down) ? RGB(255, 255, 255) : (g_capActive ? kText : kMuted);
        if (k == 1) seg(cx - s, cy + .5f, cx + s, cy + .5f, ink);
        else if (k == 2 && !g_capMax) { seg(cx - s, cy - s, cx + s, cy - s, ink); seg(cx + s, cy - s, cx + s, cy + s, ink); seg(cx + s, cy + s, cx - s, cy + s, ink); seg(cx - s, cy + s, cx - s, cy - s, ink); }
        else if (k == 2) { float o = 2; seg(cx - s, cy - s + o, cx + s - o, cy - s + o, ink); seg(cx + s - o, cy - s + o, cx + s - o, cy + s, ink); seg(cx + s - o, cy + s, cx - s, cy + s, ink); seg(cx - s, cy + s, cx - s, cy - s + o, ink);
                           seg(cx - s + o, cy - s, cx + s, cy - s, ink); seg(cx + s, cy - s, cx + s, cy + s - o, ink); }      // (restore: two windows)
        else { seg(cx - s, cy - s, cx + s, cy + s, ink); seg(cx + s, cy - s, cx - s, cy + s, ink); }
    }
}
void PaintHeader(HDC dc, const RECT& client, int height, const wchar_t* subtitle) {
    int W = client.right; if (W <= 0 || height <= 0) return;
    if (!g_banner || g_bannerW != W || g_bannerH != height || g_bannerGen != g_artGen) Compose(dc, W, height, subtitle);
    if (!g_banner) return; HBITMAP show = g_banner;
    if ((g_boardOn || g_capOn) && g_bannerBits) {      // the banner, then the line as it is now (still and solved when Windows' animations are off) and the caption buttons
        if (!g_frame || g_frameW != W || g_frameH != height) { if (g_frame) DeleteObject(g_frame); g_frame = NewDib(dc, W, height, &g_frameBits); g_frameW = W; g_frameH = height; }
        if (g_frame && g_frameBits) {
            GdiFlush(); memcpy(g_frameBits, g_bannerBits, (size_t)W * height * 4); Canvas c{W, height, g_frameBits};
            if (g_boardOn) { DrawMovingLine(c, g_clock - g_cycle0, !g_motion); ++g_frames; } if (g_capOn) DrawCaption(c); show = g_frame;
        }
    }
    HDC mem = CreateCompatibleDC(dc); HGDIOBJ o = SelectObject(mem, show); BitBlt(dc, 0, 0, W, height, mem, 0, 0, SRCCOPY); SelectObject(mem, o); DeleteDC(mem);
}
void AnimTick(HWND wnd, bool active) {
    ULONGLONG now = GetTickCount64(); double dt = g_lastTick ? double(std::min<ULONGLONG>(100, now - g_lastTick)) : 0; g_lastTick = now;
    bool motion = Motion(); if (motion != g_motion) { g_motion = motion; if (g_banner) { RECT r = {0, 0, g_bannerW, g_bannerH}; InvalidateRect(wnd, &r, FALSE); } }
    g_active = active; if (!active || !g_motion || !g_boardOn || !g_banner) return;
    g_clock += dt; if (g_clock - g_cycle0 >= SeqMs()) NewPuzzle();
    auto resting = [&](double ms) { Seq q = SeqAt(ms); return q.cur + 1 == g_boards.size() && q.st.phase == trace::kRest; };
    if (now - g_lastInval >= 33 && !(resting(g_clock - g_cycle0) && resting(g_clock - g_cycle0 - 100))) { g_lastInval = now; RECT r = {0, 0, g_bannerW, g_bannerH}; InvalidateRect(wnd, &r, FALSE); }
}
std::string AnimState() {
    const char* ph[] = {"trace", "solved", "fade", "rest"}; Seq q = g_boards.empty() ? Seq{0, {trace::kRest, 0, 0, 0}} : SeqAt(g_clock - g_cycle0); std::string sizes;
    for (auto& b : g_boards) sizes += (sizes.empty() ? "" : ",") + std::to_string(b.g.cols) + "x" + std::to_string(b.g.rows);
    return std::string(!g_boardOn ? "off" : !g_motion ? "still" : g_active ? "on" : "paused") + " board " + (g_boardOn ? (g_boardPanel ? "panel" : "panels") : "none") + " " + (sizes.empty() ? "0x0" : sizes) +
           " at " + std::to_string(q.cur) + " phase " + ph[q.st.phase] + " puzzles " + std::to_string(g_puzzles) + " frames " + std::to_string(g_frames);
}
HBITMAP Thumb(const uint8_t* rgba, int w, int h, int tw, int th) {
    HDC screen = GetDC(nullptr); uint32_t* bits = nullptr; HBITMAP b = NewDib(screen, tw, th, &bits); ReleaseDC(nullptr, screen); if (!b || !bits) return b; Canvas c{tw, th, bits};
    if (rgba && w > 0 && h > 0) Cover(c, rgba, w, h, .42f);
    else { Shade(c, kSurface, [](int, int) { return 1.f; }); float ph = th * .78f, pw = std::min(tw * .8f, ph * 1.3f); Puzzle(c, (tw - pw) / 2, (th - ph) / 2, pw, ph); }
    return b;
}
}  // namespace th
