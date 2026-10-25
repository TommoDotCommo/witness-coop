#pragma once
// The launcher's look, after the game's: a dark panel, the pale puzzle line, the cyan of a line that has power, the warm white of a solved panel. Native controls stay native (the tests drive them
// with window messages) and are painted over: checkboxes are puzzle start circles that light up, buttons are flat with a thin line, tabs are a row of words with a line under the chosen one.
#include <windows.h>

namespace th {
extern COLORREF kBg, kSurface, kSurface2, kLine, kAccent, kWarm, kText, kMuted, kDanger;
void Init();
HFONT Body(); HFONT Small(); HFONT Bold(); HFONT Title();
HBRUSH BgBrush(); HBRUSH SurfaceBrush();
void Style(HWND h);                                                    // paint this control in the theme (by its class); call after it is created and has its text
void MarkMuted(HWND h);                                                // a static text that is a hint: dimmer
LRESULT CtlColor(UINT msg, HDC dc, HWND ctl);                          // the answer to WM_CTLCOLOR*
void StyleTab(HWND tab);                                               // the row of tabs as words
void StyleList(HWND list);                                             // a list view
void PaintHeader(HDC dc, const RECT& client, int height, const wchar_t* subtitle);       // the banner at the top of the window
void DrawDot(HDC dc, int cx, int cy, int r, COLORREF c, bool filled);  // a round marker (members, voice)
}  // namespace th
