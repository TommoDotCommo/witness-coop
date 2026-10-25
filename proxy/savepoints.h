#pragma once
// Save points and presets: copies of the game's own save file, kept next to the DLL, restored by loading through the game's pause menu
// (keys injected into its input pump, the same way the QA reload test does it).
//   savepoints\<name>.witness_campaign  (+ .png and .txt)   made by "Save point now"
//   presets\<name>.witness_campaign                          kept for starting from a given amount of progress
#include <string>
#include <vector>

struct SaveEntry { std::string name; bool preset; bool hasGameSave; std::string note; unsigned long long when; };

// The game's save keeps only progress made through its own solve code (it stores each traced solution line), so progress made by the dev
// tools (a forced solve has no line) is kept as a list of solved panels and put back by the mod after a load. The menu supplies both ends.
void SaveSetHooks(std::string (*write)(bool forcedOnly), void (*apply)(const std::string& stateText));   // write: the progress as text (forcedOnly: just what the dev tools solved); apply: solve what the text lists
std::wstring SaveFolder();                            // the game's save folder (redirected in an instance): <save_dir>\\The Witness
void SaveListRefresh();                               // re-read the two folders
const std::vector<SaveEntry>& SaveList();
bool SaveBusy();                                      // a save or restore is running (a few seconds)
void SaveStartSave(const std::string& name, bool asPreset, const std::string& note);   // open the pause menu (the game saves), copy the newest save, close it
void SaveStartRestore(const SaveEntry& e);            // with a game save: copy it in and Load a Game from the pause menu; then (or at once, for a list-only preset) apply the solved-panel list
void SaveStartPreset(const std::string& name, const std::string& note);   // list-only preset of the current progress, no game save
void SaveTick(unsigned long nowMs);                   // render thread, every frame
const char* SaveStatus();                             // one line for the menu
