#pragma once
// Immediate-mode 2D drawing for the in-game dev menu: filled rectangles, lines and text (a glyph atlas drawn with GDI at start-up),
// batched into one dynamic vertex buffer and drawn over the game's back buffer at the end of the frame.
#include <cstdint>
#include <d3d11.h>
#include "ui/theme.h"

bool UiInit(ID3D11Device* dev, ID3D11DeviceContext* ctx);                       // false: the menu stays off
void UiBegin(float width, float height);                                         // start a frame, in back-buffer pixels
void UiRect(float x, float y, float w, float h, UiColor c);
void UiFrame(float x, float y, float w, float h, UiColor c, float t = 1);       // outline
void UiLine(float x0, float y0, float x1, float y1, UiColor c, float t = 1);
void UiTri(float cx, float cy, float size, float angle, UiColor c);              // a pointer: tip at angle (radians, screen space), for player markers
void UiText(float x, float y, const char* s, UiColor c, float scale = 1);        // top-left origin; '\n' starts a new line
float UiTextWidth(const char* s, float scale = 1);
float UiLineHeight(float scale = 1);
void UiDumpAtlas(const char* name);                                              // test command: the glyph atlas as a bitmap
void UiEnd(ID3D11RenderTargetView* rtv);                                         // draw everything queued
ID3D11ShaderResourceView* UiMakeTexture(const uint8_t* bgra, int w, int h);     // an RGBA picture as a texture (the caller keeps it; null on failure)
void UiClip(float x, float y, float w, float h);                                 // scissor for the following quads (flushes the batch); w<=0 clears
void UiImage(float x, float y, float w, float h, ID3D11ShaderResourceView* srv, UiColor tint = {1, 1, 1, 1});   // a whole RGBA texture, stretched into the rectangle

void UiLogStats(); // test oracle: previous frame text bounds and smallest font

void UiCheckTarget(float width, float height, float scale); // accessible target-size oracle
