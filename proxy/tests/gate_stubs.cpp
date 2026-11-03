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
// the diagnostics (proxy/diag.cpp) start only after the gate passes; here they do nothing
extern "C" __declspec(dllexport) volatile LONG coop_diaglines;      // session-log lines (a passive copy must write none)
volatile LONG coop_diaglines;
void DiagLine(const char*) { InterlockedIncrement(&coop_diaglines); }
void DiagStart() {}
void DiagStop() {}
struct _EXCEPTION_POINTERS; void DiagFirstChance(struct _EXCEPTION_POINTERS*) {}
