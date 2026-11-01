/* Game-free test of the build check (common/buildid.h): a synthetic exe stands in for the game (the real one is not in the repo),
 * built in memory both as a file and as a loader-mapped image. Run under AddressSanitizer: a read past the end is a failure.
 * With the real exe: buildid_test <exe> also checks the header's constants against it and that it is "known". */
#include "buildid.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <fstream>
#include <iterator>
static int failures;
static void check(bool ok, const char* what) { if (!ok) { fprintf(stderr, "FAIL: %s\n", what); ++failures; } }
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
static buildid::Verdict File(const std::vector<uint8_t>& v, const buildid::Spec& s) { std::vector<uint8_t> e(v); return buildid::Inspect(e.data(), e.size(), false, s).verdict; }
static buildid::Verdict Image(const std::vector<uint8_t>& v, const buildid::Spec& s) { std::vector<uint8_t> e(v); return buildid::Inspect(e.data(), e.size(), true, s).verdict; }

int main(int argc, char** argv)
{
    /* SHA-256 against the standard vectors */
    uint8_t d[32]; char h[65];
    coop_sha256_buf("abc", 3, d); coop_sha256_hex(d, h);
    check(!strcmp(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "sha256 of abc");
    coop_sha256_buf("", 0, d); coop_sha256_hex(d, h);
    check(!strcmp(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"), "sha256 of nothing");
    std::vector<uint8_t> big(1000003, 'a'); coop_sha256 c; coop_sha256_init(&c);
    for (size_t i = 0; i < big.size(); i += 777) coop_sha256_update(&c, &big[i], std::min<size_t>(777, big.size() - i));
    coop_sha256_final(&c, d); coop_sha256_hex(d, h);
    uint8_t d2[32]; char h2[65]; coop_sha256_buf(big.data(), big.size(), d2); coop_sha256_hex(d2, h2);
    check(!strcmp(h, h2), "sha256 in pieces equals in one go");

    Fixture ok = Make(0x5c48cf09, 0x1100, 1);
    check(File(ok.file, ok.spec) == buildid::kKnown, "the known build, as a file");
    check(Image(ok.image, ok.spec) == buildid::kKnown, "the known build, mapped like the loader does");
    check(buildid::MappedSize(ok.image.data()) == 0x3000, "mapped image size read from the first page");

    Fixture other = Make(0x5c48cf0a, 0x1100, 1);                            /* a newer build: other time stamp */
    check(File(other.file, ok.spec) == buildid::kOtherHeader, "another time stamp is another build");
    Fixture entry = Make(0x5c48cf09, 0x1180, 1);
    check(Image(entry.image, ok.spec) == buildid::kOtherHeader, "another entry point is another build");
    Fixture patched = ok; patched.file[0x200 + 17] ^= 1; patched.image[0x1000 + 17] ^= 1;      /* same header, one code byte changed (a repack, a crack, a patch) */
    check(File(patched.file, ok.spec) == buildid::kOtherCode, "one changed code byte: file");
    check(Image(patched.image, ok.spec) == buildid::kOtherCode, "one changed code byte: mapped");
    Fixture data = ok; data.file[0x600 + 3] ^= 1;                           /* outside the code: not part of the fingerprint */
    check(File(data.file, ok.spec) == buildid::kKnown, "a change in data does not matter");

    /* truncated and damaged files never read out of bounds (AddressSanitizer) and are never "known" */
    for (size_t cut : {size_t(0), size_t(1), size_t(0x3f), size_t(0x100), size_t(0x1ff), size_t(0x200), size_t(0x250), size_t(0x400), size_t(0x5ef)}) {
        std::vector<uint8_t> t(ok.file.begin(), ok.file.begin() + std::min(cut, ok.file.size()));
        check(File(t, ok.spec) != buildid::kKnown, "a truncated exe is not the known build");
    }
    std::vector<uint8_t> junk(0x400, 0xAB); check(File(junk, ok.spec) == buildid::kNotPe, "garbage is not a PE");
    std::vector<uint8_t> text(0x400, 'x'); check(File(text, ok.spec) == buildid::kNotPe, "text is not a PE");
    Fixture badpe = ok; put32(badpe.file, 0x3c, 0xFFFFFFF0u); check(File(badpe.file, ok.spec) == buildid::kNotPe, "e_lfanew far outside the file");
    Fixture badsec = ok; put16(badsec.file, 0x86, 90); check(File(badsec.file, ok.spec) != buildid::kKnown, "section table cut off");
    Fixture bigptr = ok; put32(bigptr.file, 0x98 + 0xf0 + 20, 0xFFFFFF00u); check(File(bigptr.file, ok.spec) != buildid::kKnown, "code section pointer outside the file");
    Fixture x86 = ok; put16(x86.file, 0x84, 0x14c); check(File(x86.file, ok.spec) == buildid::kNotPe, "a 32-bit exe is refused");

    if (argc > 1) {                                                          /* the real exe (optional) */
        std::ifstream in(argv[1], std::ios::binary); std::vector<uint8_t> exe((std::istreambuf_iterator<char>(in)), {});
        buildid::Info i = buildid::Inspect(exe.data(), exe.size(), false, buildid::Witness());
        char line[200]; buildid::Describe(i, line, sizeof line); printf("real exe: %s\n", line);
        check(i.verdict == buildid::kKnown, "the real exe is the known build");
        exe[exe.size() / 2] ^= 1;                                            /* a changed byte in the middle of the file */
        check(buildid::Inspect(exe.data(), exe.size(), false, buildid::Witness()).verdict != buildid::kNotPe, "still a PE");
    }
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("buildid: all checks passed\n");
    return 0;
}
