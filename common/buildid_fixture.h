/* Test-only: a small synthetic 64-bit exe (file view and loader-mapped view) with a code section whose bytes and header fields the test chooses. */
#ifndef COOP_BUILDID_FIXTURE_H
#define COOP_BUILDID_FIXTURE_H
#include "buildid.h"
#include <vector>
static void put32(std::vector<uint8_t>& v, size_t at, uint32_t x) { for (int i = 0; i < 4; i++) v[at + i] = (uint8_t)(x >> (8 * i)); }
static void put16(std::vector<uint8_t>& v, size_t at, uint16_t x) { v[at] = (uint8_t)x; v[at + 1] = (uint8_t)(x >> 8); }

/* A small PE: headers 0x200, .text raw 0x400 at file 0x200 / virtual 0x1000, then .data raw 0x200. */
struct Fixture { std::vector<uint8_t> file, image; buildid::Spec spec; };
static Fixture Make(uint32_t ts, uint32_t entry, uint8_t codeFill)
{
    Fixture f; std::vector<uint8_t>& v = f.file; v.assign(0x200 + 0x400 + 0x200, 0);
    v[0] = 'M'; v[1] = 'Z'; put32(v, 0x3c, 0x80); memcpy(&v[0x80], "PE\0\0", 4);
    put16(v, 0x84, 0x8664); put16(v, 0x86, 2); put32(v, 0x88, ts); put16(v, 0x94, 0xf0);
    size_t opt = 0x98; put16(v, opt, 0x20b); put32(v, opt + 4, 0x400); put32(v, opt + 16, entry); put32(v, opt + 56, 0x3000);
    size_t s = opt + 0xf0;
    memcpy(&v[s], ".text", 5); put32(v, s + 8, 0x3f0); put32(v, s + 12, 0x1000); put32(v, s + 16, 0x400); put32(v, s + 20, 0x200); put32(v, s + 36, 0x60000020);
    s += 40; memcpy(&v[s], ".data", 5); put32(v, s + 8, 0x100); put32(v, s + 12, 0x2000); put32(v, s + 16, 0x200); put32(v, s + 20, 0x600); put32(v, s + 36, 0xc0000040);
    for (size_t i = 0; i < 0x400; i++) v[0x200 + i] = (uint8_t)(codeFill + i * 7);
    f.image.assign(0x3000, 0); memcpy(&f.image[0], &v[0], 0x200); memcpy(&f.image[0x1000], &v[0x200], 0x400); memcpy(&f.image[0x2000], &v[0x600], 0x200);
    uint8_t d[32]; coop_sha256_buf(&v[0x200], 0x3f0, d);
    f.spec.timestamp = ts; f.spec.imageSize = 0x3000; f.spec.entryRva = entry; f.spec.codeSize = 0x400; coop_sha256_hex(d, f.spec.textSha256);
    return f;
}
#endif
