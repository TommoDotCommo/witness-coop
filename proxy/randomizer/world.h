#pragma once
// A recorded game world for the game-free test (header-only): every read the randomizers made of a real game, enough to run them again without it.
// The game records one with the `randrecord` command (game_randomizer.cpp): each randomizer runs over a RecordingRaw, which keeps what it read,
// and the file also keeps what each run made (its fingerprint), so the test can check that the generator built for the test makes exactly the
// puzzles the generator built into the mod made in the game. The file is game data: it stays in local/, out of the repository.
//
// File: "WRW1", u32 entities, (i32 id, u64 address)..., u32 ranges, (u64 address, u32 n, n bytes)..., u32 runs, (u16 n, spec text, u64 fingerprint,
// u32 watchdogs, u32 entities, u32 bytes)...; little-endian.
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "backend.h"

namespace wrand {
struct RecordedRun { std::string spec; uint64_t fingerprint = 0; uint32_t watchdogs = 0, entities = 0, bytes = 0; };

class RangeStore {
public:
    void put(uintptr_t a, const uint8_t* p, size_t n) {
        if (!n) return;
        // inside a range already kept (the usual case: the same field read again): written there, nothing moves
        if (uint8_t* in = at(a, n)) { std::memcpy(in, p, n); return; }
        // otherwise merged with the ranges it overlaps (never with ranges it only touches: neighbouring objects would grow into one range that
        // every later read copies); the newest bytes win
        uintptr_t lo = a, hi = a + n;
        auto it = r_.upper_bound(a); if (it != r_.begin()) --it;
        std::vector<std::pair<uintptr_t, std::vector<uint8_t>>> merged;
        while (it != r_.end() && it->first < hi) {
            uintptr_t s = it->first, e = s + it->second.size();
            if (e > lo) { if (s < lo) lo = s; if (e > hi) hi = e; merged.emplace_back(s, std::move(it->second)); it = r_.erase(it); }
            else ++it;
        }
        std::vector<uint8_t> out(hi - lo);
        for (auto& m : merged) std::memcpy(out.data() + (m.first - lo), m.second.data(), m.second.size());
        std::memcpy(out.data() + (a - lo), p, n);
        r_[lo] = std::move(out);
    }
    bool get(uintptr_t a, void* d, size_t n) const {
        auto it = r_.upper_bound(a); if (it == r_.begin()) return false; --it;
        if (a < it->first || a + n > it->first + it->second.size()) return false;
        std::memcpy(d, it->second.data() + (a - it->first), n); return true;
    }
    uint8_t* at(uintptr_t a, size_t n) {
        auto it = r_.upper_bound(a); if (it == r_.begin()) return nullptr; --it;
        if (a < it->first || a + n > it->first + it->second.size()) return nullptr;
        return it->second.data() + (a - it->first);
    }
    const std::map<uintptr_t, std::vector<uint8_t>>& ranges() const { return r_; }
    std::map<uintptr_t, std::vector<uint8_t>>& ranges() { return r_; }
    size_t bytes() const { size_t s = 0; for (const auto& x : r_) s += x.second.size(); return s; }
private:
    std::map<uintptr_t, std::vector<uint8_t>> r_;
};

// Passes everything to the real game and keeps a copy of what was read.
class RecordingRaw : public Raw {
public:
    explicit RecordingRaw(Raw* inner) : in_(inner) {}
    uintptr_t entity(int id) override { uintptr_t e = in_->entity(id); if (e) ents_[id] = e; return e; }
    bool read(uintptr_t a, void* d, size_t n) override { if (!in_->read(a, d, n)) return false; mem_.put(a, (const uint8_t*)d, n); return true; }
    bool write(uintptr_t, const void*, size_t) override { return false; }      // recording only reads
    const std::map<int, uintptr_t>& ents() const { return ents_; }
    const RangeStore& mem() const { return mem_; }
private:
    Raw* in_; std::map<int, uintptr_t> ents_; RangeStore mem_;
};

// Serves exactly what was recorded; anything else is a miss (the generator then sees a failed read, as it would for memory the game does not have).
class ReplayRaw : public Raw {
public:
    std::map<int, uintptr_t> ents; RangeStore mem; unsigned misses = 0;
    int missEntity = -1; uintptr_t missAddr = 0;      // the last entity or address the recording did not have
    uintptr_t entity(int id) override { auto it = ents.find(id); if (it == ents.end()) { misses++; missEntity = id; return 0; } return it->second; }
    bool read(uintptr_t a, void* d, size_t n) override { if (mem.get(a, d, n)) return true; misses++; missAddr = a; return false; }
    bool write(uintptr_t a, const void* s, size_t n) override { mem.put(a, (const uint8_t*)s, n); return true; }      // (applying into a copy of the world in the test)
};

inline bool SaveWorld(const char* path, const RecordingRaw& rec, const std::vector<RecordedRun>& runs) {
    FILE* f = std::fopen(path, "wb"); if (!f) return false;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); }; auto u64 = [&](uint64_t v) { std::fwrite(&v, 8, 1, f); };
    std::fwrite("WRW1", 1, 4, f);
    u32((uint32_t)rec.ents().size()); for (const auto& e : rec.ents()) { u32((uint32_t)e.first); u64(e.second); }
    u32((uint32_t)rec.mem().ranges().size()); for (const auto& r : rec.mem().ranges()) { u64(r.first); u32((uint32_t)r.second.size()); std::fwrite(r.second.data(), 1, r.second.size(), f); }
    u32((uint32_t)runs.size());
    for (const auto& r : runs) { uint16_t n = (uint16_t)r.spec.size(); std::fwrite(&n, 2, 1, f); std::fwrite(r.spec.data(), 1, n, f); u64(r.fingerprint); u32(r.watchdogs); u32(r.entities); u32(r.bytes); }
    bool ok = !std::ferror(f); std::fclose(f); return ok;
}
inline bool LoadWorld(const char* path, ReplayRaw* w, std::vector<RecordedRun>* runs) {
    FILE* f = std::fopen(path, "rb"); if (!f) return false;
    bool ok = true;
    auto rd = [&](void* p, size_t n) { if (std::fread(p, 1, n, f) != n) ok = false; };
    auto u32 = [&]() { uint32_t v = 0; rd(&v, 4); return v; }; auto u64 = [&]() { uint64_t v = 0; rd(&v, 8); return v; };
    char magic[4] = {}; rd(magic, 4); if (std::memcmp(magic, "WRW1", 4)) ok = false;
    for (uint32_t i = 0, n = ok ? u32() : 0; ok && i < n; i++) { int id = (int)u32(); w->ents[id] = (uintptr_t)u64(); }
    for (uint32_t i = 0, n = ok ? u32() : 0; ok && i < n; i++) { uintptr_t a = (uintptr_t)u64(); uint32_t k = u32(); if (k > (1u << 28)) { ok = false; break; } std::vector<uint8_t> b(k); rd(b.data(), k); if (ok) w->mem.put(a, b.data(), k); }
    for (uint32_t i = 0, n = ok ? u32() : 0; ok && i < n; i++) {
        RecordedRun r; uint16_t k = 0; rd(&k, 2); r.spec.resize(k); rd(&r.spec[0], k); r.fingerprint = u64(); r.watchdogs = u32(); r.entities = u32(); r.bytes = u32(); runs->push_back(r);
    }
    std::fclose(f); return ok;
}
}  // namespace wrand
