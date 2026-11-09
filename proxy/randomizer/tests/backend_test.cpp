// Game-free (T0): the randomizer's memory layer. The generator's writes stay in the overlay until applied, reads see them, arrays are copied
// before they are written, the fingerprint is the same for the same puzzles whatever the addresses, and applying refuses a reloaded world.
// Mutants (each must fail this test): WR_MUT_COW_NO_COPY (the copy of an array loses its tail), WR_MUT_FP_ADDRESSES (the fingerprint hashes addresses).
#include <cassert>
#include <cstdio>
#include <cstring>
#include "fake_raw.h"
#include "Memory.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

// a small world: panel 0x10 with a 6-int array at +0x418 (decorations) and a count at +0x430; panel 0x20 pointing to another array at +0x270
static void Build(FakeRaw& w) {
    uintptr_t a = 0, b = 0;
    w.addEntity(0x10); w.addEntity(0x20); w.addEntity(0x30);
    int deco[6] = {1, 2, 3, 4, 5, 6}; a = w.addArray(deco, sizeof deco);
    char name[16] = "audio_prefix"; b = w.addArray(name, sizeof name);
    w.put<uint64_t>(0x10, 0x418, a); w.put<int>(0x10, 0x430, 6); w.put<float>(0x10, 0x2A0, 1.0f);
    w.put<uint64_t>(0x20, 0x270, b); w.put<int>(0x20, 0x440, 5);
}

// what the generator does in this test: reads, a field write, a partial array write, a pointer copied between panels, a new array
static void Script(wrand::Backend* be) {
    wrand::ScopedBackend use(be);
    Memory* m = Memory::get();
    int n = m->ReadPanelData<int>(0x10, 0x430);
    std::vector<int> deco = m->ReadArray<int>(0x10, 0x418, n);
    deco[0] = 99; deco.resize(2);
    m->WriteArray<int>(0x10, 0x418, deco);                         // 2 <= 6 known: in place upstream (the other 4 stay)
    m->WritePanelData<float>(0x10, 0x2A0, {0.0f, 0.0f});
    uint64_t p = m->ReadPanelData<uint64_t>(0x20, 0x270);
    m->WritePanelData<uint64_t>(0x30, 0x270, {p});                 // a game pointer moved to another panel (a shuffle does this)
    m->WriteArray<int>(0x30, 0x418, std::vector<int>{7, 8, 9});    // nothing known there: a new array
    m->WritePanelData<int>(0x30, 0x430, 3);
}

int main() {
    // 1. the overlay leaves the game alone, sees its own writes, and applying gives what writing live (upstream) gives
    {
        FakeRaw live; Build(live); FakeRaw over; Build(over);
        wrand::Arrays al, ao;
        wrand::Live L(&live, &al); Script(&L);
        auto before = over.snapshot();
        wrand::Overlay O(&over, &ao); Script(&O);
        CHECK(over.snapshot() == before);                           // nothing reached the game
        { wrand::ScopedBackend use(&O); Memory* m = Memory::get();
          CHECK(m->ReadPanelData<float>(0x10, 0x2A0) == 0.0f);
          std::vector<int> d = m->ReadArray<int>(0x10, 0x418, 6);
          CHECK(d[0] == 99 && d[1] == 2 && d[2] == 3 && d[5] == 6);      // the tail of the copied array survives
          CHECK(m->ReadArray<int>(0x30, 0x418, 3)[2] == 9); }
        CHECK(O.apply(&over));
        // the same puzzle as upstream's in-place write: compare what each panel's arrays hold
        auto arr = [](FakeRaw& w, wrand::Arrays& a, int id, int off, int n) {
            wrand::Live x(&w, &a); wrand::ScopedBackend use(&x); return Memory::get()->ReadArray<int>(id, off, n); };
        CHECK(arr(over, ao, 0x10, 0x418, 6) == arr(live, al, 0x10, 0x418, 6));
        CHECK(arr(over, ao, 0x30, 0x418, 3) == arr(live, al, 0x30, 0x418, 3));
        CHECK(over.get<float>(0x10, 0x2A0) == 0.0f && over.get<int>(0x30, 0x430) == 3);
        CHECK(over.get<uint64_t>(0x30, 0x270) == over.get<uint64_t>(0x20, 0x270));
        // the game's own decoration array was not written (copy on write): upstream's in-place write changed it, ours did not
        uintptr_t orig = live.get<uint64_t>(0x10, 0x418);
        CHECK(live.at(orig, 4)[0] == 99);
        uintptr_t origO = 0; for (auto& r : over.mem) { if (r.second.size() == 24) { int v; std::memcpy(&v, r.second.data(), 4); if (v == 1) origO = r.first; } }
        CHECK(origO != 0);
    }
    // 2. the fingerprint: the same on two PCs whose game data is the same at other addresses; different when the puzzles differ
    {
        FakeRaw a(0x140000000ull), b(0x7FF600000000ull); Build(a); Build(b);
        wrand::Arrays aa, ab; wrand::Overlay oa(&a, &aa), ob(&b, &ab);
        Script(&oa); Script(&ob);
        CHECK(oa.fingerprint() == ob.fingerprint());
        FakeRaw c; Build(c); wrand::Arrays ac; wrand::Overlay oc(&c, &ac); Script(&oc);
        { wrand::ScopedBackend use(&oc); Memory::get()->WriteArray<int>(0x30, 0x418, std::vector<int>{7, 8, 10}, true); }
        CHECK(oc.fingerprint() != oa.fingerprint());
    }
    // 3. applying refuses a world that was reloaded in between (an entity is another object now), and writes nothing
    {
        FakeRaw w; Build(w); wrand::Arrays ar; wrand::Overlay o(&w, &ar); Script(&o);
        auto before = w.snapshot();
        w.ents[0x30] = w.region(0x600);                           // the game re-created panel 0x30
        auto before2 = w.snapshot();
        CHECK(!o.apply(&w));
        CHECK(w.snapshot() == before2);
        (void)before;
    }
    // 4. reads are a snapshot: the game changing a field after the generator read it does not change what the generator sees
    {
        FakeRaw w; Build(w); wrand::Arrays ar; wrand::Overlay o(&w, &ar);
        wrand::ScopedBackend use(&o); Memory* m = Memory::get();
        CHECK(m->ReadPanelData<int>(0x20, 0x440) == 5);
        w.put<int>(0x20, 0x440, 6);
        CHECK(m->ReadPanelData<int>(0x20, 0x440) == 5);
    }
    // 5. a missing entity is an error the generator sees (an exception), not a silent zero
    {
        FakeRaw w; Build(w); wrand::Arrays ar; wrand::Overlay o(&w, &ar);
        wrand::ScopedBackend use(&o); bool threw = false;
        try { Memory::get()->ReadPanelData<int>(0x99, 0x10); } catch (const std::exception&) { threw = true; }
        CHECK(threw);
    }
    std::printf(fails ? "backend: %d FAILED\n" : "backend: ok\n", fails);
    return fails ? 1 : 0;
}
