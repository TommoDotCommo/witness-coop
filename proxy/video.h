#pragma once
// Partner view: each game sends a small picture of its own screen to the other (about 5 per second, JPEG, in UDP chunks that are
// simply dropped when one is lost), and shows the latest picture it received in an inset. This is how one player sees what the
// other is looking at or solving.
#include <d3d11.h>
#include <dxgi.h>
#include <cstdint>
#include <cstddef>

bool VideoEnabled();                                         // partner_view=1 (or dev_menu=1) in witness_coop.cfg
void VideoFrame(IDXGISwapChain* sc, ID3D11Device* dev, ID3D11DeviceContext* ctx);   // render thread, every frame: capture schedule
void VideoOnPacket(const uint8_t* data, size_t n);           // network thread: one chunk from the peer
ID3D11ShaderResourceView* VideoRemoteSrv(int* w, int* h, unsigned long* ageMs);   // render thread: the latest picture from the peer (null until one arrived)
void VideoToggleView();                                      // F9: off, small, large
int VideoViewMode();                                         // 0 off, 1 small, 2 large
void VideoSavePartnerShot(const char* name);                 // test command: the latest received picture to shots\<name>.bmp
void VideoStats(unsigned* sent, unsigned* received, unsigned* decoded);
void VideoSetMode(int mode);                         // test command
