#pragma once
// What the mod writes next to each of the game's own save files (<save>.coop.txt) so a save can be told apart outside the game: who was in the game and how far it got.
// Game-free; the launcher reads the same files. The text is plain lines (tab separated where a name may hold spaces):
//   coop-save 1 / saved <date time> / session_seconds N / total_seconds N / progress SOLVED TOTAL / lasers LIT 11 / doors OPEN TOTAL / area <name> SOLVED TOTAL / player <id> <name> <character> <seconds> <sessions>
#include <set>
#include <string>
#include <vector>
#include "progress.h"

namespace wprog {
struct MetaArea { std::string name; unsigned solved = 0, total = 0; };
struct MetaPlayer { unsigned id = 0; std::string name, character; unsigned seconds = 0, sessions = 0; };
struct SaveMeta {
    std::string savedAt;                 // "2026-10-26 01:17:36"
    unsigned sessionSeconds = 0, totalSeconds = 0;      // this session, and all sessions of this campaign (earlier ones come from the previous save's file)
    unsigned solved = 0, total = 0, lasers = 0, lasersTotal = 0, doorsOpen = 0, doorsTotal = 0;
    std::vector<MetaArea> areas;         // only areas with panels
    std::vector<MetaPlayer> players;     // everyone who was in the game, this session and earlier ones, by name
};
// The progress half of a SaveMeta from the panels solved (entity ids): per area counts (optional panels left out), lasers lit, panels in total.
void FillProgress(const Data& d, const std::set<unsigned>& solved, unsigned doorsOpen, unsigned doorsTotal, SaveMeta* m);
// Players of earlier sessions join this session's: same name = same player (seconds and sessions add up, the newest character wins).
void MergePlayers(std::vector<MetaPlayer>* into, const std::vector<MetaPlayer>& earlier);
std::string FormatMeta(const SaveMeta& m);
bool ParseMeta(const std::string& text, SaveMeta* m);
std::string OneLine(const SaveMeta& m);              // "21% done, 2 lasers, Alpha + Beta (3 h 12 min)" for a list
}  // namespace wprog
