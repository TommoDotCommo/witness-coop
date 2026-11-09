/*
Things to do for V2:
- Better interface design. It's way too simplistic, take (some) notes from talos.
  - Seed: [   ] (Randomize)
  ?? Challenge

  - [] Prevent speedrun snipes // Shadows, Swamp, Town, Quarry stairs
  - [] Speed up various autoscrollers // Swamp platforms, Desert/Mountain elevators, Desert rotating panels

  (Additional required panels)
  - [] Desert 8
  - [] Pond 5
  - [] Both keep halves
  - [] Town lattice requires stars door // and stars door will be randomized

  (Debug version only)
  - [] Noclip
  - [] Noclip speed


- Really randomize panels. Sorted by ROI
  - Random with style
    - Tutorial
    - Mountain 1 orange, green, blue, purple
    - Mountain 2 multipanel
    - Mountain 3 pillars
    - Laser areas (Glass Factory, Symmetry, Quarry, Treehouse, Swamp, Keep pressure plates, Town 25 dots)
    - (low) Discarded panels
    - (low) Tutorials // Dots, Stones, Swamp

  - Keep Hedges become like hedges 4, intersection between path and panel
  - Keep Pressure plates: Random with style

  - No idea how to randomize:
    - Symmetry transparent
    - Desert
    - Shadows
    - Town (lattice, RGB area, snipes, triple)
    - Monastery
    - Jungle
    - Bunker
    - UTM
    - Mountain 2 rainbow
    - Challenge

- Any RNG rerolls should be based on previous seed so that everyone can go to next seed easily

- Stability. Duh. I need to clearly define the ownership between the randomizer and the game.

- Challenge should have some way to 'reroll every run'
- Challenge should not turn off after time limit?
- Challenge triangles should not turn off




*/






/*
 * Try to wire up both keep halves
 * Wire up both halves of symmetry laser
 * Turn off floating panel in desert
 * Try randomizing default-on for pitches & bunker


 * Speed up *everything* ? Or maybe we'll just stop using this setting entirely.

 * Add setting for "Don't reset the challenge seed on new challenge"
 * Don't rerandomize anything outside of challenge on re-click
 * Change re-randomization prevention?


 * BUGS:
 * Shipwreck vault is solved reversed? -> Not reversed, just "half", you can normally solve orange. Seems to need pattern name.
 * Tutorial sounds don't always play -> Unsure. Not controlled by pattern name.
 * Rainbow seems to be not copying background?
 ** Rainbow 1 <-> Green 3 (the poly one) worked
 ** Rainbow 2 <-> Treehouse Right Orange 1 didn't
 * FEATURES:
 * Start the game if it isn't running?
 * Randomize audio logs -- Hard, seem to be unloaded some times?
 * Swap sounds in jungle (along with panels) -- maybe impossible
 * Make orange 7 (all of oranges?) hard. Like big = hard. (See: HARD_MODE)
 * Try turning on first half of wire in shadows once tutorial is done
 * It might be possible to remove the texture on top of rainbow 5 (so that any panel can be placed there)
 * 20 challenges with 20 consecutive seeds
 * Random *rotation* of desert laser redirect?
*/
#include "pch.h"
#include "Randomizer.h"
#include "Panels.h"
#include "Random.h"


template <class T>
int find(const std::vector<T> &data, T search, size_t startIndex = 0) {
    for (size_t i=startIndex ; i<data.size(); i++) {
        if (data[i] == search) return static_cast<int>(i);
    }
    std::cout << "Couldn't find " << search << " in data!" << std::endl;
    throw std::runtime_error("Couldn't find value in data!"); // witness-coop: std::exception(const char*) is MSVC only
}

Randomizer::Randomizer(const std::shared_ptr<Memory>& memory) : _memory(memory) {}

void Randomizer::Randomize() {
    // reveal_exit_hall - Prevent actually ending the game (EEE)
    _memory->AddSigScan({0x45, 0x8B, 0xF7, 0x48, 0x8B, 0x4D}, [&](int index){
        _memory->WriteData<byte>({index + 0x15}, {0xEB}); // jz -> jmp
    });

    // begin_endgame_1 - Prevent actually ending the game (Wonkavator)
    _memory->AddSigScan({0x83, 0x7C, 0x01, 0xD0, 0x04}, [&](int index){
        if (GLOBALS == 0x5B28C0) { // Version differences.
            index += 0x75;
        } else if (GLOBALS == 0x62D0A0) {
            index += 0x86;
        }
        _memory->WriteData<byte>({index}, {0xEB}); // jz -> jmp
    });

    _memory->ExecuteSigScans();

    // Tutorial Bend
    for (int panel : utmPerspective) {
        Tutorialise(panel, 0x00182);
    }
    // Tutorial Straight
    for (int panel : squarePanels) {
        Tutorialise(panel, 0x00064);
    }
    // Town Laser Redirect Control
    for (int panel : treehousePivots) {
        Tutorialise(panel, 0x09F98);

        // Mark the panel as pivotable.
        int panelFlags = _memory->ReadEntityData<int>(panel, STYLE_FLAGS, 1)[0];
        _memory->WriteEntityData<int>(panel, STYLE_FLAGS, { panelFlags | 0x8000 });
    }

    // Disable tutorial cursor speed modifications (not working?)
    _memory->WriteEntityData<float>(0x00295, CURSOR_SPEED_SCALE, { 1.0 });
    _memory->WriteEntityData<float>(0x0C373, CURSOR_SPEED_SCALE, { 1.0 });
    _memory->WriteEntityData<float>(0x00293, CURSOR_SPEED_SCALE, { 1.0 });
    _memory->WriteEntityData<float>(0x002C2, CURSOR_SPEED_SCALE, { 1.0 });

    // Ensure that whatever pivot panels we have are flagged as "pivotable"
    // @Bug: Can return {}, be careful!
    int panelFlags = _memory->ReadEntityData<int>(0x17DD1, STYLE_FLAGS, 1)[0];
    _memory->WriteEntityData<int>(0x17DD1, STYLE_FLAGS, { panelFlags | 0x8000 });
    panelFlags = _memory->ReadEntityData<int>(0x17CE3, STYLE_FLAGS, 1)[0];
    _memory->WriteEntityData<int>(0x17CE3, STYLE_FLAGS, { panelFlags | 0x8000 });
    panelFlags = _memory->ReadEntityData<int>(0x17DB7, STYLE_FLAGS, 1)[0];
    _memory->WriteEntityData<int>(0x17DB7, STYLE_FLAGS, { panelFlags | 0x8000 });
    panelFlags = _memory->ReadEntityData<int>(0x17E52, STYLE_FLAGS, 1)[0];
    _memory->WriteEntityData<int>(0x17E52, STYLE_FLAGS, { panelFlags | 0x8000 });
}

