// See game_randomizer.h. Build 11370412.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "game_randomizer.h"
#include "generate.h"
#include "world.h"
#include "Watchdog.h"          // third_party/witness-randomizer/src (upstream's watchdogs)
#include "../../common/notices.h"
#include "../../common/randomizers.h"
#include "../config.h"
#include "../coop.h"
#include "../game.h"
#include "../core/sync_core.h"

void Log(const char* msg);
void DevMenuNotice(const char* text);

namespace {
// ---- the game's memory: reads that fail instead of faulting (any thread), writes (render thread)
struct GameRaw : wrand::Raw {
    uintptr_t entity(int id) override { return id >= 0 && id < 0x40000 ? GameEntity((unsigned)id) : 0; }
    bool read(uintptr_t a, void* d, size_t n) override { SIZE_T got = 0; return ReadProcessMemory(GetCurrentProcess(), (void*)a, d, n, &got) && got == n; }
    bool write(uintptr_t a, const void* s, size_t n) override { SIZE_T put = 0; return WriteProcessMemory(GetCurrentProcess(), (void*)a, s, n, &put) && put == n; }
};
GameRaw g_raw;

// ---- memory for the randomizer's arrays: 1 MB chunks handed out in order (upstream made one VirtualAllocEx per array), freed with the generation.
// No array starts at a chunk's first bytes: upstream tests some array pointers for null through their low 32 bits (ReadPanelData<int>), which
// are 0 at an address that is a multiple of 4 GB.
class Arena {
public:
    ~Arena() { for (void* c : chunks_) VirtualFree(c, 0, MEM_RELEASE); }
    uint8_t* alloc(size_t n) {
        n = (n + 15) & ~(size_t)15;
        if (!cur_ || used_ + n > cap_) {
            size_t want = n + 16 > kChunk ? ((n + 16 + 0xFFFF) & ~(size_t)0xFFFF) : kChunk;
            void* p = VirtualAlloc(nullptr, want, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (!p) return nullptr;      // the heap then (Arrays falls back)
            chunks_.push_back(p); cur_ = (uint8_t*)p; used_ = 16; cap_ = want;
        }
        uint8_t* r = cur_ + used_; used_ += n; return r;
    }
    size_t chunks() const { return chunks_.size(); }
private:
    static constexpr size_t kChunk = 1 << 20;
    std::vector<void*> chunks_; uint8_t* cur_ = nullptr; size_t used_ = 0, cap_ = 0;
};

// ---- one generation: its arrays, its result
struct Gen {
    wrand::Spec spec;
    uint64_t world = 0;
    std::unique_ptr<Arena> arena;
    std::unique_ptr<wrand::Arrays> arrays;
    wrand::Result result;
    std::atomic<bool> done{false};
    std::string status;  SRWLOCK statusLock = SRWLOCK_INIT;
    DWORD started = 0, finished = 0;
};
DWORD WINAPI GenThread(void* p) {
    Gen* g = (Gen*)p;
    g->result = wrand::Generate(g->spec, &g_raw, g->arrays.get(), [g](const std::string& s) { AcquireSRWLockExclusive(&g->statusLock); g->status = s; ReleaseSRWLockExclusive(&g->statusLock); });
    g->finished = GetTickCount(); g->done = true;
    return 0;
}

enum State { kOff, kWaitWorld, kGenerating, kApplied, kFailed };
const char* StateName(State s) { static const char* n[] = {"off", "waiting for the world", "generating", "applied", "failed"}; return n[s]; }
State g_state = kOff;
std::atomic<bool> g_settled{false};
std::atomic<uint64_t> g_fp{0};
wrand::Spec g_spec; bool g_specRead = false;
std::unique_ptr<Gen> g_gen;                       // the one generating or applied
std::vector<std::unique_ptr<Gen>> g_old;          // replaced ones: their arrays may still be read for a moment (freed two generations later)
uint64_t g_lastWorld = 0; DWORD g_stableSince = 0;
struct Dog { Watchdog* w; DWORD next; };
std::vector<Dog> g_dogs;
wrand::Arrays g_liveArrays([](size_t n) -> uint8_t* { static Arena a; return a.alloc(n); });      // what the watchdogs make (a failed arrow trace: a new sequence)
DWORD g_nextPeerCheck = 0; std::string g_toldDiffer;

uint64_t WorldId() {      // the addresses of three tutorial panels: a loaded save re-creates every entity (state.cpp's generation)
    uint64_t h = 1469598103934665603ull;
    for (unsigned id : {0x64u, 0x182u, 0x293u}) { uintptr_t e = GameEntity(id); if (!e) return 0; h = (h ^ (uint64_t)e) * 1099511628211ull; }
    return h;
}
void Logf(const char* fmt, ...) { char m[600]; va_list a; va_start(a, fmt); vsnprintf(m, sizeof m, fmt, a); va_end(a); Log(m); }

void SetCore(uint8_t state) { if (auto* c = CoopCore()) c->setPuzzles(g_fp.load(), state); }

void StartGeneration(uint64_t world) {
    if (g_gen) { g_old.push_back(std::move(g_gen)); while (g_old.size() > 2) g_old.erase(g_old.begin()); }
    g_dogs.clear();
    auto* g = new Gen; g->spec = g_spec; g->world = world; g->arena.reset(new Arena);
    Arena* arena = g->arena.get();
    g->arrays.reset(new wrand::Arrays([arena](size_t n) { return arena->alloc(n); }));
    g->started = GetTickCount();
    g_gen.reset(g);
    HANDLE t = CreateThread(nullptr, 1 << 22, GenThread, g, 0, nullptr);      // 4 MB of stack: upstream's path search recurses
    if (!t) { g->result.error = "could not start the generator's thread"; g->done = true; } else { SetThreadPriority(t, THREAD_PRIORITY_ABOVE_NORMAL); CloseHandle(t); }      // (it competes with the game's own loading; the player waits for it)
    g_state = kGenerating; g_settled = false; g_fp = 0; SetCore(witness::PUZ_WORKING);
    Logf("randomizer: generating '%s' for world %016llx", wrand::SpecText(g_spec).c_str(), (unsigned long long)world);
}

void Finish() {
    Gen* g = g_gen.get();
    uint64_t now = WorldId();
    if (!g->result.ok) {
        g_state = kFailed; g_settled = true; g_fp = 0; SetCore(witness::PUZ_FAILED);
        Logf("randomizer: FAILED after %lu ms: %s (last status '%s')", g->finished - g->started, g->result.error.c_str(), g->result.lastStatus.c_str());
        DevMenuNotice(wnotice::PuzzlesFailed().c_str());
        return;
    }
    if (now != g->world) { Logf("randomizer: the world changed while generating: again"); g_state = kWaitWorld; g_settled = false; SetCore(witness::PUZ_WORKING); return; }
#ifndef WR_MUT_NO_APPLY
    if (!g->result.overlay->apply(&g_raw)) { Logf("randomizer: could not apply (an entity changed): again"); g_state = kWaitWorld; g_settled = false; return; }
#endif
    DWORD t = GetTickCount();
    for (Watchdog* w : g->result.watchdogs) g_dogs.push_back(Dog{w, t + (DWORD)(w->sleepTime * 1000)});
    g_state = kApplied; g_fp = g->result.fingerprint; g_settled = true; SetCore(witness::PUZ_READY);
    Logf("randomizer: applied '%s' in %lu ms: %zu entities, %zu bytes, %zu arrays (%zu bytes), %zu watchdogs, fingerprint %016llx%s",
         wrand::SpecText(g->spec).c_str(), g->finished - g->started, g->result.entities, g->result.bytes, g->arrays->count(), g->arrays->bytes(),
         g->result.watchdogs.size(), (unsigned long long)g->result.fingerprint, "");
    DevMenuNotice(("Puzzles randomized: " + wrand::SpecSummary(g->spec)).c_str());
}

void TickWatchdogs() {
    if (g_dogs.empty()) return;
    wrand::Live live(&g_raw, &g_liveArrays);
    wrand::ScopedBackend use(&live);
    DWORD now = GetTickCount();
    for (size_t i = 0; i < g_dogs.size();) {
        Dog& d = g_dogs[i];
        if (d.w->terminate) { g_dogs.erase(g_dogs.begin() + i); continue; }
        if ((LONG)(now - d.next) >= 0) {
            try { d.w->action(); } catch (const std::exception& e) { Logf("randomizer: a watchdog stopped: %s", e.what()); d.w->terminate = true; }
            d.next = now + (DWORD)(d.w->sleepTime * 1000);
        }
        i++;
    }
}

void CheckPeers() {
    DWORD now = GetTickCount(); if ((LONG)(now - g_nextPeerCheck) < 0) return; g_nextPeerCheck = now + 2000;
    auto* c = CoopCore(); if (!c) return;
    uint64_t mine = g_fp.load(); bool ready = g_state == kApplied || g_state == kFailed || g_state == kOff;
    if (!ready) return;
    for (const auto& p : c->peerPuzzles()) {
        if (p.state == witness::PUZ_WORKING) continue;
        bool same = p.fingerprint == mine;
        std::string key = std::to_string(p.id) + ":" + std::to_string(p.fingerprint) + ":" + std::to_string(mine);
        if (!same && g_toldDiffer.find("|" + key + "|") == std::string::npos) {
            g_toldDiffer += "|" + key + "|";
            std::string name = p.name.empty() ? "Player " + std::to_string(p.id) : p.name;
            Logf("randomizer: %s's puzzles differ (theirs %016llx state %u, mine %016llx)", name.c_str(), (unsigned long long)p.fingerprint, p.state, (unsigned long long)mine);
            DevMenuNotice(wnotice::PuzzlesDiffer(name).c_str());
        }
    }
}

// ---- randrecord: every randomizer over a recording of this world, written with what each made (for the game-free test)
struct Recording { std::string path, only; std::atomic<bool> done{false}; bool ok = false; };
DWORD WINAPI RecordThread(void* p) {
    Recording* r = (Recording*)p;
    wrand::RecordingRaw rec(&g_raw);
    std::vector<wrand::RecordedRun> runs;
    std::vector<wrand::Spec> specs;
    for (const auto& i : wrand::Randomizers()) {
        wrand::Spec s; s.mode = i.mode; s.seed = 4242; specs.push_back(s);
        s.seed = 7; s.fast = true; s.colorblind = true; s.shuffle = i.canShuffle; specs.push_back(s);
    }
    if (!r->only.empty()) { specs.clear(); specs.push_back(wrand::ParseSpec(r->only)); }
    for (const auto& s : specs) {
        wrand::Arrays arrays;
        DWORD t0 = GetTickCount();
        auto res = wrand::Generate(s, &rec, &arrays);
        DWORD ms = GetTickCount() - t0;
        wrand::RecordedRun run; run.spec = wrand::SpecText(s); run.fingerprint = res.ok ? res.fingerprint : 0; run.watchdogs = (uint32_t)res.watchdogs.size();
        run.entities = (uint32_t)res.entities; run.bytes = (uint32_t)res.bytes; runs.push_back(run);
        char m[300]; snprintf(m, sizeof m, "randrecord: '%s' %s in %lu ms fingerprint %016llx watchdogs %u entities %u bytes %u", run.spec.c_str(), res.ok ? "ok" : res.error.c_str(),
                             ms, (unsigned long long)run.fingerprint, run.watchdogs, run.entities, run.bytes);
        Log(m);
        r->ok = wrand::SaveWorld(r->path.c_str(), rec, runs);      // after every run: a long one still leaves the others
    }
    char m[400]; snprintf(m, sizeof m, "randrecord: done, %s %s (%zu runs, %zu entities, %zu ranges, %zu bytes)", r->ok ? "wrote" : "could not write", r->path.c_str(), runs.size(), rec.ents().size(), rec.mem().ranges().size(), rec.mem().bytes());
    Log(m); r->done = true;
    return 0;
}
}  // namespace

void RandomizerFrame() {
    if (!g_specRead) {
        g_specRead = true; g_spec = wrand::ParseSpec(GetCfg().randomizer);
        if (g_spec.on()) { g_state = kWaitWorld; Logf("randomizer: %s ('%s')", wrand::SpecSummary(g_spec).c_str(), wrand::SpecText(g_spec).c_str()); }
        else { g_state = kOff; g_settled = true; }
    }
    if (g_state == kOff) { SetCore(witness::PUZ_VANILLA); CheckPeers(); return; }
    uint64_t world = WorldId();
    DWORD now = GetTickCount();
    if ((g_state == kApplied || g_state == kFailed) && g_gen && world != g_gen->world) {      // another world (a save loaded, a new game): its own puzzles
        Logf("randomizer: the world was loaded again: new puzzles for it");
        g_state = kWaitWorld; g_settled = false; g_fp = 0; g_dogs.clear(); SetCore(witness::PUZ_WORKING);
    }
    switch (g_state) {
    case kWaitWorld:
        g_settled = false;
        if (!world) { g_lastWorld = 0; g_stableSince = 0; break; }
        if (world != g_lastWorld) { g_lastWorld = world; g_stableSince = now; break; }
        if (now - g_stableSince >= 1500) StartGeneration(world);      // the same world for a moment (a load builds it in steps)
        break;
    case kGenerating:
        if (g_gen && g_gen->done) Finish();
        break;
    case kApplied: TickWatchdogs(); break;
    default: break;
    }
    CheckPeers();
}
bool RandomizerSettled() {
#ifdef WR_MUT_NOT_GATED
    return true;
#endif
    return g_settled.load();
}
uint64_t RandomizerFingerprint() { return g_fp.load(); }

bool RandomizerCommand(const char* line) {
    if (!strncmp(line, "randstat", 8)) {
        Gen* g = g_gen.get();
        Logf("randstat: state %s spec '%s' settled %d fingerprint %016llx world %016llx entities %zu bytes %zu arrays %zu watchdogs %zu active %zu ms %lu error '%s'",
             StateName(g_state), wrand::SpecText(g_spec).c_str(), (int)g_settled.load(), (unsigned long long)g_fp.load(), (unsigned long long)(g ? g->world : 0),
             g && g->done ? g->result.entities : 0, g && g->done ? g->result.bytes : 0, g && g->done ? g->arrays->count() : 0, g && g->done ? g->result.watchdogs.size() : 0,
             g_dogs.size(), g && g->done ? g->finished - g->started : 0, g && g->done ? g->result.error.c_str() : "");
        if (auto* c = CoopCore()) for (const auto& p : c->peerPuzzles()) Logf("randstat: peer %u fingerprint %016llx state %u", p.id, (unsigned long long)p.fingerprint, p.state);
        return true;
    }
    if (!strncmp(line, "randpanel ", 10)) {      // a panel's puzzle as the game has it now: size, counts and a hash of its decorations and dots
        unsigned id = 0; if (sscanf(line + 10, "%x", &id) != 1) return true;
        uintptr_t e = GameEntity(id); if (!e) { Logf("randpanel: %x missing", id); return true; }
        auto rd32 = [&](int off) { uint32_t v = 0; g_raw.read(e + off, &v, 4); return v; };
        auto rd64 = [&](int off) { uint64_t v = 0; g_raw.read(e + off, &v, 8); return v; };
        uint32_t nd = rd32(0x430), nDots = rd32(0x3B4), gx = rd32(0x440), gy = rd32(0x444), style = rd32(0x448);
        uint64_t h = wrand::kFnvInit;
        std::vector<uint8_t> b;
        if (uint64_t p = rd64(0x418)) { b.resize(nd * 4u); if (nd && nd < 4096 && g_raw.read(p, b.data(), b.size())) h = wrand::Fnv(h, b.data(), b.size()); }
        if (uint64_t p = rd64(0x3C8)) { b.resize(nDots * 4u); if (nDots && nDots < 8192 && g_raw.read(p, b.data(), b.size())) h = wrand::Fnv(h, b.data(), b.size()); }
        Logf("randpanel: %x grid %ux%u style %x decorations %u dots %u hash %016llx", id, gx, gy, style, nd, nDots, (unsigned long long)h);
        return true;
    }
    if (!strncmp(line, "randrecord ", 11)) {
        static Recording* r = nullptr;
        if (r && !r->done) { Log("randrecord: one is running"); return true; }
        r = new Recording; std::string a = line + 11; while (!a.empty() && (a.back() == '\r' || a.back() == '\n' || a.back() == ' ')) a.pop_back();
        size_t sp = a.find(' '); r->path = a.substr(0, sp); if (sp != std::string::npos) r->only = a.substr(sp + 1);      // randrecord <file> [spec]
        HANDLE t = CreateThread(nullptr, 1 << 22, RecordThread, r, 0, nullptr); if (t) CloseHandle(t);
        Log("randrecord: started");
        return true;
    }
    return false;
}
