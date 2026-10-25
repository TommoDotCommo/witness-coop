#pragma once
// Social features on top of the sync core's chat / ping / presence layer: the chat box (T opens it, Enter sends, Esc cancels), the chat log, bubbles above the players (the last
// message for a few seconds, three dots while someone is typing), and location pings (P marks the point the cross-hair is on; everybody sees a marker and, off screen, an arrow).
// Keys are configurable (key_chat=, key_ping=, ... in witness_coop.cfg).
bool SocialKey(unsigned vk, bool down);       // window thread (the game's message loop): true when the key was taken
bool SocialChar(unsigned ch);                 // window thread: a WM_CHAR; true when the chat box took it
bool SocialCapturing();                       // the chat box is open: it has the keyboard and the mouse look is frozen
void SocialFrame(float W, float H, const float* cam, const float* ang);   // render thread, inside the UI frame (after the name tags)
void SocialCommand(const char* line);         // witness_cmd.txt: chat <text> | typing on|off | ping | pingat x y z | typetext <text> | socialstat
bool GamePaused();                            // overlay.cpp: the game's pause menu is open
