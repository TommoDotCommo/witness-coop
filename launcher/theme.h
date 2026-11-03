#pragma once
// The launcher's look, after the game's: a dark panel, the pale puzzle line, the cyan of a line that has power, the warm white of a solved panel. Native controls stay native (the tests drive them
// with window messages) and are painted over: checkboxes are puzzle start circles that light up, buttons are flat with a thin line, tabs are a row of words with a line under the chosen one.
#include <windows.h>
#include <cstdint>
#include <string>

namespace th {
extern COLORREF kBg, kSurface, kSurface2, kLine, kAccent, kWarm, kText, kMuted, kDanger;
void Init(const std::wstring& gameDir = L"");                           // headings, tabs and buttons in the game's own face (Karmina Bold, read in memory from the player's data-pc.zip), else Segoe UI
const std::string& FontState();                                        // which face the headings use and why (the stat file's "font" line)
HFONT Body(); HFONT Small(); HFONT Bold(); HFONT Title();
HBRUSH BgBrush(); HBRUSH SurfaceBrush();
void Style(HWND h);                                                    // paint this control in the theme (by its class); call after it is created and has its text
void MarkMuted(HWND h);                                                // a static text that is a hint: dimmer
LRESULT CtlColor(UINT msg, HDC dc, HWND ctl);                          // the answer to WM_CTLCOLOR*
unsigned TabPaintCount();                                              // how many times the tab row was painted (the test counts them: a hover must not repaint it again and again)
void StyleTab(HWND tab);                                               // the row of tabs as words
void StyleList(HWND list);                                             // a list view
// The banner at the top of the window: the player's newest save picture (SetArt), cropped to fill, with a soft shade where the title sits, or without one a
// Witness puzzle panel drawn here (a yellow panel, its grid, a start circle, an end cap and a traced white line). Nothing from the game is shipped.
int BannerHeight(int clientH);
void SetArt(const uint8_t* rgba, int w, int h, const std::string& name);  // the picture (RGBA, w x h) or nullptr: the drawn panel
const std::string& ArtState();                                          // the stat file's "art" line: "picture <name> <w>x<h>" or "panel"
void PaintHeader(HDC dc, const RECT& client, int height, const wchar_t* subtitle);
// A save's thumbnail for the Saves list (its picture cropped to fill), or the drawn panel when it has none; the caller owns the bitmap.
HBITMAP Thumb(const uint8_t* rgba, int w, int h, int tw, int th);
void MarkTile(HWND radio, HWND desc);                                  // a radio button drawn as a tile (its name, a start circle, the description from `desc`, which is hidden)
void DrawDot(HDC dc, int cx, int cy, int r, COLORREF c, bool filled);  // a round marker (members, voice)
}  // namespace th
