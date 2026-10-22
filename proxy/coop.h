#pragma once
#include <windows.h>
#include <cstdint>
struct RemotePose { bool valid; float pos[3]; float ang[2]; DWORD ageMs; unsigned seq; unsigned id; };
bool CoopGetRemote(RemotePose* out);   // latest pose from the peer, with its age
void CoopStart();                      // reads witness_coop.cfg next to the DLL and starts the UDP thread

// Reliable shared-state events (panel solved, door opened): numbered, acknowledged, resent, applied once in order.
enum EvKind : uint8_t { EV_PANEL_SOLVED = 1, EV_DOOR_OPENED = 2 };
void CoopSendEvent(uint8_t kind, uint32_t entity);
bool CoopPopRemoteEvent(uint8_t* kind, uint32_t* entity);   // events received from the peer, in order
bool CoopTakeResyncRequest();   // true once each time the peer (re)appears: send the full current state
unsigned CoopStats(int which);  // 0 sent, 1 resent, 2 applied(received in order), 3 duplicates, 4 out-of-order, 5 acked
// Test-only switches from witness_coop.cfg (all off by default): drop_first_event=1, no_resend=1, no_echo_guard=1
bool CoopTestFlag(int which);   // 0 drop first send of each event, 1 never resend, 2 disable the echo guard
