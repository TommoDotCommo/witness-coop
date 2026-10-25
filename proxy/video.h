#pragma once
// Partner view: each game sends a small picture of its own screen to the other (about 5 per second, JPEG, in UDP chunks that are
// simply dropped when one is lost), and shows the latest picture it received in an inset. This is how one player sees what the
// other is looking at or solving.
#include <d3d11.h>
#include <dxgi.h>
#include <cstdint>
#include <cstddef>
#include <vector>

bool VideoEnabled();                                         // on by default; partner_view=0 turns it off (dev_menu=1 keeps it on)
void VideoFrame(IDXGISwapChain* sc, ID3D11Device* dev, ID3D11DeviceContext* ctx);   // render thread, every frame: capture schedule
void VideoOnPacket(const uint8_t* data, size_t n);           // network thread: one chunk from the peer
ID3D11ShaderResourceView* VideoRemoteSrv(int* w, int* h, unsigned long* ageMs);   // render thread: the latest picture from the peer (null until one arrived)
void VideoToggleView();                                      // F9: show the first player's view, large, off
unsigned VideoWatching();                                    // the player whose screen is shown (0: none)
void VideoWatch(unsigned id); void VideoClear(); bool VideoToggleWatch(unsigned id);
unsigned VideoNext(const std::vector<unsigned>& players); unsigned VideoPrev(const std::vector<unsigned>& players);     // cycle in id order; after the last, off
void VideoUpdatePlayers(const std::vector<unsigned>& players);     // the ids in the game: a watched player who left is dropped
void VideoPovStats(unsigned* watching, unsigned* subscribers, unsigned* requestsSent);
void VideoOnRequest(const uint8_t* data, size_t n);          // network thread: a request for this game's pictures
int VideoViewMode();                                         // 0 off, 1 small, 2 large
void VideoSavePartnerShot(const char* name);                 // test command: the latest received picture to shots\<name>.bmp
void VideoStats(unsigned* sent, unsigned* received, unsigned* decoded);
void VideoSetMode(int mode);                         // test command
