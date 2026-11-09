// The Witness Trainer's options that suit co-op (the launcher's Randomizers tab: "Witness Trainer"), ported from jbzdarkid's witness-trainer
// (https://github.com/jbzdarkid/witness-trainer, MIT licence, copyright 2026 jbzdarkid; dist/THIRD-PARTY-NOTICES.txt). Upstream is a separate
// program that finds the game's values with byte-pattern scans and writes them from outside; here the same scans run over the loaded exe and the
// writes happen on the render thread. The scans, their offsets and the values written are upstream's (Source/Trainer.cpp, Memory.cpp). Each player's
// own: sprint speed, field of view, noclip (a key), no challenge time limit, the environmental-puzzle overlay. Left out: upstream's practice tools
// (saved positions, random-doors practice, disabling saves, opening doors, distance gating), which are about one player's speedrun practice.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "trainer.h"
#include "config.h"
#include "game.h"

void Log(const char* msg);
void DevMenuNotice(const char* text);

namespace {
struct Found { int64_t noclip = 0, noclipSpeed = 0, fov = 0, runSpeed = 0, walkAccel = 0, walkDecel = 0, recordPlayerUpdate = 0, showPatternStatus = 0; bool done = false; };
Found g_f;
std::atomic<bool> g_noclipToggle{false};
bool g_noclipOn = false, g_applied = false;
float g_baseRun = 0, g_baseAccel = 0, g_baseDecel = 0;

uintptr_t Base() { return (uintptr_t)GetModuleHandleA(nullptr); }
bool Rd(uintptr_t a, void* d, size_t n) { SIZE_T got = 0; return ReadProcessMemory(GetCurrentProcess(), (void*)a, d, n, &got) && got == n; }
bool Wr(uintptr_t a, const void* s, size_t n) { SIZE_T put = 0; bool ok = WriteProcessMemory(GetCurrentProcess(), (void*)a, s, n, &put) && put == n; FlushInstructionCache(GetCurrentProcess(), (void*)a, n); return ok; }
template <class T> T R(int64_t rva) { T v{}; Rd(Base() + (uintptr_t)rva, &v, sizeof v); return v; }
template <class T> bool W(int64_t rva, T v) { return Wr(Base() + (uintptr_t)rva, &v, sizeof v); }

// upstream's Memory::ReadStaticInt: (the address of the next instruction) + (the 4-byte value at index): an address relative to the exe
int64_t StaticInt(const std::vector<uint8_t>& d, int index, int bytesToEOL = 4) { int32_t v; std::memcpy(&v, &d[index], 4); return (int64_t)index + bytesToEOL + v; }
std::vector<int> FindAll(const std::vector<uint8_t>& d, const char* hex) {
    std::vector<uint8_t> pat; for (const char* p = hex; *p;) { while (*p == ' ') p++; if (!*p) break; pat.push_back((uint8_t)strtoul(std::string(p, 2).c_str(), nullptr, 16)); p += 2; }
    std::vector<int> out;
    for (size_t i = 0; i + pat.size() <= d.size(); i++) if (std::memcmp(&d[i], pat.data(), pat.size()) == 0) out.push_back((int)i);
    return out;
}

void Scan() {      // upstream's sigscans (Trainer::Trainer), over the exe's image as it is loaded (offset 0 = the exe's base)
    auto* dos = (IMAGE_DOS_HEADER*)Base(); auto* nt = (IMAGE_NT_HEADERS*)(Base() + dos->e_lfanew);
    std::vector<uint8_t> d(nt->OptionalHeader.SizeOfImage);
    for (size_t i = 0; i < d.size(); i += 0x1000) Rd(Base() + i, &d[i], std::min<size_t>(0x1000, d.size() - i));      // (pages that cannot be read stay 0)
    for (int index : FindAll(d, "84 C0 75 59 BA 20 00 00 00")) {      // noclip: "search until we find mov eax, [addr]"
        for (int i = index; i < (int)d.size(); i++) if (d[i - 2] == 0x8B && d[i - 1] == 0x05) { g_f.noclip = StaticInt(d, i); break; }
        break;
    }
    for (int index : FindAll(d, "0F 29 7C 24 70 44 0F 29 54 24 60")) { g_f.noclipSpeed = StaticInt(d, index + 0x4F); break; }
    for (int index : FindAll(d, "48 85 C0 74 0A C7 80 28 03")) {      // the current field of view
        for (int i = index; i > 0; i--) if (d[i + 4] == 0x33 && d[i + 5] == 0xC9) { g_f.fov = StaticInt(d, i); break; }
        break;
    }
    for (int index : FindAll(d, "F3 0F 59 FD F3 0F 5C C8")) {      // walking acceleration and deceleration ("jmp +08"), then the run speed ("movss xmm1, [addr]")
        int i = index;
        for (; i < (int)d.size(); i++) if (d[i - 2] == 0xEB && d[i - 1] == 0x08) { g_f.walkAccel = StaticInt(d, i - 0x06); g_f.walkDecel = StaticInt(d, i + 0x04); break; }
        for (; i < (int)d.size(); i++) if (d[i - 4] == 0xF3 && d[i - 3] == 0x0F && d[i - 2] == 0x10 && d[i - 1] == 0x0D) { g_f.runSpeed = StaticInt(d, i); break; }
        if (g_f.runSpeed) break;
    }
    for (int index : FindAll(d, "00 00 00 05 00 00 00 E9 B3")) { g_f.recordPlayerUpdate = index - 0x0C; break; }      // the challenge's time limit
    for (int index : FindAll(d, "0F 84 38 06 00 00 48 89 58 F0")) { g_f.showPatternStatus = StaticInt(d, index - 5, 5); break; }
    g_f.done = true;
    char m[300]; snprintf(m, sizeof m, "trainer: scans noclip %llx speed %llx fov %llx run %llx accel %llx decel %llx challenge %llx eps %llx",
        (long long)g_f.noclip, (long long)g_f.noclipSpeed, (long long)g_f.fov, (long long)g_f.runSpeed, (long long)g_f.walkAccel, (long long)g_f.walkDecel,
        (long long)g_f.recordPlayerUpdate, (long long)g_f.showPatternStatus);
    Log(m);
}
bool Wanted() { const Cfg& c = GetCfg(); return c.trainerSpeed != 100 || c.trainerFov > 0 || c.trainerNoclip || c.trainerNoChallengeLimit || c.trainerEpOverlay; }

void SetFov(double fov) {      // upstream's SetFov: the horizontal angle chosen, as the game's vertical one (fov_vertical_from_horizontal)
    if (!g_f.fov) return;
    fov *= 0.00872664625997165; fov = tan(fov); fov *= 0.5625f; fov = atan(fov); fov *= 114.5915590261646;
    W<float>(g_f.fov, (float)fov);
}
void ApplyOnce() {
    const Cfg& c = GetCfg();
    if (g_f.runSpeed && g_f.walkAccel && g_f.walkDecel) {      // upstream's SetSprintSpeed, from the game's own values (never compounded)
        g_baseRun = R<float>(g_f.runSpeed); g_baseAccel = R<float>(g_f.walkAccel); g_baseDecel = R<float>(g_f.walkDecel);
        if (c.trainerSpeed != 100 && g_baseRun > 0) {
            float k = c.trainerSpeed / 100.0f;
            W<float>(g_f.runSpeed, g_baseRun * k); W<float>(g_f.walkAccel, g_baseAccel * k); W<float>(g_f.walkDecel, g_baseDecel * k);
        }
    }
    if (c.trainerNoChallengeLimit && g_f.recordPlayerUpdate) {      // upstream's SetInfiniteChallenge: jump over abort_speed_run
        uint8_t orig[4], want[4] = {0x48, 0x8B, 0x4B, 0x18}, patch[4] = {0xEB, 0x07, 0x66, 0x90};
        if (Rd(Base() + g_f.recordPlayerUpdate, orig, 4) && std::memcmp(orig, want, 4) == 0) Wr(Base() + g_f.recordPlayerUpdate, patch, 4);
        else Log("trainer: the challenge's code is not as expected: its time limit stays");
    }
    if (c.trainerEpOverlay && g_f.showPatternStatus) W<uint8_t>(g_f.showPatternStatus, 1);
    char m[200]; snprintf(m, sizeof m, "trainer: applied speed %d%% (run %.4f) fov %d noclip key %d no challenge limit %d ep overlay %d", c.trainerSpeed, g_baseRun, c.trainerFov,
                          c.trainerNoclip ? 1 : 0, c.trainerNoChallengeLimit ? 1 : 0, c.trainerEpOverlay ? 1 : 0);
    Log(m);
}
}  // namespace

