#include "text.h"
#include "theme.h"
#include "guide_layout.h"
#include <cstdio>
#include <string>
static int failures;
static void check(bool ok, const char* what) { if (!ok) { std::printf("FAILED: %s\n", what); ++failures; } }
int main() {
    uitext::Glyph glyphs[95];
    for (int i = 0; i < 95; ++i) glyphs[i].advance = 8;
    glyphs['i' - 32].advance = 3; glyphs['M' - 32].advance = 15; glyphs[' ' - 32].advance = 4;
    check(uitext::Width("iiii", 1, glyphs) == 12, "narrow glyphs use their own advances");
    check(uitext::Width("MMMM", 1, glyphs) == 60, "wide glyphs use their own advances");
    check(uitext::Width("i i", 1, glyphs) == 10, "spaces have proportional width");
    check(uitext::Width("MMMM\nii", 1, glyphs) == 60, "multiline width is the widest line");
    check(uitext::Width("iiii", 2, glyphs) == 24, "metrics scale linearly");
    auto measure = [&](const char* s) { return uitext::Width(s, 1, glyphs); };
    std::string wrapped = uitext::Wrap("ii ii MMMM MMMM MMMM", 150, measure);
    check(wrapped == "ii ii MMMM MMMM\nMMMM", "wrapping uses measured pixels rather than character cells");
    for (const auto& line : {std::string("ii ii MMMM MMMM"), std::string("MMMM")}) check(measure(line.c_str()) <= 150, "wrapped lines fit their bounds");
    std::string longWord = uitext::Wrap("MMMMMMMM", 32, measure);
    check(longWord == "MM\nMM\nMM\nMM", "long words split without clipping or loss");
    check(uitext::ContainedScale(100, 200, 1) == 0.5f, "layout shrinks text to its container");
    check(uitext::ContainedScale(200, 100, 1) == 1, "layout does not enlarge beyond the requested size");
    check(uitext::ContainedScale(100, 400, 2) == 0.5f, "fitting scaled text accounts for its existing scale");
    check(uitext::Wrap("ii\n\nMM", 100, measure) == "ii\n\nMM", "explicit blank lines survive wrapping");
    for (const auto& size : {std::pair<float,float>{1280,720}, {1920,1080}, {2560,1440}, {3440,1440}, {3840,2160}, {1024,768}}) {
        auto layout = uitext::FitGuide(size.first, size.second, guide::Content(Cfg()),
            [&](const char* t, float sc) { return uitext::Width(t, sc, glyphs); });
        for (const auto& col : layout.columns) {
            check((col.size() * 24 + 94) * layout.scale <= size.second - 24.0f + .01f, "guide rows fit vertically");
            for (const auto& line : col) check(uitext::Width(line.c_str(), layout.scale, glyphs) <= layout.columnWidth - 28 * layout.scale + .01f, "measured guide lines fit column");
        }
    }
    using namespace uitheme;
    check(Contrast(Text, Surface) >= 7, "primary text has accessible contrast on menu surfaces");
    check(Contrast(Muted, Surface) >= 4.5f, "secondary text has accessible contrast");
    check(Contrast(Text, Selected) >= 7, "selected rows retain accessible contrast");
    check(Contrast(Text, Hover) >= 7, "hover rows retain accessible contrast");
    std::printf("%s\n", failures ? "FAILED" : "ALL OK"); return failures ? 1 : 0;
}
