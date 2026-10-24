#pragma once
// Finds the game's scene depth buffer by watching which depth-stencil view it binds, and keeps a readable copy for the overlay.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
void DepthInstall(ID3D11DeviceContext* ctx, ID3D11Device* dev, UINT bbW, UINT bbH);   // patches the context's shared vtable (once)
void DepthDiagFrame(unsigned frame);                                                   // debug_depth=1: logs what the game did with depth this frame
bool DepthScene(ID3D11ShaderResourceView** srv, bool* multisampled);   // a view of the game's scene depth buffer (valid for the frame); false: not found yet
