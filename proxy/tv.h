#pragma once
#include <d3d11.h>
#include <cstdint>
#include <cstddef>
// Snapshot TV is independent of the legacy partner inset. Both settings default off.
bool TvEnabled(); bool TvConsent();
void TvEnable(bool on); void TvAllow(bool on);
void TvTick(const float camera[3], bool captureAllowed); // render thread, select nearest live player within 12 m
bool TvWanted(); // capture needed by a consenting sender with live subscribers
void TvCapture(const uint8_t* rgba, unsigned w, unsigned h); // encoder thread, newest snapshot only
void TvOnPacket(const uint8_t* p,size_t n); // network thread, source endpoint already validated
unsigned TvTarget();
ID3D11ShaderResourceView* TvTexture(ID3D11Device*,ID3D11DeviceContext*,bool* live); // render thread
void TvDrawn(unsigned vertices,bool live,float sway,float power); // actual draw bridge
void TvVisible(unsigned pixels);
void TvDump(); // render thread: metadata only, never image bytes
