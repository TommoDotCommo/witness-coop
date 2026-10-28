#pragma once
#include <algorithm>
#include <cstddef>
#include <string>

namespace uitext {
struct Glyph { float advance = 0; };

template <size_t N>
float Width(const char* text, float scale, const Glyph (&glyphs)[N], int first = 32) {
    float line = 0, widest = 0;
    for (; *text; ++text) {
        if (*text == '\n') { widest = std::max(widest, line); line = 0; continue; }
        int ch = static_cast<unsigned char>(*text) - first;
        line += (ch >= 0 && static_cast<size_t>(ch) < N ? glyphs[ch].advance : glyphs['?' - first].advance) * scale;
    }
    return std::max(widest, line);
}

template <typename Measure>
std::string Wrap(const char* text, float maxWidth, Measure measure) {
    std::string out, line;
    const char* p = text;
    while (*p) {
        const char* end = p; while (*end && *end != ' ' && *end != '\n') ++end;
        std::string word(p, end);
        std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && measure(candidate.c_str()) > maxWidth) { out += line + '\n'; line.clear(); candidate = word; }
        while (measure(word.c_str()) > maxWidth && word.size() > 1) {
            size_t keep = 1;
            while (keep < word.size() && measure(word.substr(0, keep + 1).c_str()) <= maxWidth) ++keep;
            if (!line.empty()) { out += line + '\n'; line.clear(); }
            out += word.substr(0, keep) + '\n'; word.erase(0, keep);
        }
        line = line.empty() ? word : line + " " + word;
        p = end;
        if (*p == '\n') { out += line + '\n'; line.clear(); ++p; }
        else if (*p == ' ') ++p;
    }
    return out + line;
}

inline float ContainedScale(float available, float needed, float wanted, float minimum = 0.0f) {
    if (needed <= 0 || available <= 0) return minimum;
    return std::max(minimum, std::min(wanted, available / needed));
}
} // namespace uitext
