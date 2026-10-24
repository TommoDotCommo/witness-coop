#pragma once
#include <windows.h>
#include <cstdint>
#include "core/sync_core.h"
struct RemotePose { bool valid; float pos[3]; float ang[2]; DWORD ageMs; unsigned seq; unsigned id; };
bool CoopGetRemote(RemotePose* out);        // latest pose from the peer, with its age
void CoopStart();                           // reads witness_coop.cfg next to the DLL and starts the UDP thread
witness::SyncCore* CoopCore();              // the sync core; null until the config is read and the socket is bound
witness::World* GameWorldInstance();        // state.cpp: the core's view of the game's memory
void CoopSendRaw(const uint8_t* data, size_t n);   // any thread: one datagram to the peer, as is (partner view pictures)
void CoopSetFakeRemote(bool on, const float* pos, float yaw);   // test hook: show the peer at this eye position instead of where it is (witness_cmd.txt: fakepose x y z yaw | fakepose off)
