#pragma once
#include <cstdint>
// Calls into the game's own functions (build 11370412, RVAs from the module base).
void GameStart();       // starts the command-file poller (witness_cmd.txt next to the DLL)
void GameRunQueued();   // call from the render thread (Present hook): runs queued commands
void GameEnqueueDoorOpen(unsigned id, float amount);
uintptr_t GameEntity(unsigned id);                    // entity pointer or 0
bool GameDoorOpenNow(unsigned id, float amount);      // render thread only; refuses non-Door entities
bool GamePanelPowerOnNow(unsigned id);                // render thread only; refuses non-panel entities or a wrong prologue
bool GameCablePowerOnNow(unsigned id);                // render thread only; refuses non-cable entities or a wrong prologue
void GameEnqueueCablePower(unsigned id);              // development command: run GameCablePowerOnNow on the render thread
void GameMarkPanelSolved(unsigned id);                // writes the fields a real solve sets (render thread)
bool GameDispatchTargetNow(unsigned panelId);         // runs the game's target dispatcher (exe+0xC24F0) for the panel's target when it is of a type with no dedicated event; render thread only
