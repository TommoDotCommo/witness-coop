// See mapgen.h.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "mapgen.h"
#include "config.h"
#include "game.h"

void Log(const char* msg);
bool GamePaused();      // overlay.cpp
extern HMODULE g_selfModule();
bool OverlayCaptureRgba(IDXGISwapChain* sc, std::vector<uint8_t>* px, unsigned* w, unsigned* h);      // overlay.cpp
bool DevMenuMapInfo(float* minx, float* miny, float* maxx, float* maxy);                               // devmenu.cpp
bool DevMenuGroundNear(float x, float y, float maxDist, float* z);

namespace {
const float kPi = 3.14159265f, kVFov = 60.0f;
enum Phase { Idle, Moving, Capturing, Returning };
struct Tile { float cx, cy, gz; };
Phase g_phase = Idle; std::vector<Tile> g_plan; size_t g_i; DWORD g_t0; float g_home[3], g_homeAng[2]; DWORD g_started;
float g_minx, g_miny, g_maxx, g_maxy, g_res; int g_W, g_H; std::vector<float> g_acc; std::vector<float> g_wt;      // the mosaic: weighted colour sums
double g_nccSum; unsigned g_nccN, g_overlayFrames; unsigned g_serial; std::vector<uint8_t> g_done; int g_doneW, g_doneH; float g_dbounds[4]; bool g_haveDone;
const float kYaw = 1.5707963f, kPitch = -1.55f;      // (asked: straight down; the game stops at kAssumedPitch)

std::wstring Dir() { wchar_t p[MAX_PATH]; GetModuleFileNameW(g_selfModule(), p, MAX_PATH); wchar_t* s = wcsrchr(p, L'\\'); if (s) *(s + 1) = 0; return p; }
bool Camera(float pos[3], float ang[2]) {
    uintptr_t base = (uintptr_t)GetModuleHandleA(nullptr); const float* c = (const float*)(base + 0x6304B0 + 0x10); const float* a = (const float*)(base + 0x6303EC);
    if (IsBadReadPtr(c, 12) || IsBadReadPtr(a, 8)) return false; memcpy(pos, c, 12); ang[0] = a[0]; ang[1] = a[1]; return true;
}
bool Project(const float* cam, float th, float ph, float wx, float wy, float wz, float W, float H, float* sx, float* sy) {
    float d[3] = {wx - cam[0], wy - cam[1], wz - cam[2]};
    float fwd[3] = {cosf(ph) * cosf(th), cosf(ph) * sinf(th), sinf(ph)}, right[3] = {sinf(th), -cosf(th), 0}, up[3] = {-sinf(ph) * cosf(th), -sinf(ph) * sinf(th), cosf(ph)};
    float f = d[0] * fwd[0] + d[1] * fwd[1] + d[2] * fwd[2], r = d[0] * right[0] + d[1] * right[1] + d[2] * right[2], u = d[0] * up[0] + d[1] * up[1] + d[2] * up[2];
    if (f < 0.5f) return false;
    float fovTan = tanf(kVFov * (GetCfg().mapgenBadScale ? 1.15f : 1.0f) * kPi / 360.0f), tw = fovTan * (W / H);
    *sx = ((r / f) / tw * 0.5f + 0.5f) * W; *sy = (0.5f - (u / f) / fovTan * 0.5f) * H; if (GetCfg().mapgenMirror) *sx = W - *sx; return true;
}
void Finish(bool ok) {
    GameHoverOff(); g_phase = Idle;
    if (!ok || g_wt.empty()) { Log("mapgen: stopped"); return; }
    std::vector<uint8_t> bgra((size_t)g_W * g_H * 4, 0); size_t covered = 0; double sum = 0, sum2 = 0;
    for (int j = 0; j < g_H; ++j) for (int i = 0; i < g_W; ++i) {
        size_t k = (size_t)j * g_W + i; float w = g_wt[k]; uint8_t* o = &bgra[k * 4];
        if (w > 0.02f) { o[2] = (uint8_t)std::min(255.0f, g_acc[k * 3] / w); o[1] = (uint8_t)std::min(255.0f, g_acc[k * 3 + 1] / w); o[0] = (uint8_t)std::min(255.0f, g_acc[k * 3 + 2] / w); o[3] = 255; ++covered; float l = 0.3f * o[2] + 0.59f * o[1] + 0.11f * o[0]; sum += l; sum2 += l * l; }
    }
    std::wstring dir = Dir() + L"witness_map"; CreateDirectoryW(dir.c_str(), nullptr);
    FILE* f = _wfopen((dir + L"\\underlay.bmp").c_str(), L"wb");
    if (f) { BITMAPFILEHEADER fh = {0x4D42, (DWORD)(54 + bgra.size()), 0, 0, 54}; BITMAPINFOHEADER ih = {40, g_W, -g_H, 1, 32, BI_RGB, (DWORD)bgra.size(), 0, 0, 0, 0}; fwrite(&fh, 14, 1, f); fwrite(&ih, 40, 1, f); fwrite(bgra.data(), 1, bgra.size(), f); fclose(f); }
    f = _wfopen((dir + L"\\underlay.txt").c_str(), L"w"); if (f) { fprintf(f, "%.3f %.3f %.3f %.3f %d %d\n", g_minx, g_miny, g_maxx, g_maxy, g_W, g_H); fclose(f); }
    double mean = covered ? sum / covered : 0, sd = covered ? std::sqrt(std::max(0.0, sum2 / covered - mean * mean)) : 0;
    g_done = bgra; g_doneW = g_W; g_doneH = g_H; g_dbounds[0] = g_minx; g_dbounds[1] = g_miny; g_dbounds[2] = g_maxx; g_dbounds[3] = g_maxy; g_haveDone = true; ++g_serial;
    char m[300]; snprintf(m, sizeof m, "mapgen: done %dx%d, covered %.1f%%, luminance sd %.1f, seams mean_ncc %.3f over %u, overlay frames during capture %u, tiles %zu in %lu s", g_W, g_H, 100.0 * covered / ((double)g_W * g_H), sd, g_nccN ? g_nccSum / g_nccN : 0.0, g_nccN, g_overlayFrames, g_plan.size(), (GetTickCount() - g_started) / 1000); Log(m);
    g_acc.clear(); g_wt.clear();
    GameHover(g_home[0], g_home[1], g_home[2], g_homeAng[0], g_homeAng[1], 3);      // back where the player was
}
const float kAssumedPitch = -0.932f;      // the game keeps the player's pitch within about 53 degrees of level whatever is asked: the camera looks forward and down, not straight down
// where the ray through a pixel meets the ground plane z = gz (false: the ray does not go down)
bool GroundHit(const float* cam, float th, float ph, float W, float H, float sx, float sy, float gz, float* wx, float* wy) {
    float fwd[3] = {cosf(ph) * cosf(th), cosf(ph) * sinf(th), sinf(ph)}, right[3] = {sinf(th), -cosf(th), 0}, up[3] = {-sinf(ph) * cosf(th), -sinf(ph) * sinf(th), cosf(ph)};
    float fovTan = tanf(kVFov * kPi / 360.0f), tw = fovTan * (W / H), r = (sx / W * 2 - 1) * tw, u = (1 - sy / H * 2) * fovTan, d[3];
    for (int i = 0; i < 3; ++i) d[i] = fwd[i] + right[i] * r + up[i] * u;
    if (d[2] > -0.02f) return false; float t = (gz - cam[2]) / d[2]; *wx = cam[0] + d[0] * t; *wy = cam[1] + d[1] * t; return true;
}
const float kCropX = 0.45f, kCropY = 0.6f;      // the part of the frame that is used: this share of the half width and the half height around the middle
void Plan() {
    const Cfg& c = GetCfg(); float minx, miny, maxx, maxy;
    if (!DevMenuMapInfo(&minx, &miny, &maxx, &maxy)) { Log("mapgen: the world is not loaded"); return; }
    const float margin = 50.0f; g_minx = minx - margin; g_miny = miny - margin; g_maxx = maxx + margin; g_maxy = maxy + margin;
    float height = c.mapgenHeight, ahead = height / tanf(-kAssumedPitch), stepX = height * 0.85f, stepY = height * 0.6f;      // the middle of the picture meets the ground this far in front of the camera
    g_res = c.mapgenRes; g_W = (int)ceilf((g_maxx - g_minx) / g_res); g_H = (int)ceilf((g_maxy - g_miny) / g_res);
    while ((size_t)g_W * g_H > 3200u * 3200u) { g_res *= 1.25f; g_W = (int)ceilf((g_maxx - g_minx) / g_res); g_H = (int)ceilf((g_maxy - g_miny) / g_res); }
    g_acc.assign((size_t)g_W * g_H * 3, 0.0f); g_wt.assign((size_t)g_W * g_H, 0.0f); g_plan.clear(); g_nccSum = 0; g_nccN = 0; g_overlayFrames = 0;
    int row = 0; for (float y = g_miny; y < g_maxy + stepY; y += stepY, ++row) {
        std::vector<Tile> line; for (float x = g_minx; x < g_maxx + stepX; x += stepX) { float z; if (DevMenuGroundNear(x, y, 140.0f, &z)) line.push_back({x, y - ahead, z}); }      // (cx, cy): where the camera is; the tile's own ground is `ahead` further on
        if (row & 1) std::reverse(line.begin(), line.end()); g_plan.insert(g_plan.end(), line.begin(), line.end());
    }
    char m[200]; snprintf(m, sizeof m, "mapgen: %zu tiles over %.0f x %.0f m, %dx%d pixels (%.2f m each), flying %.0f m above the ground, looking %.0f m ahead", g_plan.size(), g_maxx - g_minx, g_maxy - g_miny, g_W, g_H, g_res, height, ahead); Log(m);
}
void Composite(const std::vector<uint8_t>& px, unsigned W, unsigned H, const Tile& t) {
    float cam[3], ang[2]; if (!Camera(cam, ang)) return;
    { static int n; if (n++ < 3) { char m[200]; snprintf(m, sizeof m, "mapgen: tile camera (%.1f, %.1f, %.1f) angles %.3f %.3f, ground %.1f", cam[0], cam[1], cam[2], ang[0], ang[1], t.gz); Log(m); } }
    float bx0 = 1e9f, bx1 = -1e9f, by0 = 1e9f, by1 = -1e9f; int hits = 0;      // the ground the used part of the frame sees: the box round the rays through its corners and edge middles
    const float fxs[8] = {-1, 0, 1, -1, 1, -1, 0, 1}, fys[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
    for (int k = 0; k < 8; ++k) { float wx, wy; if (GroundHit(cam, ang[0], ang[1], (float)W, (float)H, W / 2.0f + fxs[k] * kCropX * W / 2, H / 2.0f + fys[k] * kCropY * H / 2, t.gz, &wx, &wy)) { bx0 = std::min(bx0, wx); bx1 = std::max(bx1, wx); by0 = std::min(by0, wy); by1 = std::max(by1, wy); ++hits; } }
    if (hits < 8) return;
    int i0 = std::max(0, (int)((bx0 - g_minx) / g_res)), i1 = std::min(g_W - 1, (int)((bx1 - g_minx) / g_res));
    int j0 = std::max(0, (int)((g_maxy - by1) / g_res)), j1 = std::min(g_H - 1, (int)((g_maxy - by0) / g_res));
    double sa = 0, sb = 0, sab = 0, saa = 0, sbb = 0; unsigned n = 0;
    std::vector<float> rgb; std::vector<size_t> idx; std::vector<float> wts;
    for (int j = j0; j <= j1; ++j) for (int i = i0; i <= i1; ++i) {
        float wx = g_minx + (i + 0.5f) * g_res, wy = g_maxy - (j + 0.5f) * g_res, sx, sy; if (!Project(cam, ang[0], ang[1], wx, wy, t.gz, (float)W, (float)H, &sx, &sy)) continue;
        float dx = (sx - W / 2.0f) / (kCropX * W / 2), dy = (sy - H / 2.0f) / (kCropY * H / 2); if (fabsf(dx) >= 1 || fabsf(dy) >= 1) continue;
        int x0 = (int)sx, y0 = (int)sy; if (x0 < 0 || y0 < 0 || x0 + 1 >= (int)W || y0 + 1 >= (int)H) continue; float fx = sx - x0, fy = sy - y0; float col[3];
        for (int ch = 0; ch < 3; ++ch) { auto P = [&](int x, int y) { return (float)px[((size_t)y * W + x) * 4 + ch]; }; col[ch] = (P(x0, y0) * (1 - fx) + P(x0 + 1, y0) * fx) * (1 - fy) + (P(x0, y0 + 1) * (1 - fx) + P(x0 + 1, y0 + 1) * fx) * fy; }
        float w = (1 - fabsf(dx)) * (1 - fabsf(dy)); w = w * w * 3.0f + 0.01f; size_t k = (size_t)j * g_W + i;
        if (g_wt[k] > 0.3f) { float la = 0.3f * g_acc[k * 3] / g_wt[k] + 0.59f * g_acc[k * 3 + 1] / g_wt[k] + 0.11f * g_acc[k * 3 + 2] / g_wt[k], lb = 0.3f * col[0] + 0.59f * col[1] + 0.11f * col[2]; sa += la; sb += lb; sab += la * lb; saa += la * la; sbb += lb * lb; ++n; }
        idx.push_back(k); wts.push_back(w); rgb.insert(rgb.end(), col, col + 3);
    }
    if (n > 400) { double ma = sa / n, mb = sb / n, va = saa / n - ma * ma, vb = sbb / n - mb * mb; if (va > 4 && vb > 4) { g_nccSum += (sab / n - ma * mb) / std::sqrt(va * vb); ++g_nccN; } }      // how well this tile agrees with what is already there
    for (size_t q = 0; q < idx.size(); ++q) { size_t k = idx[q]; g_acc[k * 3] += rgb[q * 3] * wts[q]; g_acc[k * 3 + 1] += rgb[q * 3 + 1] * wts[q]; g_acc[k * 3 + 2] += rgb[q * 3 + 2] * wts[q]; g_wt[k] += wts[q]; }
}
}  // namespace

bool MapGenActive() { return g_phase != Idle; }
volatile LONG g_cmd;      // 1 start, 2 stop, 3 status: taken by the render thread
void MapGenCommand(const char* args) { g_cmd = !strncmp(args, "start", 5) ? 1 : (!strncmp(args, "stop", 4) ? 2 : 3); }
static void RunCommand(int cmd) {
    if (cmd == 1) {
        if (g_phase != Idle) { Log("mapgen: already running"); return; }
        if (GamePaused()) { Log("mapgen: the game is paused: nothing to take a picture of"); return; }
        if (!Camera(g_home, g_homeAng)) return; Plan(); if (g_plan.empty()) return; g_i = 0; g_phase = Moving; g_t0 = 0; g_started = GetTickCount();
    } else if (cmd == 2) { if (g_phase != Idle) Finish(false); }
    else { char m[160]; snprintf(m, sizeof m, "mapgen: phase %d tile %zu of %zu serial %u", (int)g_phase, g_i, g_plan.size(), g_serial); Log(m); }
}
void MapGenTick(IDXGISwapChain* sc) {
    if (LONG cmd = InterlockedExchange(&g_cmd, 0)) RunCommand((int)cmd);
    if (g_phase == Idle) return; DWORD now = GetTickCount(); const Cfg& c = GetCfg();
    if (g_phase == Moving) {      // fly to the tile and hold there; the picture is taken after the game has had time to draw the ground in
        if (g_i % 10 == 0) { char m[100]; snprintf(m, sizeof m, "mapgen: tile %zu of %zu", g_i, g_plan.size()); Log(m); }
        const Tile& t = g_plan[g_i]; GameHover(t.cx, t.cy, t.gz + c.mapgenHeight, kYaw, kPitch, 6); g_t0 = now; g_phase = Capturing; return;
    }
    if (g_phase == Capturing) {
        if (now - g_t0 < (DWORD)c.mapgenSettleMs) return;      // (a tile always ends: the frame after the wait is taken whatever is on it)
        std::vector<uint8_t> px; unsigned W, H; if (!GamePaused() && OverlayCaptureRgba(sc, &px, &W, &H)) Composite(px, W, H, g_plan[g_i]);      // (a pause menu is not part of the island)
        if (++g_i >= g_plan.size()) { Finish(true); return; }
        g_phase = Moving;
    }
}
bool MapUnderlayInfo(float* minx, float* miny, float* maxx, float* maxy, int* w, int* h) {
    if (!g_haveDone) {      // from an earlier run: the files next to the DLL
        std::wstring dir = Dir() + L"witness_map"; FILE* f = _wfopen((dir + L"\\underlay.txt").c_str(), L"r"); if (!f) return false; float a, b, c2, d; int W, H;
        bool ok = fscanf(f, "%f %f %f %f %d %d", &a, &b, &c2, &d, &W, &H) == 6; fclose(f); if (!ok || W <= 0 || H <= 0 || (size_t)W * H > 3200u * 3200u) return false;
        f = _wfopen((dir + L"\\underlay.bmp").c_str(), L"rb"); if (!f) return false; fseek(f, 54, SEEK_SET); g_done.resize((size_t)W * H * 4); size_t n = fread(g_done.data(), 1, g_done.size(), f); fclose(f); if (n != g_done.size()) return false;
        g_doneW = W; g_doneH = H; g_dbounds[0] = a; g_dbounds[1] = b; g_dbounds[2] = c2; g_dbounds[3] = d; g_haveDone = true; ++g_serial;
    }
    *minx = g_dbounds[0]; *miny = g_dbounds[1]; *maxx = g_dbounds[2]; *maxy = g_dbounds[3]; *w = g_doneW; *h = g_doneH; return true;
}
const uint8_t* MapUnderlayPixels() { return g_done.data(); }
unsigned MapGenSerial() { return g_serial; }
void MapGenCountOverlayFrame() { if (g_phase != Idle) ++g_overlayFrames; }
