#pragma once
// A held key sends WM_KEYDOWN again about 30 times a second (bit 30 of lParam set: the key was already down). The mod's press keys act once per press
// (H3: toggles are single presses), so their autorepeat is taken and does nothing: before this, a held F8 opened and closed the developer menu 30 times
// a second, a held Z flipped the drawing canvas, a held V pinged on every repeat. Hold keys (F1 guide, Tab player list, H emote wheel, X draw, B push to
// talk, R radio) are not listed: they act on the press and the release, and their handlers already treat a repeat as "still held". The keys that type text
// or move in a list (letters and arrows in the chat box, the developer menu, the hold-Tab panel) still repeat there.
// Game-free: proxy/ui/key_repeat_test.cpp (make -C proxy/ui test), mutants WK_MUT_NO_IGNORE, WK_MUT_WHILE_TYPING.
#include "../../common/guide.h"
namespace wkeys {
struct Busy { bool typing = false, menu = false, panel = false; };      // the chat box is open / the developer menu is open / the hold-Tab panel is open
inline bool RepeatIgnored(const Cfg& c, unsigned vk, bool repeat, Busy b = {}) {
#ifdef WK_MUT_NO_IGNORE
    return false;      // twin: every autorepeat press acts again (the behaviour before)
#endif
    if (!repeat || c.keyRepeatToggle) return false;
    const guide::TvKeyAction tv = guide::TvKey(c, vk);
    // function keys act on a press whatever is open
    if (vk == guide::Boost || vk == guide::Nearby || vk == guide::DevMenu || vk == guide::Video || (vk == guide::MarkerKey(c) && vk) ||
        tv == guide::TvKeyAction::View || tv == guide::TvKeyAction::Consent) return true;
#ifndef WK_MUT_WHILE_TYPING
    if (b.typing || b.menu || b.panel) return false;      // the box, the menu or the panel takes these keys: a held letter types again, a held arrow moves again
#endif
    if (vk == guide::PrevView || vk == guide::NextView || tv == guide::TvKeyAction::Deploy || tv == guide::TvKeyAction::Seat) return true;
    if (vk == c.keyPing || vk == c.keyCanvas || vk == c.keyVoiceChannel || (vk == c.keyColor && c.keyColor != guide::Wheel)) return true;
    return false;
}
}
