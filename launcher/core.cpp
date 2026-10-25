// See core.h.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>
#include "core.h"

namespace wl {
static bool Active(const std::string& line, const std::string& key) { return !line.empty() && line[0] != '#' && line.compare(0, key.size(), key) == 0 && line.size() > key.size() && line[key.size()] == '='; }
CfgFile CfgFile::Parse(const std::string& text) {
    CfgFile c; std::string cur; for (char ch : text) { if (ch == '\n') { if (!cur.empty() && cur.back() == '\r') cur.pop_back(); c.lines.push_back(cur); cur.clear(); } else cur.push_back(ch); }
    if (!cur.empty()) c.lines.push_back(cur); return c;
}
std::string CfgFile::Text() const { std::string t; for (const auto& l : lines) t += l + "\n"; return t; }
bool CfgFile::Has(const std::string& key) const { for (const auto& l : lines) if (Active(l, key)) return true; return false; }
std::string CfgFile::Get(const std::string& key, const std::string& def) const { for (const auto& l : lines) if (Active(l, key)) return l.substr(key.size() + 1); return def; }
std::vector<std::string> CfgFile::GetAll(const std::string& key) const { std::vector<std::string> r; for (const auto& l : lines) if (Active(l, key)) r.push_back(l.substr(key.size() + 1)); return r; }
void CfgFile::Set(const std::string& key, const std::string& value) { SetAll(key, {value}); }
void CfgFile::Unset(const std::string& key) { SetAll(key, {}); }
void CfgFile::SetAll(const std::string& key, const std::vector<std::string>& values) {
    std::vector<std::string> out; bool placed = false;
    for (const auto& l : lines) {
        if (!Active(l, key)) { out.push_back(l); continue; }
        if (!placed) { for (const auto& v : values) out.push_back(key + "=" + v); placed = true; }      // later duplicates are dropped
#ifdef WL_MUT_KEEP_DUPLICATES
        else out.push_back(l);
#endif
    }
    if (!placed) for (const auto& v : values) out.push_back(key + "=" + v);
    lines = out;
}

// ---- Launcher.variables
const char* const kResolutionNames[5] = {"Display resolution", "1080p", "720p", "Dynamic resolution", "Custom"};
const char* const kMsaaNames[5] = {"Off", "2x MSAA", "4x MSAA", "8x MSAA", "16x MSAA"};
const char* const kQualityNames[3] = {"Low", "Medium", "High"};
bool ParseVideo(const std::string& text, Video* out) {
    Video v; std::istringstream in(text); std::string line; bool head = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == ":/launcher") { head = true; continue; }
        std::istringstream ls(line); std::string k, val; if (!(ls >> k >> val)) continue; int n = atoi(val.c_str()); bool b = val == "true";
        if (k == "render_profile") v.renderProfile = n; else if (k == "fullscreen") v.fullscreen = b; else if (k == "vsync") v.vsync = b; else if (k == "highdpi") v.highdpi = b;
        else if (k == "resolution") v.resolution = n; else if (k == "multisampling") v.multisampling = n; else if (k == "custom_width") v.customW = n; else if (k == "custom_height") v.customH = n;
        else if (k == "shading_quality") v.shading = n; else if (k == "reflection_quality") v.reflection = n; else if (k == "texture_quality") v.texture = n; else if (k == "texture_detail") v.textureDetail = n; else if (k == "geometry_detail") v.geometry = n;
    }
    if (!head) return false; *out = v; return true;
}
std::string FormatVideo(const Video& v) {
#ifdef WL_MUT_NO_ROUNDTRIP
    if (v.fullscreen) return ":/launcher\nfullscreen true\n";      // (mutant) loses the other values
#endif
    char b[600]; snprintf(b, sizeof b, ":/launcher\nrender_profile %d\nfullscreen %s\nvsync %s\nhighdpi %s\nresolution %d\nmultisampling %d\ncustom_width %d\ncustom_height %d\nshading_quality %d\nreflection_quality %d\ntexture_quality %d\ntexture_detail %d\ngeometry_detail %d\n",
        v.renderProfile, v.fullscreen ? "true" : "false", v.vsync ? "true" : "false", v.highdpi ? "true" : "false", v.resolution, v.multisampling, v.customW, v.customH, v.shading, v.reflection, v.texture, v.textureDetail, v.geometry);
    return b;
}
std::string CheckVideo(const Video& v) {
    auto in = [](int x, int lo, int hi) { return x >= lo && x <= hi; };
    if (!in(v.renderProfile, 0, 2)) return "render profile: 0 to 2";
    if (!in(v.resolution, 0, 4)) return "resolution: 0 to 4";
    if (!in(v.multisampling, 0, 4)) return "multisampling: 0 to 4";
    if (!in(v.shading, 0, 2) || !in(v.reflection, 0, 2) || !in(v.texture, 0, 2) || !in(v.textureDetail, 0, 2) || !in(v.geometry, 0, 2)) return "qualities: 0 (low) to 2 (high)";
    if (v.resolution == 4 && (!in(v.customW, 320, 16384) || !in(v.customH, 200, 16384))) return "custom resolution: 320 x 200 up to 16384 x 16384";
    return "";
}

