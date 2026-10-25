#pragma once
// The race game mode in the game (race/race.h is the logic): the state machine ticks on the network thread, the HUD is drawn on the render thread.
#include <cstdint>
#include <functional>
#include <set>
#include <vector>

void RaceStart(unsigned myId, const std::vector<unsigned>& others, const std::function<void(const std::vector<uint8_t>&)>& send);   // network thread, once (game_mode=race)
void RaceTick(uint64_t now, bool worldReady, const std::set<unsigned>& solved);       // network thread, every few ms
void RaceOnPacket(const uint8_t* data, size_t n, uint64_t now);                       // network thread
bool RaceActive();
void RaceDraw(float w, float h);                                                       // render thread, between UiBegin and UiEnd
void RaceStat();                                                                       // test command racestat
