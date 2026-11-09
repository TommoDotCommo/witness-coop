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
typedef void* HWND;
typedef void* HANDLE;
typedef const wchar_t* LPCWSTR;
typedef wchar_t WCHAR;
typedef int BOOL;
inline void OutputDebugStringW(const wchar_t*) {}
inline void Sleep(DWORD) {}
#include <cstdlib>
// MSVC's secure CRT (the older forks' debug output)
inline int mbstowcs_s(size_t* ret, wchar_t* dst, size_t n, const char* src, size_t count) { size_t r = dst ? std::mbstowcs(dst, src, n ? n - 1 : 0) : std::mbstowcs(nullptr, src, count); if (dst && n) dst[r < n ? r : n - 1] = 0; if (ret) *ret = r + 1; return 0; }
