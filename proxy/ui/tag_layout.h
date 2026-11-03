#pragma once
// A remote player's name tag over their head: "Name  12 ms" on a dark box with a bar in their colour under it, and while they talk the three
// moving voice bars beside the text, inside the same box (side by side: before 2026-11-04 the bars were drawn 30 px right of the head, over the ping).
// Pure layout: overlay.cpp draws the tag, social.cpp animates the bars in the box this gives; tag_test.cpp checks it at every scale.
namespace wtag {
struct Box { float x, y, w, h; };
struct Layout { Box back, under, bars; float textX, textY; };      // bars.w is 0 when the player is not talking
constexpr float kBarW = 4.5f, kBarStep = 7.f, kBarMin = 7.f, kBarMax = 16.f, kGap = 8.f;      // at scale 1: bar width, bar pitch, height range, the gap after the text
// cx: the projected head point (the tag is centred on it); bottom: the text's baseline row ends here; textW / lineH: the text's size at scale k.
inline Layout Compute(float cx, float bottom, float textW, float lineH, float k, bool speaking) {
    Layout L; float barsW = speaking ? kGap * k + 2 * kBarStep * k + kBarW * k : 0, total = textW + barsW;
    L.textX = cx - total / 2; L.textY = bottom - lineH;
    L.back = {L.textX - 5 * k, L.textY - 3 * k, total + 10 * k, lineH + 6 * k};
    L.under = {L.back.x, L.textY + lineH + 3 * k, L.back.w, 3 * k};
    L.bars = {L.textX + textW + (speaking ? kGap * k : 0), L.textY, speaking ? barsW - kGap * k : 0, lineH};
#ifdef WT_MUT_OVERLAP
    L.textX = cx - textW / 2; L.back = {L.textX - 5 * k, L.textY - 3 * k, textW + 10 * k, lineH + 6 * k}; L.bars.x = cx + 30 * k;      // (mutant: the old place, 30 px right of the head)
#endif
    return L;
}
// One bar's height at phase s (0..1), never taller than the box it sits in.
inline float BarHeight(float s, float k, float boxH) { float h = (kBarMin + (kBarMax - kBarMin) * s) * k; return h < boxH ? h : boxH; }
}  // namespace wtag
