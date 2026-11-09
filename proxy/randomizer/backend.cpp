// See backend.h. Game-free: the game's memory is reached only through Raw.
#include "backend.h"
#include <algorithm>
#include <cstring>
#include "Memory.h"            // third_party/witness-randomizer/shim
#include "ClientWindow.h"

namespace wrand {
static thread_local Backend* t_backend = nullptr;
static thread_local Memory* t_memory = nullptr;      // the Memory of t_backend (its array-size bookkeeping lasts as long as the scope)

Backend* CurrentBackend() { return t_backend; }
ScopedBackend::ScopedBackend(Backend* b) : prev_(t_backend) { t_backend = b; delete t_memory; t_memory = b ? new Memory(b) : nullptr; }
ScopedBackend::~ScopedBackend() { delete t_memory; t_backend = prev_; t_memory = prev_ ? new Memory(prev_) : nullptr; }

// ---- Arrays
uintptr_t Arrays::make(const void* src, size_t n) {
    size_t cap = std::max<size_t>(n, 8);
    uint8_t* p = alloc_ ? alloc_(cap) : nullptr;
    std::unique_ptr<std::vector<uint8_t>> own;
    if (!p) { own.reset(new std::vector<uint8_t>(cap, 0)); p = own->data(); }
    else std::memset(p, 0, cap);
    if (n) std::memcpy(p, src, n);
    uintptr_t a = (uintptr_t)p;
    blocks_[a] = Block{n, std::move(own)};
    return a;
}
bool Arrays::owns(uintptr_t p, size_t* size) const {
    auto it = blocks_.find(p);
    if (it == blocks_.end()) return false;
    if (size) *size = it->second.size;
    return true;
}
uint8_t* Arrays::data(uintptr_t p) { return owns(p) ? (uint8_t*)p : nullptr; }
size_t Arrays::bytes() const { size_t s = 0; for (const auto& b : blocks_) s += b.second.size; return s; }

// ---- Live
bool Live::readField(int id, int off, void* dst, size_t n) { uintptr_t e = raw_->entity(id); return e && raw_->read(e + off, dst, n); }
bool Live::writeField(int id, int off, const void* src, size_t n) { uintptr_t e = raw_->entity(id); return e && raw_->write(e + off, src, n); }
bool Live::readArray(int id, int off, void* dst, size_t n) {
    uint64_t p = 0; if (!readField(id, off, &p, 8) || !p) return false;
    size_t size = 0;
    if (arrays_->owns(p, &size)) { std::memset(dst, 0, n); std::memcpy(dst, (const void*)(uintptr_t)p, std::min(n, size)); return true; }
    return raw_->read(p, dst, n);
}
bool Live::writeArrayInPlace(int id, int off, const void* src, size_t n, size_t known) {
    uint64_t p = 0; if (!readField(id, off, &p, 8)) return false;
    size_t size = 0;
    if (!p) return writeArrayNew(id, off, src, n);
    if (arrays_->owns(p, &size)) {
        if (size >= n) { std::memcpy((void*)(uintptr_t)p, src, n); return true; }
        std::vector<uint8_t> b((const uint8_t*)(uintptr_t)p, (const uint8_t*)(uintptr_t)p + size); b.resize(n); std::memcpy(b.data(), src, n);
        return writeArrayNew(id, off, b.data(), b.size());
    }
    (void)known;
    return raw_->write(p, src, n);       // upstream writes into the game's own array
}
bool Live::writeArrayNew(int id, int off, const void* src, size_t n) { uint64_t p = arrays_->make(src, n); return writeField(id, off, &p, 8); }

// ---- Overlay
Overlay::Ent& Overlay::ent(int id) { return e_[id]; }
static void Grow(std::vector<uint8_t>& v, size_t n) { if (v.size() < n) v.resize(n, 0); }

bool Overlay::baseRead(int id, int off, void* dst, size_t n) {
    // The game's bytes, read once: later reads of the same bytes see the same value (a consistent snapshot), whatever the game does meanwhile.
    if (off < 0) return false;
    auto known = ents_.find(id);
    uintptr_t e = known != ents_.end() ? known->second : raw_->entity(id);
    if (!e) return false;
    ents_[id] = e;
    Ent& x = ent(id);
    Grow(x.bytes, off + n); Grow(x.dirty, off + n); Grow(x.have, off + n);
    bool missing = false;
    for (size_t i = 0; i < n && !missing; i++) missing = !x.have[off + i];
    if (missing) {
        std::vector<uint8_t> tmp(n);
        if (!raw_->read(e + off, tmp.data(), n)) return false;
        for (size_t i = 0; i < n; i++) if (!x.have[off + i]) { x.bytes[off + i] = tmp[i]; x.have[off + i] = 1; }
        notePointers(id, off, tmp.data(), n);
    }
    std::memcpy(dst, x.bytes.data() + off, n);
    return true;
}
void Overlay::notePointers(int id, int off, const uint8_t* p, size_t n) {
    // every aligned qword the game had that could be an address: where it was first seen (the fingerprint names it by that place)
    for (size_t i = 0; i + 8 <= n; i++) {
        if ((off + i) % 8) continue;
        uint64_t v; std::memcpy(&v, p + i, 8);
        if (v >= 0x10000 && v < (1ull << 47) && !origin_.count(v)) origin_[v] = {id, (int)(off + i)};
    }
}
bool Overlay::readField(int id, int off, void* dst, size_t n) {
    if (!n) return true;
    // what the game had (cached), with the generator's own writes on top
    Ent* x = nullptr;
    { auto it = e_.find(id); if (it != e_.end()) x = &it->second; }
    bool allDirty = x && x->dirty.size() >= off + n;
    for (size_t i = 0; allDirty && i < n; i++) allDirty = x->dirty[off + i];
    if (allDirty) { std::memcpy(dst, x->bytes.data() + off, n); return true; }
    return baseRead(id, off, dst, n);       // the bytes it wrote are in the cache too (a write marks them known)
}
bool Overlay::writeField(int id, int off, const void* src, size_t n) {
    if (off < 0) return false;
    if (!ents_.count(id)) { uintptr_t e = raw_->entity(id); if (!e) return false; ents_[id] = e; }
    Ent& x = ent(id);
    Grow(x.bytes, off + n); Grow(x.dirty, off + n); Grow(x.have, off + n);
    std::memcpy(x.bytes.data() + off, src, n);
    for (size_t i = 0; i < n; i++) { x.dirty[off + i] = 1; x.have[off + i] = 1; }
    return true;
}
bool Overlay::readArray(int id, int off, void* dst, size_t n) {
    uint64_t p = 0; if (!readField(id, off, &p, 8) || !p) return false;
    size_t size = 0;
    if (arrays_->owns(p, &size)) { std::memset(dst, 0, n); std::memcpy(dst, (const void*)(uintptr_t)p, std::min(n, size)); return true; }
    return raw_->read(p, dst, n);
}
bool Overlay::writeArrayInPlace(int id, int off, const void* src, size_t n, size_t known) {
    uint64_t p = 0; if (!readField(id, off, &p, 8)) return false;
    if (!p) return writeArrayNew(id, off, src, n);
    size_t size = 0;
    if (arrays_->owns(p, &size) && size >= n) { std::memcpy((void*)(uintptr_t)p, src, n); return true; }
    // Copy on write: the array the game has (as much of it as the generator knows exists, or our smaller one) is copied into a new one of
    // ours, which then takes the write. The game's array is never written; the pointer to the copy is an overlay write like any other.
    size_t have = arrays_->owns(p, &size) ? size : known;
    std::vector<uint8_t> b(std::max(have, n), 0);
    if (have) {
        if (arrays_->owns(p)) std::memcpy(b.data(), (const void*)(uintptr_t)p, have);
#ifdef WR_MUT_COW_NO_COPY
        else {}      // mutant: the copy starts empty, so the part of the game's array beyond the write is lost
#else
        else if (!raw_->read(p, b.data(), have)) return false;
#endif
    }
    std::memcpy(b.data(), src, n);
    return writeArrayNew(id, off, b.data(), b.size());
}
bool Overlay::writeArrayNew(int id, int off, const void* src, size_t n) { uint64_t p = arrays_->make(src, n); return writeField(id, off, &p, 8); }

std::map<int, std::vector<Overlay::Run>> Overlay::changes() const {
    std::map<int, std::vector<Run>> out;
    for (const auto& kv : e_) {
        const Ent& x = kv.second;
        for (size_t i = 0; i < x.dirty.size();) {
            if (!x.dirty[i]) { i++; continue; }
            size_t j = i; while (j < x.dirty.size() && x.dirty[j]) j++;
            out[kv.first].push_back(Run{(int)i, std::vector<uint8_t>(x.bytes.begin() + i, x.bytes.begin() + j)});
            i = j;
        }
    }
    return out;
}
size_t Overlay::changedBytes() const { size_t s = 0; for (const auto& kv : e_) for (uint8_t d : kv.second.dirty) s += d; return s; }

uint64_t Overlay::fingerprint() const {
    uint64_t h = kFnvInit;
    for (const auto& kv : changes()) {
        int id = kv.first; h = Fnv(h, &id, 4);
        for (const Run& r : kv.second) {
            for (size_t i = 0; i < r.bytes.size();) {
                int off = r.off + (int)i;
                if (off % 8 == 0 && i + 8 <= r.bytes.size()) {
                    uint64_t v; std::memcpy(&v, r.bytes.data() + i, 8);
                    size_t size = 0;
#ifndef WR_MUT_FP_ADDRESSES
                    if (v && arrays_->owns(v, &size)) {          // one of ours: what it holds
                        h = Fnv(h, "A", 1); h = Fnv(h, &off, 4); h = Fnv(h, &size, sizeof size); h = Fnv(h, (const void*)(uintptr_t)v, size);
                        i += 8; continue;
                    }
                    auto o = origin_.find(v);
                    if (v && o != origin_.end()) {               // the game's: where it came from
                        h = Fnv(h, "P", 1); h = Fnv(h, &off, 4); h = Fnv(h, &o->second.first, 4); h = Fnv(h, &o->second.second, 4);
                        i += 8; continue;
                    }
#endif
                }
                h = Fnv(h, &off, 4); h = Fnv(h, &r.bytes[i], 1);
                i++;
            }
        }
    }
    return h;
}

bool Overlay::apply(Raw* live) const {
    auto ch = changes();
    for (const auto& kv : ch) { auto it = ents_.find(kv.first); if (it == ents_.end() || live->entity(kv.first) != it->second) return false; }
    for (const auto& kv : ch) {
        uintptr_t e = ents_.at(kv.first);
        for (const Run& r : kv.second) if (!live->write(e + r.off, r.bytes.data(), r.bytes.size())) return false;
    }
    return true;
}
}  // namespace wrand

// ---- the vendored code's singletons
Memory* Memory::get() {
    if (!wrand::t_memory) throw std::runtime_error("randomizer: no backend on this thread");
    return wrand::t_memory;
}
static thread_local std::function<void(const std::string&)>* t_status = nullptr;
namespace wrand {
ScopedStatus::ScopedStatus(std::function<void(const std::string&)> f) : f_(std::move(f)), prev_(t_status) { t_status = &f_; }
ScopedStatus::~ScopedStatus() { t_status = prev_; }
}
ClientWindow* ClientWindow::get() { static ClientWindow w; return &w; }
void ClientWindow::setStatusMessage(const std::string& text) { if (t_status && *t_status) (*t_status)(text); }
