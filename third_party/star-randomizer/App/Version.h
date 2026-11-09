#pragma once

#define TO_STRING2(s) L ## #s // witness-coop: L#s is MSVC only
#define TO_STRING(s) TO_STRING2(s)

#define MAJOR 1
#define MINOR 0
#define PATCH 1

#define VERSION_STR     TO_STRING(MAJOR) L"." TO_STRING(MINOR) L"." TO_STRING(PATCH)
#define VERSION			MAJOR, MINOR, PATCH

#define PRODUCT_NAME L"Witness Random Puzzle Generator"
#define WINDOW_CLASS L"WitnessRandomizer"
