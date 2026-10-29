#include "../common/guide.h"
// In-game dev menu (build 11370412). See devmenu.h. Everything the menu knows comes from the entity table (positions, solved and powered
// words, each panel's target at +0x2B4); every action calls the same game functions the sync code uses, on the render thread.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "devmenu.h"
#include "ui.h"
#include "ui/text.h"
#include "coop.h"
#include "game.h"
#include "config.h"
#include "video.h"
#include "tv.h"
#include "savepoints.h"
#include "progress/progress.h"
#include "mapgen.h"
extern HMODULE g_selfModule();

void Log(const char* msg);

namespace {
const uintptr_t kGlobals = 0x62D0A0, kPanelTI = 0x62E670, kDoorTI = 0x62DE70, kCableTI = 0x62EB10, kCamPos = 0x6304B0 + 0x10, kCamAng = 0x6303EC;
const unsigned kMaxId = 0x40000;
const float kPi = 3.14159265f;

struct Pn { unsigned id; float x, y, z, rz; bool solved, powered; int target; char ttype[28]; unsigned nodes; };
struct Dr { unsigned id; float x, y, z; bool open; };
struct Spot { int pn; int count; int cx, cy; };
std::vector<Pn> g_pn; std::vector<Dr> g_dr; std::vector<Spot> g_spots; std::map<unsigned, int> g_pidx;       // panel id -> index
std::map<int, std::vector<int>> g_rev;                                                                              // target entity index -> panels (indices) that target it
unsigned g_baseId = 1; bool g_built; DWORD g_lastRefresh;
float g_minx, g_maxx, g_miny, g_maxy;

CRITICAL_SECTION g_cs; bool g_csInit;
void Init0() { if (!g_csInit) { InitializeCriticalSection(&g_cs); g_csInit = true; } }
volatile bool g_open; int g_page;
float g_cx = 400, g_cy = 300; bool g_rdown, g_ldown;
struct Ev { int type; int a, b; };               // 0 button down (a = which), 1 button up, 2 wheel, 3 key
std::vector<Ev> g_ev;

// selection and view
enum { SelNone, SelPoint, SelPanel, SelPlayer };
int g_selKind = SelNone, g_selPanel = -1; float g_selX, g_selY; int g_selPlayer;
float g_vx, g_vy, g_vscale; bool g_viewInit; unsigned g_underlayDrawn; bool g_underlayLoaded;
float g_spotScroll, g_nearScroll; float g_radius = 40;
char g_msg[200]; DWORD g_msgAt;
void Msg(const char* fmt, ...) { va_list ap; va_start(ap, fmt); vsnprintf(g_msg, sizeof g_msg, fmt, ap); va_end(ap); g_msgAt = GetTickCount(); Log(g_msg); }

uintptr_t Base() { return (uintptr_t)GetModuleHandleA(nullptr); }
bool Ok(const void* p, size_t n) { return !IsBadReadPtr(p, n); }
const char* TypeName(uintptr_t e) {
    static std::map<uintptr_t, std::string> cache; if (!e || !Ok((void*)(e + 8), 8)) return "?";
    uintptr_t d = *(uintptr_t*)(e + 8); auto it = cache.find(d); if (it != cache.end()) return it->second.c_str();
    std::string s = "?";
    if (d && Ok((void*)(d + 8), 8)) { uintptr_t np = *(uintptr_t*)(d + 8); if (np && Ok((void*)np, 24)) { char b[25] = {}; memcpy(b, (void*)np, 24); bool good = b[0] != 0; for (int i = 0; b[i]; i++) if (b[i] < 32 || b[i] > 126) { good = false; b[i] = 0; break; } if (good) s = b; } }
    return (cache[d] = s).c_str();
}
float Rf(uintptr_t a) { return Ok((void*)a, 4) ? *(float*)a : 0; }
unsigned Ru(uintptr_t a) { return Ok((void*)a, 4) ? *(unsigned*)a : 0; }
bool One(uintptr_t a) { return Ru(a) == 0x3F800000; }

void ReadPanel(Pn& p, uintptr_t e) {
    p.solved = One(e + 0x294) && Ru(e + 0x298) == 1; p.powered = One(e + 0x2A4);
    unsigned t = Ru(e + 0x2B4); p.target = (t >= g_baseId && t - g_baseId < kMaxId && GameEntity(t - g_baseId)) ? (int)(t - g_baseId) : -1;
}

void Build() {
    g_pn.clear(); g_dr.clear(); g_pidx.clear(); g_rev.clear(); g_spots.clear();
    uintptr_t glob = Ok((void*)(Base() + kGlobals), 8) ? *(uintptr_t*)(Base() + kGlobals) : 0; if (!glob) return;
    g_baseId = Ru(glob + 8);
    g_minx = g_miny = 1e9f; g_maxx = g_maxy = -1e9f;
    for (unsigned id = 0; id < kMaxId; id++) {
        uintptr_t e = GameEntity(id); if (!e || !Ok((void*)(e + 8), 8)) continue; uintptr_t ti = *(uintptr_t*)(e + 8);
        if (ti == Base() + kPanelTI) {
            Pn p = {}; p.id = id; p.x = Rf(e + 0x24); p.y = Rf(e + 0x28); p.z = Rf(e + 0x2C);
            float qz = Rf(e + 0x3C), qw = Rf(e + 0x40); p.rz = 2 * atan2f(qz, qw); p.nodes = Ru(e + 0x3B4);
            if (fabsf(p.x) + fabsf(p.y) < 1) continue;       // panels parked at the origin are not in the world
            ReadPanel(p, e); g_pidx[id] = (int)g_pn.size(); g_pn.push_back(p);
            g_minx = std::min(g_minx, p.x); g_maxx = std::max(g_maxx, p.x); g_miny = std::min(g_miny, p.y); g_maxy = std::max(g_maxy, p.y);
        } else if (ti == Base() + kDoorTI) {
            Dr d = {}; d.id = id; d.x = Rf(e + 0x24); d.y = Rf(e + 0x28); d.z = Rf(e + 0x2C); d.open = One(e + 0x1D0);
            if (fabsf(d.x) + fabsf(d.y) >= 1) g_dr.push_back(d);
        }
    }
    for (size_t i = 0; i < g_pn.size(); i++) {
        Pn& p = g_pn[i]; uintptr_t t = p.target >= 0 ? GameEntity(p.target) : 0;
        const char* tn = t ? TypeName(t) : "-"; if (!strcmp(tn, "Machine_Panel")) tn = "panel"; else if (!strcmp(tn, "Power_Cable")) tn = "cable"; else if (!strcmp(tn, "Pressure_Plate")) tn = "plate";
        snprintf(p.ttype, sizeof p.ttype, "%s", tn);
        if (p.target >= 0) g_rev[p.target].push_back((int)i);
    }
    std::map<std::pair<int, int>, std::vector<int>> cells;
    for (size_t i = 0; i < g_pn.size(); i++) cells[{(int)floorf(g_pn[i].x / 80), (int)floorf(g_pn[i].y / 80)}].push_back((int)i);
    float ox = g_pn.empty() ? 0 : g_pn[0].x, oy = g_pn.empty() ? 0 : g_pn[0].y; auto f = g_pidx.find(0x64); if (f != g_pidx.end()) { ox = g_pn[f->second].x; oy = g_pn[f->second].y; }
    for (auto& kv : cells) {
        float cx = (kv.first.first + .5f) * 80, cy = (kv.first.second + .5f) * 80; int best = kv.second[0];
        for (int i : kv.second) if (hypotf(g_pn[i].x - cx, g_pn[i].y - cy) < hypotf(g_pn[best].x - cx, g_pn[best].y - cy)) best = i;
        g_spots.push_back({best, (int)kv.second.size(), kv.first.first, kv.first.second});
    }
    std::sort(g_spots.begin(), g_spots.end(), [&](const Spot& a, const Spot& b) { return hypotf(g_pn[a.pn].x - ox, g_pn[a.pn].y - oy) < hypotf(g_pn[b.pn].x - ox, g_pn[b.pn].y - oy); });
    if (g_pn.size() > 40) {      // fit the view to the bulk of the island: a few panels far away must not shrink the map
        std::vector<float> xs, ys; for (auto& p : g_pn) { xs.push_back(p.x); ys.push_back(p.y); } std::sort(xs.begin(), xs.end()); std::sort(ys.begin(), ys.end());
        size_t lo = xs.size() * 3 / 100, hi = xs.size() - 1 - lo; g_minx = xs[lo]; g_maxx = xs[hi]; g_miny = ys[lo]; g_maxy = ys[hi];
    }
    g_built = !g_pn.empty(); g_viewInit = false;
    char m[120]; snprintf(m, sizeof m, "devmenu: %zu panels, %zu doors, %zu spots", g_pn.size(), g_dr.size(), g_spots.size()); Log(m);
}

void Refresh() {
    for (auto& p : g_pn) { uintptr_t e = GameEntity(p.id); if (e && Ok((void*)(e + 8), 8) && *(uintptr_t*)(e + 8) == Base() + kPanelTI) ReadPanel(p, e); }
    for (auto& d : g_dr) { uintptr_t e = GameEntity(d.id); if (e) d.open = One(e + 0x1D0); }
}

bool Camera(float pos[3], float ang[2]) {
    const float* c = (const float*)(Base() + kCamPos); const float* a = (const float*)(Base() + kCamAng);
    if (!Ok(c, 12) || !Ok(a, 8)) return false; memcpy(pos, c, 12); memcpy(ang, a, 8); return true;
}

// ---- actions
// A teleport is checked a moment later: the game puts the player back at its spawn point when the target is inside geometry, so the
// next try uses another height and distance (the same sequence the tour tool uses).
struct Pending { bool on; float px, py, pz, rz; int attempt; DWORD at; unsigned id; } g_tp;
const float kTryDist[] = {3, 3, 2, 4, 5}, kTryDz[] = {8, 0.3f, 8, 8, 12};
void FireTeleport() {
    int k = g_tp.attempt; float sx = g_tp.px + kTryDist[k] * cosf(g_tp.rz), sy = g_tp.py + kTryDist[k] * sinf(g_tp.rz);
    GameTeleport(sx, sy, g_tp.pz + kTryDz[k], remainderf(g_tp.rz + kPi, 2 * kPi), 0); g_tp.at = GetTickCount();
}
void TeleportToPanel(const Pn& p) {      // 3 m in front of the panel at eye level, held for 2 s so the player settles on the ground there instead of being snapped to the lowest level once
    g_tp.on = false; GameHover(p.x + 3 * cosf(p.rz), p.y + 3 * sinf(p.rz), p.z + 0.3f, remainderf(p.rz + kPi, 2 * kPi), 0, 2);
    Msg("teleported in front of panel %#x", p.id); g_open = false;
}
bool GroundNear(float x, float y, float* z) {
    int best = -1; float bd = 30;
    for (size_t i = 0; i < g_pn.size(); i++) { float d = hypotf(g_pn[i].x - x, g_pn[i].y - y); if (d < bd) { bd = d; best = (int)i; } }
    if (best < 0) return false; *z = g_pn[best].z + 6; return true;
}
void TeleportToPoint(float x, float y) {
    float z; if (!GroundNear(x, y, &z)) { Msg("no known ground within 30 m of that point: pick a panel or a spot"); return; }
    float pos[3], ang[2]; float yaw = Camera(pos, ang) ? ang[0] : 0;
    GameTeleport(x, y, z, yaw, 0); Msg("teleport: to %.0f %.0f (dropping from %.0f)", x, y, z); g_open = false;
}
void TeleportToPlayer(const RemotePose& rp) { GameTeleport(rp.pos[0], rp.pos[1], rp.pos[2] + 0.5f, rp.ang[0], rp.ang[1]); Msg("teleport: to the other player"); g_open = false; }

// ---- drawing helpers
struct Ui { float w, h, s; };
float g_mx, g_my; bool g_cl, g_cr, g_cm; int g_wheel; bool g_keyEsc, g_keyTab, g_keyUp, g_keyDown, g_keyEnter;
bool Hit(float x, float y, float w, float h) { return g_mx >= x && g_mx < x + w && g_my >= y && g_my < y + h; }
void Pane(float x, float y, float w, float h, bool inset = false) {
    UiRect(x, y, w, h, inset ? uitheme::Inset : uitheme::Surface); UiFrame(x, y, w, h, uitheme::Border);
    UiRect(x + 1, y + 1, w - 2, 2, Rgba(uitheme::Accent.r, uitheme::Accent.g, uitheme::Accent.b, 0.30f));
}
bool Button(float x, float y, float w, float h, const char* label, bool on = false, float sc = 1) {
    UiCheckTarget(w, h, sc);
    bool hov = Hit(x, y, w, h);
    UiRect(x, y, w, h, on ? uitheme::Selected : hov ? uitheme::Hover : uitheme::Raised);
    UiFrame(x, y, w, h, on || hov ? uitheme::Accent : uitheme::Border, on ? 2 : 1);
    if (on) UiRect(x + 1, y + 1, 3, h - 2, uitheme::Accent);
    float fit = uitext::ContainedScale(w - 16 * sc, UiTextWidth(label, sc), sc, sc * 0.7f);
    float tw = UiTextWidth(label, fit), th = UiLineHeight(fit); UiText(x + (w - tw) / 2, y + (h - th) / 2, label, uitheme::Text, fit);
    return hov && g_cl;
}
std::string Wrap(const char* t, float maxw, float sc) {
    return uitext::Wrap(t, maxw, [&](const char* s) { return UiTextWidth(s, sc); });
}
const char* YesNo(bool b) { return b ? "yes" : "no"; }
UiColor PanelColor(const Pn& p) { return p.solved ? uitheme::Good : p.powered ? uitheme::Accent : uitheme::Muted; }

void SelectPanel(int i) { g_selKind = SelPanel; g_selPanel = i; }

// ---- panels of the "what is connected" view: downstream and upstream chains over panel targets
void Chain(float x, float& y, float w, const char* title, bool down, int start, float sc) {      // the title and the panels of the chain share one row, which wraps at the pane's width
    const bool old = GetCfg().noDetailFit; if (old) w = 520;
    UiCheckReachable(x, y, UiTextWidth(title, sc), UiLineHeight(sc) + 4); UiText(x, y + (old ? 0 : 2), title, uitheme::Cool, sc); if (old) y += UiLineHeight(sc);
    int cur = start; std::vector<int> seen = {cur}; float px = x + (old ? 10 : UiTextWidth(title, sc) + 10);
    for (int depth = 0; depth < 6; depth++) {
        int next = -1;
        if (down) { if (g_pn[cur].target >= 0) { auto it = g_pidx.find((unsigned)g_pn[cur].target); if (it != g_pidx.end()) next = it->second; } }
        else { auto it = g_rev.find((int)g_pn[cur].id); if (it != g_rev.end() && !it->second.empty()) next = it->second[0]; }
        if (next < 0 || std::find(seen.begin(), seen.end(), next) != seen.end()) break;
        char b[48]; snprintf(b, sizeof b, "%#x%s", g_pn[next].id, g_pn[next].solved ? " ok" : ""); float bw = UiTextWidth(b, sc) + 12;
        if (px + bw > x + w) break;
        UiCheckReachable(px, y, bw, UiLineHeight(sc) + 4);
        if (Button(px, y, bw, UiLineHeight(sc) + 4, b, false, sc)) SelectPanel(next);
        px += bw + 6; cur = next; seen.push_back(next);
    }
    y += UiLineHeight(sc) + 10;
}

void Refresh();
std::vector<int> g_solveQ; size_t g_solveTotal;
void QueueSolveSpots(int n) {      // complete the first n spots: solve every panel in their map cells (a few per frame, with its target)
    if (!g_built) Build(); g_solveQ.clear();
    for (int k = 0; k < n && k < (int)g_spots.size(); k++) for (size_t i = 0; i < g_pn.size(); i++) if ((int)floorf(g_pn[i].x / 80) == g_spots[k].cx && (int)floorf(g_pn[i].y / 80) == g_spots[k].cy && !g_pn[i].solved) g_solveQ.push_back((int)i);
    g_solveTotal = g_solveQ.size(); Msg("solving %zu panels in the first %d spots", g_solveTotal, n);
}
std::set<unsigned> g_forced;      // panels the dev tools solved (the game's own save has no solution line for them)
std::string StateWrite(bool forcedOnly) { Refresh(); std::string t; char b[40]; for (auto& p : g_pn) if (p.solved && (!forcedOnly || g_forced.count(p.id))) { snprintf(b, sizeof b, "S %x\n", p.id); t += b; } return t; }
void StateApply(const std::string& text) {      // solve every panel the list names that is not solved here yet (a few per frame)
    if (!g_built) Build(); Refresh(); unsigned id; size_t pos = 0, n = 0; g_solveQ.clear();
    while (pos < text.size()) { size_t e = text.find('\n', pos); if (e == std::string::npos) e = text.size(); if (sscanf(text.c_str() + pos, "S %x", &id) == 1) { auto it = g_pidx.find(id); if (it != g_pidx.end() && !g_pn[it->second].solved) { g_solveQ.push_back(it->second); n++; } } pos = e + 1; }
    std::reverse(g_solveQ.begin(), g_solveQ.end()); g_solveTotal = n; Msg("applying a progress list: %zu panels to solve", n);
}
std::string ProgressNote() { Refresh(); int s = 0, o = 0; for (auto& p : g_pn) s += p.solved; for (auto& d : g_dr) o += d.open; char b[100]; snprintf(b, sizeof b, "%d of %zu panels solved, %d of %zu doors open", s, g_pn.size(), o, g_dr.size()); return b; }
// ---- a new game with parts of the map completed (progress/progress.h): the launcher arms start_progress=<spec> in the cfg; once the world is up this solves what the spec
// names, writes the list next to the saves (coop_progress.txt) and disarms the setting. The game's own save has no solution line for forced panels, so a later session that
// loads a campaign puts the list back (only when the loaded world already has solved panels).
std::vector<unsigned> g_doorQ; DWORD g_doorAt;      // doors that should be open once the panels are solved (a few tutorial doors are opened by the game's own script, not by a panel's target)
void QueueDoors(const std::vector<unsigned>& ids) {
    std::set<unsigned> have(ids.begin(), ids.end()); g_doorQ.clear();
    for (const auto& d : wprog::Builtin().doors) for (const auto& alt : d.opensWith) { bool all = true; for (unsigned p : alt) if (!have.count(p)) { all = false; break; } if (all) { g_doorQ.push_back(d.id); break; } }
    g_doorAt = 0;
}
std::string ProgressText(const std::vector<unsigned>& ids) { std::string t; char b[24]; for (unsigned id : ids) { snprintf(b, sizeof b, "S %x\n", id); t += b; } return t; }
std::wstring ProgressSidecar() { std::wstring d = SaveFolder(); return d.empty() ? L"" : d + L"\\coop_progress.txt"; }
std::string ReadAll(const std::wstring& p) { std::string t; FILE* f = p.empty() ? nullptr : _wfopen(p.c_str(), L"rb"); if (f) { char b[4096]; size_t k; while ((k = fread(b, 1, sizeof b, f)) > 0) t.append(b, k); fclose(f); } return t; }
void DisarmStartProgress() {      // witness_coop.cfg: the start_progress line becomes a comment, so the next start is an ordinary one
    wchar_t p[MAX_PATH]; GetModuleFileNameW(g_selfModule(), p, MAX_PATH); wchar_t* s = wcsrchr(p, L'\\'); if (s) *(s + 1) = 0; std::wstring path = std::wstring(p) + L"witness_coop.cfg";
    std::string t = ReadAll(path), out; size_t pos = 0; bool changed = false;
    while (pos < t.size()) { size_t e = t.find('\n', pos); if (e == std::string::npos) e = t.size() - 1; std::string line = t.substr(pos, e - pos + 1); if (!line.compare(0, 15, "start_progress=")) { line = "# applied: " + line; changed = true; } out += line; pos = e + 1; }
    if (changed) { FILE* f = _wfopen(path.c_str(), L"wb"); if (f) { fwrite(out.data(), 1, out.size(), f); fclose(f); } }
}
}      // (the namespace of the rest of this file is closed for the exported function below and opened again)
bool DevMenuPanelPoints(std::vector<float>* xyz) {
    if (!g_built) { static DWORD next; DWORD now = GetTickCount(); if (now < next) return false; next = now + 2000; Build(); if (g_pn.size() < 300) { g_built = false; return false; } }
    xyz->clear(); for (const auto& p : g_pn) { xyz->push_back(p.x); xyz->push_back(p.y); xyz->push_back(p.z); } return true;
}
namespace {
unsigned SolvedCount() { unsigned n = 0; for (auto& p : g_pn) n += p.solved; return n; }
void StartProgressTick() {
    static bool done; static DWORD nextTry, stableSince; static uintptr_t firstPanel;
    const Cfg& c = GetCfg(); if (done) return;
    bool armed = !c.startProgress.empty() && !c.noStartProgress;
    DWORD now = GetTickCount(); if (now < nextTry) return; nextTry = now + 500;
    if (!g_built) { Build(); if (!g_built || g_pn.size() < 300) { g_built = false; stableSince = 0; return; } }
    Refresh(); uintptr_t fp = GameEntity(0x64);
    if (!stableSince || fp != firstPanel) { firstPanel = fp; stableSince = now; return; }      // the same world for a while (a title screen may build one that a new game then replaces)
    if (now - stableSince < 6000) return;
    done = true;
    if (armed) {
        std::string err; auto ids = wprog::Resolve(wprog::Builtin(), c.startProgress, &err, !c.progressNoPrereq);
        if (ids.empty()) { Log(("start progress: nothing to do: " + (err.empty() ? std::string("empty") : err)).c_str()); DisarmStartProgress(); return; }
        std::string text = ProgressText(ids); StateApply(text); QueueDoors(ids); for (unsigned id : ids) g_forced.insert(id);
        std::wstring sc = ProgressSidecar(); if (!sc.empty()) { FILE* f = _wfopen(sc.c_str(), L"wb"); if (f) { fwrite(text.data(), 1, text.size(), f); fclose(f); } }
        char m[200]; snprintf(m, sizeof m, "start progress: '%s' -> %zu panels to solve", c.startProgress.c_str(), ids.size()); Log(m); DisarmStartProgress();
    } else if (SolvedCount() > 0) {      // a loaded campaign: put back what a new game with progress forced
        std::string text = ReadAll(ProgressSidecar()); if (!text.empty()) { StateApply(text); char m[100]; snprintf(m, sizeof m, "start progress: loaded campaign, putting back the forced list"); Log(m); }
    }
}
bool Snapshot(std::set<unsigned>* solved, unsigned* doorsOpen, unsigned* doorsTotal) {      // the census: solved panels, doors open and in all (savemeta_game.cpp)
    if (!g_built) Build(); if (!g_built) return false; Refresh(); for (auto& p : g_pn) if (p.solved) solved->insert(p.id); *doorsOpen = 0; for (auto& d : g_dr) *doorsOpen += d.open; *doorsTotal = (unsigned)g_dr.size(); return true;
}
void ProgressCommand(const char* spec) {      // test command: progress <spec>
    if (!g_built) Build(); std::string err; auto ids = wprog::Resolve(wprog::Builtin(), spec, &err, !GetCfg().progressNoPrereq);
    if (ids.empty()) { Msg("progress: %s", err.empty() ? "nothing" : err.c_str()); Log(("progress: error " + err).c_str()); return; }
    StateApply(ProgressText(ids)); QueueDoors(ids); for (unsigned id : ids) g_forced.insert(id); char m[100]; snprintf(m, sizeof m, "progress: %zu panels asked for", ids.size()); Log(m);
}
void ProgressStat() {      // test command: every solved panel (hex ids, 40 per line) and the state of the doors the data knows
    if (!g_built) Build(); Refresh(); std::vector<unsigned> ids; for (auto& p : g_pn) if (p.solved) ids.push_back(p.id); std::sort(ids.begin(), ids.end());
    char m[600]; snprintf(m, sizeof m, "progressstat: panels %zu solved %zu queued %zu", g_pn.size(), ids.size(), g_solveQ.size()); Log(m);
    for (size_t i = 0; i < ids.size(); i += 40) { std::string l = "progressstat: ids"; char b[16]; for (size_t k = i; k < ids.size() && k < i + 40; k++) { snprintf(b, sizeof b, " %x", ids[k]); l += b; } Log(l.c_str()); }
    std::string l = "progressstat: doors open"; char b[16]; for (auto& d : g_dr) if (d.open) { snprintf(b, sizeof b, " %x", d.id); l += b; } Log(l.c_str());
}
bool Do(const char* what, int pi) {      // one action on panel index pi, by name (the buttons and the 'menudo' test command)
    if (pi < 0 || pi >= (int)g_pn.size()) return false; Pn& p = g_pn[pi]; uintptr_t te = p.target >= 0 ? GameEntity(p.target) : 0; const char* tt = te ? TypeName(te) : "";
    if (!strcmp(what, "teleport")) TeleportToPanel(p);
    else if (!strcmp(what, "solve")) { g_forced.insert(p.id); GameMarkPanelSolved(p.id); GameDispatchTargetNow(p.id, true); Msg("solved panel %#x and ran its target", p.id); }
    else if (!strcmp(what, "power")) { GamePanelPowerOnNow(p.id); Msg("powered panel %#x", p.id); }
    else if (!strcmp(what, "run") && te) { GameDispatchTargetNow(p.id, true); Msg("ran the target of %#x", p.id); }
    else if (!strcmp(what, "door") && te && !strcmp(tt, "Door")) { GameDoorOpenNow((unsigned)p.target, 1.0f); Msg("opened door %#x", (unsigned)p.target); }
    else if (!strcmp(what, "cableon") && te && !strcmp(tt, "Power_Cable")) { GameCablePowerOnNow((unsigned)p.target); Msg("lit cable %#x", (unsigned)p.target); }
    else if (!strcmp(what, "cableoff") && te && !strcmp(tt, "Power_Cable")) { GameCableOffNow((unsigned)p.target); Msg("cable %#x switched off", (unsigned)p.target); }
    else return false;
    return true;
}
float g_detailNeed = 0;      // height the detail pane's content needed on the last frame, so the map page can give the pane that much
void DetailPane(float x, float y, float w, float sc) {
    float lh = UiLineHeight(sc); char b[256]; const float top = y;
    if (g_selKind == SelPanel && g_selPanel >= 0 && g_selPanel < (int)g_pn.size()) {
        Pn& p = g_pn[g_selPanel]; uintptr_t te = p.target >= 0 ? GameEntity(p.target) : 0; const char* tt = te ? TypeName(te) : "nothing";
        snprintf(b, sizeof b, "Panel %#x   %u nodes", p.id, p.nodes); UiText(x, y, b, uitheme::Text, sc * 1.15f); y += lh * 1.3f;
        snprintf(b, sizeof b, "solved: %s    powered: %s\nat %.1f %.1f %.1f", YesNo(p.solved), YesNo(p.powered), p.x, p.y, p.z); UiText(x, y, b, uitheme::Muted, sc); y += lh * 2.3f;
        if (te) {
            bool lit = !strcmp(tt, "Power_Cable") && One(te + 0xC4) && One(te + 0xC8), open = !strcmp(tt, "Door") && One(te + 0x1D0);
            snprintf(b, sizeof b, "Targets: %s %#x%s", tt, (unsigned)p.target, lit ? " (lit)" : open ? " (open)" : ""); UiText(x, y, b, uitheme::Accent, sc); y += lh * 1.3f;
        } else { UiText(x, y, "Targets: nothing", uitheme::Muted, sc); y += lh * 1.3f; }
        float bx = x, bh = lh + 12;
        auto act = [&](const char* label) { float bw2 = UiTextWidth(label, sc) + 24; if (bx > x && bx + bw2 > x + w) { bx = x; y += bh + 8; } UiCheckReachable(bx, y, bw2, bh); bool r = Button(bx, y, bw2, bh, label, false, sc); bx += bw2 + 8; return r; };
        if (act("Teleport to it")) Do("teleport", g_selPanel);
        if (act("Solve (and run target)")) Do("solve", g_selPanel);
        if (!p.powered && act("Power on")) Do("power", g_selPanel);
        if (te && act("Run its target")) Do("run", g_selPanel);
        if (te && !strcmp(tt, "Door") && act("Open target door")) Do("door", g_selPanel);
        if (te && !strcmp(tt, "Power_Cable") && act("Light cable")) Do("cableon", g_selPanel);
        if (te && !strcmp(tt, "Power_Cable") && act("Cable off")) Do("cableoff", g_selPanel);
        if (te && p.target >= 0 && g_pidx.count((unsigned)p.target) && act("Select target panel")) SelectPanel(g_pidx[(unsigned)p.target]);
        y += bh + 12;
        auto rv = g_rev.find((int)p.id); size_t nrev = rv == g_rev.end() ? 0 : rv->second.size();
        const bool old = GetCfg().noDetailFit;
        if (old) snprintf(b, sizeof b, "Targeted by %zu panel(s)", nrev); else snprintf(b, sizeof b, "Targeted by %zu:", nrev);
        UiCheckReachable(x, y, UiTextWidth(b, sc), lh + 4); UiText(x, y + (old ? 0 : 2), b, uitheme::Cool, sc);
        float px = x + (old ? 0 : UiTextWidth(b, sc) + 10); if (old) y += lh * 1.2f; for (size_t k = 0; k < nrev && k < 8; k++) { int i = rv->second[k]; snprintf(b, sizeof b, "%#x", g_pn[i].id); float bw = UiTextWidth(b, sc) + 12; if (!old && px + bw > x + w) { px = x; y += lh + 8; } UiCheckReachable(px, y, bw, lh + 4); if (Button(px, y, bw, lh + 4, b, false, sc)) SelectPanel(i); px += bw + 6; }
        y += lh + 12;
        Chain(x, y, w, "Powers on from here:", true, g_selPanel, sc); Chain(x, y, w, "Fed by:", false, g_selPanel, sc);
    } else if (g_selKind == SelPoint) {
        snprintf(b, sizeof b, "Point %.0f %.0f", g_selX, g_selY); UiText(x, y, b, uitheme::Text, sc * 1.15f); y += lh * 1.5f;
        float z; bool ok = GroundNear(g_selX, g_selY, &z);
        UiText(x, y, Wrap(ok ? "Ground height is taken from the nearest panel; you drop in from 6 m above it." : "No panel within 30 m: no known ground here.", w, sc).c_str(), uitheme::Muted, sc); y += lh * 3.2f;
        if (ok && Button(x, y, 170 * sc, lh + 12, "Teleport here", false, sc)) TeleportToPoint(g_selX, g_selY);
    } else if (g_selKind == SelPlayer) {
        RemotePose rp; bool have = CoopGetRemote(&rp, (unsigned)g_selPlayer);
        snprintf(b, sizeof b, "Player %d", g_selPlayer); UiText(x, y, b, uitheme::Accent, sc * 1.15f); y += lh * 1.5f;
        if (have) { snprintf(b, sizeof b, "at %.1f %.1f %.1f\nseen %lu ms ago", rp.pos[0], rp.pos[1], rp.pos[2], (unsigned long)rp.ageMs); UiText(x, y, b, uitheme::Muted, sc); y += lh * 2.5f; if (Button(x, y, 200 * sc, lh + 12, "Teleport to them", false, sc)) TeleportToPlayer(rp); }
        else UiText(x, y, "Not connected.", uitheme::Danger, sc);
    } else { UiText(x, y, Wrap("Click a dot on the map, a spot in the list, or a panel in the nearby list.", w, sc).c_str(), uitheme::Muted, sc); y += lh * 2.5f; }
    g_detailNeed = y - top;
}

void MapPage(float x, float y, float w, float h, float sc) {
    Pane(x, y, w, h, true);
    if (!g_viewInit && g_built) { g_vx = (g_minx + g_maxx) / 2; g_vy = (g_miny + g_maxy) / 2; g_vscale = std::min(w / (g_maxx - g_minx + 40), h / (g_maxy - g_miny + 40)); g_viewInit = true; }
    auto sx = [&](float wx) { return x + w / 2 + (wx - g_vx) * g_vscale; }; auto sy = [&](float wy) { return y + h / 2 - (wy - g_vy) * g_vscale; };
    bool over = Hit(x, y, w, h);
    if (over && g_wheel) {     // zoom about the cursor
        float wx = g_vx + (g_mx - x - w / 2) / g_vscale, wy = g_vy - (g_my - y - h / 2) / g_vscale;
        g_vscale *= powf(1.25f, (float)g_wheel); g_vscale = std::max(0.3f, std::min(g_vscale, 40.f));
        g_vx = wx - (g_mx - x - w / 2) / g_vscale; g_vy = wy + (g_my - y - h / 2) / g_vscale;
    }
    static float lastMx, lastMy; if (over && g_rdown) { g_vx -= (g_mx - lastMx) / g_vscale; g_vy += (g_my - lastMy) / g_vscale; } lastMx = g_mx; lastMy = g_my;
    UiClip(x + 1, y + 1, w - 2, h - 2);
    { static unsigned serial = ~0u; static ID3D11ShaderResourceView* tex; static float b[4]; static bool have; static unsigned drawn;
      if (!GetCfg().noUnderlay) {
        if (serial != MapGenSerial() || (!have && serial == ~0u)) { int uw, uh; have = MapUnderlayInfo(&b[0], &b[1], &b[2], &b[3], &uw, &uh); serial = MapGenSerial(); if (tex) { tex->Release(); tex = nullptr; } if (have) tex = UiMakeTexture(MapUnderlayPixels(), uw, uh); if (tex) Log("devmenu: map underlay loaded"); }
        if (tex) { UiImage(sx(b[0]), sy(b[3]), (b[2] - b[0]) * g_vscale, (b[3] - b[1]) * g_vscale, tex, Rgba(1, 1, 1, 0.92f)); ++drawn; }
      }
      g_underlayDrawn = drawn; g_underlayLoaded = tex != nullptr; }
    for (float gx = floorf((g_vx - w / 2 / g_vscale) / 50) * 50; gx < g_vx + w / 2 / g_vscale; gx += 50) UiLine(sx(gx), y, sx(gx), y + h, Rgba(1, 1, 1, 0.05f));
    for (float gy = floorf((g_vy - h / 2 / g_vscale) / 50) * 50; gy < g_vy + h / 2 / g_vscale; gy += 50) UiLine(x, sy(gy), x + w, sy(gy), Rgba(1, 1, 1, 0.05f));
    for (auto& d : g_dr) UiFrame(sx(d.x) - 2, sy(d.y) - 2, 5, 5, d.open ? Rgba(0.3f, 0.8f, 1, 0.9f) : Rgba(0.9f, 0.9f, 0.9f, 0.5f));
    float ps = std::max(3.f, std::min(8.f, g_vscale * 1.5f)); int hover = -1; float hd = 9;
    for (size_t i = 0; i < g_pn.size(); i++) {
        float px = sx(g_pn[i].x), py = sy(g_pn[i].y); UiRect(px - ps / 2, py - ps / 2, ps, ps, PanelColor(g_pn[i]));
        float d = hypotf(px - g_mx, py - g_my); if (over && d < hd) { hd = d; hover = (int)i; }
    }
    if (g_selKind == SelPanel && g_selPanel >= 0) {       // the selected panel, what it targets and what targets it
        Pn& p = g_pn[g_selPanel]; UiFrame(sx(p.x) - 7, sy(p.y) - 7, 15, 15, Rgba(1, 1, 0.3f), 2);
        if (p.target >= 0) { auto it = g_pidx.find((unsigned)p.target); if (it != g_pidx.end()) UiLine(sx(p.x), sy(p.y), sx(g_pn[it->second].x), sy(g_pn[it->second].y), Rgba(1, 0.9f, 0.3f, 0.9f), 2); }
        auto rv = g_rev.find((int)p.id); if (rv != g_rev.end()) for (int i : rv->second) UiLine(sx(g_pn[i].x), sy(g_pn[i].y), sx(p.x), sy(p.y), Rgba(0.4f, 0.8f, 1, 0.9f), 2);
    }
    if (g_selKind == SelPoint) { UiLine(sx(g_selX) - 9, sy(g_selY), sx(g_selX) + 9, sy(g_selY), Rgba(1, 0.3f, 0.3f), 2); UiLine(sx(g_selX), sy(g_selY) - 9, sx(g_selX), sy(g_selY) + 9, Rgba(1, 0.3f, 0.3f), 2); }
    float pos[3], ang[2];
    if (Camera(pos, ang)) { UiTri(sx(pos[0]), sy(pos[1]), 11, -ang[0], Rgba(0.2f, 0.9f, 1)); UiText(sx(pos[0]) + 10, sy(pos[1]) - 10, "you", Rgba(0.2f, 0.9f, 1), sc); }
    int hoveredPlayer = -1;
    for (const auto& rp : CoopGetRemotes()) if (rp.ageMs < 5000) {
        float rpx = sx(rp.pos[0]), rpy = sy(rp.pos[1]); char label[40]; snprintf(label, sizeof label, "player %u", rp.id);
        UiTri(rpx, rpy, 11, -rp.ang[0], Rgba(1, 0.55f, 0.1f)); UiText(rpx + 10, rpy + 4, label, Rgba(1, 0.55f, 0.1f), sc);
        if (hypotf(rpx - g_mx, rpy - g_my) < 12) hoveredPlayer = (int)rp.id;
    }
    UiClip(0, 0, 0, 0);
    bool onBtn = false;
    {      // the map picture: made from the game's own frames by flying the player over the island (a few minutes); the button starts it
        bool have = g_underlayLoaded; const char* label = have ? "Make the island picture again" : "Make a picture of the island under the map"; float bw = UiTextWidth(label, sc) + 16, bh = UiLineHeight(sc) + 10, bx = x + w - bw - 8, by = y + 8; bool hov = Hit(bx, by, bw, bh); onBtn = hov;
        UiRect(bx, by, bw, bh, hov ? uitheme::Hover : uitheme::Raised); UiFrame(bx, by, bw, bh, hov ? uitheme::Accent : uitheme::Border); UiText(bx + 8, by + 5, label, uitheme::Text, sc);
        if (hov && g_cl) { MapGenCommand("start"); g_open = false; Msg("flying over the island to take its picture: the menu comes back when the map has one"); }
    }
    if (over && g_cl && !onBtn) {
        if (hoveredPlayer >= 0) { g_selKind = SelPlayer; g_selPlayer = hoveredPlayer; }
        else if (hover >= 0) SelectPanel(hover);
        else { g_selKind = SelPoint; g_selX = g_vx + (g_mx - x - w / 2) / g_vscale; g_selY = g_vy - (g_my - y - h / 2) / g_vscale; }
    }
    if (over && hover >= 0) { char b[64]; snprintf(b, sizeof b, "%#x  %s", g_pn[hover].id, g_pn[hover].solved ? "solved" : g_pn[hover].powered ? "powered" : "off"); float tw = UiTextWidth(b, sc) + 10; UiRect(g_mx + 14, g_my + 8, tw, UiLineHeight(sc) + 4, Rgba(0, 0, 0, 0.85f)); UiText(g_mx + 19, g_my + 10, b, Rgba(1, 1, 1), sc); }
    UiText(x + 8, y + h - UiLineHeight(sc * .7f) - 6, "Wheel zoom / right-drag pan / click select", uitheme::Muted, sc * .7f);
}

void SpotList(float x, float y, float w, float h, float sc) {
    Pane(x, y, w, h);
    float rh = UiLineHeight(sc) + 6; bool over = Hit(x, y, w, h);
    if (over && g_wheel) g_spotScroll -= g_wheel * rh * 2; g_spotScroll = std::max(0.f, std::min(g_spotScroll, std::max(0.f, rh * g_spots.size() - h)));
    UiClip(x + 1, y + 1, w - 2, h - 2);
    for (size_t i = 0; i < g_spots.size(); i++) {
        float ry = y + i * rh - g_spotScroll; if (ry + rh < y || ry > y + h) continue; Pn& p = g_pn[g_spots[i].pn];
        int solved = 0; for (auto& q : g_pn) if (q.solved && hypotf(q.x - p.x, q.y - p.y) < 56) solved++;
        char b[80]; snprintf(b, sizeof b, "%2zu  panel %#x  (%d panels)", i + 1, p.id, g_spots[i].count);
        bool sel = g_selKind == SelPanel && g_selPanel == g_spots[i].pn;
        bool hov = Hit(x, ry, w, rh);
        if (hov && g_cl) { SelectPanel(g_spots[i].pn); g_vx = p.x; g_vy = p.y; }
        UiRect(x + 2, ry + 1, w - 4, rh - 2, sel ? uitheme::Selected : hov ? uitheme::Hover : Rgba(0, 0, 0, 0));
        if (sel) UiRect(x + 2, ry + 1, 3, rh - 2, uitheme::Accent);
        UiText(x + 8, ry + 3, b, p.solved ? uitheme::Good : p.powered ? uitheme::Accent : uitheme::Muted, sc);
        (void)solved;
    }
    UiClip(0, 0, 0, 0);
}

void NearbyPage(float x, float y, float w, float h, float sc) {
    Pane(x, y, w, h);
    float pos[3], ang[2]; if (!Camera(pos, ang)) { UiText(x + 10, y + 10, "no player position yet", uitheme::Danger, sc); return; }
    struct R { int i; float d; }; std::vector<R> rows;
    for (size_t i = 0; i < g_pn.size(); i++) { float d = hypotf(g_pn[i].x - pos[0], g_pn[i].y - pos[1]); if (d <= g_radius && fabsf(g_pn[i].z - pos[2]) < 40) rows.push_back({(int)i, d}); }
    std::sort(rows.begin(), rows.end(), [](const R& a, const R& b) { return a.d < b.d; });
    char b[120]; snprintf(b, sizeof b, "%zu puzzles within %.0f m", rows.size(), g_radius); UiText(x + 8, y + 6, b, uitheme::Text, sc);
    if (Button(x + w - 72 * sc, y + 4 * sc, 28 * sc, UiLineHeight(sc) + 4 * sc, "-", false, sc)) g_radius = std::max(10.f, g_radius - 10);
    if (Button(x + w - 38 * sc, y + 4 * sc, 28 * sc, UiLineHeight(sc) + 4 * sc, "+", false, sc)) g_radius = std::min(300.f, g_radius + 10);
    float ly = y + UiLineHeight(sc) + 14, rh = UiLineHeight(sc) + 6, lh = h - (ly - y) - 4;
    if (Hit(x, ly, w, lh) && g_wheel) g_nearScroll -= g_wheel * rh * 2; g_nearScroll = std::max(0.f, std::min(g_nearScroll, std::max(0.f, rh * rows.size() - lh)));
    UiClip(x + 1, ly, w - 2, lh);
    for (size_t k = 0; k < rows.size(); k++) {
        float ry = ly + k * rh - g_nearScroll; if (ry + rh < ly || ry > ly + lh) continue; Pn& p = g_pn[rows[k].i];
        bool sel = g_selKind == SelPanel && g_selPanel == rows[k].i;
        bool hov = Hit(x, ry, w, rh);
        if (hov && g_cl && Hit(x, ly, w, lh)) SelectPanel(rows[k].i);
        UiRect(x + 2, ry + 1, w - 4, rh - 2, sel ? uitheme::Selected : hov ? uitheme::Hover : Rgba(0, 0, 0, 0));
        if (sel) UiRect(x + 2, ry + 1, 3, rh - 2, uitheme::Accent);
        snprintf(b, sizeof b, "%#8x %5.1fm %-3s %-3s -> %s", p.id, rows[k].d, p.solved ? "S" : "", p.powered ? "P" : "", p.ttype); UiText(x + 8, ry + 3, b, PanelColor(p), sc);
    }
    UiClip(0, 0, 0, 0);
}
void SavesPage(float x, float y, float w, float h, float sc) {
    Pane(x, y, w, h);
    float lh = UiLineHeight(sc), bh = lh + 14, bx = x + 12, by = y + 12; static int nSpots = 4; static char nm[64];
    auto act = [&](const char* label) { float bw = UiTextWidth(label, sc) + 26 * sc; bool r = Button(bx, by, bw, bh, label, false, sc); bx += bw + 10 * sc; return r; };
    SYSTEMTIME st; GetLocalTime(&st); snprintf(nm, sizeof nm, "%02d%02d%02d-%02d%02d%02d", st.wYear % 100, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    if (act("Save point now") && !SaveBusy()) { SaveStartSave(std::string("save-") + nm, false, ProgressNote()); g_open = false; }
    if (act("Preset from current progress (list only)") && !SaveBusy()) { SaveStartPreset(std::string("preset-") + nm, ProgressNote()); }
    UiText(x + 12, by + bh + 8, Wrap(SaveBusy() ? "working... (the pause menu opens and closes by itself; do not touch anything)" : (SaveStatus()[0] ? SaveStatus() : "A save point is a copy of the game's save; a restore loads it through the pause menu. Presets are kept for starting from a given amount of progress."), w - 24, sc * 0.9f).c_str(), SaveBusy() ? uitheme::Accent : uitheme::Muted, sc * 0.9f);
    float ty = by + bh + 8 + lh * 2.2f; char b[120]; snprintf(b, sizeof b, "Make progress for a preset: complete the first %d of %zu spots (all their panels)", nSpots, g_spots.size()); UiText(x + 12, ty, b, uitheme::Text, sc);
    bx = x + 12; by = ty + lh + 6;
    if (act("-")) nSpots = std::max(1, nSpots - 1); if (act("+")) nSpots = std::min((int)g_spots.size(), nSpots + 1); if (act("Solve them now")) { QueueSolveSpots(nSpots); g_open = false; }
    if (!g_solveQ.empty()) { snprintf(b, sizeof b, "%zu panels left to solve", g_solveQ.size()); UiText(bx + 20, by + 6, b, uitheme::Accent, sc); }
    float ly = by + bh + 16; UiText(x + 12, ly, "Saved:", uitheme::Accent, sc); ly += lh + 4; float rh = bh;
    auto& L = SaveList(); if (L.empty()) UiText(x + 12, ly, "nothing yet", uitheme::Muted, sc);
    for (size_t i = 0; i < L.size() && ly + rh < y + h; i++) {
        bool hov = Hit(x + 8, ly, w - 16, rh - 2);
        UiRect(x + 8, ly, w - 16, rh - 2, hov ? uitheme::Hover : uitheme::Raised);
        UiFrame(x + 8, ly, w - 16, rh - 2, uitheme::Border);
        snprintf(b, sizeof b, "%s  %s", L[i].preset ? "[preset]" : "[save]  ", L[i].name.c_str());
        UiClip(x + 16, ly, std::max(1.f, w - 360 * sc), rh);
        UiText(x + 16, ly + 7, b, L[i].preset ? uitheme::Accent : uitheme::Text, sc); UiClip(0, 0, 0, 0);
        float nameW = UiTextWidth(b, sc); float noteX = x + 16 + std::max(220.f * sc, nameW + 16 * sc);
        float noteRight = x + w - 340 * sc;
        if (noteRight > noteX) { UiClip(noteX, ly, noteRight - noteX, rh); UiText(noteX, ly + 7, L[i].note.c_str(), uitheme::Muted, sc * 0.9f); UiClip(0, 0, 0, 0); }
        const char* lab = L[i].hasGameSave ? "Restore (load save + progress)" : "Apply progress list"; float rw = UiTextWidth(lab, sc) + 26; if (Button(x + w - rw - 14, ly + 1, rw, rh - 4, lab, false, sc) && !SaveBusy()) { SaveStartRestore(L[i]); g_open = false; }
        ly += rh;
    }
}
struct Cmd { std::string what; unsigned id; };
std::vector<Cmd> g_cmds;
}  // namespace

void DevMenuCommand(const char* what, unsigned id) { Init0(); EnterCriticalSection(&g_cs); g_cmds.push_back({what, id}); LeaveCriticalSection(&g_cs); }

bool DevMenuMapInfo(float* minx, float* miny, float* maxx, float* maxy) {      // the census' rectangle (trimmed like the map's view), for the map picture
    if (!g_built) Build(); if (!g_built) return false; *minx = g_minx; *miny = g_miny; *maxx = g_maxx; *maxy = g_maxy; return true;
}
bool DevMenuGroundNear(float x, float y, float maxDist, float* z) {      // the ground under the nearest panel (a panel is about 1.4 m above it)
    if (!g_built) return false; int best = -1; float bd = maxDist;
    for (size_t i = 0; i < g_pn.size(); i++) { float d = hypotf(g_pn[i].x - x, g_pn[i].y - y); if (d < bd) { bd = d; best = (int)i; } }
    if (best < 0) return false; *z = g_pn[best].z - 1.4f; return true;
}
bool DevMenuEnabled() { return GetCfg().devMenu; }
bool DevMenuSnapshot(std::set<unsigned>* solved, unsigned* doorsOpen, unsigned* doorsTotal) { return Snapshot(solved, doorsOpen, doorsTotal); }
void ProcessCmds() {
    Init0();
    std::vector<Cmd> cmds; EnterCriticalSection(&g_cs); cmds.swap(g_cmds); LeaveCriticalSection(&g_cs);
    if (!cmds.empty() && !g_built) Build();
    for (auto& c : cmds) {      // test commands: 'select' picks a panel, anything else is an action on the selected panel (or panel c.id)
        if (c.what.rfind("save:", 0) == 0) { SaveStartSave(c.what.substr(5), false, ProgressNote()); continue; }
        if (c.what.rfind("preset:", 0) == 0) { SaveStartPreset(c.what.substr(7), ProgressNote()); continue; }
        if (c.what.rfind("restore:", 0) == 0) { SaveListRefresh(); std::string n = c.what.substr(8); bool found = false; for (auto& e : SaveList()) if (e.name == n) { SaveStartRestore(e); found = true; break; } if (!found) Msg("no save called '%s'", n.c_str()); continue; }
        if (c.what.rfind("progress:", 0) == 0) { ProgressCommand(c.what.substr(9).c_str()); continue; }
        if (c.what == "progressstat") { ProgressStat(); continue; }
        if (c.what == "mapstat") { char m[120]; snprintf(m, sizeof m, "mapstat: underlay loaded %d drawn %u page %d open %d", g_underlayLoaded ? 1 : 0, g_underlayDrawn, g_page, g_open ? 1 : 0); Log(m); continue; }
        if (c.what == "complete") { QueueSolveSpots((int)c.id); continue; }
        auto it = g_pidx.find(c.id);
        if (c.what == "select") { if (it != g_pidx.end()) { SelectPanel(it->second); g_vx = g_pn[it->second].x; g_vy = g_pn[it->second].y; Msg("selected panel %#x", c.id); } else Msg("no such panel %#x", c.id); }
        else if (!Do(c.what.c_str(), g_selKind == SelPanel ? g_selPanel : -1)) Msg("action '%s' not possible here", c.what.c_str());
    }
}
void DevMenuTick() {      // every frame, open or not: test commands, saves, the solve queue and the check of a pending teleport (retried when refused)
    { static bool hooked; if (!hooked) { SaveSetHooks(StateWrite, StateApply); hooked = true; } }
    ProcessCmds(); StartProgressTick(); SaveTick(GetTickCount());
    { static char last[200]; if (strcmp(last, SaveStatus())) { snprintf(last, sizeof last, "%s", SaveStatus()); if (last[0]) Msg("%s", last); } }
    for (int k = 0; k < 8 && !g_solveQ.empty(); k++) { int i = g_solveQ.back(); g_solveQ.pop_back(); g_forced.insert(g_pn[i].id); GameMarkPanelSolved(g_pn[i].id); GameDispatchTargetNow(g_pn[i].id, true); if (g_solveQ.empty()) Msg("solved the panels of the chosen spots"); }
    if (g_solveQ.empty() && !g_doorQ.empty()) {      // a moment after the last solve: open the doors the data says are open that the solves did not open
        if (!g_doorAt) g_doorAt = GetTickCount() + 1500;
        else if (GetTickCount() >= g_doorAt) { Refresh(); std::set<unsigned> openNow; for (auto& d : g_dr) if (d.open) openNow.insert(d.id); unsigned n = 0; for (unsigned id : g_doorQ) if (!openNow.count(id) && GameEntity(id)) { GameDoorOpenNow(id, 1.0f); ++n; } g_doorQ.clear(); char m[100]; snprintf(m, sizeof m, "progress: %u doors opened by hand", n); Log(m); }
    }
    if (!g_tp.on || GetTickCount() - g_tp.at < 1500) return;
    float pos[3], ang[2]; if (!Camera(pos, ang)) return;
    int k = g_tp.attempt;
    if (hypotf(pos[0] - (g_tp.px + kTryDist[k] * cosf(g_tp.rz)), pos[1] - (g_tp.py + kTryDist[k] * sinf(g_tp.rz))) < 2.5f) { g_tp.on = false; Msg("teleported in front of panel %#x", g_tp.id); return; }
    if (++g_tp.attempt >= (int)(sizeof kTryDist / sizeof kTryDist[0])) { g_tp.on = false; Msg("teleport to panel %#x refused by the game (it put you back at the spawn point): that place cannot be entered this way", g_tp.id); return; }
    FireTeleport();
}
bool DevMenuToastWanted() { return !g_open && g_msg[0] && (GetTickCount() - g_msgAt <= 6000 || SaveBusy()); }
void DevMenuToast(float W, float H) {      // the last message, for a few seconds after the menu closed
    if (g_open || !g_msg[0] || (GetTickCount() - g_msgAt > 6000 && !SaveBusy())) return;
    float sc = std::max(1.f, H / 1080.f); sc = uitext::ContainedScale(W - 48 * sc, UiTextWidth(g_msg, sc), sc, sc * 0.7f);
    float tw = UiTextWidth(g_msg, sc) + 24 * sc;
    UiRect(W / 2 - tw / 2, H - 84 * sc, tw, UiLineHeight(sc) + 14 * sc, uitheme::Surface); UiFrame(W / 2 - tw / 2, H - 84 * sc, tw, UiLineHeight(sc) + 14 * sc, uitheme::Border);
    UiText(W / 2 - tw / 2 + 12 * sc, H - 84 * sc + 7 * sc, g_msg, uitheme::Accent, sc);
}
bool DevMenuOpen() { return g_open; }
static void Init() { if (!g_csInit) { InitializeCriticalSection(&g_cs); g_csInit = true; } }
void DevMenuToggle() { Init(); g_open = !g_open; g_ldown = g_rdown = false; if (g_open) g_built = false; }      // rebuild on open: entities may have been recreated by a load
void DevMenuSetPage(int p) { Init(); g_page = p; if (p == 2) SaveListRefresh(); if (!g_open) { g_open = true; g_built = false; } }
#include "povui.h"
#include "social.h"
void PanelBoostToggle();      // input.cpp
void PanelBoostMsg(bool on, float gain) { Msg("panel mouse boost %s (x%.1f, F3 toggles)", on ? "ON" : "off", gain); }
bool DevMenuKey(unsigned vk, bool down) {
    if(down && (vk=='W' || vk=='A' || vk=='S' || vk=='D' || vk==VK_SPACE || vk==VK_ESCAPE)) TvSeatExit();
    if(down && vk==GetCfg().keyTvSeat && !DevMenuOpen() && !GamePaused() && !SocialCapturing()) { TvSeatRequest(); return true; }
    Init(); if (down && vk == guide::TvView) { TvEnable(!TvEnabled()); return true; }
    if (down && vk == guide::TvConsent) { TvAllow(!TvConsent()); return true; }
    if (down && vk == GetCfg().keyTvDeploy) { TvDeployAction(OverlayTvAim()); return true; }
    if (PovKey(vk, down)) return true;
    if (vk == guide::Boost && down) { PanelBoostToggle(); return true; }
    if (vk == guide::Video && down && VideoEnabled()) { VideoToggleView(); return true; }
    if (!DevMenuEnabled()) return false;
    if (vk == guide::DevMenu && down) { DevMenuToggle(); return true; }
    if (vk == guide::Nearby && down) { if (g_open && g_page == 1) DevMenuToggle(); else DevMenuSetPage(1); return true; }
    if (!g_open) return false;
    if (down) { EnterCriticalSection(&g_cs); g_ev.push_back({3, (int)vk, 0}); LeaveCriticalSection(&g_cs); }
    return down;          // key-ups still reach the game, so a key held when the menu opened is released there
}
bool DevMenuMouseButton(int button, bool down) {
    Init(); if (!g_open) return false;
    EnterCriticalSection(&g_cs); g_ev.push_back({down ? 0 : 1, button, 0}); LeaveCriticalSection(&g_cs);
    return down;
}
bool DevMenuWheel(int delta) { Init(); if (!g_open) return false; EnterCriticalSection(&g_cs); g_ev.push_back({2, delta, 0}); LeaveCriticalSection(&g_cs); return true; }
void DevMenuRawMove(int dx, int dy) { Init(); EnterCriticalSection(&g_cs); g_cx += (float)dx; g_cy += (float)dy; LeaveCriticalSection(&g_cs); }
void DevMenuSetCursor(float x, float y) { Init(); EnterCriticalSection(&g_cs); g_cx = x; g_cy = y; LeaveCriticalSection(&g_cs); }

void DevMenuDraw(float W, float H) {
    Init(); if (!g_open) return;
    if (!g_built) { Build(); g_lastRefresh = GetTickCount(); }
    else if (GetTickCount() - g_lastRefresh > 500) { Refresh(); g_lastRefresh = GetTickCount(); }
    std::vector<Ev> evs; EnterCriticalSection(&g_cs); evs.swap(g_ev); g_cx = std::max(0.f, std::min(g_cx, W - 1)); g_cy = std::max(0.f, std::min(g_cy, H - 1)); g_mx = g_cx; g_my = g_cy; LeaveCriticalSection(&g_cs);
    g_cl = g_cr = g_cm = false; g_wheel = 0; g_keyEsc = g_keyTab = g_keyUp = g_keyDown = g_keyEnter = false;
    for (auto& e : evs) {
        if (e.type == 0) { if (e.a == 0) { g_cl = true; g_ldown = true; } else if (e.a == 1) { g_cr = true; g_rdown = true; } else g_cm = true; }
        else if (e.type == 1) { if (e.a == 0) g_ldown = false; else if (e.a == 1) g_rdown = false; }
        else if (e.type == 2) g_wheel += e.a > 0 ? 1 : -1;
        else if (e.type == 3) { if (e.a == VK_ESCAPE) g_keyEsc = true; if (e.a == VK_TAB) g_keyTab = true; if (e.a == VK_UP) g_keyUp = true; if (e.a == VK_DOWN) g_keyDown = true; if (e.a == VK_RETURN) g_keyEnter = true; }
    }
    if (g_cl || g_cr) { char m[100]; snprintf(m, sizeof m, "devmenu: click %s at %.0f %.0f", g_cl ? "left" : "right", g_mx, g_my); Log(m); }
    if (g_keyEsc) { g_open = false; return; }
    if (g_keyTab) g_page = (g_page + 1) % 3;
    float sc = std::min(W / 1280.f, H / 900.f), pad = 18 * sc; float x0 = pad, y0 = pad, w = W - 2 * pad, h = H - 2 * pad;
    UiRect(0, 0, W, H, uitheme::Canvas); UiRect(x0, y0, w, h, uitheme::Surface); UiFrame(x0, y0, w, h, uitheme::Border, 1);
    UiRect(x0, y0, 4 * sc, h, uitheme::Accent);
    UiText(x0 + 20 * sc, y0 + 12 * sc, "WITNESS / CO-OP", uitheme::Text, sc * 1.25f);
    UiText(x0 + 21 * sc, y0 + 43 * sc, "FIELD NOTES & SHARED WORLD", uitheme::Muted, sc * 0.72f);
    float ty = y0 + 12 * sc, th = UiLineHeight(sc) + 10 * sc;
    float mapW = 84 * sc, nearW = 156 * sc, savesW = 88 * sc, tabGap = 8 * sc;
    float tx = x0 + w - mapW - nearW - savesW - 2 * tabGap - 18 * sc;
    if (Button(tx, ty, mapW, th, "Map", g_page == 0, sc)) g_page = 0;
    if (Button(tx + mapW + tabGap, ty, nearW, th, "Nearby puzzles", g_page == 1, sc)) g_page = 1;
    if (Button(tx + mapW + nearW + 2 * tabGap, ty, savesW, th, "Saves", g_page == 2, sc)) { g_page = 2; SaveListRefresh(); }
    { const char* hint = "F8 / Esc close    F4 nearby    Tab page"; float hs = sc * 0.75f; UiText(x0 + w - UiTextWidth(hint, hs) - 20 * sc, y0 + 45 * sc, hint, uitheme::Muted, hs); }
    float tvY = y0 + 68 * sc, tvW = std::min(250.f * sc, (w - 60 * sc) / 3);
    if (Button(x0 + 20 * sc, tvY, tvW, th, TvEnabled() ? "Back TV: ON [F10]" : "Back TV: off [F10]", TvEnabled(), sc)) TvEnable(!TvEnabled());
    if (Button(x0 + 30 * sc + tvW, tvY, tvW, th, TvConsent() ? "Share my POV: ON [F11]" : "Share my POV: off [F11]", TvConsent(), sc)) TvAllow(!TvConsent());
    if (Button(x0 + 40 * sc + 2 * tvW, tvY, tvW, th, TvDeploymentTarget() ? "Retract sky TV [J]" : "Deploy aimed sky TV [J]", TvDeploymentTarget() != 0, sc)) TvDeployAction(OverlayTvAim());
    float cy0 = tvY + th + 12 * sc, ch = h - (cy0 - y0) - UiLineHeight(sc) - 18 * sc, lw = std::max(300 * sc, std::min(380 * sc, w * 0.29f)), gap = 12 * sc;
    if (!g_built) { UiText(x0 + 20 * sc, cy0 + 20 * sc, "The world is not loaded yet.", uitheme::Danger, sc); }
    else {
        float rx = x0 + 20 * sc + lw + gap, rw = w - 40 * sc - lw - gap, dh = std::min(ch * 0.46f, 440.f * sc);
        if (g_page == 0 && !GetCfg().noDetailFit) dh = std::min(ch * 0.66f, std::max(dh, g_detailNeed + 24 * sc));      // the selected panel's chains need room: grow the pane (the list above shrinks) rather than cut them off
        if (g_page == 2) SavesPage(x0 + 20 * sc, cy0, w - 40 * sc, ch, sc);
        else if (g_page == 0) { SpotList(x0 + 20 * sc, cy0, lw, ch - dh - gap, sc); MapPage(rx, cy0, rw, ch, sc); }
        else if (g_page == 1) { NearbyPage(x0 + 20 * sc, cy0, lw, ch, sc); Pane(rx, cy0, rw, ch); }
        if (g_page == 0) {
            float dy = cy0 + ch - dh; Pane(x0 + 20 * sc, dy, lw, dh); UiClip(x0 + 21 * sc, dy + 1, lw - 2 * sc, dh - 2 * sc);
            DetailPane(x0 + 30 * sc, dy + 10 * sc, lw - 20 * sc, sc * 0.9f); UiClip(0, 0, 0, 0);
        } else if (g_page == 1) {
            UiClip(rx + 1, cy0 + 1, rw - 2, ch - 2); DetailPane(rx + 16 * sc, cy0 + 12 * sc, rw - 32 * sc, sc); UiClip(0, 0, 0, 0);
        }
    }
    if (GetTickCount() - g_msgAt < 6000 && g_msg[0]) UiText(x0 + 20 * sc, y0 + h - UiLineHeight(sc) - 8 * sc, g_msg, uitheme::Accent, sc);
    UiTri(g_cx + 2, g_cy + 2, 12, 0.9f, Rgba(1, 1, 1)); UiTri(g_cx + 2, g_cy + 2, 8, 0.9f, Rgba(0, 0, 0));
}
