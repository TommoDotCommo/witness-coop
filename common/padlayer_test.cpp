// T0 of the co-op pad layer (padlayer.h) from recorded pad states: tap and hold of View, the D-pad actions, the wheels, what the game may see,
// the glyph switch, the automatic voice mode and the on-screen keyboard. Mutants: PAD_MUT_LEAK (the D-pad reaches the game while View is held),
// PAD_MUT_STUCK_GLYPHS (the hints never go back to keys).
#include "padlayer.h"
#include <cstdio>
#include <string>
#include <vector>
using namespace padlayer;
static int g_fail;
static void Check(bool ok, const char* what) { printf("  %-74s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) ++g_fail; }
static Pad P(uint16_t b, int16_t rx = 0, int16_t ry = 0, uint8_t rt = 0, int16_t lx = 0, int16_t ly = 0) { Pad p; p.connected = true; p.buttons = b; p.rx = rx; p.ry = ry; p.rt = rt; p.lx = lx; p.ly = ly; return p; }
struct Frame { uint32_t t; Pad p; };
// plays a recording at 60 Hz steps given by the frames' times; returns every frame's output and the pad the game got
static std::vector<Out> Play(Layer& L, const std::vector<Frame>& rec, std::vector<Pad>* game = nullptr) {
    std::vector<Out> out;
    for (const auto& f : rec) { out.push_back(L.Step(f.p, f.t)); if (game) game->push_back(L.Filter(f.p)); }
    return out;
}
int main() {
    printf("pad layer\n");
    {   // a tap of View is a ping, nothing else; the game never sees View
        Layer L; std::vector<Pad> g; auto o = Play(L, {{0, P(0)}, {16, P(View)}, {100, P(View)}, {130, P(0)}, {150, P(0)}}, &g);
        Check(o[3].ping && !o[1].list && !o[2].list, "tap View (130 ms): one ping, no player list");
        int pings = 0; for (auto& x : o) pings += x.ping; Check(pings == 1, "exactly one ping for one tap");
        bool view = false; for (auto& x : g) view |= (x.buttons & View) != 0; Check(!view, "the game never sees View");
    }
    {   // hold View: the list after 250 ms, no ping on release
        Layer L; auto o = Play(L, {{0, P(View)}, {200, P(View)}, {260, P(View)}, {900, P(View)}, {916, P(0)}});
        Check(!o[1].list && o[2].list && o[3].list, "hold View: the player list opens at 250 ms and stays while held");
        Check(!o[4].list && !o[4].ping, "let go after a hold: the list closes, no ping");
    }
    {   // View + D-pad up: the emote wheel, the right stick aims, release closes; the D-pad and the stick never reach the game
        Layer L; std::vector<Pad> g;
        auto o = Play(L, {{0, P(View)}, {50, P(View | Up)}, {70, P(View, 30000, 0)}, {90, P(View | LB, 30000, 0)}, {110, P(View, 30000, 0)}, {130, P(0, 30000, 0)}, {150, P(Up)}, {170, P(0)}}, &g);
        Check(o[1].wheel == Wheel::Emote && o[2].wheel == Wheel::Emote && !o[1].list, "View + D-pad up opens the emote wheel (not the list)");
        Check(o[2].aimX > 0.9f && o[2].aimY == 0, "the right stick aims the wheel (right = +x)");
        Check(o[3].emotePage && !o[4].emotePage, "LB turns the emote wheel's page once");
        Check(o[5].wheel == Wheel::None && !o[5].ping, "letting go of View closes the wheel (it chooses), no ping");
        bool dpad = false, stick = false; for (size_t i = 0; i < 5; ++i) { dpad |= (g[i].buttons & (Up | Down | Left | Right)) != 0; stick |= g[i].rx != 0; }
        Check(!dpad, "while View is held the D-pad never reaches the game");
        Check(!stick, "while View is held the right stick never reaches the game (the camera stays)");
        Check(g[5].rx == 30000, "after View is let go the right stick is the game's again");
        Check((g[6].buttons & Up) != 0, "a fresh D-pad press after View is the game's again");
    }
    {   // a D-pad press held from the layer stays the mod's until released
        Layer L; std::vector<Pad> g; Play(L, {{0, P(View)}, {40, P(View | Left)}, {80, P(Left)}, {100, P(Left)}, {120, P(0)}, {140, P(Left)}}, &g);
        Check(!(g[2].buttons & Left) && !(g[3].buttons & Left), "D-pad left pressed with View stays swallowed after View is let go");
        Check((g[5].buttons & Left) != 0, "and the next press of it reaches the game");
    }
    {   // the other D-pad actions are single shots; Y holds the guide; RB the quick chat wheel
        Layer L; auto o = Play(L, {{0, P(View)}, {30, P(View | Down)}, {60, P(View)}, {90, P(View | Left)}, {110, P(View | Left)}, {130, P(View | Right)}, {150, P(View | Y)}, {170, P(View)}, {190, P(View | RB)}, {210, P(View)}, {230, P(0)}});
        Check(o[1].chat && !o[2].chat, "View + D-pad down opens chat once");
        Check(o[3].watchPrev && !o[4].watchPrev && o[5].watchNext, "View + left / right: watch previous / next, once per press");
        Check(o[6].guide && !o[7].guide, "View + Y holds the quick guide");
        Check(o[8].wheel == Wheel::Quick && o[9].wheel == Wheel::Quick && o[10].wheel == Wheel::None, "View + RB opens the quick chat wheel until View is let go");
        int pings = 0; for (auto& x : o) pings += x.ping; Check(pings == 0, "a View press used with another button is no ping");
    }
    {   // the list's click: RT or A
        Layer L; auto o = Play(L, {{0, P(View)}, {300, P(View)}, {320, P(View, 0, 0, 200)}, {340, P(View, 0, 0, 200)}, {360, P(View | A)}, {380, P(0)}});
        Check(o[2].click && !o[3].click && o[4].click, "in the list RT and A click, once per press");
    }
    {   // the on-screen keyboard has the pad: the game gets nothing while it is up
        Layer L; L.osk = true; std::vector<Pad> g; Play(L, {{0, P(A, 0, 0, 0, 0, 30000)}}, &g);
        Check(g[0].buttons == 0 && g[0].ly == 0, "while the on-screen keyboard is up the game gets no pad");
        L.osk = false; Play(L, {{20, P(A)}, {40, P(0)}, {60, P(A)}}, &g);
        Check(!(g[1].buttons & A) && (g[3].buttons & A), "A held from the keyboard stays the mod's until released");
    }
    {   // pad_layer=0: the game gets everything, the mod nothing
        Layer L; L.enabled = false; std::vector<Pad> g; auto o = Play(L, {{0, P(View | Up)}, {50, P(0)}}, &g);
        Check(g[0].buttons == (View | Up) && !o[1].ping && o[0].wheel == Wheel::None, "layer off: the game gets View and the D-pad, no co-op action");
    }
    printf("wheel slots\n");
    Check(WheelSlot(0, -100, 5) == 0 && WheelSlot(100, 0, 4) == 1 && WheelSlot(0, 100, 4) == 2 && WheelSlot(-100, 0, 4) == 3, "slot 0 up, then clockwise");
    Check(WheelSlot(5, 5, 5) == -1, "the middle chooses nothing");
    Check(kQuickCount == 5 && std::string(kQuickChat[0]) == "Ready" && std::string(kQuickChat[3]) == "Come look at this", "the quick chat phrases are the shared ones");
    printf("glyphs (K7)\n");
    {
        Glyphs G; Check(!G.pad(), "keys at start");
        G.Pad(P(0, 0, 0)); Check(!G.pad(), "a connected pad at rest does not switch");
        G.Pad(P(A)); Check(G.pad(), "a pad press switches to pad buttons at once");
        G.Key(); Check(!G.pad(), "a key switches back to keys at once");
        G.Pad(P(A)); Check(!G.pad(), "a button still held from before does not switch again");
        G.Pad(P(0, 0, 0, 0, 20000, 0)); Check(G.pad(), "moving a stick switches to pad buttons");
        Check(std::string(Hint(true, "Esc", "B")) == "B" && std::string(Hint(false, "Esc", "B")) == "Esc", "a hint shows the pad's button or the key");
    }
    printf("voice (K5)\n");
    Check(VoiceMode(3, false, 0) == 2 && VoiceMode(3, true, 0) == 1, "automatic: push to talk on keys, open mic on a pad");
    Check(VoiceMode(3, true, 2) == 2 && PadVoiceButton(2) == RS, "automatic with a stick click chosen: push to talk on the pad too");
    Check(VoiceMode(2, true, 0) == 2 && VoiceMode(1, false, 0) == 1 && VoiceMode(0, true, 0) == 0, "a mode the player chose stays");
    printf("on-screen keyboard (K6)\n");
    {
        Osk K; K.Reset(P(View | Down));
        auto o = K.Step(P(Down), 0, true); Check(o.key == OskKey::None && K.row == 1, "the D-pad down that opened chat does not move the keyboard");
        K.Step(P(0), 16, true); K.Step(P(Right), 32, true); K.Step(P(0), 48, true);
        o = K.Step(P(A), 64, true); Check(o.key == OskKey::Char && o.ch == 'w', "right then A types w");
        K.Step(P(0), 80, false); o = K.Step(P(B), 96, false); Check(o.key == OskKey::Back, "B deletes a character");
        K.Step(P(0), 112, true); o = K.Step(P(B), 128, true); Check(o.key == OskKey::Close, "B on an empty line closes");
        K.Step(P(0), 144, false); o = K.Step(P(Start), 160, false); Check(o.key == OskKey::Send, "Menu sends");
        K.Step(P(0), 176, false); o = K.Step(P(Y), 192, false); Check(o.key == OskKey::Space, "Y types a space");
        Osk R; R.Reset(P(0)); R.Step(P(Up), 0, false); int r0 = R.row; R.Step(P(Up), 300, false); R.Step(P(Up), 420, false); Check(r0 == 0 && R.row == 3, "a held direction repeats after 400 ms");
    }
    printf(g_fail ? "pad layer: %d FAILED\n" : "pad layer: all ok\n", g_fail);
    return g_fail ? 1 : 0;
}
