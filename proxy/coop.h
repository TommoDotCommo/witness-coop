#pragma once
#include <windows.h>
#include <cstdint>
#include "core/sync_core.h"
struct RemotePose { bool valid; float pos[3]; float ang[2]; DWORD ageMs; unsigned seq; unsigned id; };
bool CoopGetRemote(RemotePose* out);        // latest pose from the peer, with its age
void CoopStart();                           // reads witness_coop.cfg next to the DLL and starts the UDP thread
witness::SyncCore* CoopCore();              // the sync core; null until the config is read and the socket is bound
witness::World* GameWorldInstance();        // state.cpp: the core's view of the game's memory
