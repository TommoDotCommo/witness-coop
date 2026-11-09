#pragma once
// The built-in randomizers in the game (build 11370412). For every world the game loads (a new game, a save, a save loaded again), the randomizer
// chosen in witness_coop.cfg (randomizer=<spec>, common/randomizers.h) makes that world's puzzles on a thread of its own (generate.h: nothing is written
// while it works), then the render thread writes them in one frame and runs upstream's watchdogs from then on. Until that is done the sync core does
// not call the world stable (World::worldReady), so nothing of the other players lands on a panel before its puzzle does, and what the randomizer
// changed is part of the starting state. The fingerprint of the puzzles goes to the other players, and the game says when theirs differ.
#include <cstdint>
#include <string>

void RandomizerFrame();                 // render thread, every frame
bool RandomizerSettled();               // any thread: no randomizer, or this world's puzzles are in (or the randomizer gave up on this world)
uint64_t RandomizerFingerprint();       // 0: no randomized puzzles in this world
bool RandomizerCommand(const char* line);   // test commands (witness_cmd.txt): randstat | randpanel <hex id> | randrecord <file> [spec]; false: not one of them
