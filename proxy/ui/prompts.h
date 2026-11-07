#pragma once
// The short on-screen prompts of the social layer, one style for all: "key: what it does". Pure text, so prompts_test.cpp checks that
// each names the key that really does it (2026-11-06 sweep: the canvas said "B colour" after the ink colour had moved to the mouse wheel;
// the "[V] watch" prompt over an aimed player and the corner reminder to watch were removed at the owner's request). Keys come in as the player reads them (guide::DisplayKey).
#include <string>
namespace wprompt {
// Under the drawing canvas.
inline std::string Canvas(const std::string& draw, const std::string& colour, const std::string& canvas) {
#ifdef PR_MUT_OLD
    (void)draw; (void)colour; (void)canvas; return "canvas: move the mouse, hold X to draw, B colour, Backspace undo, Shift+X clear all, Z leaves";
#endif
    return "Move the mouse   Hold " + draw + ": draw   " + colour + ": colour   Backspace: undo   Shift+" + draw + ": clear   " + canvas + ": close";
}
inline std::string Seat(const std::string& key, bool seated) { return key + (seated ? ": stand up (or just move)" : ": sit and watch"); }
inline std::string SkyTv(const std::string& key, const std::string& name, bool deployed) { return key + (deployed ? ": take down " : ": put up ") + name + "'s sky TV"; }
}  // namespace wprompt
