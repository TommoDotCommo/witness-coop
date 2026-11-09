#pragma once
// The launcher's puzzle editor (launcher/editor.cpp): a window of its own, opened from the Randomizers tab ("Your puzzles"). The player's puzzle
// sets live in <data folder>/puzzles/sets/<name>.txt (the editor's library, wpz::Print text); the copy a game plays is
// <data folder>/puzzles/<hash>.txt, which never changes (PublishSet writes it when a game is started, the lobby stores the host's there).
#include <windows.h>
#include <string>
#include <vector>
#include "../common/wpuzzle.h"

namespace ped {

std::wstring PuzzlesDir(const std::wstring& dataDir);                    // <data>\puzzles\ (made when missing)
std::vector<std::string> ListSets(const std::wstring& dataDir);          // the library's set names, sorted
bool LoadSet(const std::wstring& dataDir, const std::string& name, wpz::Set* out, std::string* err = nullptr);
bool SaveSet(const std::wstring& dataDir, const std::string& name, const wpz::Set& set);
std::string SafeName(const std::string& name);                           // a set name usable as a file name
std::string PublishSet(const std::wstring& dataDir, const wpz::Set& set); // writes <hash>.txt when missing; its hash
bool HaveSet(const std::wstring& dataDir, const std::string& hash);
std::string SetText(const std::wstring& dataDir, const std::string& hash); // the published text, or empty
bool StoreSet(const std::wstring& dataDir, const std::string& hash, const std::string& text);      // a received set: kept only when its text has that hash

// Opens the editor (or brings it forward) on this set; `changed` is called after the library changed (the Randomizers tab refreshes its list).
void OpenEditor(HWND owner, const std::wstring& dataDir, const std::string& setName, void (*changed)());
bool EditorOpen();
void EditorTest(const std::string& command);      // tests and pictures: select <n>, solve, count, try, tool <n>
std::string EditorStat();      // for the launcher's stat file: "editor open <0|1> set <name> puzzles <n> tool <tool>"

}  // namespace ped
