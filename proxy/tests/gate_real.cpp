// A fake "real" Ansel DLL for the build-gate test: getVersion returns a marker the host can recognise.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
extern "C" __declspec(dllexport) unsigned long long getVersion() { return 0x5A5A1234ull; }
