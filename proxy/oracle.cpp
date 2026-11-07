// Engine collision oracle for the walking bot: the game's own ray cast and ground query, called on the render thread.
// Build 11370412 (offsets from the function that logs "Unable to get ground hit from player"; see docs/COLLISION-ORACLE.md):
//   exe+0x241870  Ground(pos*, outPos*, Hit*, a, b, c, maxDist)  the player's "find the floor under me" (a ray down, plus a few offset rays)
//   exe+0x1934C0  BoxQuery(world, p0*, p1*, mask, List*, 0.0f, 0.0f)   the entities whose bounds meet the segment
//   exe+0x254FF0  RayCast(origin*, dir*, maxDist, flags, mask, Hit*, List*)  tests the listed entities, keeps the nearest in Hit
//   exe+0x2538A0  HitInit(Hit*)  (distance = FLT_MAX); a Hit is 0x70 bytes: +0 distance, +8 the entity that was hit
//   exe+0x2E7FA0  Reserve(&data, &cap, n, elemSize), exe+0x2E7F90  Free(&data)   (the game's array: int count, int cap, T* data)
// Commands (witness_cmd.txt, lines; or `oracle` = run the lines of witness_oracle_in.txt):
//   ray x y z dx dy dz [maxdist] [raw]    -> "ray hit dist=.. at=x,y,z ent=0x.. type=0x.." or "ray miss"
//   scan ID cx0 cy0 nx ny [ztop zbot]     -> witness_scan_ID.bin: the cells (cx0.., cy0..) of the walking map (tools/win/wnav.py REC records), built by core/oracle_cell.h
//   where                                 -> "where feet=x,y,z cam=x,y,z yaw=.. pitch=.."
//   ground x y [zfrom] [maxdist]          -> "ground z=.." or "ground none"   (zfrom: where the ray starts, default 400; maxdist default 800)
// Results: witness_oracle_out.txt, replaced whole after each batch, first line "# seq N n=COUNT" (a tool waits for a new seq).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
#include "game.h"
#include "core/oracle_cell.h"

void Log(const char* msg);
HMODULE g_selfModule();

namespace {
const uintptr_t kGlobals = 0x62D0A0, kGround = 0x241870, kBox = 0x1934C0, kRay = 0x254FF0, kHitInit = 0x2538A0, kReserve = 0x2E7FA0, kFree = 0x2E7F90;
struct Hit { unsigned char b[0x70]; };
struct List { int count, cap; void** data; };
typedef bool(__fastcall* GroundFn)(const float*, float*, Hit*, bool, bool, bool, float);
typedef void(__fastcall* BoxFn)(void*, const float*, const float*, int, List*, float, float);
typedef void(__fastcall* RayFn)(const float*, const float*, float, int, int, Hit*, List*);
typedef Hit*(__fastcall* HitInitFn)(Hit*);
const char* TypeName(uintptr_t ent, char* buf, size_t n) {      // the entity's type descriptor holds a pointer to its name at +8 ("Inanimate", "Collision_Volume", "Machine_Panel", ...)
    buf[0] = 0; if (!ent || IsBadReadPtr((void*)(ent + 8), 8)) return buf; uintptr_t ti = *(uintptr_t*)(ent + 8); if (IsBadReadPtr((void*)(ti + 8), 8)) return buf;
    const char* nm = *(const char**)(ti + 8); if (!nm || IsBadReadPtr(nm, 2)) return buf;
    size_t i = 0; for (; i + 1 < n && nm[i] >= 32 && nm[i] < 127 && !IsBadReadPtr(nm + i, 1); i++) buf[i] = nm[i]; buf[i] = 0; return buf;
}
typedef void(__fastcall* ReserveFn)(void*, void*, int, int);
typedef void(__fastcall* FreeFn)(void*);

CRITICAL_SECTION g_cs; bool g_init; std::vector<std::string> g_q; unsigned g_seq;
void Init() { if (!g_init) { InitializeCriticalSection(&g_cs); g_init = true; } }
uintptr_t Base() { return (uintptr_t)GetModuleHandleA(nullptr); }
bool Prologue(uintptr_t off, const unsigned char* want, size_t n) { const unsigned char* p = (const unsigned char*)(Base() + off); return !IsBadReadPtr(p, n) && !memcmp(p, want, n); }
// the build check: the prologues the offsets were read from (a different build refuses instead of calling into the middle of something)
bool Ready() {
    static int ok = -1; if (ok >= 0) return ok == 1;
    const unsigned char ground[] = {0x48, 0x8B, 0xC4, 0x4C, 0x89, 0x40, 0x18, 0x48, 0x89, 0x50, 0x10}, ray[] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x44, 0x89, 0x48, 0x20}, box[] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89, 0x70, 0x18};
    ok = Prologue(kGround, ground, sizeof ground) && Prologue(kRay, ray, sizeof ray) && Prologue(kBox, box, sizeof box) ? 1 : 0;
    if (!ok) Log("oracle: prologue mismatch, disabled"); return ok == 1;
}
void* World() { uintptr_t g = Base() + kGlobals; if (IsBadReadPtr((void*)g, 8)) return nullptr; return *(void**)g; }

