// Stands in for everything the proxy starts once the build is known (tools/test_build_gate.sh): each entry only counts its calls,
// so the test can tell "hooked / started" from "left alone" without a game. Exported so the host program can read the counters.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
extern "C" __declspec(dllexport) volatile LONG coop_gate[5];
volatile LONG coop_gate[5];
void InstallHooks() { InterlockedIncrement(&coop_gate[0]); }
void OverlayInstall() { InterlockedIncrement(&coop_gate[1]); }
void CoopStart() { InterlockedIncrement(&coop_gate[2]); }
void GameStart() { InterlockedIncrement(&coop_gate[3]); }
void StateStart() { InterlockedIncrement(&coop_gate[4]); }
