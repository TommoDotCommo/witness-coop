#pragma once
// Force-included before every vendored randomizer file (and its variant's glue). Every header a vendored file may include is included here first,
// outside any namespace: the same #include inside the variant's namespace is then a no-op (include guards), so only upstream's own declarations
// land in the namespace. The shims (third_party/witness-randomizer/shim) are global and shared by every variant.
#include "compat.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <cassert>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <stack>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "backend.h"
#include "Memory.h"
#include "ClientWindow.h"
#include "Utilities.h"
#include "variant.h"
// Upstream's window: a message box (Expert's welcome) would stop the game's thread and wait for a click; the status text goes to the mod.
#undef MessageBox
#define MessageBox(...) 0
#undef SetWindowText
#define SetWindowText(h, t) ::wrand::StatusW(t)
// MSVC's windows.h defines min and max as macros (MinGW's does in C only); the older forks call them bare, and a macro evaluates its winner twice,
// which matters when an argument draws a random number: for those variants they are the same macros (variants.mk, <v>_FLAGS).
#ifdef WRAND_MSVC_MINMAX
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#endif
