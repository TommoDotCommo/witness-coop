// Game-free test of common/gamefont.h: a zip built in memory with a made-up OpenType font (no game file is needed or committed). Checks the member
// lookup, the XOR undo of ".mod_otf" members, the header check, the family name, and that every bad case is an error (the caller then keeps Segoe UI).
// With GAMEFONT_ZIP=<the game's data-pc.zip> it also reads the real Karmina and Noto Serif (local runs only; skipped otherwise).
// Mutant GF_MUT_NO_XOR: the inverted member is not undone, so it must fail the header check and this test must fail.
#include "gamefont.h"
#include <cstdio>
#include <cstdlib>

static int failures;
static void check(bool ok, const char* what) { if (!ok) { std::printf("FAILED: %s\n", what); ++failures; } }

static void P16(std::vector<uint8_t>& v, uint16_t x) { v.push_back(x & 255); v.push_back(x >> 8); }
static void P32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back((x >> (8 * i)) & 255); }
static void B16(std::vector<uint8_t>& v, uint16_t x) { v.push_back(x >> 8); v.push_back(x & 255); }
static void B32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 3; i >= 0; --i) v.push_back((x >> (8 * i)) & 255); }

// A minimal sfnt: header, one table record ("name"), a name table with name ID 1 in Windows Unicode and Macintosh Roman.
static std::vector<uint8_t> FakeFont(uint32_t tag, const std::string& family, bool withName = true) {
    std::vector<uint8_t> name; std::vector<uint8_t> w; for (char c : family) B16(w, (uint8_t)c);
    B16(name, 0); B16(name, 2); B16(name, 6 + 2 * 12);
    B16(name, 1); B16(name, 0); B16(name, 0); B16(name, withName ? 1 : 2); B16(name, (uint16_t)family.size()); B16(name, 0);
    B16(name, 3); B16(name, 1); B16(name, 0x409); B16(name, withName ? 1 : 2); B16(name, (uint16_t)w.size()); B16(name, (uint16_t)family.size());
    name.insert(name.end(), family.begin(), family.end()); name.insert(name.end(), w.begin(), w.end());
    std::vector<uint8_t> f; B32(f, tag); B16(f, 1); B16(f, 16); B16(f, 0); B16(f, 0);
    f.insert(f.end(), {'n', 'a', 'm', 'e'}); B32(f, 0); B32(f, 12 + 16); B32(f, (uint32_t)name.size());
    f.insert(f.end(), name.begin(), name.end()); return f;
}
struct Member { std::string name; std::vector<uint8_t> data; uint16_t method; };
static std::vector<uint8_t> Zip(const std::vector<Member>& ms) {
    std::vector<uint8_t> z, cd; std::vector<uint32_t> offs;
    for (const auto& m : ms) {
        offs.push_back((uint32_t)z.size());
        P32(z, 0x04034b50); P16(z, 20); P16(z, 0); P16(z, m.method); P16(z, 0); P16(z, 0); P32(z, 0); P32(z, (uint32_t)m.data.size()); P32(z, (uint32_t)m.data.size() + (m.method ? 7 : 0));
        P16(z, (uint16_t)m.name.size()); P16(z, 3); z.insert(z.end(), m.name.begin(), m.name.end()); z.insert(z.end(), {1, 2, 3}); z.insert(z.end(), m.data.begin(), m.data.end());
    }
    for (size_t i = 0; i < ms.size(); ++i) {
        const auto& m = ms[i];
        P32(cd, 0x02014b50); P16(cd, 20); P16(cd, 20); P16(cd, 0); P16(cd, m.method); P16(cd, 0); P16(cd, 0); P32(cd, 0); P32(cd, (uint32_t)m.data.size()); P32(cd, (uint32_t)m.data.size() + (m.method ? 7 : 0));
        P16(cd, (uint16_t)m.name.size()); P16(cd, 0); P16(cd, 0); P16(cd, 0); P16(cd, 0); P32(cd, 0); P32(cd, offs[i]); cd.insert(cd.end(), m.name.begin(), m.name.end());
    }
    uint32_t cdOff = (uint32_t)z.size(); z.insert(z.end(), cd.begin(), cd.end());
    P32(z, 0x06054b50); P16(z, 0); P16(z, 0); P16(z, (uint16_t)ms.size()); P16(z, (uint16_t)ms.size()); P32(z, (uint32_t)cd.size()); P32(z, cdOff); P16(z, 0);
    return z;
}
static gamefont::Result Load(const std::vector<uint8_t>& z, const std::string& member) {
    gamefont::Reader rd = [&](uint64_t off, void* out, size_t n) { if (off + n > z.size()) return false; memcpy(out, &z[off], n); return true; };
    return gamefont::FromZip(rd, z.size(), member);
}

