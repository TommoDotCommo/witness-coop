#pragma once
// The game's own typeface for the co-op text (the overlay's glyph atlas and the launcher's headings). Nothing of the game is shipped or written to disk:
// the font is read at run time from the player's own data-pc.zip (a plain zip; its fonts are stored, not compressed, as data/fonts/<name> members
// named "<name>.raw"), kept in memory and handed to GDI as a private memory font. The ".mod_otf" member is an OpenType file with every byte
// inverted (XOR 0xFF); the others are plain. Anything unexpected (no zip, no member, compressed member, not a font, no family name) returns an
// error and the caller keeps Segoe UI.
// The parsing is pure (a read callback over the zip), so the test feeds it a zip built in memory (common/gamefont_test.cpp); FromZipFile is the
// Windows wrapper. Header-only: the proxy and the launcher both include it.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace gamefont {

// The faces the co-op text may use, by the name a cfg gives (ui_font=): member in the zip, and the GDI weight the face has.
struct Face { const char* key; const char* member; int weight; };
inline const Face* Faces(size_t* n) {
    static const Face k[] = {{"karmina", "KarminaBold.mod_otf.raw", 700}, {"notoserif", "NotoSerif-Regular.ttf.raw", 400}};
    *n = sizeof k / sizeof k[0]; return k;
}
inline const Face* FindFace(const std::string& key) { size_t n; const Face* f = Faces(&n); for (size_t i = 0; i < n; ++i) if (key == f[i].key) return &f[i]; return nullptr; }

struct Result {
    std::vector<uint8_t> bytes;      // the font file, ready for AddFontMemResourceEx
    std::string family, error;       // name ID 1 (e.g. "Karmina"); error empty on success
    bool ok() const { return error.empty() && !bytes.empty(); }
};

using Reader = std::function<bool(uint64_t offset, void* out, size_t n)>;      // read n bytes at offset; false when short or failed

inline uint16_t U16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
inline uint32_t U32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
inline uint16_t B16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
inline uint32_t B32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

// One stored member of a zip, found through the central directory (the archive has no zip64 records: it is under 4 GB). The member's name
// matches when it equals `name` or ends with "/" + name.
inline bool ReadStoredMember(const Reader& rd, uint64_t zipSize, const std::string& name, std::vector<uint8_t>* out, std::string* err) {
    const size_t kTail = 66000; uint64_t tailAt = zipSize > kTail ? zipSize - kTail : 0; std::vector<uint8_t> tail((size_t)(zipSize - tailAt));
    if (tail.size() < 22 || !rd(tailAt, tail.data(), tail.size())) { *err = "zip too short or unreadable"; return false; }
    size_t eocd = std::string::npos;
    for (size_t i = tail.size() - 22 + 1; i-- > 0;) if (U32(&tail[i]) == 0x06054b50) { eocd = i; break; }
    if (eocd == std::string::npos) { *err = "no end of central directory"; return false; }
    uint32_t cdSize = U32(&tail[eocd + 12]), cdOff = U32(&tail[eocd + 16]);
    if ((uint64_t)cdOff + cdSize > zipSize || cdSize > (64u << 20)) { *err = "central directory out of range"; return false; }
    std::vector<uint8_t> cd(cdSize); if (!rd(cdOff, cd.data(), cd.size())) { *err = "central directory unreadable"; return false; }
    for (size_t p = 0; p + 46 <= cd.size();) {
        if (U32(&cd[p]) != 0x02014b50) { *err = "bad central directory entry"; return false; }
        uint16_t method = U16(&cd[p + 10]), nl = U16(&cd[p + 28]), xl = U16(&cd[p + 30]), cl = U16(&cd[p + 32]);
        uint32_t csize = U32(&cd[p + 20]), usize = U32(&cd[p + 24]), local = U32(&cd[p + 42]);
        if (p + 46 + nl > cd.size()) break;
        std::string n((const char*)&cd[p + 46], nl);
        bool match = n == name || (n.size() > name.size() && n.compare(n.size() - name.size(), name.size(), name) == 0 && n[n.size() - name.size() - 1] == '/');
        if (match) {
            if (method != 0 || csize != usize) { *err = "member " + name + " is compressed (method " + std::to_string(method) + ")"; return false; }
            if (usize == 0 || usize > (32u << 20)) { *err = "member " + name + " has an unexpected size"; return false; }
            uint8_t lh[30]; if (!rd(local, lh, 30) || U32(lh) != 0x04034b50) { *err = "bad local header for " + name; return false; }
            uint64_t data = (uint64_t)local + 30 + U16(lh + 26) + U16(lh + 28);
            if (data + usize > zipSize) { *err = "member " + name + " runs past the end"; return false; }
            out->resize(usize); if (!rd(data, out->data(), usize)) { *err = "member " + name + " unreadable"; return false; }
            return true;
        }
        p += 46 + nl + xl + cl;
    }
    *err = "no member " + name; return false;
}

