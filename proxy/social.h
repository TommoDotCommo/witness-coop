#pragma once
// Social features on top of the sync core's chat / ping / presence layer: the chat box (T opens it, Enter sends, Esc cancels), the chat log, bubbles above the players (the last
// message for a few seconds, three dots while someone is typing), and location pings (P marks the point the cross-hair is on; everybody sees a marker and, off screen, an arrow).
#include <vector>
#include <cstddef>
struct SocialSnapshot { int emote; unsigned sequence; int page; bool wheel, guide, paused; };
SocialSnapshot SocialState(); // process-state bridge, no log parsing
// Keys are configurable (key_chat=, key_ping=, ... in witness_coop.cfg).
bool SocialKey(unsigned vk, bool down);       // window thread (the game's message loop): true when the key was taken
bool SocialSwallowRaw(unsigned vk);           // raw input thread: the mod took this key's press a moment ago, the game must not see it
bool SocialWheel(int delta);                      // window thread: a wheel step (ink colour while drawing); true when taken
bool SocialBusy();                                // the chat box or the emote wheel is open
bool SocialChar(unsigned ch);                 // window thread: a WM_CHAR; true when the chat box took it
bool SocialCapturing();                       // the chat box is open: it has the keyboard, and the mouse look is frozen
bool SocialMouseCaptured();                   // that, or the emote wheel is open: the mouse look is frozen (the wheel takes the motion)
void SocialRawMove(int dx, int dy);           // raw mouse motion while the wheel is open (the game gets none)
bool SocialEmoteOf(unsigned playerId, int* emote, float* seconds);   // the emote a remote player is playing now (1..24) and for how long
#include "core/appearance.h"
witness::Appearance SocialMyAppearance();       // this player's own look (the third-person view draws it)
bool SocialMyEmote(int* emote, float* seconds, unsigned* sequence);   // this player's own emote now (1..24) and for how long; sequence: counts every emote started
const char* SocialEmoteName(int emote);
void SocialFrame(float W, float H, const float* cam, const float* ang);   // render thread, inside the UI frame (after the name tags)
void SocialCommand(const char* line);         // witness_cmd.txt: chat <text> | typing on|off | ping | pingat x y z | typetext <text> | socialstat
struct LineVert;
unsigned SocialInkVerts(const float cam[3], std::vector<LineVert>& out, size_t maxVerts);   // render thread: the ink as ribbons for the line pipeline; returns the number of segments
unsigned SocialSpeakDrawn();                  // speaker marks drawn in the last frame (tests)
bool SocialMicHudDrawn();
// the pad layer (pad.cpp, render thread)
void SocialPadPing();                         // tap View: ping what the crosshair is on
bool SocialPadChat(bool steamKeyboard);       // View + D-pad down: open the chat line (false: it could not open)
void SocialPadUseSteamKeyboard();             // Steam's floating keyboard is up for it (not ours)
bool SocialOskOpen();                         // the chat line is open with our on-screen keyboard
bool SocialPadChatOpen();                     // the chat line was opened from the pad (either keyboard)
bool SocialInputEmpty();
void SocialOskKey(int key, char ch);          // padlayer::OskKey: a character, a space, delete, send, close
void SocialPadWheel(int kind, bool open);     // 1 the emote wheel, 2 the quick chat wheel: open / close (closing chooses)
void SocialPadAim(float x, float y);          // the right stick on an open wheel (-1..1, y down; 0 0: keep the last aim)
void SocialPadEmotePage();
bool VoiceKey(unsigned vk, bool down);        // voice_audio.cpp: the push-to-talk key (true: taken)
bool VoiceCycleChannel();                     // voice_audio.cpp: the next voice channel (proximity, radio, global, direct to each player); true: taken
void VoiceSetChannel(const char* text);       // test command voicechannel <proximity|radio|global|direct:N>
void VoiceChannelText(char* out, size_t n);   // "Proximity", "Radio", "Open line", "Private: <name>"
bool VoiceProximitySpeaking();              // activity-qualified proximity speech only
bool VoiceSpeaking();                         // this player's microphone is transmitting
int VoiceMicLevel();                          // 0..1000, the latest microphone peak
void VoiceStat();                             // log the voice state (test command voicestat)
bool GamePaused();                            // overlay.cpp: the game's pause menu is open

bool VoiceRadioKey(bool down, bool blocked);
bool VoiceRadioTx();
unsigned VoiceChannel();
void VoicePlayerVolume(unsigned id, float volume);
int VoiceGlobalVolume(); void VoiceSetGlobalVolume(int percent);      // everybody's voices, percent (the player panel; starts at voice_volume)

float VoiceGetPlayerVolume(unsigned id);
void SocialKeyStats(unsigned* canvasFlips, unsigned* pingKeys);      // test oracle (command keystat)
