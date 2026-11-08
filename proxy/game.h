#pragma once
#include <cstdint>
// Calls into the game's own functions (build 11370412, RVAs from the module base).
void HooksCursorStat(unsigned* clip, unsigned* setpos);    // hooks.cpp: how often the game tried to cage or move the mouse while its window was not the foreground one
void OverlayPovDrawn(unsigned* box, unsigned* pic);        // overlay.cpp: frames the other player's screen was drawn / drawn with a picture
void OverlaySetLineMirror(bool on);                    // test command linemirror 0|1 (overlay.cpp)
void OverlayLineDump();                                // test command linedump (overlay.cpp)
void GameStart();       // starts the command-file poller (witness_cmd.txt next to the DLL)
void GameRunQueued();   // call from the render thread (Present hook): runs queued commands
void GameEnqueueDoorOpen(unsigned id, float amount);
uintptr_t GameEntity(unsigned id);                    // entity pointer or 0
bool GameDoorOpenNow(unsigned id, float amount);      // render thread only; refuses non-Door entities
bool GamePanelPowerOnNow(unsigned id);                // render thread only; refuses non-panel entities or a wrong prologue
bool GameCablePowerOnNow(unsigned id);                // render thread only; refuses non-cable entities or a wrong prologue
void GameEnqueueCablePower(unsigned id);              // development command: run GameCablePowerOnNow on the render thread
void GameMarkPanelSolved(unsigned id);                // writes the fields a real solve sets (render thread)
bool GameMarkPanelUnsolved(unsigned id);              // writes the fields a timer panel's reset leaves (render thread); false: not a panel
bool GameReactNow(unsigned panelId, bool remote, int* go);      // the game's reactions to a solve of this panel (exe+0xB7F80; go: whether its target is dispatched); false when not run (remote: the skipped behaviours); render thread only
bool GameNativeSolveNow(unsigned panelId, int* reacted);      // TEST ONLY (nativesolve): a solve with the game's per-name reactions, as its own handler; render thread only
bool GameBridgeAdvanceNow(unsigned panelId);       // tells the panel's Bridge (Treehouse) that it is solved, as the game's solve handler does after the dispatch; render thread only
bool GameDispatchTargetNow(unsigned panelId, bool any = false);         // runs the game's target dispatcher (exe+0xC24F0) for the panel's target when it is of a type with no dedicated event; render thread only
bool GameCableOffNow(unsigned id);                  // render thread only; the game's own cable power-off (exe+0x175E50: requested level to 0, the glow fades)
void GameEnqueueCableOff(unsigned id);                // development command
void GameHover(float x, float y, float z, float yaw, float pitch, unsigned seconds, bool firm = false);   // like GameTeleport, but the player is held there (about every ms) for up to `seconds`: for places with no collision; it lets go when the game resets the player (a sideways jump), unless firm (a test held far from any ground)
void GameHoverOff();
void GameTeleport(float x, float y, float z, float yaw, float pitch);   // camera position (eye) and angles, held for a few frames like the tp command
// Render-thread seat hold: uses camera-eye coordinates just like teleport.
unsigned OverlayRosterFlips();     // overlay.cpp: test oracle, player list opened/closed count
unsigned DevMenuToggles();         // devmenu.cpp: test oracle, dev menu opened/closed count
bool GameSeatPose(const float eye[3],float yaw,float pitch);
