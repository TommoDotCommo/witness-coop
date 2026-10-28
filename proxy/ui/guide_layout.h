#pragma once
#include "text.h"
#include "../../common/guide.h"

namespace uitext {
struct GuideLayout {
    guide::Columns columns;
    float scale, columnWidth, rowHeight;
};
// Measure at the actual font size. Search the largest size that fits every word
// and every row, with the same padding and line height used by the renderer.
template <typename Measure>
GuideLayout FitGuide(float width, float height, const guide::Columns& content, Measure measure) {
    GuideLayout out{};
    out.columnWidth = (width - 60) / 3;
    auto layout = [&](float scale) {
        out.scale = scale; out.rowHeight = 24 * scale;
        size_t rows = 0;
        for (size_t c = 0; c < content.size(); ++c) {
            out.columns[c].clear();
            for (const auto& paragraph : content[c]) {
                auto wrapped = Wrap(paragraph.c_str(), out.columnWidth - 28 * scale,
                    [&](const char* s) { return measure(s, scale); });
                size_t at = 0;
                do {
                    auto end = wrapped.find('\n', at);
                    out.columns[c].push_back(wrapped.substr(at, end - at));
                    if (end == std::string::npos) break;
                    at = end + 1;
                } while (true);
                if (c) out.columns[c].push_back("");
            }
            rows = std::max(rows, out.columns[c].size());
        }
        return (rows * 24 + 94) * scale <= height - 24;
    };
    float lo = 0.1f, hi = std::min(width / 1280.f, height / 720.f);
    for (int i = 0; i < 20; ++i) {
        float mid = (lo + hi) / 2;
        if (layout(mid)) lo = mid; else hi = mid;
    }
    layout(lo);
    return out;
}
} // namespace uitext
