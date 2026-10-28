#include "guide_ui.h"
#include "ui.h"
#include "ui/theme.h"
#include "../common/guide.h"
namespace { guide::Hold hold; }
bool GuideKey(unsigned vk, bool down) { return hold.Key(vk, down); }
bool GuideOpen() { return hold.Open(); }
void GuideReset() { hold.Reset(); }
void GuideDraw(float w, float h) {
    if (!GuideOpen()) return;
    auto cols = guide::Wrap(guide::Content(GetCfg()));
    auto layout = guide::Fit(w, h, cols);
    float sc = layout.scale, row = layout.rowHeight, cw = layout.columnWidth;
    UiRect(0, 0, w, h, uitheme::Canvas);
    float cardX = 16, cardY = 12, cardW = w - 32, cardH = h - 24;
    UiRect(cardX, cardY, cardW, cardH, uitheme::Surface);
    UiFrame(cardX, cardY, cardW, cardH, uitheme::Border);
    UiRect(cardX, cardY, 4 * sc, cardH, uitheme::Accent);
    UiText(cardX + 18 * sc, cardY + 12 * sc, "THE WITNESS CO-OP - QUICK GUIDE", uitheme::Text, sc * 1.25f);
    UiText(cardX + 18 * sc, cardY + 42 * sc, "Hold F1 to read / release to return", uitheme::Muted, sc * 0.85f);
    float topY = cardY + 70 * sc, paneH = cardH - (topY - cardY) - 14 * sc;
    for (int c = 0; c < 3; ++c) {
        float x = cardX + 14 * sc + c * cw, y = topY + 10 * sc;
        UiRect(x, topY, cw - 8 * sc, paneH, uitheme::Inset);
        UiFrame(x, topY, cw - 8 * sc, paneH, uitheme::Border);
        UiRect(x + 1, topY + 1, cw - 10 * sc, 2, Rgba(uitheme::Accent.r, uitheme::Accent.g, uitheme::Accent.b, 0.25f));
        for (const auto& line : cols[c]) {
            bool heading = !line.empty() && line.find_first_of("abcdefghijklmnopqrstuvwxyz") == std::string::npos;
            UiText(x + 10 * sc, y, line.c_str(), heading ? uitheme::Accent : uitheme::Text, sc);
            y += row;
        }
    }
}