struct ListGuard {      // a game array the destructor frees with the game's own Free
    List l{0, 0, nullptr};
    ListGuard() { ((ReserveFn)(Base() + kReserve))(&l.data, &l.cap, 16, 8); }
    ~ListGuard() { if (l.data) ((FreeFn)(Base() + kFree))(&l.data); }
};

// one ray against the world: the nearest hit, or false (the same queries as the ray command)
int g_rayMask = 4, g_boxMask = 0xC980001, g_rayFlags = -1;      // (the ray command can change them: `mask=0x404 box=0x4980001 flags=-1`; tests of what blocks the player)
bool RayQuery(const float* o, const float* dir, float maxd, float* dist, float* nrm, uintptr_t* entOut, int* cand) {
    void* w = World(); float end[3] = {o[0] + dir[0] * maxd, o[1] + dir[1] * maxd, o[2] + dir[2] * maxd};
    ListGuard lg; Hit hit; ((HitInitFn)(Base() + kHitInit))(&hit);
    ((BoxFn)(Base() + kBox))(w, o, end, g_boxMask, &lg.l, 0.0f, 0.0f);
    if (cand) *cand = lg.l.count;
    ((RayFn)(Base() + kRay))(o, dir, maxd, g_rayFlags, g_rayMask, &hit, &lg.l);
    float dd; uintptr_t ent; memcpy(&dd, hit.b, 4); memcpy(&ent, hit.b + 8, 8);
    if (entOut) *entOut = ent; if (nrm) memcpy(nrm, hit.b + 0x28, 12);
    if (!ent || !(dd < maxd + 1e-3f)) return false;
    *dist = dd; return true;
}
int g_groundFlags = 3;      // bits: a=1, b=2, c=4 of the game's own call (true, true, false); `ground ... flags=N`
bool GroundQuery(float x, float y, float zfrom, float maxd, float* z) {
    Hit hit; ((HitInitFn)(Base() + kHitInit))(&hit); float pos[3] = {x, y, zfrom}, out[3] = {0, 0, 0};
    if (!((GroundFn)(Base() + kGround))(pos, out, &hit, (g_groundFlags & 1) != 0, (g_groundFlags & 2) != 0, (g_groundFlags & 4) != 0, maxd)) return false;
    *z = out[2]; return true;
}
struct RealWorld {
    bool ground(float x, float y, float zfrom, float maxd, float* z) { return GroundQuery(x, y, zfrom, maxd, z); }
    bool ray(const float o[3], const float d[3], float maxd, float* dist, float* nz = nullptr) {
        float n[3]; uintptr_t ent = 0; bool h = RayQuery(o, d, maxd, dist, n, &ent, nullptr);
        if (h && nz) { char tn[40]; TypeName(ent, tn, sizeof tn); *nz = !strcmp(tn, "Collision_Volume") ? 0.0f : n[2]; }      // (the flat top of an invisible collision volume is not water: only the lake's own plane is)
        return h;
    }
};
struct ScanJob { std::string id; int cx0 = 0, cy0 = 0, nx = 0, ny = 0; float ztop = 250, zbot = -60; int at = 0; std::vector<unsigned char> out; unsigned withGround = 0; DWORD t0 = 0; };
std::deque<ScanJob> g_jobs;      // (a deque: the running job is referenced while the command thread adds more)

