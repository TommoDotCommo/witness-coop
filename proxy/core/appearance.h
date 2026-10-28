#pragma once
// Portable appearance ABI. Never send native struct bytes; use Encode/Decode.
#include <array>
#include <cstdint>
#include <cstddef>
#include <string>
#include <algorithm>
#include <cmath>
namespace witness {
constexpr size_t kAppearanceBytes = 12, kAppearanceFeatures = 8;
constexpr const char* kAppearanceNames[] = {"skin", "hair", "eyes", "coat", "scarf", "trousers", "boots", "gear"};
struct Appearance {
    std::array<uint8_t, kAppearanceFeatures> choice{};
    bool operator==(const Appearance& o) const { return choice == o.choice; }
    bool operator!=(const Appearance& o) const { return !(*this == o); }
};
inline Appearance ClampAppearance(Appearance a) {
#ifndef WA_MUT_CLAMP
    for (auto& c : a.choice) c = std::min<uint8_t>(c, 3);
#endif
    return a;
}
inline std::array<uint8_t, kAppearanceBytes> EncodeAppearance(Appearance a) {
    a = ClampAppearance(a); std::array<uint8_t, kAppearanceBytes> b{}; b[0] = 1;
    std::copy(a.choice.begin(), a.choice.end(), b.begin() + 1); return b;
}
inline bool DecodeAppearance(const uint8_t* b, size_t n, Appearance* out) {
#ifndef WA_MUT_LENGTH
    if (n != kAppearanceBytes) return false;
#else
    (void)n;
#endif
#ifndef WA_MUT_VERSION
    if (b[0] != 1) return false;
#endif
#ifndef WA_MUT_RESERVED
    if (b[9] || b[10] || b[11]) return false;
#endif
    Appearance a; std::copy(b + 1, b + 9, a.choice.begin()); *out = ClampAppearance(a); return true;
}
// Decimal text is deliberately strict: no signs, whitespace, overflow or trailing junk.
inline bool SetAppearanceChoice(Appearance* a, const std::string& name, const std::string& value) {
#ifndef WA_MUT_TEXT
    if (value.size() != 1 || value[0] < '0' || value[0] > '3') return false;
#else
    if (value.empty()) return false;
#endif
    for (size_t i = 0; i < kAppearanceFeatures; ++i) if (name == kAppearanceNames[i]) { a->choice[i] = uint8_t(value[0] - '0'); return true; }
    return false;
}
// Configuration parsing is shared with native tests; legacy selection keys are
// deliberately outside this vocabulary and cannot affect the descriptor.
inline bool SetAppearanceConfigLine(Appearance* a, std::string line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    const std::string prefix = "appearance_";
    if (line.rfind(prefix, 0) != 0) return false;
    size_t eq = line.find('='); if (eq == std::string::npos) return false;
    return SetAppearanceChoice(a, line.substr(prefix.size(), eq - prefix.size()), line.substr(eq + 1));
}
// Multipliers act on the baked base colour, preserving weave, shading and relative
// colour of companion materials. Slot zero is exactly identity for existing players.
inline void AppearanceTint(const Appearance& raw, unsigned group, float out[3]) {
    out[0] = out[1] = out[2] = 1;
    if (!group || group > kAppearanceFeatures) return;
    const auto a = ClampAppearance(raw); unsigned c = a.choice[group - 1];
    if (!c) return;
    // Linear RGB ratios, deliberately bounded. Groups: skin/hair/eyes/coat/scarf/trousers/boots/gear.
    static constexpr float palette[8][3][3] = {
        {{0.70f,0.55f,0.45f},{0.40f,0.26f,0.20f},{1.16f,1.27f,1.35f}},
        {{0.35f,0.35f,0.35f},{2.20f,1.55f,0.85f},{2.10f,2.10f,2.10f}},
        {{0.55f,1.60f,2.60f},{0.70f,1.80f,0.85f},{1.70f,1.10f,0.55f}},
        {{1.70f,0.55f,0.45f},{0.50f,0.75f,1.65f},{1.20f,1.10f,0.65f}},
        {{0.30f,0.65f,1.70f},{0.75f,0.20f,0.25f},{0.50f,1.20f,0.65f}},
        {{0.45f,0.50f,0.55f},{0.55f,0.65f,1.10f},{0.70f,0.90f,0.50f}},
        {{0.45f,0.50f,0.55f},{1.50f,1.20f,0.80f},{0.70f,0.95f,0.70f}},
        {{0.45f,0.50f,0.55f},{1.40f,1.30f,1.10f},{0.55f,0.95f,0.65f}}
    };
    for (int k = 0; k < 3; ++k) out[k] = palette[group - 1][c - 1][k];
}
} // namespace witness
