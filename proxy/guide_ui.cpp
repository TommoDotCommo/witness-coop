#include "guide_ui.h"
#include "ui.h"
#include "ui/theme.h"
#include "ui/guide_layout.h"
#include "../common/guide.h"
#include "pad.h"
namespace { guide::Hold hold; }
bool GuideKey(unsigned vk, bool down) { return hold.Key(vk, down); }
bool GuideOpen() { return hold.Open(); }
void GuideReset() { hold.Reset(); }
void GuideDraw(float w, float h) {
    if (!GuideOpen()) return;
    auto layout = uitext::FitGuide(w, h, guide::Content(GetCfg(), PadGlyphs()), UiTextWidth);
    const auto& cols = layout.columns;
    float sc = layout.scale, row = layout.rowHeight, cw = layout.columnWidth;
    UiRect(0, 0, w, h, uitheme::Canvas);
    float cardX = 16, cardY = 12, cardW = w - 32, cardH = h - 24;
    UiRect(cardX, cardY, cardW, cardH, uitheme::Surface);
    UiFrame(cardX, cardY, cardW, cardH, uitheme::Border);
    UiRect(cardX, cardY, 4 * sc, cardH, uitheme::Accent);
    UiText(cardX + 18 * sc, cardY + 14 * sc, "QUICK GUIDE", uitheme::Accent, sc * 1.25f);
    { float tw = UiTextWidth("QUICK GUIDE", sc * 1.25f); UiText(cardX + 18 * sc + tw + 14 * sc, cardY + 14 * sc + 5 * sc, "The Witness Co-op", uitheme::Muted, sc); }
    const char* hint = PadGlyphs() ? "Hold View + Y to keep this open" : "Hold F1 to keep this open";
    UiText(cardX + cardW - 18 * sc - UiTextWidth(hint, sc * 0.85f), cardY + 20 * sc, hint, uitheme::Muted, sc * 0.85f);
    float topY = cardY + 58 * sc, paneH = cardH - (topY - cardY) - 14 * sc;
    for (int c = 0; c < 3; ++c) {
        float x = cardX + 14 + c * cw, y = topY + 12 * sc, kw = layout.keyWidth[c];
        UiRect(x, topY, cw - 8 * sc, paneH, uitheme::Inset);
        UiFrame(x, topY, cw - 8 * sc, paneH, uitheme::Border);
        for (const auto& line : cols[c]) {
            auto tab = line.find('\t');
            if (guide::IsHeading(line)) {      // a section: accent title over a faint rule
                UiText(x + 12 * sc, y, line.c_str(), uitheme::Accent, sc * 0.9f);
                UiRect(x + 12 * sc, y + row - 3 * sc, cw - 32 * sc, 1, Rgba(uitheme::Accent.r, uitheme::Accent.g, uitheme::Accent.b, 0.3f));
            } else if (tab != std::string::npos) {      // a key row: the key, then what it does, aligned
                UiText(x + 12 * sc, y, line.substr(0, tab).c_str(), uitheme::Text, sc);
                UiText(x + 12 * sc + kw, y, line.c_str() + tab + 1, uitheme::Muted, sc);
            } else UiText(x + 12 * sc, y, line.c_str(), uitheme::Text, sc);
            y += row;
        }
    }
}
