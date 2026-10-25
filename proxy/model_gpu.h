#pragma once
// Draws the remote player as a skinned glTF model inside the game's frame (called from the Present hook in overlay.cpp).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include "model/model.h"

struct ModelDrawStats { unsigned visiblePixels; unsigned frames; unsigned submittedPlayers; int clip; float speed; float animTime; int from; float blend; float eye; float yaw; bool drawn; float pos[3], cam[3]; };
bool ModelGpuInit(ID3D11Device* dev, ID3D11DeviceContext* ctx);      // loads the configured model; false: use the flat puppet instead
bool ModelGpuReady();
// Draws the player at `pos` (eye position), heading `yaw`. `valid` false: the peer is gone, nothing is drawn.
// sceneDepth: a shader resource view of a copy of the game's depth buffer, or null (nothing hides the model).
bool ModelGpuDraw(ID3D11RenderTargetView* rtv, UINT w, UINT h, const wm::Cam& cam, bool valid, unsigned seq, const float pos[3], float yaw, unsigned playerId = 0, bool first = true);
// Coloured ribbons in world space (the other players' drawn lines), depth-tested against the game's scene. Triangle list; each vertex: position, rgba.
struct LineVert { float p[3]; float c[4]; };
struct LineDrawStats { unsigned lines, verts, visiblePixels, frames; };
bool ModelGpuDrawLines(ID3D11RenderTargetView* rtv, UINT w, UINT h, const wm::Cam& cam, const LineVert* verts, size_t count, unsigned lineCount);
LineDrawStats ModelGpuLineStats();
void ModelGpuLogLineStats();          // witness_cmd.txt: linestat -> one log line
ModelDrawStats ModelGpuStats();
void ModelGpuDumpPlayers(); // render-thread test bridge: submitted ids, positions and gait speeds
void ModelGpuRequestCenterDepth();     // the next frame measures the distance of the geometry under the middle of the screen
bool ModelGpuTakeCenterDepth(float* dist);   // true once the answer is there: the distance in metres along the view direction, or -1 (sky / no depth buffer)
void ModelGpuRequestProbe();      // the next frame logs the scene depth at a few pixels (witness_cmd.txt: probe)
void ModelGpuLogStats();               // witness_cmd.txt: modelstat -> one log line: pixels of the model that survived the depth test (GPU occlusion query), clip, speed
void ModelGpuDepthView(bool on);          // witness_cmd.txt: depthview on|off (diagnostic: draw the game's depth buffer instead of the model)
void ModelGpuSelect(int delta);        // F6 (-1) / F7 (+1): next model; takes effect at the next frame
void ModelGpuSelectName(const char* name);   // witness_cmd.txt: model <name>
