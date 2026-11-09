#pragma once
// witness-coop: this file replaces upstream Source/Memory.h and Memory.cpp. Upstream reads and writes the game from another process
// (ReadProcessMemory, VirtualAllocEx); here the randomizer runs inside the game, and every access goes to the calling thread's backend
// (proxy/randomizer/backend.h). The method names, overloads and the array-size bookkeeping are upstream's, so the vendored code compiles unchanged.
// Two flavours, as the vendored randomizers have them: Memory::get() is the current upstream's singleton; Memory("witness64_d3d11.exe") is the older
// forks' (Despair, Variety/Ascension, Symbol Shuffler, Star): an object per use, whose WritePanelData also forgets what it knew of that field's array.
#include <windows.h>
#include <cstdint>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "backend.h"

class Memory {
public:
    static Memory* get();                         // the calling thread's Memory (proxy/randomizer/backend.cpp); throws when the thread has no backend
    explicit Memory(wrand::Backend* b) : b_(b) {}
    explicit Memory(const std::string&) : b_(wrand::CurrentBackend()), legacy_(true) { if (!b_) throw std::runtime_error("randomizer: no backend on this thread"); }      // the older forks' Memory: one per use
    Memory(const Memory&) = delete;
    Memory& operator=(const Memory&) = delete;

    template <class T> std::vector<T> ReadArray(int panel, int offset, int size) {
        if (size <= 0) return std::vector<T>();
        sizes_[std::make_pair(panel, offset)] = size;
        std::vector<T> v((size_t)size);
        if (!b_->readArray(panel, offset, v.data(), v.size() * sizeof(T))) throw std::runtime_error("randomizer: array read failed");
        return v;
    }
    template <class T> void WriteArray(int panel, int offset, const std::vector<T>& data) {
        if (data.empty()) return;
        int known = sizes_[std::make_pair(panel, offset)];
        bool ok = (int)data.size() > known ? b_->writeArrayNew(panel, offset, data.data(), data.size() * sizeof(T))
                                           : b_->writeArrayInPlace(panel, offset, data.data(), data.size() * sizeof(T), (size_t)known * sizeof(T));
        if (!ok) throw std::runtime_error("randomizer: array write failed");
    }
    template <class T> void WriteArray(int panel, int offset, const std::vector<T>& data, bool force) {
        if (force) sizes_[std::make_pair(panel, offset)] = 0;
        WriteArray(panel, offset, data);
    }

    template <class T> std::vector<T> ReadPanelData(int panel, int offset, size_t size, bool) { return ReadPanelData<T>(panel, offset, size); }
    template <class T> std::vector<T> ReadPanelData(int panel, int offset, size_t size) {
        if (size == 0) return std::vector<T>();
        std::vector<T> v(size);
        if (!b_->readField(panel, offset, v.data(), size * sizeof(T))) throw std::runtime_error("randomizer: field read failed");
        return v;
    }
    template <class T> T ReadPanelData(int panel, int offset) {
        T v{};
        if (!b_->readField(panel, offset, &v, sizeof(T))) throw std::runtime_error("randomizer: field read failed");
        return v;
    }
    template <class T> void WritePanelData(int panel, int offset, T data, bool) { WritePanelData<T>(panel, offset, std::vector<T>{data}); }
    template <class T> void WritePanelData(int panel, int offset, T data) { WritePanelData<T>(panel, offset, std::vector<T>{data}); }
    template <class T> void WritePanelData(int panel, int offset, const std::vector<T>& data, bool) { WritePanelData<T>(panel, offset, data); }
    template <class T> void WritePanelData(int panel, int offset, const std::vector<T>& data) {
        if (legacy_) sizes_.erase(std::make_pair(panel, offset));      // (the older forks: "this might be an array pointer, in which case we need to invalidate")
        if (data.empty()) return;
        if (!b_->writeField(panel, offset, data.data(), data.size() * sizeof(T))) throw std::runtime_error("randomizer: field write failed");
    }

    // Archipelago's puzzle skips only (Special::SkipPanel); the randomizers never call them
    void OpenDoor(int) { throw std::logic_error("randomizer: OpenDoor is not part of witness-coop"); }
    void PowerGauge(int, int, int) { throw std::logic_error("randomizer: PowerGauge is not part of witness-coop"); }
    void UpdatePanelJunctions(int) { throw std::logic_error("randomizer: UpdatePanelJunctions is not part of witness-coop"); }
    // upstream's test helpers (Special.h templates that are never instantiated) name these; declared only
    bool ReadAbsolute(LPCVOID address, LPVOID buffer, SIZE_T size);
    bool Read(LPCVOID address, LPVOID buffer, SIZE_T size);
    template <class T> std::vector<T> ReadData(const std::vector<int>& offsets, size_t numItems);
    void ClearOffsets() {}                        // (an address cache upstream; nothing to clear here)
    static int GLOBALS;                           // the older forks' findGlobals (their window's, never run here)
    bool retryOnFail = true;

private:
    wrand::Backend* b_;
    bool legacy_ = false;
    std::map<std::pair<int, int>, int> sizes_;    // upstream's _arraySizes: the element count last read from each array
};
