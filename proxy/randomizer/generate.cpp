// See generate.h.
#include "generate.h"
#include <atomic>
#include <cwchar>

namespace wrand {
VariantRun FindVariant(Mode m) {
    switch (m) {
    case kNormal: case kExpert: case kVariety: return RunNsv;
    case kAscension: return RunVariety;
    case kDespairSymbols: case kDespair: return RunDespair;
    case kShuffler: case kEmptiness: case kPuzzlePack: return RunShuffler;
    case kStar: return RunStar;
    case kTutorializer: return RunTutorializer;
    default: return nullptr;
    }
}

static std::atomic_flag g_one = ATOMIC_FLAG_INIT;      // (MinGW's win32 threads have no std::mutex)
struct One { One() { while (g_one.test_and_set(std::memory_order_acquire)) {} } ~One() { g_one.clear(std::memory_order_release); } };
static thread_local std::function<void(const std::string&)>* t_report = nullptr;
void StatusW(const wchar_t* text) {
    if (!t_report || !*t_report || !text) return;
    std::string s; for (const wchar_t* p = text; *p; ++p) s += (*p < 128) ? (char)*p : '?';
    (*t_report)(s);
}

Result Generate(const Spec& spec, Raw* raw, Arrays* arrays, const std::function<void(const std::string&)>& status, const std::atomic<bool>* cancel) {
    One one;
    Result r;
    VariantRun run = spec.on() ? FindVariant(spec.mode) : nullptr;
    if (!run) { r.error = spec.on() ? "no generator for this randomizer" : "no randomizer chosen"; return r; }
    r.overlay.reset(new Overlay(raw, arrays, cancel));
    std::string last;
    std::function<void(const std::string&)> report = [&](const std::string& s) { last = s; if (status) status(s); };
    {
        ScopedBackend use(r.overlay.get());
        ScopedStatus said(report);
        t_report = &report;
        try {
            r.watchdogs = run(spec);
            r.ok = true;
        } catch (const std::exception& e) {
            r.error = std::string("the generator stopped: ") + e.what();
        } catch (...) {
            r.error = "the generator stopped (unknown error)";
        }
        t_report = nullptr;
    }
    r.lastStatus = last;
    if (!r.ok) { r.overlay.reset(); r.watchdogs.clear(); return r; }
    r.fingerprint = r.overlay->fingerprint();
    r.entities = r.overlay->changes().size();
    r.bytes = r.overlay->changedBytes();
    r.arrays = arrays->count();
    return r;
}
}  // namespace wrand
