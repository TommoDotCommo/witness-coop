#pragma once
// witness-coop: replaces upstream Source/Utilities.h (file and clock helpers for the Archipelago client). The randomizers use only
// isAprilFools, in Archipelago's skip text; it is always false here so that every PC generates the same puzzles whatever the date.
class Utilities {
public:
    static bool isAprilFools() { return false; }
};
