#pragma once
// A fake game memory for the game-free tests: regions at made-up addresses, entities as regions, reads and writes that fail outside them.
#include <cstring>
#include <map>
#include <vector>
#include "../backend.h"

struct FakeRaw : wrand::Raw {
    std::map<uintptr_t, std::vector<uint8_t>> mem;       // region start -> bytes
    std::map<int, uintptr_t> ents;
    uintptr_t next;
    unsigned reads = 0, writes = 0;
    explicit FakeRaw(uintptr_t base = 0x140000000ull) : next(base) {}
    uintptr_t region(size_t n) { uintptr_t a = next; mem[a] = std::vector<uint8_t>(n, 0); next += (n + 0xFFF) & ~(uintptr_t)0xFFF; next += 0x1000; return a; }
    uintptr_t addEntity(int id, size_t n = 0x600) { uintptr_t a = region(n); ents[id] = a; return a; }
    uintptr_t addArray(const void* src, size_t n) { uintptr_t a = region(n); std::memcpy(mem[a].data(), src, n); return a; }
    uint8_t* at(uintptr_t a, size_t n) {
        auto it = mem.upper_bound(a); if (it == mem.begin()) return nullptr; --it;
        if (a < it->first || a + n > it->first + it->second.size()) return nullptr;
        return it->second.data() + (a - it->first);
    }
    template <class T> void put(int id, int off, T v) { std::memcpy(at(ents.at(id) + off, sizeof v), &v, sizeof v); }
    template <class T> T get(int id, int off) { T v; std::memcpy(&v, at(ents.at(id) + off, sizeof v), sizeof v); return v; }
    uintptr_t entity(int id) override { auto it = ents.find(id); return it == ents.end() ? 0 : it->second; }
    bool read(uintptr_t a, void* d, size_t n) override { reads++; uint8_t* p = at(a, n); if (!p) return false; std::memcpy(d, p, n); return true; }
    bool write(uintptr_t a, const void* s, size_t n) override { writes++; uint8_t* p = at(a, n); if (!p) return false; std::memcpy(p, s, n); return true; }
    std::vector<uint8_t> snapshot() const { std::vector<uint8_t> all; for (const auto& r : mem) all.insert(all.end(), r.second.begin(), r.second.end()); return all; }
};
