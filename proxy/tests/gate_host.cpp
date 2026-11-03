// Host program for the build-gate test: it is the "game". Its own PE header is not the known build, so the proxy must leave it alone.
//   gate_host fingerprint          print this exe's fingerprint as the C initialiser the test build of the proxy takes (COOP_TEST_SPEC)
//   gate_host run <folder>         load <folder>\dsound.dll the way the game does and print what the proxy did
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <string>
#include "../../common/buildid.h"
int main(int argc, char** argv)
{
    if (argc >= 2 && !strcmp(argv[1], "fingerprint")) {
        const uint8_t* base = (const uint8_t*)GetModuleHandleW(nullptr);
        buildid::Info i = buildid::Inspect(base, buildid::MappedSize(base), true, buildid::Witness());
        printf("0x%xu,0x%xu,0x%xu,0x%xu,\"%s\"\n", i.timestamp, i.imageSize, i.entryRva, i.codeSize, i.textSha256);
        return 0;
    }
    if (argc < 3 || strcmp(argv[1], "run")) return 2;
    std::string dll = std::string(argv[2]) + "\\dsound.dll";
    HMODULE m = LoadLibraryA(dll.c_str());
    printf("loaded=%d\n", m ? 1 : 0);
    if (!m) return 1;
    volatile LONG* gate = (volatile LONG*)GetProcAddress(m, "coop_gate");
    Sleep(1500);            // the proxy's start-up thread runs now
    if (gate) printf("gate=%ld,%ld,%ld,%ld,%ld\n", gate[0], gate[1], gate[2], gate[3], gate[4]);
    typedef long (*Fn)(const void*, void*, void*);
    Fn id = (Fn)GetProcAddress(m, "GetDeviceID");
    printf("GetDeviceID=0x%lx\n", id ? (unsigned long)id(nullptr, nullptr, nullptr) : 0ul);
    Fn c8 = (Fn)GetProcAddress(m, MAKEINTRESOURCEA(11));      // the game imports DirectSoundCreate8 by ordinal 11
    printf("ordinal11=0x%lx\n", c8 ? (unsigned long)c8(nullptr, nullptr, nullptr) : 0ul);
    char chained[8] = "";
    GetEnvironmentVariableA("COOP_CHAIN_LOADED", chained, sizeof chained);
    printf("chain=%s\n", chained[0] ? chained : "0");
    return 0;
}
