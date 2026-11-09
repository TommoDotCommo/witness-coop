#pragma once

class Randomizer {
public:
    Randomizer(const std::shared_ptr<Memory>& memory);
    void Randomize();

private:
    int _lastRandomizedFrame = 1 << 30;

    void Tutorialise(int panel1, int copyFrom);

    std::shared_ptr<Memory> _memory;

    friend class SwapTests_Shipwreck_Test;
};

