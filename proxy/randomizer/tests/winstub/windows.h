#pragma once
// Game-free tests build the vendored randomizer natively (g++ on Linux): the few Windows names it uses.
#include <cstddef>
#include <cstdint>
typedef unsigned char byte;
typedef int64_t INT64;
typedef const void* LPCVOID;
typedef void* LPVOID;
typedef size_t SIZE_T;
typedef unsigned long DWORD;
inline void OutputDebugStringW(const wchar_t*) {}
inline void Sleep(DWORD) {}