void TrainerFrame() {
    if (!Wanted()) return;
    if (!g_f.done) Scan();
    if (!g_applied && GameEntity(0x1E465)) { g_applied = true; ApplyOnce(); }      // once the player exists (a world is loaded)
    const Cfg& c = GetCfg();
    if (c.trainerFov > 0) SetFov(c.trainerFov);      // every frame: the game sets it again from its own option
    if (g_noclipToggle.exchange(false) && c.trainerNoclip && g_f.noclip) {
        if (g_noclipOn) {      // upstream: leaving noclip puts the player's feet under the camera (the eye is 1.69 above them)
            float eye[3]; uintptr_t p = GameEntity(0x1E465);
            if (p && Rd(Base() + 0x6304C0, eye, sizeof eye)) { float feet[3] = {eye[0], eye[1], eye[2] - 1.69f}; Wr(p + 0x24, feet, sizeof feet); }
        }
        g_noclipOn = !g_noclipOn; W<uint8_t>(g_f.noclip, g_noclipOn ? 1 : 0);
        DevMenuNotice(g_noclipOn ? "Noclip on (Witness Trainer)" : "Noclip off");
    }
}
bool TrainerKey(unsigned vk, bool down) {
    const Cfg& c = GetCfg();
    if (!c.trainerNoclip || !c.keyNoclip || vk != c.keyNoclip) return false;
    if (down) g_noclipToggle = true;
    return true;
}
bool TrainerCommand(const char* line) {      // trainerstat: what the scans found and the values now (tests)
    if (strncmp(line, "trainerstat", 11)) return false;
    char m[300]; snprintf(m, sizeof m, "trainerstat: found %d run %.3f accel %.3f decel %.3f fov %.3f noclip %d challenge %02x eps %d", g_f.done ? 1 : 0,
        g_f.runSpeed ? R<float>(g_f.runSpeed) : 0.f, g_f.walkAccel ? R<float>(g_f.walkAccel) : 0.f, g_f.walkDecel ? R<float>(g_f.walkDecel) : 0.f,
        g_f.fov ? R<float>(g_f.fov) : 0.f, g_f.noclip ? (int)R<uint8_t>(g_f.noclip) : -1, g_f.recordPlayerUpdate ? R<uint8_t>(g_f.recordPlayerUpdate) : 0,
        g_f.showPatternStatus ? (int)R<uint8_t>(g_f.showPatternStatus) : -1);
    Log(m); return true;
}
