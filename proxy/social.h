#pragma once
// Social features on top of the sync core's chat / ping / presence layer: the chat box (T opens it, Enter sends, Esc cancels), the chat log, bubbles above the players (the last
// message for a few seconds, three dots while someone is typing), and location pings (P marks the point the cross-hair is on; everybody sees a marker and, off screen, an arrow).
#include <vector>
#include <cstddef>
// Keys are configurable (key_chat=, key_ping=, ... in witness_coop.cfg).
bool SocialKey(unsigned vk, bool down);       // window thread (the game's message loop): true when the key was taken
bool SocialSwallowRaw(unsigned vk);           // raw input thread: the mod took this key's press a moment ago, the game must not see it
bool SocialChar(unsigned ch);                 // window thread: a WM_CHAR; true when the chat box took it
bool SocialCapturing();                       // the chat box or the character list is open: it has the keyboard, and the mouse look is frozen
bool SocialMouseCaptured();                   // that, or the emote wheel is open: the mouse look is frozen (the wheel takes the motion)
void SocialRawMove(int dx, int dy);           // raw mouse motion while the wheel is open (the game gets none)
bool SocialEmoteOf(unsigned playerId, int* emote, float* seconds);   // the emote a remote player is playing now (1..8) and for how long
const char* SocialEmoteName(int emote);
void SocialFrame(float W, float H, const float* cam, const float* ang);   // render thread, inside the UI frame (after the name tags)
void SocialCommand(const char* line);         // witness_cmd.txt: chat <text> | typing on|off | ping | pingat x y z | typetext <text> | socialstat
struct LineVert;
unsigned SocialInkVerts(const float cam[3], std::vector<LineVert>& out, size_t maxVerts);   // render thread: the ink as ribbons for the line pipeline; returns the number of segments
unsigned SocialSpeakDrawn();                  // speaker marks drawn in the last frame (tests)
bool SocialMicHudDrawn();
bool VoiceKey(unsigned vk, bool down);        // voice_audio.cpp: the push-to-talk key (true: taken)
bool VoiceSpeaking();                         // this player's microphone is transmitting
int VoiceMicLevel();                          // 0..1000, the latest microphone peak
void VoiceStat();                             // log the voice state (test command voicestat)
const char* SocialMyCharacter();               // the character this player announced (empty: the default)
bool GamePaused();                            // overlay.cpp: the game's pause menu is open
