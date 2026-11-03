// Game-free test of the autorepeat rule (key_repeat.h): a real hold is one press and then repeats about 30 a second; a press key acts exactly once per hold,
// a hold key and a typed key are left alone. Mutants: WK_MUT_NO_IGNORE (every repeat acts), WK_MUT_WHILE_TYPING (a held letter in the chat box stops typing).
#include "key_repeat.h"
#include <iostream>
static int failures;
static void check(bool ok, const char* what) { if (!ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; } }
// How often a key's handler runs during a 3 s hold: the first press, then 90 autorepeat presses, each one reaching it unless the rule takes it.
static int Acts(const Cfg& c, unsigned vk, wkeys::Busy b = {}) { int n = 0; for (int i = 0; i <= 90; ++i) if (!wkeys::RepeatIgnored(c, vk, i > 0, b)) ++n; return n; }
int main() {
    Cfg c;
    for (unsigned vk : {guide::Boost, guide::Nearby, guide::DevMenu, guide::Video}) check(Acts(c, vk) == 1, "a held function key (F3, F4, F8, F9) acts once");
    check(Acts(c, guide::DevMenu, {false, true, false}) == 1, "a held F8 with the menu open closes it once");
    check(Acts(c, guide::MarkerKey(c)) == 1, "a held F2 marks one problem");
    for (unsigned vk : {guide::PrevView, guide::NextView}) check(Acts(c, vk) == 1, "a held [ or ] watches the next player once");
    for (unsigned vk : {c.keyPing, c.keyCanvas, c.keyVoiceChannel}) check(Acts(c, vk) == 1, "a held V pings once, Z flips the canvas once, Y moves one channel");
    { Cfg k = c; k.keyColor = 'C'; check(Acts(k, 'C') == 1, "an ink colour key steps one colour per press"); }
    // hold keys: their handler sees every repeat (it treats it as still held)
    for (unsigned vk : {guide::Help, guide::Roster, c.keyEmote, c.keyDraw, c.keyVoice, c.keyRadio}) check(Acts(c, vk) == 91, "a hold key's repeats reach its handler");
    // keys that type or move: the chat box, the menu and the panel get their repeats
    for (unsigned vk : {c.keyPing, c.keyCanvas, c.keyVoiceChannel, guide::PrevView}) {
        check(Acts(c, vk, {true, false, false}) == 91, "a held letter in the chat box types again");
        check(Acts(c, vk, {false, true, false}) == 91 && Acts(c, vk, {false, false, true}) == 91, "the developer menu and the player panel get their repeats");
    }
    for (unsigned vk : {0x26u, 0x28u, unsigned('W'), 0x0Du}) check(Acts(c, vk) == 91, "game and list keys repeat");
    // the TV keys belong to the game while experimental_tv is off
    check(Acts(c, guide::TvView) == 91 && Acts(c, c.keyTvDeploy) == 91, "TV keys untouched while the TV is off");
    { Cfg t = c; t.experimentalTv = true; check(Acts(t, guide::TvView) == 1 && Acts(t, guide::TvConsent) == 1 && Acts(t, t.keyTvDeploy) == 1 && Acts(t, t.keyTvSeat) == 1, "TV keys act once per press while on"); }
    // the twin switch: every repeat acts again
    { Cfg t = c; t.keyRepeatToggle = true; check(Acts(t, guide::DevMenu) == 91, "key_repeat_toggle=1 restores the old behaviour (the live twin)"); }
    if (failures) { std::cerr << failures << " failures\n"; return 1; }
    std::cout << "key repeat: ALL OK\n"; return 0;
}