// ---- options inside a save
const std::vector<OptionDef>& SaveOptionDefs() {
    static const std::vector<OptionDef> d = {
        {"mouse_speed_3d", "Mouse speed (looking around)", 1, 20, 0.5f, OptKind::Float}, {"mouse_speed_2d", "Mouse speed (on a puzzle)", 1, 20, 0.5f, OptKind::Float},
        {"invert_y_axis", "Invert the mouse up / down", 0, 1, 1, OptKind::Bool}, {"invert_x_axis", "Invert the mouse left / right", 0, 1, 1, OptKind::Bool}, {"mouse_smoothing", "Smooth the mouse", 0, 1, 1, OptKind::Bool},
        {"user_brightness", "Brightness", 0.5f, 2.0f, 0.05f, OptKind::Float}, {"user_fov_vertical", "Field of view (vertical, degrees)", 30, 90, 1, OptKind::Float},
        {"user_reticle", "Reticle", 0, 3, 1, OptKind::Int}, {"user_vignetting", "Vignetting", 0, 1, 1, OptKind::Bool}, {"look_magnetism_enabled", "Look magnetism", 0, 1, 1, OptKind::Bool}};
    return d;
}
static bool FindOption(const std::vector<uint8_t>& b, const std::string& name, size_t* valueAt) {      // "\0name\0" then the 4-byte value
    std::string pat;
#ifndef WL_MUT_LOOKALIKE
    pat.push_back('\0');
#endif
    pat += name; pat.push_back('\0');
    auto it = std::search(b.begin(), b.end(), pat.begin(), pat.end()); if (it == b.end()) return false;
    size_t at = size_t(it - b.begin()) + pat.size(); if (at + 8 > b.size()) return false; *valueAt = at; return true;
}
std::vector<SaveOption> ReadSaveOptions(const std::vector<uint8_t>& b) {
    std::vector<SaveOption> r;
    for (const auto& d : SaveOptionDefs()) {
        size_t at; if (!FindOption(b, d.name, &at)) continue; float v; int32_t n;
        if (d.kind == OptKind::Float) memcpy(&v, &b[at], 4); else { memcpy(&n, &b[at], 4); v = (float)n; }
        r.push_back({d.name, v, at});
    }
    return r;
}
bool WriteSaveOption(std::vector<uint8_t>* b, const std::string& name, float value) {
    for (const auto& d : SaveOptionDefs()) if (name == d.name) {
#ifndef WL_MUT_NO_RANGE
        if (!(value >= d.min && value <= d.max)) return false;
#endif
        size_t at; if (!FindOption(*b, name, &at)) return false;
        if (d.kind == OptKind::Float
#ifdef WL_MUT_INT_AS_FLOAT
            || true
#endif
            ) memcpy(&(*b)[at], &value, 4); else { int32_t n = (int32_t)(value + (value < 0 ? -0.5f : 0.5f)); memcpy(&(*b)[at], &n, 4); }
        return true;
    }
    return false;
}

// ---- saves
static unsigned long long Ft(const FILETIME& f) { return ((unsigned long long)f.dwHighDateTime << 32) | f.dwLowDateTime; }
static std::string Utf8(const std::wstring& s) { char b[600]; WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, b, 600, nullptr, nullptr); return b; }
static std::string ReadAll(const std::wstring& p) { std::string t; FILE* f = _wfopen(p.c_str(), L"rb"); if (f) { char b[4096]; size_t k; while ((k = fread(b, 1, sizeof b, f)) > 0) t.append(b, k); fclose(f); } return t; }
std::vector<SaveInfo> ListSaves(const std::wstring& dir) {
    std::vector<SaveInfo> r; WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*.witness_campaign").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            SaveInfo s; s.path = dir + L"\\" + fd.cFileName; std::wstring stem = fd.cFileName; stem = stem.substr(0, stem.rfind(L'.')); s.stem = Utf8(stem);
            s.bytes = ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow; s.when = Ft(fd.ftLastWriteTime);
            s.hasPng = GetFileAttributesW((dir + L"\\" + stem + L".png").c_str()) != INVALID_FILE_ATTRIBUTES;
            std::string meta = ReadAll(dir + L"\\" + stem + L".coop.txt"); s.hasMeta = !meta.empty() && wprog::ParseMeta(meta, &s.meta);
            r.push_back(s);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(r.begin(), r.end(), [](const SaveInfo& a, const SaveInfo& b) { return a.when != b.when ? a.when > b.when : a.stem > b.stem; });
    return r;
}

// ---- the launch
std::string ProgressSpec(const std::vector<std::string>& areas, int lasers, const std::vector<std::string>& presets) {
    std::string s; auto add = [&](const std::string& t) { if (!s.empty()) s += ";"; s += t; };
    for (const auto& a : areas) add("area:" + a); if (lasers > 0) add("lasers:" + std::to_string(lasers)); for (const auto& p : presets) add("preset:" + p); return s;
}
std::string LaunchArgs(const CfgFile& cfg, bool newGame) {
    std::string a = "-skip_config_dialog";
    if (cfg.Get("launcher_no_sound") == "1") a += " -no_sound";
    if (newGame) a += " -new_game";
    return a;
}
}  // namespace wl
