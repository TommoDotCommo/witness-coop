// A fake system dsound.dll for the build-gate test (it sits in the test's stand-in system folder): GetDeviceID and DirectSoundCreate8
// return markers the host can recognise, so the test sees that the proxy forwarded to this file and not to itself.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
extern "C" __declspec(dllexport) long GetDeviceID(const void*, void*) { return 0x5A5A1234; }
extern "C" __declspec(dllexport) long DirectSoundCreate8(const void*, void**, void*) { return 0x5A5A0011; }
