#pragma once
#include <windows.h>
// Synthetic input for the game, without the OS focus. The game reads the keyboard and mouse through WM_INPUT / GetRawInputData,
// which Windows only delivers to the focused window. We post our own WM_INPUT to the game window with a private handle and
// answer the game's GetRawInputData call for that handle with the event we queued.
void InputInstallHook();                       // from InstallHooks (DllMain): patches the exe's GetRawInputData import
void InputKey(unsigned vk, unsigned scan, bool down);
void InputMouse(int dx, int dy, unsigned buttonFlags);      // RI_MOUSE_* flags, 0 for a pure move
void InputPump(HWND wnd);                      // render thread, once per frame: post the queued events to the game window
unsigned InputStats(int which);                // 0 posted, 1 answered, 2 hook calls for real handles, 3 hook calls for our handles
