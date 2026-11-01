#pragma once
// The in-game Steam transport: messages to and from other players' SteamIDs, relayed by Steam when there is no direct path (no port forwarding).
// It borrows the game's own Steam connection (the game calls SteamAPI_Init when it starts with Steam): no init, no shutdown, no Steam DLL of ours, no app-id override.
// One virtual channel for every packet class (they keep their magics); unreliable and unbuffered: the sync core and the other layers resend what must arrive.
// Membership is the cfg's roster (wpeers::PeerTable): only its SteamIDs are sent to, and coop.cpp drops what any other SteamID sends.
#include <cstddef>
#include <cstdint>

namespace wsteam {
constexpr int kGameChannel = 2;      // the launcher's lobby uses channel 1 (a virtual channel, not a UDP port)
// Waits for the game's Steam API (up to timeoutMs) and gets the networking interface. false: `why` says what is missing in words a player understands.
bool Open(unsigned timeoutMs, uint64_t* self, char* why, size_t cap);
bool Send(uint64_t to, const uint8_t* d, size_t n);
int Receive(uint8_t* buf, size_t cap, uint64_t* from);      // one message (0: none), never more than cap bytes (larger ones are dropped)
int State(uint64_t peer);                                    // ESteamNetworkingConnectionState of the session with that player (steamabi kConn*)
const char* StateName(int s);
void Stat(const uint64_t* peers, const unsigned* ids, size_t n);      // `steamstat` readout to the log (tests and players' logs)
}  // namespace wsteam
