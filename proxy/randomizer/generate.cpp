// See generate.h.
#include "generate.h"
#include <atomic>
#include "Randomizer.h"          // third_party/witness-randomizer/src
#include "Panel.h"
#include "Generate.h"
#include "Special.h"
#include "Watchdog.h"
#include "Random.h"

// Upstream keeps state in statics between runs (one run per process there). A second run in the same game (after a save is loaded) must start
// from the same state as the first, or it would make other puzzles than a partner's first run. Panel and Generate name these two classes as
// friends (upstream's panel test and Archipelago's watchdog, neither of which is built here), which lets the reset reach their private statics
// without changing upstream's files.
class PanelExtractionTests { public: static void Reset() { Panel::generatedPanels.clear(); Panel::arrowPuzzles.clear(); } };
class APWatchdog { public: static void Reset() { Generate::_SHAPEDIRECTIONS.clear(); } };

std::mt19937 Random::gen = std::mt19937(0);        // upstream's Random.cpp seeds it from the clock; every run here seeds it from the Spec

namespace wrand {
static thread_local std::vector<Watchdog*>* t_started = nullptr;
}
#ifndef WR_MUT_WATCHDOG_THREAD
void Watchdog::start() { if (wrand::t_started) wrand::t_started->push_back(this); }      // upstream: a thread of its own (Watchdog.cpp, left out by WITNESS_COOP)
#else
void Watchdog::start() {}      // mutant: the watchdogs are lost
#endif
void Watchdog::run() {}

namespace wrand {
static std::atomic_flag g_one = ATOMIC_FLAG_INIT;      // (MinGW's win32 threads have no std::mutex)
struct One { One() { while (g_one.test_and_set(std::memory_order_acquire)) {} } ~One() { g_one.clear(std::memory_order_release); } };

Result Generate(const Spec& spec, Raw* raw, Arrays* arrays, const std::function<void(const std::string&)>& status) {
    One one;
    Result r;
    if (!spec.on()) { r.error = "no randomizer chosen"; return r; }
    r.overlay.reset(new Overlay(raw, arrays));
    std::vector<Watchdog*> started;
    t_started = &started;
    std::string last;
    {
        ScopedBackend use(r.overlay.get());
        ScopedStatus report([&](const std::string& s) { last = s; if (status) status(s); });
#ifndef WR_MUT_NO_RESET
        PanelExtractionTests::Reset(); APWatchdog::Reset(); Point::pillarWidth = 0; Special::correctShapesById.clear();
#endif
        Random::seed((int)(spec.seed & 0x7FFFFFFFu));
        try {
            Randomizer rz;
            rz.seed = (int)(spec.seed & 0x7FFFFFFFu);
            rz.seedIsRNG = false;
            rz.colorblind = spec.colorblind;
            rz.doubleMode = spec.shuffle;
            if (spec.mode == kNormal) rz.GenerateNormal();
            else if (spec.mode == kExpert) rz.GenerateHard();
            else if (spec.mode == kVariety) rz.GenerateVariety();
            if (spec.fast) rz.AdjustSpeed();
            r.ok = true;
        } catch (const std::exception& e) {
            r.error = std::string("the generator stopped: ") + e.what();
        } catch (...) {
            r.error = "the generator stopped (unknown error)";
        }
    }
    t_started = nullptr;
    r.lastStatus = last;
    if (!r.ok) { r.overlay.reset(); return r; }
    r.watchdogs = std::move(started);
    r.fingerprint = r.overlay->fingerprint();
    r.entities = r.overlay->changes().size();
    r.bytes = r.overlay->changedBytes();
    r.arrays = arrays->count();
    return r;
}
}  // namespace wrand
