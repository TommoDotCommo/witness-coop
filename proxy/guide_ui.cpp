#include "guide_ui.h"
#include "ui.h"
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
    UiRect(0, 0, w, h, Rgba(0.015f, 0.03f, 0.04f, 0.98f));
    UiText(16, 12, "THE WITNESS CO-OP - QUICK GUIDE", Rgba(1, .86f, .6f), sc * 1.2f);
    UiText(16, 40, "Hold F1 to read / release to return", Rgba(.4f, .85f, .95f), sc);
    for (int c = 0; c < 3; ++c) {
        float x = 16 + c * cw, y = 72;
        UiRect(x - 4, 67, cw - 6, h - 77, Rgba(.04f, .075f, .09f));
        for (const auto& line : cols[c]) {
            bool heading = !line.empty() && line.find_first_of("abcdefghijklmnopqrstuvwxyz") == std::string::npos;
            UiText(x, y, line.c_str(), heading ? Rgba(1, .86f, .6f) : Rgba(.87f, .91f, .93f), sc);
            y += row;
        }
    }
}
