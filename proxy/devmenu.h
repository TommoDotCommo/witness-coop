#pragma once
// In-game dev menu (F8): a map to teleport on, with both players shown, a list of the nearby puzzles with what they are connected to
// and actions on them. Drawn over the back buffer by overlay.cpp; while it is open the game gets no keyboard or mouse input.
#include <windows.h>

bool DevMenuEnabled();                        // dev_menu=1 in witness_coop.cfg
bool DevMenuOpen();
void DevMenuToggle();
void DevMenuSetPage(int page);                // 0 map, 1 nearby puzzles; opens the menu
bool DevMenuKey(unsigned vk, bool down);      // window thread: true when the menu took the key
bool DevMenuMouseButton(int button, bool down);   // 0 left, 1 right, 2 middle; true when taken
bool DevMenuWheel(int delta);
void DevMenuRawMove(int dx, int dy);          // raw mouse motion while open (the game is given none)
void DevMenuSetCursor(float x, float y);      // test command: absolute cursor position in back-buffer pixels
void DevMenuDraw(float w, float h);           // render thread, between UiBegin and UiEnd
void DevMenuCommand(const char* what, unsigned id);   // test command channel: "select" (panel id) or an action name (teleport, solve, power, run, door, cableon, cableoff) on the selected panel; runs on the next frame
void DevMenuTick();                           // render thread, every frame: pending teleport check
void DevMenuToast(float w, float h);          // render thread, between UiBegin and UiEnd, when the menu is closed
bool DevMenuToastWanted();                    // a message is still to be shown
