#pragma once
// Where the randomizer's memory accesses go (game-free). The vendored generator (third_party/witness-randomizer) calls Memory::get()->ReadPanelData and the
// like; our Memory (third_party/witness-randomizer/shim/Memory.h) forwards each call to the calling thread's Backend:
//   generation thread   Overlay over the live game: reads see the game plus what the generator wrote, writes stay in the overlay (nothing reaches the game)
//   render thread       the live game itself (applying an overlay, the watchdogs)
//   tests               a fake world (tests/fake_world.h) or a recorded one
// An entity is addressed by its id in the game's entity table; a field by its byte offset in the entity; an array by the pointer field that points to it.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace wrand {

class Backend {
public:
    virtual ~Backend() = default;
    virtual bool readField(int id, int off, void* dst, size_t n) = 0;
    virtual bool writeField(int id, int off, const void* src, size_t n) = 0;
    virtual bool readArray(int id, int off, void* dst, size_t n) = 0;                 // n bytes from where the pointer at (id, off) points
    // n bytes into the array the pointer at (id, off) points to; `known` bytes of it are known to exist (the upstream size bookkeeping), known >= n
    virtual bool writeArrayInPlace(int id, int off, const void* src, size_t n, size_t known) = 0;
    virtual bool writeArrayNew(int id, int off, const void* src, size_t n) = 0;     // a new array of n bytes, the pointer at (id, off) set to it
};

// The calling thread's backend for the duration of a scope (Memory::get() reads it).
Backend* CurrentBackend();
class ScopedBackend {
public:
    explicit ScopedBackend(Backend* b);
    ~ScopedBackend();
    ScopedBackend(const ScopedBackend&) = delete; ScopedBackend& operator=(const ScopedBackend&) = delete;
private:
    Backend* prev_;
};

// The calling thread's progress reports from the generator (upstream's status line) for the duration of a scope.
class ScopedStatus {
public:
    explicit ScopedStatus(std::function<void(const std::string&)> f);
    ~ScopedStatus();
private:
    std::function<void(const std::string&)> f_; std::function<void(const std::string&)>* prev_;
};

// Arrays the randomizer made. They live in this process, in memory the game reads once the overlay is applied, and live as long as this
// object. `alloc` gives the memory (the game: below 4 GB, because upstream keeps some pointers in 32-bit longs); none: the heap.
class Arrays {
public:
    explicit Arrays(std::function<uint8_t*(size_t)> alloc = nullptr) : alloc_(std::move(alloc)) {}
    uintptr_t make(const void* src, size_t n);                  // a new array holding a copy of src (n bytes, at least 8 allocated)
    bool owns(uintptr_t p, size_t* size = nullptr) const;       // p is the start of one of ours (size: the bytes it was made with)
    uint8_t* data(uintptr_t p);                                 // nullptr when not ours
    size_t count() const { return blocks_.size(); }
    size_t bytes() const;
private:
    struct Block { size_t size; std::unique_ptr<std::vector<uint8_t>> own; };
    std::function<uint8_t*(size_t)> alloc_;
    std::map<uintptr_t, Block> blocks_;
};

// Raw memory of the live game: reads that fail instead of faulting (the platform's safe copy). Tests give a fake one.
class Raw {
public:
    virtual ~Raw() = default;
    virtual uintptr_t entity(int id) = 0;                       // 0: no such entity
    virtual bool read(uintptr_t addr, void* dst, size_t n) = 0;
    virtual bool write(uintptr_t addr, const void* src, size_t n) = 0;
};

// The live game through Raw: what the render thread uses (watchdogs, applying). New arrays come from `arrays`.
class Live : public Backend {
public:
    Live(Raw* raw, Arrays* arrays) : raw_(raw), arrays_(arrays) {}
    bool readField(int id, int off, void* dst, size_t n) override;
    bool writeField(int id, int off, const void* src, size_t n) override;
    bool readArray(int id, int off, void* dst, size_t n) override;
    bool writeArrayInPlace(int id, int off, const void* src, size_t n, size_t known) override;
    bool writeArrayNew(int id, int off, const void* src, size_t n) override;
private:
    Raw* raw_; Arrays* arrays_;
};

// Reads go to the game (through Raw) under what was written; writes stay here until Apply. An array the generator writes is copied first
// (copy on write) into one of `arrays`, so the game's own arrays are never touched by the generator.
class Overlay : public Backend {
public:
    Overlay(Raw* raw, Arrays* arrays) : raw_(raw), arrays_(arrays) {}
    bool readField(int id, int off, void* dst, size_t n) override;
    bool writeField(int id, int off, const void* src, size_t n) override;
    bool readArray(int id, int off, void* dst, size_t n) override;
    bool writeArrayInPlace(int id, int off, const void* src, size_t n, size_t known) override;
    bool writeArrayNew(int id, int off, const void* src, size_t n) override;

    // the result
    struct Run { int off; std::vector<uint8_t> bytes; };
    std::map<int, std::vector<Run>> changes() const;            // per entity, the changed bytes as runs (ascending, merged)
    std::map<int, uintptr_t> entities() const { return ents_; } // the entity pointer each touched id had when it was first read
    size_t changedBytes() const;
    // A hash of what changed that is the same on every PC for the same game data: our arrays are hashed by their contents and a pointer the
    // game had by where it was first read from (entity and offset), never by its address.
    uint64_t fingerprint() const;
    // Writes every change into the live game (render thread). False, and nothing written, when an entity is gone or is not the object the
    // generation read (the world was reloaded in between).
    bool apply(Raw* live) const;
private:
    struct Ent { std::vector<uint8_t> bytes, dirty, have; };      // have: the byte is known (read from the game once, or written)
    Ent& ent(int id);
    bool baseRead(int id, int off, void* dst, size_t n);
    void notePointers(int id, int off, const uint8_t* p, size_t n);
    Raw* raw_; Arrays* arrays_;
    std::map<int, Ent> e_;
    std::map<int, uintptr_t> ents_;
    std::map<uint64_t, std::pair<int, int>> origin_;          // a qword the game had -> the first (entity, offset) it was read at
};

// FNV-1a, 64 bits
inline uint64_t Fnv(uint64_t h, const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; } return h; }
constexpr uint64_t kFnvInit = 1469598103934665603ull;

}  // namespace wrand
