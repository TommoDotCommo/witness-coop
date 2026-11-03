// A fixture dsound.chain.dll for the build-gate test: another mod that was named dsound.dll before the co-op mod came. It records that
// it was loaded (an environment variable of the process, safe under the loader lock) so the host can tell the proxy chained it.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) SetEnvironmentVariableA("COOP_CHAIN_LOADED", "1");
    return TRUE;
}
