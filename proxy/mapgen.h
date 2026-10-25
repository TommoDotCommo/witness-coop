#pragma once
// A picture of the island for the dev menu's map: the player is flown over the island on a grid at some height, looking straight down, and the game's own frames are rectified onto the
// ground and stitched into one image (witness_map\underlay.bmp next to the DLL, with underlay.txt giving the world rectangle it covers). Nothing of the mod is drawn while this
// runs. The picture is made from the player's own copy of the game and stays on their machine.
#include <dxgi.h>
#include <cstdint>
#include <vector>

bool MapGenActive();                          // frames are being captured: draw nothing of ours
void MapGenCommand(const char* args);         // witness_cmd.txt: mapgen start | stop | status
void MapGenTick(IDXGISwapChain* sc);          // render thread, every frame before the shot is taken
bool MapUnderlayInfo(float* minx, float* miny, float* maxx, float* maxy, int* w, int* h);   // the finished picture, if there is one
const uint8_t* MapUnderlayPixels();           // BGRA, top row first, w * h * 4 bytes (valid after MapUnderlayInfo said yes)
unsigned MapGenSerial();                      // changes whenever a new picture has been made
