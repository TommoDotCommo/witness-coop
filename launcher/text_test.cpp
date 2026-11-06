// Launcher text in the game's face (theme.cpp th::Text): drawn through DirectWrite, smooth (ClearType or grey, in both directions), not GDI's coarse grey edges the
// owner saw as pixelated (2026-11-06); and placed and measured as GDI did, so no layout moves. Needs the player's data-pc.zip for the font:
// usage: text-test.exe <game folder>   (no folder or no zip: SKIP, exit 0). The mutant WL_MUT_GDI_GAME_FACE keeps GDI and must fail.
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "theme.h"
static int g_fail;
static void Check(bool ok, const char* what, const std::string& detail = "") { printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : ": ", detail.c_str()); if (!ok) ++g_fail; }
struct Shot { int w, h; uint32_t* px; HDC dc; HBITMAP bmp; };
static Shot NewShot(int w, int h) { Shot s{w, h, nullptr, CreateCompatibleDC(nullptr), nullptr}; BITMAPINFO bi = {}; bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB}; s.bmp = CreateDIBSection(s.dc, &bi, DIB_RGB_COLORS, (void**)&s.px, nullptr, 0); SelectObject(s.dc, s.bmp); RECT r = {0, 0, w, h}; FillRect(s.dc, &r, th::BgBrush()); SetBkMode(s.dc, TRANSPARENT); SetTextColor(s.dc, th::kText); return s; }
static void Free(Shot& s) { DeleteDC(s.dc); DeleteObject(s.bmp); }
struct Ink { int left = 1 << 30, top = 1 << 30, right = -1, bottom = -1, partial = 0, fringe = 0, steps = 0; };
// the ink: its box, how many edge pixels are part-covered, how many have ClearType's colour fringe, and how many grey steps the edges use
static Ink Measure(const Shot& s) {
    Ink k; bool seen[256] = {}; uint32_t bg = s.px[0];
    for (int y = 0; y < s.h; ++y) for (int x = 0; x < s.w; ++x) {
        uint32_t p = s.px[y * s.w + x]; if (p == bg) continue; int r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
        k.left = std::min(k.left, x); k.right = std::max(k.right, x); k.top = std::min(k.top, y); k.bottom = std::max(k.bottom, y);
        int lum = (r + g + b) / 3; if (lum > 40 && lum < 200) { ++k.partial; if (!seen[lum]) { seen[lum] = true; ++k.steps; } }
        if (abs(r - b) > 30) ++k.fringe;
    }
    return k;
}
int wmain(int argc, wchar_t** argv) {
    std::wstring dir = argc > 1 ? argv[1] : L""; if (dir.empty() || GetFileAttributesW((dir + L"\\data-pc.zip").c_str()) == INVALID_FILE_ATTRIBUTES) { printf("SKIP text-test: no game folder with data-pc.zip given\n"); return 0; }
    th::Init(dir); printf("font %s, engine %s\n", th::FontState().c_str(), th::TextEngine());
    Check(th::FontState().rfind("Karmina", 0) == 0, "the game's face was read", th::FontState());
    Check(std::string(th::TextEngine()) == "directwrite", "the game's face is drawn through DirectWrite", th::TextEngine());
    const wchar_t* t = L"Through Steam (friends; no port forwarding)";
    Shot a = NewShot(420, 40); SelectObject(a.dc, th::Bold()); RECT r = {10, 0, 410, 40}; th::Text(a.dc, t, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS); GdiFlush(); Ink k = Measure(a);
    BOOL smooth = FALSE; UINT type = 0; SystemParametersInfoW(SPI_GETFONTSMOOTHING, 0, &smooth, 0); SystemParametersInfoW(SPI_GETFONTSMOOTHINGTYPE, 0, &type, 0);
    char d[200]; snprintf(d, sizeof d, "ink %d..%d x %d..%d, %d part-covered pixels in %d grey steps, %d with colour fringe", k.left, k.right, k.top, k.bottom, k.partial, k.steps, k.fringe); printf("%s\n", d);
    Check(k.right > k.left + 200 && k.bottom > k.top + 8, "the text is drawn", d);
    if (smooth && type == FE_FONTSMOOTHINGCLEARTYPE) Check(k.fringe > 150, "ClearType edges (Windows has ClearType on): GDI drew this face grey only", d);
    else Check(k.steps > 60, "smooth grey edges (ClearType off in Windows)", d);
    // placed as GDI places it: vertically centred the same, and DT_CALCRECT measures what is drawn
    HFONT gdiTwin = CreateFontW(-16, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Karmina");
    Shot b = NewShot(420, 40); SelectObject(b.dc, gdiTwin); RECT r2 = r; DrawTextW(b.dc, t, -1, &r2, DT_LEFT | DT_VCENTER | DT_SINGLELINE); GdiFlush(); Ink g = Measure(b);
    snprintf(d, sizeof d, "ink top/bottom %d/%d, GDI's %d/%d; width %d, GDI's %d", k.top, k.bottom, g.top, g.bottom, k.right - k.left, g.right - g.left); printf("%s\n", d);
    Check(abs(k.top - g.top) <= 2 && abs(k.bottom - g.bottom) <= 2, "vertically where GDI put it (DT_VCENTER)", d);
    Check(abs((k.right - k.left) - (g.right - g.left)) <= (g.right - g.left) / 12, "about as wide as GDI drew it", d);
    RECT m = {0, 0, 0, 0}; th::Text(a.dc, t, -1, &m, DT_CALCRECT | DT_SINGLELINE); snprintf(d, sizeof d, "calc %ldx%ld, ink %d wide", m.right, m.bottom, k.right - k.left + 1);
    Check(m.right >= k.right - k.left + 1 - 1 && m.right <= k.right - k.left + 1 + 6 && m.bottom >= 16 && m.bottom <= 26, "DT_CALCRECT measures the drawn text", d);
    // a word-wrapped label: DT_CALCRECT's height is what drawing takes
    RECT w = {0, 0, 120, 0}; int h = th::Text(a.dc, t, -1, &w, DT_CALCRECT | DT_WORDBREAK); snprintf(d, sizeof d, "%d px tall at 120 px wide (returned %d)", (int)w.bottom, h); Check(w.bottom >= 3 * 18 && w.bottom == h, "wrapped text measures several lines", d);
    // other faces stay GDI's (Segoe UI): the same pixels as DrawTextW
    Shot c1 = NewShot(300, 30), c2 = NewShot(300, 30); SelectObject(c1.dc, th::Body()); SelectObject(c2.dc, th::Body()); RECT q1 = {4, 0, 296, 30}, q2 = q1;
    th::Text(c1.dc, L"Open a lobby; friends join", -1, &q1, DT_VCENTER | DT_SINGLELINE); DrawTextW(c2.dc, L"Open a lobby; friends join", -1, &q2, DT_VCENTER | DT_SINGLELINE); GdiFlush();
    Check(memcmp(c1.px, c2.px, 300 * 30 * 4) == 0, "Segoe UI text is GDI's, unchanged");
    Free(a); Free(b); Free(c1); Free(c2); DeleteObject(gdiTwin);
    printf(g_fail ? "text-test FAILED (%d)\n" : "text-test OK\n", g_fail); return g_fail ? 1 : 0;
}