std::string DoRay(const float* o, const float* d, float maxd, bool raw) {
    float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); if (len < 1e-6f || maxd <= 0) return "ray bad";
    float dir[3] = {d[0] / len, d[1] / len, d[2] / len}, dist, nrm[3]; uintptr_t ent = 0; int cand = 0; char m[512], tn[40];
    if (!RayQuery(o, dir, maxd, &dist, nrm, &ent, &cand)) { snprintf(m, sizeof m, "ray miss cand=%d", cand); return m; }
    snprintf(m, sizeof m, "ray hit dist=%.4f at=%.4f,%.4f,%.4f n=%.3f,%.3f,%.3f type=%s ent=%#llx cand=%d", dist, o[0] + dir[0] * dist, o[1] + dir[1] * dist, o[2] + dir[2] * dist, nrm[0], nrm[1], nrm[2], TypeName(ent, tn, sizeof tn), (unsigned long long)ent, cand);
    (void)raw; return m;      // (the raw hit struct dump went when the layout was settled: distance +0, entity +8, normal +0x28)
}

std::string DoGround(float x, float y, float zfrom, float maxd) {
    ListGuard unused; (void)unused; Hit hit; ((HitInitFn)(Base() + kHitInit))(&hit);
    float pos[3] = {x, y, zfrom}, out[3] = {0, 0, 0}; char m[200];
    bool ok = ((GroundFn)(Base() + kGround))(pos, out, &hit, true, true, false, maxd);
    uintptr_t ent; memcpy(&ent, hit.b + 8, 8); char tn[40];
    if (!ok) return "ground none";
    snprintf(m, sizeof m, "ground z=%.4f type=%s", out[2], TypeName(ent, tn, sizeof tn)); return m;
}

std::string Run(const std::string& line) {
    float f[7]; int n;
    if (!strncmp(line.c_str(), "ray ", 4)) {
        const char* t; if ((t = strstr(line.c_str(), "mask="))) g_rayMask = (int)strtol(t + 5, nullptr, 0); if ((t = strstr(line.c_str(), "box="))) g_boxMask = (int)strtol(t + 4, nullptr, 0); if ((t = strstr(line.c_str(), "flags="))) g_rayFlags = (int)strtol(t + 6, nullptr, 0);
        n = sscanf(line.c_str(), "ray %f %f %f %f %f %f %f", &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6]); if (n < 6) return "ray bad";
        return DoRay(f, f + 3, n >= 7 ? f[6] : 100.0f, line.find(" raw") != std::string::npos);
    }
    if (!strncmp(line.c_str(), "ground ", 7)) {
        f[2] = 400; f[3] = 800; n = sscanf(line.c_str(), "ground %f %f %f %f", &f[0], &f[1], &f[2], &f[3]); if (n < 2) return "ground bad";
        const char* t; if ((t = strstr(line.c_str(), "flags="))) g_groundFlags = (int)strtol(t + 6, nullptr, 0);
        return DoGround(f[0], f[1], f[2], f[3]);
    }
    if (line == "where") {      // the player's feet (entity +0x24) and the camera (position and angles), as the teleport writes them
        uintptr_t pl = GameEntity(0x1E465); float* cam = (float*)(Base() + 0x6304C0); float* ang = (float*)(Base() + 0x6303EC); char m[300];
        if (!pl || IsBadReadPtr((void*)(pl + 0x24), 12) || IsBadReadPtr(cam, 12) || IsBadReadPtr(ang, 8)) return "where none";
        float* ft = (float*)(pl + 0x24); snprintf(m, sizeof m, "where feet=%.4f,%.4f,%.4f cam=%.4f,%.4f,%.4f yaw=%.4f pitch=%.4f", ft[0], ft[1], ft[2], cam[0], cam[1], cam[2], ang[0], ang[1]);
        return m;
    }
    return "bad";
}
std::wstring Dir() { wchar_t p[MAX_PATH]; GetModuleFileNameW(g_selfModule(), p, MAX_PATH); wchar_t* sl = wcsrchr(p, L'\\'); if (sl) sl[1] = 0; return p; }
}

