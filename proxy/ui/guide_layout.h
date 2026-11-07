#pragma once
#include "text.h"
#include "../../common/guide.h"

namespace uitext {
// The quick guide laid out at a measured size. Lines are headings, paragraph lines, blank lines, or table rows "key\tdescription"
// (a row's wrapped description continues as "\tmore"). keyWidth[c]: where column c's descriptions start, after its widest key.
struct GuideLayout {
    guide::Columns columns;
    float scale, columnWidth, rowHeight, keyWidth[3];
};
// Measure at the actual font size. Search the largest size that fits every word
// and every row, with the same padding and line height used by the renderer.
template <typename Measure>
GuideLayout FitGuide(float width, float height, const guide::Columns& content, Measure measure) {
    GuideLayout out{};
    out.columnWidth = (width - 60) / 3;
    auto lines = [&](const std::string& text, float w, float scale) {
        std::vector<std::string> v; auto wrapped = Wrap(text.c_str(), w, [&](const char* s) { return measure(s, scale); });
        size_t at = 0; while (true) { auto end = wrapped.find('\n', at); v.push_back(wrapped.substr(at, end - at)); if (end == std::string::npos) break; at = end + 1; }
        return v; };
    auto layout = [&](float scale) {
        out.scale = scale; out.rowHeight = 24 * scale;
        size_t rows = 0; float inner = out.columnWidth - 28 * scale;
        for (size_t c = 0; c < content.size(); ++c) {
            auto& col = out.columns[c]; col.clear();
            float kw = 0;
            for (const auto& l : content[c]) { auto t = l.find('\t'); if (t != std::string::npos) kw = std::max(kw, measure(l.substr(0, t).c_str(), scale)); }
            out.keyWidth[c] = kw = std::min(kw + 18 * scale, inner * 0.45f);
            bool afterParagraph = false;
            for (const auto& l : content[c]) {
                if (guide::IsHeading(l)) { if (!col.empty() && !afterParagraph) col.push_back(""); col.push_back(l); afterParagraph = false; continue; }
                auto t = l.find('\t');
                if (t == std::string::npos) { if (!col.empty() && !col.back().empty() && !guide::IsHeading(col.back())) col.push_back(""); for (auto& w : lines(l, inner, scale)) col.push_back(w); col.push_back(""); afterParagraph = true; continue; }
                auto keys = lines(l.substr(0, t), kw - 12 * scale, scale), desc = lines(l.substr(t + 1), inner - kw, scale);
                for (size_t i = 0; i < std::max(keys.size(), desc.size()); ++i) col.push_back((i < keys.size() ? keys[i] : "") + "\t" + (i < desc.size() ? desc[i] : ""));
                afterParagraph = false;
            }
            rows = std::max(rows, col.size());
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
