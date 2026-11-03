// A fixture for the build-gate test: the co-op mod's old AnselSDK64.dll (v0.1.0-alpha layout), recognised by the text its log line had.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
extern "C" __declspec(dllexport) const char coop_old_marker[] = "real Ansel DLL %s";
extern "C" __declspec(dllexport) unsigned long long getVersion() { return 1; }
