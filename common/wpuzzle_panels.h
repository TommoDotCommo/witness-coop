#pragma once
// The panels the player's own puzzles (common/wpuzzle.h) may replace, in the order the game reaches them: ordinary grid panels whose puzzle is
// only its line and symbols (no environmental trick, no door control, no pillar, no coloured glass). A puzzle whose panel is 0 takes the first one
// of these the set has not used yet. Names as the game's progression data has them (proxy/progress/progression.txt).
#include <string>
#include <vector>

namespace wpz {

struct Slot { unsigned id; const char* area; const char* name; };
inline const std::vector<Slot>& Slots() {
    static const std::vector<Slot> s = {
        {0x00064, "Tutorial", "Straight"}, {0x00182, "Tutorial", "Bend"}, {0x002C2, "Tutorial", "Front Left"}, {0x00293, "Tutorial", "Front Center"},
        {0x00295, "Tutorial", "Center Left"}, {0x0A3B5, "Tutorial", "Back Left"}, {0x0A3B2, "Tutorial", "Back Right"},
        {0x18AF, "Outside Tutorial", "Tree Row 1"}, {0x0001B, "Outside Tutorial", "Tree Row 2"}, {0x012C9, "Outside Tutorial", "Tree Row 3"},
        {0x0001C, "Outside Tutorial", "Tree Row 4"}, {0x0001D, "Outside Tutorial", "Tree Row 5"}, {0x0001E, "Outside Tutorial", "Tree Row 6"},
        {0x0001F, "Outside Tutorial", "Tree Row 7"}, {0x00020, "Outside Tutorial", "Tree Row 8"}, {0x00021, "Outside Tutorial", "Tree Row 9"},
        {0x0005D, "Outside Tutorial", "Shed Row 1"}, {0x0005E, "Outside Tutorial", "Shed Row 2"}, {0x0005F, "Outside Tutorial", "Shed Row 3"},
        {0x00060, "Outside Tutorial", "Shed Row 4"}, {0x00061, "Outside Tutorial", "Shed Row 5"},
        {0x00022, "Symmetry Island", "Right 1"}, {0x00023, "Symmetry Island", "Right 2"}, {0x00024, "Symmetry Island", "Right 3"},
        {0x00025, "Symmetry Island", "Right 4"}, {0x00026, "Symmetry Island", "Right 5"},
        {0x00065, "Symmetry Island", "Left 1"}, {0x0006D, "Symmetry Island", "Left 2"}, {0x00072, "Symmetry Island", "Left 3"},
        {0x0006F, "Symmetry Island", "Left 4"}, {0x00070, "Symmetry Island", "Left 5"}, {0x00071, "Symmetry Island", "Left 6"},
        {0x00557, "Quarry", "Upper Row 1"}, {0x005F1, "Quarry", "Upper Row 2"}, {0x00620, "Quarry", "Upper Row 3"}, {0x009F5, "Quarry", "Upper Row 4"},
        {0x0146C, "Quarry", "Upper Row 5"}, {0x3C12D, "Quarry", "Upper Row 6"}, {0x03686, "Quarry", "Upper Row 7"}, {0x014E9, "Quarry", "Upper Row 8"},
        {0x00E0C, "Quarry", "Lower Row 1"}, {0x01489, "Quarry", "Lower Row 2"}, {0x0148A, "Quarry", "Lower Row 3"}, {0x014D9, "Quarry", "Lower Row 4"},
        {0x014E7, "Quarry", "Lower Row 5"}, {0x014E8, "Quarry", "Lower Row 6"},
        {0x021B3, "Quarry", "Front Row 1"}, {0x021B4, "Quarry", "Front Row 2"}, {0x021B0, "Quarry", "Front Row 3"}, {0x021AF, "Quarry", "Front Row 4"},
        {0x021AE, "Quarry", "Front Row 5"},
        {0x00469, "Swamp", "Intro Front 1"}, {0x00472, "Swamp", "Intro Front 2"}, {0x00262, "Swamp", "Intro Front 3"}, {0x00474, "Swamp", "Intro Front 4"},
        {0x00553, "Swamp", "Intro Front 5"}, {0x0056F, "Swamp", "Intro Front 6"},
        {0x00390, "Swamp", "Intro Back 1"}, {0x010CA, "Swamp", "Intro Back 2"}, {0x00983, "Swamp", "Intro Back 3"}, {0x00984, "Swamp", "Intro Back 4"},
        {0x00986, "Swamp", "Intro Back 5"}, {0x00985, "Swamp", "Intro Back 6"}, {0x00987, "Swamp", "Intro Back 7"}, {0x181A9, "Swamp", "Intro Back 8"},
        {0x00999, "Swamp", "Between Bridges Near 1"}, {0x0099D, "Swamp", "Between Bridges Near 2"}, {0x009A0, "Swamp", "Between Bridges Near 3"},
        {0x009A1, "Swamp", "Between Bridges Near 4"}, {0x00007, "Swamp", "Between Bridges Far 1"}, {0x00008, "Swamp", "Between Bridges Far 2"},
        {0x00009, "Swamp", "Between Bridges Far 3"}, {0x0000A, "Swamp", "Between Bridges Far 4"},
        {0x2899C, "Town", "Wooden Roof Row 1"}, {0x28A33, "Town", "Wooden Roof Row 2"}, {0x28ABF, "Town", "Wooden Roof Row 3"},
        {0x28AC0, "Town", "Wooden Roof Row 4"}, {0x28AC1, "Town", "Wooden Roof Row 5"},
        {0x28AC7, "Town", "Red Rooftop 1"}, {0x28AC8, "Town", "Red Rooftop 2"}, {0x28ACA, "Town", "Red Rooftop 3"}, {0x28ACB, "Town", "Red Rooftop 4"},
        {0x28ACC, "Town", "Red Rooftop 5"},
    };
    return s;
}
inline const Slot* FindSlot(unsigned id) { for (auto& s : Slots()) if (s.id == id) return &s; return nullptr; }
inline std::string SlotName(unsigned id) { const Slot* s = FindSlot(id); return s ? std::string(s->area) + ": " + s->name : std::string(); }

// Where each puzzle of a set goes: its own panel when it names one of the slots (the first puzzle naming a panel wins it), else the next slot no
// puzzle has taken. 0 when there is none left (the set has more puzzles than there are slots).
template <class Set> std::vector<unsigned> Placement(const Set& set) {
    std::vector<unsigned> out(set.puzzles.size(), 0); std::vector<bool> taken(Slots().size(), false);
    auto index = [](unsigned id) { for (size_t i = 0; i < Slots().size(); i++) if (Slots()[i].id == id) return (int)i; return -1; };
    for (size_t k = 0; k < set.puzzles.size(); k++) { int i = index(set.puzzles[k].panel); if (i >= 0 && !taken[i]) { taken[i] = true; out[k] = Slots()[i].id; } }
    size_t next = 0;
    for (size_t k = 0; k < set.puzzles.size(); k++) {
        if (out[k]) continue;
        while (next < taken.size() && taken[next]) next++;
        if (next < taken.size()) { taken[next] = true; out[k] = Slots()[next].id; }
    }
    return out;
}

}  // namespace wpz
