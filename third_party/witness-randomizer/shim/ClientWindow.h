#pragma once
// witness-coop: replaces upstream's randomizer window. The generator reports its progress here; the mod shows it in the game (proxy/randomizer).
#include <string>

class ClientWindow {
public:
    static ClientWindow* get();                    // proxy/randomizer/backend.cpp
    void setStatusMessage(const std::string& text);
};
