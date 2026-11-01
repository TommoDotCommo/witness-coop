#pragma once
// Diagnostics (diag.cpp): per-session logs, the crash handler with its dump, the flight recorder and the marker key.
#include <cstddef>
#include <cstdint>
#include <string>
void DiagLine(const char* text);                 // every Log() line also goes to this run's session log (thread-safe, opens it on first use)
void DiagStart();                                // the init thread: crash handler, dbghelp, the version line
void DiagStop();                                 // the process is ending normally: "session end: clean exit"
void DiagFrame(const float* cam, unsigned frames); // the render thread, every Present: the 10 Hz sampler, a pending marker, the unhandled-exception filter check
void DiagMarkKey();                              // the marker key (F2) was pressed here; any thread
bool DiagChat(unsigned from, const std::string& text);   // true: this chat line was another player's marker, not for the chat box
void DiagRxPose(const uint8_t* data, size_t n);  // the network thread: a datagram arrived (link loss is counted from pose sequence numbers)
void DiagCoreLog(const char* text);              // the network thread: a line of the sync core; the ones that mean the worlds differ dump the flight recorder
bool DiagCommand(const char* line);              // witness_cmd.txt: diagmark | diagdesync | testcrash | testhandled; true when it was one of these
const std::wstring& DiagDir();                   // where this run's logs are ("" when logging failed)
struct _EXCEPTION_POINTERS; void DiagFirstChance(struct _EXCEPTION_POINTERS* ep);   // the vectored handler: only the test mutant diag_first_chance=1 writes anything from here
