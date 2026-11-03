#pragma once
// Third-person view while emoting (core/thirdperson.h has the camera; this is the game side). Setting: emote_third_person (on by default).
// The game draws the world from its camera; while the view is out, the mod puts the third-person camera there for the duration of the
// game's world drawing only (exe+0x1C8970, build 11370412) and puts the real one back afterwards, so the game's logic (puzzle aiming,
// the pose the others receive) never sees it.
void ThirdPersonPresentBegin();  // render thread, first thing in Present (the game presents from inside its world drawing): the player's camera goes back
unsigned ThirdPersonGen();       // other threads: read this, then the game's camera position, then call ThirdPersonFixEye
void ThirdPersonFixEye(unsigned genBefore, float pos[3]);   // the position becomes the player's real eye if a third-person frame was drawn meanwhile
void ThirdPersonFrame(float aspect);   // render thread, once per Present, before the world overlays are drawn (aspect: the window's width / height)
bool ThirdPersonViewCam(float pos[3], float ang[2]);   // the camera this frame's world was drawn from, when it was not the player's own
bool ThirdPersonSelf(float pos[3], float* yaw, int* emote, float* seconds);   // this player's own figure is to be drawn (the third-person view is out)
void ThirdPersonSelfDrawn(bool drawn);   // overlay.cpp: the figure was submitted this frame
void ThirdPersonClick();        // a mouse button went down (raw input): the view returns
bool ThirdPersonCommand(const char* line);   // witness_cmd.txt: tpstate (writes witness_tp.json) | tpforce on|off|cfg (test override of the setting) | tpwatch <rva>|dump|0 (developer: who writes there)
