#pragma once
// The POV screens' controls: V watches the player you look at (again: stop), ] and [ cycle through the players (after the last one the screen goes off), F5 opens the
// player list (Up/Down, Enter watches, Backspace clears, F5 or Esc closes). While the list is open it takes the keyboard and the mouse from the game.
bool PovKey(unsigned vk, bool down);      // window thread: true when the key was taken
bool PovRosterOpen();
unsigned OverlayTvAim();
void OverlayPovState(unsigned* aimed, int* rosterOpen);      // test command povstat