// witness_cmd.txt line (command thread): `ray ...` and `ground ...` queue a line, `oracle` queues the lines of witness_oracle_in.txt
void OracleCommand(const char* line) {
    Init(); std::vector<std::string> add;
    if (!strncmp(line, "scan ", 5)) {
        ScanJob j; char id[64]; int n = sscanf(line, "scan %63s %d %d %d %d %f %f", id, &j.cx0, &j.cy0, &j.nx, &j.ny, &j.ztop, &j.zbot);
        if (n < 5 || j.nx <= 0 || j.ny <= 0 || (long long)j.nx * j.ny > 4000000) { Log("oracle: bad scan command"); return; }
        j.id = id; EnterCriticalSection(&g_cs); g_jobs.push_back(j); LeaveCriticalSection(&g_cs); return;
    }
    if (!strncmp(line, "oracle", 6)) {
        FILE* f = _wfopen((Dir() + L"witness_oracle_in.txt").c_str(), L"r"); if (!f) { Log("oracle: no witness_oracle_in.txt"); return; }
        char b[256]; while (fgets(b, sizeof b, f)) { std::string s = b; while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back(); if (!s.empty()) add.push_back(s); }
        fclose(f);
        if (add.empty()) add.push_back("");      // an empty batch still answers
    } else { std::string s = line; while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back(); add.push_back(s); }
    EnterCriticalSection(&g_cs); for (auto& s : add) g_q.push_back(s); g_q.push_back("\x01"); LeaveCriticalSection(&g_cs);      // \x01 ends a batch: the output file is written then
}

// render thread, once per frame
static void ScanTick() {      // the first scan job gets 10 ms of each frame
    EnterCriticalSection(&g_cs); bool any = !g_jobs.empty(); LeaveCriticalSection(&g_cs); if (!any) return;
    bool ready = Ready() && World() && GameEntity(0x1E465);
    EnterCriticalSection(&g_cs); ScanJob& j = g_jobs.front(); LeaveCriticalSection(&g_cs);      // (only this thread erases from the vector)
    if (!j.t0) j.t0 = GetTickCount();
    if (!ready) return;
    RealWorld rw; oraclecell::Scanner<RealWorld> sc(rw); LARGE_INTEGER f, a, b; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
    int total = j.nx * j.ny;
    while (j.at < total) {
        int cx = j.cx0 + j.at % j.nx, cy = j.cy0 + j.at / j.nx; oraclecell::Rec r = sc.scan(cx, cy, j.ztop, j.zbot); j.at++;
        if (r.z > -50.0f) j.withGround++;
        short sx = (short)cx, sy = (short)cy; unsigned char rec[17]; memcpy(rec, &sx, 2); memcpy(rec + 2, &sy, 2); memcpy(rec + 4, &r.z, 4); memcpy(rec + 8, &r.mask, 8); rec[16] = r.flags;
        j.out.insert(j.out.end(), rec, rec + 17);
        QueryPerformanceCounter(&b); if ((b.QuadPart - a.QuadPart) * 1000 / f.QuadPart >= 10) break;
    }
    if (j.at < total) return;
    std::wstring dir = Dir(), id(j.id.begin(), j.id.end()), tmp = dir + L"witness_scan_" + id + L".tmp", fin = dir + L"witness_scan_" + id + L".bin";
    FILE* fp = _wfopen(tmp.c_str(), L"wb"); if (fp) { fwrite(j.out.data(), 1, j.out.size(), fp); fclose(fp); MoveFileExW(tmp.c_str(), fin.c_str(), MOVEFILE_REPLACE_EXISTING); }
    char m[200]; snprintf(m, sizeof m, "oracle: scan %s done, %d cells (%u with ground) in %lu ms", j.id.c_str(), total, j.withGround, (unsigned long)(GetTickCount() - j.t0)); Log(m);
    EnterCriticalSection(&g_cs); g_jobs.erase(g_jobs.begin()); LeaveCriticalSection(&g_cs);
}

void OracleTick() {
    if (!g_init) return;
    ScanTick();
    EnterCriticalSection(&g_cs);
    if (g_q.empty()) { LeaveCriticalSection(&g_cs); return; }
    std::vector<std::string> q; q.swap(g_q); LeaveCriticalSection(&g_cs);
    std::string out; int n = 0; bool ready = Ready() && World() && GameEntity(0x1E465);
    for (auto& s : q) {
        if (s == "\x01") {
            std::wstring dir = Dir(), tmp = dir + L"witness_oracle_out.tmp", fin = dir + L"witness_oracle_out.txt";
            char head[64]; snprintf(head, sizeof head, "# seq %u n=%d\n", ++g_seq, n);
            FILE* f = _wfopen(tmp.c_str(), L"w"); if (f) { fputs(head, f); fputs(out.c_str(), f); fclose(f); MoveFileExW(tmp.c_str(), fin.c_str(), MOVEFILE_REPLACE_EXISTING); }
            out.clear(); n = 0; continue;
        }
        if (s.empty()) continue;
        out += (ready ? Run(s) : std::string("noworld")) + "\n"; n++;
    }
}
