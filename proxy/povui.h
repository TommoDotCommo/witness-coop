#pragma once
// The POV screens' controls: V watches the player you look at (again: stop), ] and [ cycle through the players (after the last one the screen goes off), F5 opens the
// player list (Up/Down, Enter watches, Backspace clears, F5 or Esc closes). While the list is open it takes the keyboard and the mouse from the game.
void PovPanelMove(int dx, int dy);        // raw input thread: the player panel's cursor moves
bool PovPanelClick();                     // window thread: a left click; true when the panel is open (it takes the click and does what it points at)
bool PovKey(unsigned vk, bool down);      // window thread: true when the key was taken
bool PovRosterOpen();
void PovPanelStat();                      // log the panel state (test command panelstat)
unsigned OverlayTvAim();
void OverlayPovState(unsigned* aimed, int* rosterOpen);      // test command povstat
bool OverlayTagBars(unsigned id, float* x, float* y, float* w, float* h, float* k);      // render thread, after the name tags of this frame: where a talking player's voice bars go (false: no tag drawn for them)