// The family name (name ID 1) of an OpenType/TrueType file: Windows Unicode (3,1 / 3,0, UTF-16BE, ASCII kept) first, then Macintosh Roman (1,0).
inline std::string FamilyName(const std::vector<uint8_t>& f) {
    if (f.size() < 12) return "";
    uint16_t nt = B16(&f[4]); if (12 + (size_t)nt * 16 > f.size()) return "";
    for (uint16_t i = 0; i < nt; ++i) {
        const uint8_t* t = &f[12 + i * 16]; if (memcmp(t, "name", 4)) continue;
        uint32_t off = B32(t + 8), len = B32(t + 12); if ((uint64_t)off + len > f.size() || len < 6) return "";
        const uint8_t* n = &f[off]; uint16_t count = B16(n + 2), strOff = B16(n + 4); if (6 + (size_t)count * 12 > len) return "";
        std::string mac;
        for (uint16_t k = 0; k < count; ++k) {
            const uint8_t* r = n + 6 + k * 12; uint16_t plat = B16(r), enc = B16(r + 2), id = B16(r + 6), sl = B16(r + 8), so = B16(r + 10);
            if (id != 1 || (size_t)strOff + so + sl > len) continue;
            const uint8_t* s = n + strOff + so;
            if (plat == 3 && (enc == 1 || enc == 0)) { std::string a; for (uint16_t j = 0; j + 1 < sl; j += 2) { uint16_t c = B16(s + j); a += c < 128 ? (char)c : '?'; } if (!a.empty()) return a; }
            else if (plat == 1 && enc == 0 && mac.empty()) mac.assign((const char*)s, sl);
        }
        return mac;
    }
    return "";
}

// A font file as stored in the zip -> a font file GDI takes, plus its family name.
inline Result Decode(const std::string& member, std::vector<uint8_t> raw) {
    Result r;
    bool inverted = member.find(".mod_otf") != std::string::npos;
#ifdef GF_MUT_NO_XOR
    inverted = false;
#endif
    if (inverted) for (auto& b : raw) b ^= 0xFF;
    if (raw.size() < 12) { r.error = "font too short"; return r; }
    uint32_t tag = B32(raw.data());
    if (tag != 0x4F54544F /* OTTO */ && tag != 0x00010000 && tag != 0x74727565 /* true */) { r.error = "not an OpenType font (header " + std::to_string(tag) + ")"; return r; }
    r.family = FamilyName(raw);
    if (r.family.empty()) { r.error = "font has no family name"; return r; }
    r.bytes.swap(raw); return r;
}

inline Result FromZip(const Reader& rd, uint64_t zipSize, const std::string& member) {
    Result r; std::vector<uint8_t> raw;
    if (!ReadStoredMember(rd, zipSize, member, &raw, &r.error)) return r;
    return Decode(member, std::move(raw));
}

#ifdef _WIN32
// The Windows wrapper: opens the zip for reading only, with every share mode, so the game (or another reader) is never locked out. The game itself
// opens data-pc.zip with share mode 0, so this must run before the game opens it (the proxy reads it in its CreateFile hook just before the game's own
// open) or while the game is not running (the launcher). `openFile` lets the proxy call the real CreateFileW past its own hook.
inline Result FromZipFile(const wchar_t* path, const std::string& member, void* (*openFile)(const wchar_t*) = nullptr) {
    Result r;
    void* h = openFile ? openFile(path) : (void*)CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE || !h) { r.error = "cannot open the game archive (error " + std::to_string((unsigned long)GetLastError()) + ")"; return r; }
    LARGE_INTEGER size = {}; GetFileSizeEx((HANDLE)h, &size);
    Reader rd = [h](uint64_t off, void* out, size_t n) {
        OVERLAPPED o = {}; o.Offset = (DWORD)off; o.OffsetHigh = (DWORD)(off >> 32); DWORD got = 0;
        return ReadFile((HANDLE)h, out, (DWORD)n, &got, &o) && got == n;
    };
    r = FromZip(rd, (uint64_t)size.QuadPart, member);
    CloseHandle((HANDLE)h);
    return r;
}
#endif
}  // namespace gamefont
