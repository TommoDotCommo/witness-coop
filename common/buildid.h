/* Which build of The Witness is this? Answered from the exe's own PE header and the bytes of its code section, so it works
 *  - in the proxy, on the exe already mapped in memory (no file or network I/O: the check runs inside DllMain, under the loader lock),
 *  - in the launcher and the tests, on the bytes of the exe file.
 * The mod's addresses fit one build only (Steam build 11370412, witness64_d3d11.exe SHA-256 8d672d44...). The exe has no
 * relocations, so its code section in memory equals the file's. Any other build is "not supported": nothing is patched. */
#ifndef COOP_BUILDID_H
#define COOP_BUILDID_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "sha256.h"

#define WITNESS_BUILD_NAME "Steam build 11370412"
#define WITNESS_BUILD_TIMESTAMP 0x5c48cf09u
#define WITNESS_BUILD_IMAGE_SIZE 0x4702000u
#define WITNESS_BUILD_ENTRY_RVA 0x425d1cu
#define WITNESS_BUILD_CODE_SIZE 0x511000u
#define WITNESS_BUILD_TEXT_SHA256 "63d324a709775f95b395e7fcc58e3ce83099b8b6c3ddd534fd4867d6436da6d3"   /* 5312476 bytes */
#define WITNESS_FAQ_URL "https://github.com/TommoDotCommo/witness-coop#faq-not-the-supported-build"

namespace buildid {

enum Verdict { kKnown = 0, kNotPe, kOtherHeader, kOtherCode };

struct Spec { uint32_t timestamp, imageSize, entryRva, codeSize; char textSha256[65]; };
inline Spec Witness() { Spec s = { WITNESS_BUILD_TIMESTAMP, WITNESS_BUILD_IMAGE_SIZE, WITNESS_BUILD_ENTRY_RVA, WITNESS_BUILD_CODE_SIZE, WITNESS_BUILD_TEXT_SHA256 }; return s; }

struct Info { Verdict verdict = kNotPe; uint32_t timestamp = 0, imageSize = 0, entryRva = 0, codeSize = 0; char textSha256[65] = ""; };

inline uint32_t Rd32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
inline uint16_t Rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }

/* An exe mapped by the loader: the size of its image, read from the first page (the loader always maps it). 0 if it is not a 64-bit PE. */
inline size_t MappedSize(const uint8_t* base)
{
    uint32_t pe = Rd32(base + 0x3c);
    if (base[0] != 'M' || base[1] != 'Z' || pe > 0x800 || memcmp(base + pe, "PE\0\0", 4) || Rd16(base + pe + 24) != 0x20b) return 0;
    return Rd32(base + pe + 24 + 56);
}

/* data/size: the exe file (mapped=false: sections at their file offsets) or the mapped image (mapped=true: at their virtual addresses).
 * Every offset is range-checked against size; a damaged or cut-off file is kNotPe, never a read past the end. */
inline Info Inspect(const uint8_t* data, size_t size, bool mapped, const Spec& want)
{
    Info r;
    if (size < 0x200 || data[0] != 'M' || data[1] != 'Z') return r;
    uint64_t pe = Rd32(data + 0x3c);
    if (pe + 24 + 112 > size || memcmp(data + pe, "PE\0\0", 4) || Rd16(data + pe + 4) != 0x8664) return r;
    uint64_t nsec = Rd16(data + pe + 6), optSize = Rd16(data + pe + 20), opt = pe + 24;
    if (Rd16(data + opt) != 0x20b || optSize < 112 || nsec == 0 || nsec > 96) return r;
    uint64_t table = opt + optSize;
    if (table + nsec * 40 > size) return r;
    r.timestamp = Rd32(data + pe + 8); r.codeSize = Rd32(data + opt + 4); r.entryRva = Rd32(data + opt + 16); r.imageSize = Rd32(data + opt + 56);
    r.verdict = kOtherHeader;
    for (uint64_t i = 0; i < nsec; i++) {
        const uint8_t* s = data + table + i * 40;
        uint32_t virt = Rd32(s + 8), va = Rd32(s + 12), raw = Rd32(s + 16), ptr = Rd32(s + 20), flags = Rd32(s + 36);
        if ((flags & 0x20000020u) != 0x20000020u) continue;             /* the first section with code that can run */
        uint64_t n = virt ? (virt < raw ? virt : raw) : raw, at = mapped ? va : ptr;
        if (at + n > size) return r;                                    /* cut off: another file, whatever its header says */
        coop_sha256 c; uint8_t d[32]; coop_sha256_init(&c); coop_sha256_update(&c, data + at, (size_t)n); coop_sha256_final(&c, d);
        coop_sha256_hex(d, r.textSha256);
        break;
    }
    if (r.timestamp != want.timestamp || r.imageSize != want.imageSize || r.entryRva != want.entryRva || r.codeSize != want.codeSize) return r;
#ifdef BI_MUT_SKIP_TEXT
    r.verdict = kKnown;                                                 /* mutant: only the header is compared */
#else
    r.verdict = strcmp(r.textSha256, want.textSha256) ? kOtherCode : kKnown;
#endif
    return r;
}

/* One line for a log or a dialog: what was found. */
inline void Describe(const Info& i, char* out, size_t n)
{
    if (i.verdict == kNotPe) snprintf(out, n, "not a readable 64-bit Windows program");
    else snprintf(out, n, "exe built %08x, image 0x%x, entry 0x%x, code sha256 %.12s", i.timestamp, i.imageSize, i.entryRva, i.textSha256[0] ? i.textSha256 : "(none)");
}

/* The words shown to a player. Plain text, no markup, the same everywhere (proxy dialog, launcher status). */
#define WITNESS_UNSUPPORTED_SHORT "Co-op is off: this copy of The Witness is not " WITNESS_BUILD_NAME " (the build this mod is made for)."
#define WITNESS_UNSUPPORTED_HELP \
    "The game runs normally and nothing was changed.\n\n" \
    "Why: the mod works only with " WITNESS_BUILD_NAME ". Your copy has been updated, is on a beta branch, or comes from another store.\n\n" \
    "What to do: in Steam, right-click The Witness > Properties > Betas > choose None, then Properties > Installed Files > Verify integrity of game files. " \
    "Copies from other stores are not supported yet. If Steam already shows the newest version, the mod needs an update.\n\n" \
    "More: " WITNESS_FAQ_URL

} // namespace buildid
#endif
