// Game-free check of the name tag's layout (tag_layout.h): the voice bars sit beside the name and ping, never over them, inside the tag's box,
// and the whole tag stays centred on the player's head. Mutant WT_MUT_OVERLAP (the bars at their old place, 30 px right of the head) must fail.
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include "tag_layout.h"
static int fails;
static void check(bool c, const char* what, float k, float tw) { if (!c) { ++fails; printf("FAILED %s (k %.2f, text %.0f px)\n", what, k, tw); } }
int main() {
    for (float k : {0.6f, 0.7f, 1.f, 1.5f, 2.f})
        for (float tw : {28.f, 90.f, 150.f, 320.f})      // "A  0 ms" .. a long name with its ping
            for (int sp = 0; sp < 2; ++sp) {
                float cx = 640, bottom = 300, lh = 24 * k; wtag::Layout L = wtag::Compute(cx, bottom, tw, lh, k, sp != 0);
                check(std::fabs(L.back.x + L.back.w / 2 - cx) < 0.5f, "the tag is centred on the head", k, tw);
                check(L.textX >= L.back.x && L.textX + tw <= L.back.x + L.back.w, "the text is inside its box", k, tw);
                if (!sp) { check(L.bars.w == 0, "no bars while silent", k, tw); continue; }
                check(L.bars.x >= L.textX + tw + 4 * k, "the bars start after the ping text", k, tw);
                check(L.bars.x + L.bars.w <= L.back.x + L.back.w + 0.01f, "the bars are inside the box", k, tw);
                check(L.bars.w >= 2 * wtag::kBarStep * k + wtag::kBarW * k - 0.01f, "room for three bars", k, tw);
                check(wtag::BarHeight(1, k, L.bars.h) <= L.bars.h && L.bars.y >= L.back.y, "the tallest bar fits the box", k, tw);
            }
    printf(fails ? "tag layout: %d FAILED\n" : "tag layout: all OK\n", fails); return fails ? 1 : 0;
}
