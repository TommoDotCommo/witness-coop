#pragma once
// The launcher's logic, game-free (T0 test: core_test.cpp): reading and writing the three places the game's options live in, listing saves, and building the launch.
//   witness_coop.cfg      the mod's own settings (name, keys, connection, models, ...): text lines key=value, written back with every line it does not know left as it is
//   Launcher.variables    the game's video options (in its save folder): render profile, fullscreen, vsync, resolution, multisampling, qualities
//   <save>.witness_campaign   the game's save: it carries the game's own options (mouse speed, invert, brightness, field of view, ...) as named values, patched in place
#include <cstdint>
#include <string>
#include <vector>
#include "../proxy/progress/savemeta.h"

namespace wl {
// ---- witness_coop.cfg
struct CfgFile {
    std::vector<std::string> lines;
    static CfgFile Parse(const std::string& text);
    std::string Text() const;
    std::string Get(const std::string& key, const std::string& def = "") const;      // the first active line with this key
    bool Has(const std::string& key) const;
    void Set(const std::string& key, const std::string& value);                       // replaces the first active line, drops later duplicates, else appends
    void Unset(const std::string& key);                                                // comments the lines out? no: removes them
    std::vector<std::string> GetAll(const std::string& key) const;                     // every active line (peer= may repeat)
    void SetAll(const std::string& key, const std::vector<std::string>& values);       // exactly these lines, the first where the old first was
};

// ---- Launcher.variables
struct Video {
    int renderProfile = 2, resolution = 1, multisampling = 2, customW = 1920, customH = 1080, shading = 2, reflection = 2, texture = 2, textureDetail = 2, geometry = 2;
    bool fullscreen = false, vsync = true, highdpi = false;
};
bool ParseVideo(const std::string& text, Video* v);       // false: not that file
std::string FormatVideo(const Video& v);                 // the text the game writes
std::string CheckVideo(const Video& v);                  // "" when every value is one the game knows, else what is wrong
extern const char* const kResolutionNames[5];            // index -> what the game's dialog calls it
extern const char* const kMsaaNames[5];
extern const char* const kQualityNames[3];               // 0 low, 1 medium, 2 high

// ---- the game's options inside a save
enum class OptKind { Float, Int, Bool };      // Float: a 4-byte float; Int and Bool: a 4-byte integer (what the game stores for a yes / no is the integer 1, not the float 1.0)
struct OptionDef { const char* name; const char* label; float min, max, step; OptKind kind; };
const std::vector<OptionDef>& SaveOptionDefs();
struct SaveOption { std::string name; float value; size_t offset; };
std::vector<SaveOption> ReadSaveOptions(const std::vector<uint8_t>& campaign);        // the known ones found in the file
bool WriteSaveOption(std::vector<uint8_t>* campaign, const std::string& name, float value);   // false: not in the file or out of range

// ---- saves
struct SaveInfo { std::wstring path; std::string stem; unsigned long long bytes = 0, when = 0; bool hasPng = false, hasMeta = false; wprog::SaveMeta meta; };
std::vector<SaveInfo> ListSaves(const std::wstring& dir);     // newest first; the sidecar is read when there is one

// ---- the launch
std::string ProgressSpec(const std::vector<std::string>& areas, int lasers, const std::vector<std::string>& presets);   // "area:X;area:Y;lasers:N;preset:Z"
std::string LaunchArgs(const CfgFile& cfg, bool newGame);       // the game's command line: -skip_config_dialog (the launcher has written the game's video settings), -no_sound when launcher_no_sound=1, -new_game for a new world
}  // namespace wl
