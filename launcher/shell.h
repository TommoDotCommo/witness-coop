#pragma once
// The launcher's shape (co-op standard A, E, K3, K7), game-free: the four tabs, the Play choice, the Settings sections and their rows, press-a-key capture,
// pad navigation from XInput states and the footer hints. main.cpp draws what this says; shell_test.cpp tests it without a window (T0, with mutants).
#include <cstdint>
#include <string>
#include <vector>
#include "../proxy/config.h"

namespace ls {
// ---- tabs (A1, A3)
enum Tab { kPlay, kSaves, kSettings, kAbout, kTabCount };
const char* TabLabel(int tab, bool inLobby);              // "Play" turns into "Lobby" while hosting or joined

// ---- Play (A2): three tiles and one primary button that says what it does; the last choice is remembered (cfg launcher_choice)
enum Choice { kHost, kJoin, kSolo };
const char* ChoiceKey(Choice c);                          // "host" | "join" | "solo"
Choice ParseChoice(const std::string& s);                 // anything unknown: host
std::string PrimaryLabel(Choice c, const std::string& joinName);   // "Open lobby" | "Join <name>" (or "Join") | "Play solo"

// ---- Settings (E): sections in the standard order, rows in them
enum Section { kRules, kPlayers, kVoice, kControls, kDisplay, kNetwork, kLauncher, kSectionCount };
const char* SectionName(int s);
enum RowKind { kToggle, kChoice, kSlider, kKey, kText, kPort, kPeers, kNote };
struct Row {
    std::string key;            // "cfg:<name>" (witness_coop.cfg), "vid:<name>" (Launcher.variables), "opt:<name>" (inside a save), "upd:<auto|pre>" (the updater)
    std::string label;
    Section section;
    RowKind kind;
    std::vector<std::string> choices;      // what the player sees
    std::vector<std::string> values;       // what the cfg stores, when it differs from the words shown (empty: the words themselves)
    std::string def;            // the default as text (cfg rows)
    double min = 0, max = 0, step = 1, coarse = 1;      // sliders: range, the step of a value, the step of a pad/arrow press
    std::string unit;           // sliders: "%", "m", "fps", "degrees", ...
    std::string zeroText;       // sliders: what 0 means ("none", "unlimited"), empty when 0 is a number
    std::string hint;
    bool hostDecides = false;   // E2: tagged "host decides"
    bool inSave = false;        // the game's own option inside a save: "save to change" applies
};
const std::vector<Row>& Rows();                           // every row, in section order
std::vector<const Row*> RowsOf(Section s);
const Row* FindRow(const std::string& key);
bool IsTyped(const Row& r);                               // a field the player types into (E3: only names, addresses and the port)
// sliders
int SliderSteps(const Row& r);                            // positions 0..n
int SliderPos(const Row& r, double value);
double SliderValue(const Row& r, int pos);
std::string ValueText(const Row& r, double value);        // "75 %", "25 m", "none"
// Reset section (E1): the cfg defaults of a section's rows (key without the "cfg:" prefix -> value)
std::vector<std::pair<std::string, std::string>> SectionDefaults(Section s);

// ---- press-a-key capture (E3, H4): the virtual key the player pressed, as the binding table's code; F1 and keys in use are refused
unsigned KeyFromVk(unsigned vk);                          // 0: not a key a binding can take
// "" and *out = cur with the new key when it is taken; else why not, in plain words
std::string TryBind(const Cfg& cur, const std::string& config, unsigned vk, Cfg* out);

// ---- pad (K3): XInput states in, actions out
enum PadButton : uint16_t { kDUp = 0x0001, kDDown = 0x0002, kDLeft = 0x0004, kDRight = 0x0008, kStart = 0x0010, kBackBtn = 0x0020, kLThumb = 0x0040, kRThumb = 0x0080,
                            kLB = 0x0100, kRB = 0x0200, kA = 0x1000, kB = 0x2000, kX = 0x4000, kY = 0x8000 };
struct PadState { bool connected = false; uint16_t buttons = 0; int16_t lx = 0, ly = 0; };
enum PadAction { kUp, kDown, kLeft, kRight, kConfirm, kBack, kPrevTab, kNextTab, kPrimary, kActionX, kActionY };
const char* PadActionName(PadAction a);
class PadNav {
public:
    std::vector<PadAction> Update(const PadState& s, uint32_t nowMs);      // presses become actions once; a held direction repeats after 400 ms every 120 ms
private:
    uint16_t held_ = 0; int dir_ = -1; uint32_t dirSince_ = 0, lastRepeat_ = 0;
};

// ---- footer hints (K3, K7): the page's actions, as pad buttons after pad input and as keys after keyboard input
struct PageActions { std::string primary, x, y; };
PageActions ActionsFor(Tab t, bool inLobby, bool isHost);
std::string FooterText(const PageActions& a, bool pad);
}  // namespace ls
