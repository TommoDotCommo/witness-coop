#pragma once
// The co-op pad layer in the game (standard K4-K7; the model is common/padlayer.h). The game reads its pad through XInputGetState, which it looks up
// by name (GetProcAddress, hooks.cpp): it gets PadGameGetState instead, which answers with the pad minus what the layer owns. The proxy reads the
// same pad once per frame on the render thread (PadFrame) and turns View and its chords into the co-op actions (ping, player list, wheels, chat, watch).
// A test can stand in for a pad (the command `pad <buttons hex> [lx ly rx ry lt rt]`, `pad off`), on the same path as a real one; `padstat` writes witness_pad.json.
#include <windows.h>
void* PadHookGetState(void* real);     // hooks.cpp, when the game resolves XInputGetState: returns what the game should call
void PadFrame();                       // render thread, once per frame (the Present hook), before the UI is drawn
void PadNoteKeyboard();                // a key or a mouse button: the hints show keys again (K7)
bool PadGlyphs();                      // the hints show pad buttons (the last input was a pad)
int VoiceModeNow();                    // the voice mode in force: 0 off, 1 open mic, 2 push to talk (voice_mode=auto follows the last input)
const char* PadVoiceButtonName();      // "LS" / "RS" when a stick click is push to talk, else ""
void PadOskCursor(int* row, int* col);  // where our on-screen keyboard's cursor is (social.cpp draws it)
void PadCommand(const char* line);     // witness_cmd.txt: pad <hex> [lx ly rx ry lt rt] | pad off | padstat
