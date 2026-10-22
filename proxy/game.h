#pragma once
#include <cstdint>
// Calls into the game's own functions (build 11370412, RVAs from the module base).
void GameStart();       // starts the command-file poller (witness_cmd.txt next to the DLL)
void GameRunQueued();   // call from the render thread (Present hook): runs queued commands
void GameEnqueueDoorOpen(unsigned id, float amount);
uintptr_t GameEntity(unsigned id);                    // entity pointer or 0
bool GameDoorOpenNow(unsigned id, float amount);      // render thread only; refuses non-Door entities