int main() {
    std::vector<uint8_t> inv = FakeFont(0x4F54544F, "Karmina"); for (auto& b : inv) b ^= 0xFF;
    std::vector<uint8_t> z = Zip({{"data/other.raw", {1, 2, 3, 4}, 0},
                                  {"data/fonts/KarminaBold.mod_otf.raw", inv, 0},
                                  {"data/fonts/NotoSerif-Regular.ttf.raw", FakeFont(0x00010000, "Noto Serif"), 0},
                                  {"data/fonts/Packed.otf.raw", FakeFont(0x4F54544F, "Packed"), 8},
                                  {"data/fonts/Garbage.otf.raw", std::vector<uint8_t>(64, 0x41), 0},
                                  {"data/fonts/Nameless.otf.raw", FakeFont(0x4F54544F, "X", false), 0}});
    gamefont::Result k = Load(z, "KarminaBold.mod_otf.raw");
    check(k.ok(), ("the inverted member decodes: " + k.error).c_str());
    check(k.family == "Karmina", "the family name of the inverted member is Karmina");
    check(k.bytes.size() > 4 && memcmp(k.bytes.data(), "OTTO", 4) == 0, "the decoded bytes start with OTTO");
    gamefont::Result n = Load(z, "NotoSerif-Regular.ttf.raw");
    check(n.ok() && n.family == "Noto Serif", "a plain TrueType member is used as stored");
    check(!Load(z, "Missing.otf.raw").ok(), "a missing member is an error");
    check(!Load(z, "Packed.otf.raw").ok(), "a compressed member is an error (no inflater)");
    check(!Load(z, "Garbage.otf.raw").ok(), "a member that is not a font is an error");
    check(!Load(z, "Nameless.otf.raw").ok(), "a font without a family name is an error");
    check(!Load(std::vector<uint8_t>(10, 0), "KarminaBold.mod_otf.raw").ok(), "a file too short for a zip is an error");
    std::vector<uint8_t> cut(z.begin(), z.end() - 30); check(!Load(cut, "KarminaBold.mod_otf.raw").ok(), "a zip without its end record is an error");
    check(gamefont::FindFace("karmina") && !strcmp(gamefont::FindFace("karmina")->member, "KarminaBold.mod_otf.raw") && !gamefont::FindFace("segoe"), "ui_font names map to members");

    if (const char* real = std::getenv("GAMEFONT_ZIP")) {      // the player's own archive, read in place (local run only)
        FILE* f = std::fopen(real, "rb"); check(f != nullptr, "GAMEFONT_ZIP opens");
        if (f) {
            fseek(f, 0, SEEK_END); uint64_t size = (uint64_t)ftell(f);
            gamefont::Reader rd = [f](uint64_t off, void* out, size_t len) { return fseek(f, (long)off, SEEK_SET) == 0 && fread(out, 1, len, f) == len; };
            gamefont::Result r = gamefont::FromZip(rd, size, "KarminaBold.mod_otf.raw"); std::printf("real Karmina: %s family '%s' %zu bytes\n", r.ok() ? "ok" : r.error.c_str(), r.family.c_str(), r.bytes.size());
            check(r.ok() && r.family == "Karmina", "the real KarminaBold member decodes to family Karmina");
            gamefont::Result s = gamefont::FromZip(rd, size, "NotoSerif-Regular.ttf.raw"); std::printf("real Noto Serif: %s family '%s'\n", s.ok() ? "ok" : s.error.c_str(), s.family.c_str());
            check(s.ok() && s.family == "Noto Serif", "the real Noto Serif member reads");
            std::fclose(f);
        }
    } else std::printf("(GAMEFONT_ZIP not set: the real archive is not read)\n");
    if (failures) { std::printf("gamefont: %d FAILED\n", failures); return 1; }
    std::printf("gamefont: all checks passed\n"); return 0;
}