void Randomizer::Tutorialise(int panel1, int tutorialStraight) {
    //const int tutorialStraight = 0x00064;
    
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, PATH_COLOR, 16);
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, REFLECTION_PATH_COLOR, 16);
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, DOT_COLOR, 16);
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, ACTIVE_COLOR, 16);
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, BACKGROUND_REGION_COLOR, 12);
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, SUCCESS_COLOR_A, 16);
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, SUCCESS_COLOR_B, 16);
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, STROBE_COLOR_A, 16);
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, STROBE_COLOR_B, 16);
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, ERROR_COLOR, 16);
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, PATTERN_POINT_COLOR, 16);
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, PATTERN_POINT_COLOR_A, 16);
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, PATTERN_POINT_COLOR_B, 16);
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, SYMBOL_A, 16);
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, SYMBOL_B, 16);
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, SYMBOL_C, 16);
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, SYMBOL_D, 16);
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, SYMBOL_E, 16);
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, PUSH_SYMBOL_COLORS, sizeof(int));
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, OUTER_BACKGROUND, 16);
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, OUTER_BACKGROUND_MODE, sizeof(int));
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, NUM_COLORED_REGIONS, sizeof(int));
    _memory->CopyArrayDynamicSize<int>(tutorialStraight, panel1, COLORED_REGIONS, NUM_COLORED_REGIONS);
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, TRACED_EDGES, 16);
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, PATH_WIDTH_SCALE, sizeof(float));
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, STARTPOINT_SCALE, sizeof(float));
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, NUM_DOTS, sizeof(int));
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, NUM_CONNECTIONS, sizeof(int));
    _memory->CopyArray<float>(tutorialStraight, panel1, DOT_POSITIONS, _memory->ReadEntityData<int>(tutorialStraight, NUM_DOTS, sizeof(int))[0]*2);
    _memory->CopyArrayDynamicSize<int>(tutorialStraight, panel1, DOT_FLAGS, NUM_DOTS);
    _memory->CopyArrayDynamicSize<int>(tutorialStraight, panel1, DOT_CONNECTION_A, NUM_CONNECTIONS);
    _memory->CopyArrayDynamicSize<int>(tutorialStraight, panel1, DOT_CONNECTION_B, NUM_CONNECTIONS);
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, NUM_DECORATIONS, sizeof(int));
    _memory->CopyArrayDynamicSize<int>(tutorialStraight, panel1, DECORATIONS, NUM_DECORATIONS);
    _memory->CopyArrayDynamicSize<int>(tutorialStraight, panel1, DECORATION_FLAGS, NUM_DECORATIONS);
    _memory->CopyArrayDynamicSize<int>(tutorialStraight, panel1, DECORATION_COLORS, NUM_DECORATIONS);
    if (_memory->ReadPanelData<int>(tutorialStraight, REFLECTION_DATA)) {
        _memory->CopyArrayDynamicSize<int>(tutorialStraight, panel1, REFLECTION_DATA, NUM_DOTS);
    }
    else {
        _memory->WritePanelData<long long>(panel1, REFLECTION_DATA, { 0 });
    }
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, SEQUENCE_LEN, sizeof(int));
    _memory->CopyArrayDynamicSize<int>(tutorialStraight, panel1, SEQUENCE, SEQUENCE_LEN);
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, DOT_SEQUENCE_LEN, sizeof(int));
    _memory->CopyArrayDynamicSize<int>(tutorialStraight, panel1, DOT_SEQUENCE, DOT_SEQUENCE_LEN);
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, DOT_SEQUENCE_LEN_REFLECTION, sizeof(int));
    _memory->CopyArrayDynamicSize<int>(tutorialStraight, panel1, DOT_SEQUENCE_REFLECTION, DOT_SEQUENCE_LEN_REFLECTION);
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, GRID_SIZE_X, sizeof(int));
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, GRID_SIZE_Y, sizeof(int));
    _memory->CopyEntityData<byte>(tutorialStraight, panel1, STYLE_FLAGS, sizeof(int));
    _memory->WritePanelData<byte>(panel1, RANDOMISE_ON_POWER_ON, { 0 });


        //arrays.push_back(AUDIO_PREFIX);
//        offsets[IS_CYLINDER] = sizeof(int);
//        offsets[CYLINDER_Z0] = sizeof(float);
//        offsets[CYLINDER_Z1] = sizeof(float);
//        offsets[CYLINDER_RADIUS] = sizeof(float);

        //arrays.push_back(PANEL_TARGET);
        //arrays.push_back(SPECULAR_TEXTURE);

}
