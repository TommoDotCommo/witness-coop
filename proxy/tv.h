#pragma once
#include <d3d11.h>
#include <cstdint>
#include <cstddef>
#include <vector>
#include "tv/tv.h"
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
bool TvDeployAction(unsigned target); // aimed remote player; a second press retracts this player's screen
// While the ground under a new screen is measured the player is dropped onto each point: the picture is held (overlay.cpp) and the
// others are sent the pose the player had before (state.cpp), so nobody sees the jumps.
bool TvProbing(); bool TvProbeHome(float pos[3], float ang[2]);
unsigned TvDeploymentTarget();
std::vector<wtv::Deployment> TvAudienceScreens();
ID3D11ShaderResourceView* TvAudienceTexture(ID3D11Device*,ID3D11DeviceContext*,unsigned source,bool* live);
void TvAudienceDrawn(unsigned owner,unsigned vertices,bool live);
void TvAudienceVisible(unsigned owner,unsigned pixels);
void TvDump(); // render thread: metadata only, never image bytes
// Input queues intent only; all seat movement is applied by TvTick on Present.
void TvSeatRequest(); void TvSeatExit(); bool TvSeated(); bool TvSeatAvailable();
bool TvSeatedPose(const float eye[3]); // identify a Sit pose held at an audience stool
