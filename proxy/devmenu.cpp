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
#include "coop.h"
#include "game.h"
#include "config.h"
#include "video.h"
#include "savepoints.h"

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
float g_vx, g_vy, g_vscale; bool g_viewInit;
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
void TeleportToPanel(const Pn& p) {
    g_tp = {true, p.x, p.y, p.z, p.rz, 0, 0, p.id}; FireTeleport();
    Msg("teleporting in front of panel %#x", p.id); g_open = false;
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
bool Button(float x, float y, float w, float h, const char* label, bool on = false, float sc = 1) {
    bool hov = Hit(x, y, w, h);
    UiRect(x, y, w, h, on ? Rgba(0.15f, 0.45f, 0.7f, 0.95f) : hov ? Rgba(0.3f, 0.3f, 0.36f, 0.95f) : Rgba(0.18f, 0.18f, 0.22f, 0.95f)); UiFrame(x, y, w, h, Rgba(0.45f, 0.45f, 0.55f, 1));
    float tw = UiTextWidth(label, sc), th = UiLineHeight(sc); UiText(x + (w - tw) / 2, y + (h - th) / 2, label, Rgba(1, 1, 1, 1), sc);
    return hov && g_cl;
}
std::string Wrap(const char* t, float maxw, float sc) {      // break a text at spaces so each line fits maxw pixels
    std::string out, line; const char* p = t; float cw = UiTextWidth("M", sc);
    while (*p) {
        const char* e = p; while (*e && *e != ' ' && *e != '\n') e++; std::string word(p, e);
        if (!line.empty() && (line.size() + 1 + word.size()) * cw > maxw) { out += line + "\n"; line.clear(); }
        line += (line.empty() ? "" : " ") + word; p = e;
        if (*p == '\n') { out += line + "\n"; line.clear(); p++; } else if (*p == ' ') p++;
    }
    return out + line;
}
const char* YesNo(bool b) { return b ? "yes" : "no"; }
UiColor PanelColor(const Pn& p) { return p.solved ? Rgba(0.25f, 0.9f, 0.35f) : p.powered ? Rgba(0.95f, 0.75f, 0.2f) : Rgba(0.55f, 0.55f, 0.6f); }

void SelectPanel(int i) { g_selKind = SelPanel; g_selPanel = i; }

// ---- panels of the "what is connected" view: downstream and upstream chains over panel targets
void Chain(float x, float& y, const char* title, bool down, int start, float sc) {
    UiText(x, y, title, Rgba(0.7f, 0.8f, 1), sc); y += UiLineHeight(sc);
    int cur = start; std::vector<int> seen = {cur}; float px = x + 10;
    for (int depth = 0; depth < 6; depth++) {
        int next = -1;
        if (down) { if (g_pn[cur].target >= 0) { auto it = g_pidx.find((unsigned)g_pn[cur].target); if (it != g_pidx.end()) next = it->second; } }
        else { auto it = g_rev.find((int)g_pn[cur].id); if (it != g_rev.end() && !it->second.empty()) next = it->second[0]; }
        if (next < 0 || std::find(seen.begin(), seen.end(), next) != seen.end()) break;
        char b[48]; snprintf(b, sizeof b, "%#x%s", g_pn[next].id, g_pn[next].solved ? " ok" : ""); float bw = UiTextWidth(b, sc) + 12;
        if (px + bw > x + 520) break;
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
void DetailPane(float x, float y, float w, float sc) {
    float lh = UiLineHeight(sc); char b[256];
    if (g_selKind == SelPanel && g_selPanel >= 0 && g_selPanel < (int)g_pn.size()) {
        Pn& p = g_pn[g_selPanel]; uintptr_t te = p.target >= 0 ? GameEntity(p.target) : 0; const char* tt = te ? TypeName(te) : "nothing";
        snprintf(b, sizeof b, "Panel %#x   %u nodes", p.id, p.nodes); UiText(x, y, b, Rgba(1, 1, 1), sc * 1.15f); y += lh * 1.3f;
        snprintf(b, sizeof b, "solved: %s    powered: %s\nat %.1f %.1f %.1f", YesNo(p.solved), YesNo(p.powered), p.x, p.y, p.z); UiText(x, y, b, Rgba(0.85f, 0.85f, 0.9f), sc); y += lh * 2.3f;
        if (te) {
            bool lit = !strcmp(tt, "Power_Cable") && One(te + 0xC4) && One(te + 0xC8), open = !strcmp(tt, "Door") && One(te + 0x1D0);
            snprintf(b, sizeof b, "Targets: %s %#x%s", tt, (unsigned)p.target, lit ? " (lit)" : open ? " (open)" : ""); UiText(x, y, b, Rgba(1, 0.9f, 0.5f), sc); y += lh * 1.3f;
        } else { UiText(x, y, "Targets: nothing", Rgba(0.7f, 0.7f, 0.7f), sc); y += lh * 1.3f; }
        auto rv = g_rev.find((int)p.id); size_t nrev = rv == g_rev.end() ? 0 : rv->second.size();
        snprintf(b, sizeof b, "Targeted by %zu panel(s)", nrev); UiText(x, y, b, Rgba(0.7f, 0.8f, 1), sc); y += lh * 1.2f;
        float px = x; for (size_t k = 0; k < nrev && k < 8; k++) { int i = rv->second[k]; snprintf(b, sizeof b, "%#x", g_pn[i].id); float bw = UiTextWidth(b, sc) + 12; if (Button(px, y, bw, lh + 4, b, false, sc)) SelectPanel(i); px += bw + 6; }
        y += lh + 12;
        Chain(x, y, "Powers on from here:", true, g_selPanel, sc); Chain(x, y, "Fed by:", false, g_selPanel, sc);
        float bx = x, bh = lh + 12;
        auto act = [&](const char* label) { float bw2 = UiTextWidth(label, sc) + 24; if (bx > x && bx + bw2 > x + w) { bx = x; y += bh + 8; } bool r = Button(bx, y, bw2, bh, label, false, sc); bx += bw2 + 8; return r; };
        if (act("Teleport to it")) Do("teleport", g_selPanel);
        if (act("Solve (and run target)")) Do("solve", g_selPanel);
        if (!p.powered && act("Power on")) Do("power", g_selPanel);
        if (te && act("Run its target")) Do("run", g_selPanel);
        if (te && !strcmp(tt, "Door") && act("Open target door")) Do("door", g_selPanel);
        if (te && !strcmp(tt, "Power_Cable") && act("Light cable")) Do("cableon", g_selPanel);
        if (te && !strcmp(tt, "Power_Cable") && act("Cable off")) Do("cableoff", g_selPanel);
        if (te && p.target >= 0 && g_pidx.count((unsigned)p.target) && act("Select target panel")) SelectPanel(g_pidx[(unsigned)p.target]);
    } else if (g_selKind == SelPoint) {
        snprintf(b, sizeof b, "Point %.0f %.0f", g_selX, g_selY); UiText(x, y, b, Rgba(1, 1, 1), sc * 1.15f); y += lh * 1.5f;
        float z; bool ok = GroundNear(g_selX, g_selY, &z);
        UiText(x, y, Wrap(ok ? "Ground height is taken from the nearest panel; you drop in from 6 m above it." : "No panel within 30 m: no known ground here.", w, sc).c_str(), Rgba(0.85f, 0.85f, 0.9f), sc); y += lh * 3.2f;
        if (ok && Button(x, y, 170 * sc, lh + 12, "Teleport here", false, sc)) TeleportToPoint(g_selX, g_selY);
    } else if (g_selKind == SelPlayer) {
        RemotePose rp; bool have = CoopGetRemote(&rp, (unsigned)g_selPlayer);
        snprintf(b, sizeof b, "Player %d", g_selPlayer); UiText(x, y, b, Rgba(1, 0.6f, 0.2f), sc * 1.15f); y += lh * 1.5f;
        if (have) { snprintf(b, sizeof b, "at %.1f %.1f %.1f\nseen %lu ms ago", rp.pos[0], rp.pos[1], rp.pos[2], (unsigned long)rp.ageMs); UiText(x, y, b, Rgba(0.85f, 0.85f, 0.9f), sc); y += lh * 2.5f; if (Button(x, y, 200 * sc, lh + 12, "Teleport to them", false, sc)) TeleportToPlayer(rp); }
        else UiText(x, y, "Not connected.", Rgba(0.8f, 0.5f, 0.5f), sc);
    } else UiText(x, y, Wrap("Click a dot on the map, a spot in the list, or a panel in the nearby list.", w, sc).c_str(), Rgba(0.7f, 0.7f, 0.75f), sc);
}

void MapPage(float x, float y, float w, float h, float sc) {
    UiRect(x, y, w, h, Rgba(0.04f, 0.05f, 0.07f, 0.97f)); UiFrame(x, y, w, h, Rgba(0.4f, 0.4f, 0.5f));
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
    if (over && g_cl) {
        if (hoveredPlayer >= 0) { g_selKind = SelPlayer; g_selPlayer = hoveredPlayer; }
        else if (hover >= 0) SelectPanel(hover);
        else { g_selKind = SelPoint; g_selX = g_vx + (g_mx - x - w / 2) / g_vscale; g_selY = g_vy - (g_my - y - h / 2) / g_vscale; }
    }
    if (over && hover >= 0) { char b[64]; snprintf(b, sizeof b, "%#x  %s", g_pn[hover].id, g_pn[hover].solved ? "solved" : g_pn[hover].powered ? "powered" : "off"); float tw = UiTextWidth(b, sc) + 10; UiRect(g_mx + 14, g_my + 8, tw, UiLineHeight(sc) + 4, Rgba(0, 0, 0, 0.85f)); UiText(g_mx + 19, g_my + 10, b, Rgba(1, 1, 1), sc); }
    UiText(x + 8, y + h - UiLineHeight(sc) - 6, "wheel: zoom   right-drag: pan   click: select   green solved, amber powered, grey off, boxes = doors (blue open)", Rgba(0.6f, 0.6f, 0.65f), sc * 0.85f);
}

void SpotList(float x, float y, float w, float h, float sc) {
    UiRect(x, y, w, h, Rgba(0.08f, 0.09f, 0.12f, 0.97f)); UiFrame(x, y, w, h, Rgba(0.4f, 0.4f, 0.5f));
    float rh = UiLineHeight(sc) + 6; bool over = Hit(x, y, w, h);
    if (over && g_wheel) g_spotScroll -= g_wheel * rh * 2; g_spotScroll = std::max(0.f, std::min(g_spotScroll, std::max(0.f, rh * g_spots.size() - h)));
    UiClip(x + 1, y + 1, w - 2, h - 2);
    for (size_t i = 0; i < g_spots.size(); i++) {
        float ry = y + i * rh - g_spotScroll; if (ry + rh < y || ry > y + h) continue; Pn& p = g_pn[g_spots[i].pn];
        int solved = 0; for (auto& q : g_pn) if (q.solved && hypotf(q.x - p.x, q.y - p.y) < 56) solved++;
        char b[80]; snprintf(b, sizeof b, "%2zu  panel %#x  (%d panels)", i + 1, p.id, g_spots[i].count);
        bool sel = g_selKind == SelPanel && g_selPanel == g_spots[i].pn;
        if (Hit(x, ry, w, rh) && g_cl) { SelectPanel(g_spots[i].pn); g_vx = p.x; g_vy = p.y; }
        UiRect(x + 2, ry + 1, w - 4, rh - 2, sel ? Rgba(0.15f, 0.45f, 0.7f, 0.9f) : Hit(x, ry, w, rh) ? Rgba(0.25f, 0.25f, 0.3f, 0.9f) : Rgba(0, 0, 0, 0)); UiText(x + 8, ry + 3, b, p.solved ? Rgba(0.4f, 1, 0.5f) : Rgba(0.9f, 0.9f, 0.95f), sc);
        (void)solved;
    }
    UiClip(0, 0, 0, 0);
}

void NearbyPage(float x, float y, float w, float h, float sc) {
    UiRect(x, y, w, h, Rgba(0.08f, 0.09f, 0.12f, 0.97f)); UiFrame(x, y, w, h, Rgba(0.4f, 0.4f, 0.5f));
    float pos[3], ang[2]; if (!Camera(pos, ang)) { UiText(x + 10, y + 10, "no player position yet", Rgba(1, 0.6f, 0.6f), sc); return; }
    struct R { int i; float d; }; std::vector<R> rows;
    for (size_t i = 0; i < g_pn.size(); i++) { float d = hypotf(g_pn[i].x - pos[0], g_pn[i].y - pos[1]); if (d <= g_radius && fabsf(g_pn[i].z - pos[2]) < 40) rows.push_back({(int)i, d}); }
    std::sort(rows.begin(), rows.end(), [](const R& a, const R& b) { return a.d < b.d; });
    char b[120]; snprintf(b, sizeof b, "%zu puzzles within %.0f m", rows.size(), g_radius); UiText(x + 8, y + 6, b, Rgba(1, 1, 1), sc);
    if (Button(x + w - 130, y + 4, 28, UiLineHeight(sc) + 4, "-", false, sc)) g_radius = std::max(10.f, g_radius - 10);
    if (Button(x + w - 96, y + 4, 28, UiLineHeight(sc) + 4, "+", false, sc)) g_radius = std::min(300.f, g_radius + 10);
    float ly = y + UiLineHeight(sc) + 14, rh = UiLineHeight(sc) + 6, lh = h - (ly - y) - 4;
    if (Hit(x, ly, w, lh) && g_wheel) g_nearScroll -= g_wheel * rh * 2; g_nearScroll = std::max(0.f, std::min(g_nearScroll, std::max(0.f, rh * rows.size() - lh)));
    UiClip(x + 1, ly, w - 2, lh);
    for (size_t k = 0; k < rows.size(); k++) {
        float ry = ly + k * rh - g_nearScroll; if (ry + rh < ly || ry > ly + lh) continue; Pn& p = g_pn[rows[k].i];
        bool sel = g_selKind == SelPanel && g_selPanel == rows[k].i;
        if (Hit(x, ry, w, rh) && g_cl && Hit(x, ly, w, lh)) SelectPanel(rows[k].i);
        UiRect(x + 2, ry + 1, w - 4, rh - 2, sel ? Rgba(0.15f, 0.45f, 0.7f, 0.9f) : Hit(x, ry, w, rh) ? Rgba(0.25f, 0.25f, 0.3f, 0.9f) : Rgba(0, 0, 0, 0));
        snprintf(b, sizeof b, "%#8x %5.1fm %-3s %-3s -> %s", p.id, rows[k].d, p.solved ? "S" : "", p.powered ? "P" : "", p.ttype); UiText(x + 8, ry + 3, b, PanelColor(p), sc);
    }
    UiClip(0, 0, 0, 0);
}
void SavesPage(float x, float y, float w, float h, float sc) {
    UiRect(x, y, w, h, Rgba(0.08f, 0.09f, 0.12f, 0.97f)); UiFrame(x, y, w, h, Rgba(0.4f, 0.4f, 0.5f));
    float lh = UiLineHeight(sc), bh = lh + 14, bx = x + 12, by = y + 12; static int nSpots = 4; static char nm[64];
    auto act = [&](const char* label) { float bw = UiTextWidth(label, sc) + 26; bool r = Button(bx, by, bw, bh, label, false, sc); bx += bw + 10; return r; };
    SYSTEMTIME st; GetLocalTime(&st); snprintf(nm, sizeof nm, "%02d%02d%02d-%02d%02d%02d", st.wYear % 100, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    if (act("Save point now") && !SaveBusy()) { SaveStartSave(std::string("save-") + nm, false, ProgressNote()); g_open = false; }
    if (act("Preset from current progress (list only)") && !SaveBusy()) { SaveStartPreset(std::string("preset-") + nm, ProgressNote()); }
    UiText(x + 12, by + bh + 8, SaveBusy() ? "working... (the pause menu opens and closes by itself; do not touch anything)" : (SaveStatus()[0] ? SaveStatus() : "A save point is a copy of the game's save; a restore loads it through the pause menu. Presets are kept for starting from a given amount of progress."), SaveBusy() ? Rgba(1, 0.8f, 0.4f) : Rgba(0.75f, 0.8f, 0.9f), sc * 0.9f);
    float ty = by + bh + 8 + lh * 2.2f; char b[120]; snprintf(b, sizeof b, "Make progress for a preset: complete the first %d of %zu spots (all their panels)", nSpots, g_spots.size()); UiText(x + 12, ty, b, Rgba(1, 1, 1), sc);
    bx = x + 12; by = ty + lh + 6;
    if (act("-")) nSpots = std::max(1, nSpots - 1); if (act("+")) nSpots = std::min((int)g_spots.size(), nSpots + 1); if (act("Solve them now")) { QueueSolveSpots(nSpots); g_open = false; }
    if (!g_solveQ.empty()) { snprintf(b, sizeof b, "%zu panels left to solve", g_solveQ.size()); UiText(bx + 20, by + 6, b, Rgba(1, 0.8f, 0.4f), sc); }
    float ly = by + bh + 16; UiText(x + 12, ly, "Saved:", Rgba(0.7f, 0.8f, 1), sc); ly += lh + 4; float rh = bh;
    auto& L = SaveList(); if (L.empty()) UiText(x + 12, ly, "nothing yet", Rgba(0.7f, 0.7f, 0.7f), sc);
    for (size_t i = 0; i < L.size() && ly + rh < y + h; i++) {
        UiRect(x + 8, ly, w - 16, rh - 2, Rgba(0.12f, 0.12f, 0.16f, 0.9f)); snprintf(b, sizeof b, "%s  %s", L[i].preset ? "[preset]" : "[save]  ", L[i].name.c_str());
        UiText(x + 16, ly + 7, b, L[i].preset ? Rgba(1, 0.85f, 0.4f) : Rgba(0.9f, 0.9f, 0.95f), sc); UiText(x + 16 + 36 * UiTextWidth("M", sc), ly + 7, L[i].note.c_str(), Rgba(0.7f, 0.75f, 0.8f), sc * 0.9f);
        const char* lab = L[i].hasGameSave ? "Restore (load save + progress)" : "Apply progress list"; float rw = UiTextWidth(lab, sc) + 26; if (Button(x + w - rw - 14, ly + 1, rw, rh - 4, lab, false, sc) && !SaveBusy()) { SaveStartRestore(L[i]); g_open = false; }
        ly += rh;
    }
}
struct Cmd { std::string what; unsigned id; };
std::vector<Cmd> g_cmds;
}  // namespace

void DevMenuCommand(const char* what, unsigned id) { Init0(); EnterCriticalSection(&g_cs); g_cmds.push_back({what, id}); LeaveCriticalSection(&g_cs); }

bool DevMenuEnabled() { return GetCfg().devMenu; }
void ProcessCmds() {
    Init0();
    std::vector<Cmd> cmds; EnterCriticalSection(&g_cs); cmds.swap(g_cmds); LeaveCriticalSection(&g_cs);
    if (!cmds.empty() && !g_built) Build();
    for (auto& c : cmds) {      // test commands: 'select' picks a panel, anything else is an action on the selected panel (or panel c.id)
        if (c.what.rfind("save:", 0) == 0) { SaveStartSave(c.what.substr(5), false, ProgressNote()); continue; }
        if (c.what.rfind("preset:", 0) == 0) { SaveStartPreset(c.what.substr(7), ProgressNote()); continue; }
        if (c.what.rfind("restore:", 0) == 0) { SaveListRefresh(); std::string n = c.what.substr(8); bool found = false; for (auto& e : SaveList()) if (e.name == n) { SaveStartRestore(e); found = true; break; } if (!found) Msg("no save called '%s'", n.c_str()); continue; }
        if (c.what == "complete") { QueueSolveSpots((int)c.id); continue; }
        auto it = g_pidx.find(c.id);
        if (c.what == "select") { if (it != g_pidx.end()) { SelectPanel(it->second); g_vx = g_pn[it->second].x; g_vy = g_pn[it->second].y; Msg("selected panel %#x", c.id); } else Msg("no such panel %#x", c.id); }
        else if (!Do(c.what.c_str(), g_selKind == SelPanel ? g_selPanel : -1)) Msg("action '%s' not possible here", c.what.c_str());
    }
}
void DevMenuTick() {      // every frame, open or not: test commands, saves, the solve queue and the check of a pending teleport (retried when refused)
    { static bool hooked; if (!hooked) { SaveSetHooks(StateWrite, StateApply); hooked = true; } }
    ProcessCmds(); SaveTick(GetTickCount());
    { static char last[200]; if (strcmp(last, SaveStatus())) { snprintf(last, sizeof last, "%s", SaveStatus()); if (last[0]) Msg("%s", last); } }
    for (int k = 0; k < 8 && !g_solveQ.empty(); k++) { int i = g_solveQ.back(); g_solveQ.pop_back(); g_forced.insert(g_pn[i].id); GameMarkPanelSolved(g_pn[i].id); GameDispatchTargetNow(g_pn[i].id, true); if (g_solveQ.empty()) Msg("solved the panels of the chosen spots"); }
    if (!g_tp.on || GetTickCount() - g_tp.at < 1500) return;
    float pos[3], ang[2]; if (!Camera(pos, ang)) return;
    int k = g_tp.attempt;
    if (hypotf(pos[0] - (g_tp.px + kTryDist[k] * cosf(g_tp.rz)), pos[1] - (g_tp.py + kTryDist[k] * sinf(g_tp.rz))) < 2.5f) { g_tp.on = false; Msg("teleported in front of panel %#x", g_tp.id); return; }
    if (++g_tp.attempt >= (int)(sizeof kTryDist / sizeof kTryDist[0])) { g_tp.on = false; Msg("teleport to panel %#x refused by the game (it put you back at the spawn point): that place cannot be entered this way", g_tp.id); return; }
    FireTeleport();
}
bool DevMenuToastWanted() { return !g_open && g_msg[0] && (GetTickCount() - g_msgAt <= 6000 || SaveBusy()); }
void DevMenuToast(float W, float H) {      // the last message, for a few seconds after the menu closed
    if (g_open || !g_msg[0] || (GetTickCount() - g_msgAt > 6000 && !SaveBusy())) return; float sc = std::max(1.f, H / 1080.f), tw = UiTextWidth(g_msg, sc) + 20;
    UiRect(W / 2 - tw / 2, H - 80 * sc, tw, UiLineHeight(sc) + 12, Rgba(0, 0, 0, 0.75f)); UiText(W / 2 - tw / 2 + 10, H - 80 * sc + 6, g_msg, Rgba(1, 0.95f, 0.5f), sc);
}
bool DevMenuOpen() { return g_open; }
static void Init() { if (!g_csInit) { InitializeCriticalSection(&g_cs); g_csInit = true; } }
void DevMenuToggle() { Init(); g_open = !g_open; g_ldown = g_rdown = false; if (g_open) g_built = false; }      // rebuild on open: entities may have been recreated by a load
void DevMenuSetPage(int p) { Init(); g_page = p; if (p == 2) SaveListRefresh(); if (!g_open) { g_open = true; g_built = false; } }
bool DevMenuKey(unsigned vk, bool down) {
    Init(); if (vk == VK_F9 && down && VideoEnabled()) { VideoToggleView(); return true; }
    if (!DevMenuEnabled()) return false;
    if (vk == VK_F8 && down) { DevMenuToggle(); return true; }
    if (vk == VK_F4 && down) { if (g_open && g_page == 1) DevMenuToggle(); else DevMenuSetPage(1); return true; }
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
    float sc = std::max(1.f, H / 1080.f), pad = 14 * sc; float x0 = pad, y0 = pad, w = W - 2 * pad, h = H - 2 * pad;
    UiRect(0, 0, W, H, Rgba(0, 0, 0, 0.55f)); UiRect(x0, y0, w, h, Rgba(0.1f, 0.1f, 0.14f, 0.97f)); UiFrame(x0, y0, w, h, Rgba(0.5f, 0.5f, 0.65f), 2);
    UiText(x0 + 12, y0 + 8, "WITNESS CO-OP  DEV MENU", Rgba(1, 1, 1), sc * 1.2f);
    float tx = x0 + 380 * sc, ty = y0 + 6, th = UiLineHeight(sc) + 10;
    if (Button(tx, ty, 120 * sc, th, "Map", g_page == 0, sc)) g_page = 0; if (Button(tx + 128 * sc, ty, 160 * sc, th, "Nearby puzzles", g_page == 1, sc)) g_page = 1; if (Button(tx + 296 * sc, ty, 120 * sc, th, "Saves", g_page == 2, sc)) { g_page = 2; SaveListRefresh(); }
    { const char* hint = "F8/Esc close   F4 nearby   Tab page"; UiText(x0 + w - UiTextWidth(hint, sc * 0.9f) - 14, y0 + 10, hint, Rgba(0.6f, 0.6f, 0.65f), sc * 0.9f); }
    float cy0 = y0 + th + 18, ch = h - (cy0 - y0) - UiLineHeight(sc) - 18, lw = 360 * sc, gap = 10;
    if (!g_built) { UiText(x0 + 20, cy0 + 20, "The world is not loaded yet.", Rgba(1, 0.7f, 0.7f), sc); }
    else {
        float rx = x0 + 10 + lw + gap, rw = w - 20 - lw - gap, dh = std::min(ch * 0.46f, 440.f * sc);
        if (g_page == 2) SavesPage(x0 + 10, cy0, w - 20, ch, sc);
        else if (g_page == 0) { SpotList(x0 + 10, cy0, lw, ch - dh - gap, sc); MapPage(rx, cy0, rw, ch, sc); }
        else if (g_page == 1) { NearbyPage(x0 + 10, cy0, lw, ch, sc); UiRect(rx, cy0, rw, ch, Rgba(0.08f, 0.09f, 0.12f, 0.97f)); UiFrame(rx, cy0, rw, ch, Rgba(0.4f, 0.4f, 0.5f)); }
        if (g_page == 0) { UiRect(x0 + 10, cy0 + ch - dh, lw, dh, Rgba(0.08f, 0.09f, 0.12f, 0.97f)); UiFrame(x0 + 10, cy0 + ch - dh, lw, dh, Rgba(0.4f, 0.4f, 0.5f)); DetailPane(x0 + 20, cy0 + ch - dh + 8, lw - 20, sc * 0.9f); }
        else if (g_page == 1) DetailPane(rx + 14, cy0 + 10, rw - 28, sc);
    }
    if (GetTickCount() - g_msgAt < 6000 && g_msg[0]) UiText(x0 + 12, y0 + h - UiLineHeight(sc) - 8, g_msg, Rgba(1, 0.95f, 0.5f), sc);
    UiTri(g_cx + 2, g_cy + 2, 12, 0.9f, Rgba(1, 1, 1)); UiTri(g_cx + 2, g_cy + 2, 8, 0.9f, Rgba(0, 0, 0));
}
