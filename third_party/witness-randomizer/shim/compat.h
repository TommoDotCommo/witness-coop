#pragma once
// witness-coop: force-included before every vendored file (-include compat.h). Upstream builds with MSVC, whose headers bring these in
// indirectly; MinGW's do not.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
// MinGW-w64 with the win32 thread model has no std::this_thread; upstream sleeps only in Archipelago's skip animations, never called here.
#if !defined(_GLIBCXX_HAS_GTHREADS)
#include <windows.h>
namespace std { namespace this_thread {
template <class R, class P> inline void sleep_for(const std::chrono::duration<R, P>& d) { ::Sleep((DWORD)std::chrono::duration_cast<std::chrono::milliseconds>(d).count()); }
} }
#endif
