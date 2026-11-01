/* Game-free test of dist/setup-core.h. Built natively with AddressSanitizer; arg 1 is a payload made by tools/pack_payload.py. */
#include <stdio.h>
#include <stdlib.h>
#include "setup-core.h"
static int failures;
static void check(int ok, const char* what) { if (!ok) { fprintf(stderr, "FAIL: %s\n", what); failures++; } }
static void put32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
/* one-file container by hand */
static size_t build(uint8_t* out, const char* const* names, int n, const char* body)
{
    size_t at = 0; int i; memcpy(out, "WCPAYLD1", 8); put32(out + 8, (uint32_t)n); at = 12;
    for (i = 0; i < n; i++) {
        size_t nl = strlen(names[i]), bl = strlen(body);
        out[at++] = (uint8_t)nl; out[at++] = (uint8_t)(nl >> 8); memcpy(out + at, names[i], nl); at += nl;
        put32(out + at, (uint32_t)bl); at += 4; memcpy(out + at, body, bl); at += bl;
    }
    return at;
}
int main(int argc, char** argv)
{
    wp_entry e[WP_MAX_FILES]; int count; uint8_t* buf = (uint8_t*)malloc(4096); size_t len; int i;
    const char* good[] = { "a.txt", "witness_models/adventurer.glb", "x-1_2+3.ps1" };
    /* names */
    check(wp_name_ok("a.txt", 5) && wp_name_ok("witness_models/adventurer.glb.txt", 33), "plain names are accepted");
    { const char* bad[] = { "", "..", "../x", "a/../b", "/abs", "a//b", "a/", ".", "a/./b", "C:x", "a\\b", "x:stream", "con", "NUL.txt", "a/com1", "Lpt9.x", "trail.", "a/b.", "sp ace", "a\\..\\b", "\xc3\xa9", "a\x01" };
      for (i = 0; i < (int)(sizeof bad / sizeof *bad); i++) check(!wp_name_ok(bad[i], strlen(bad[i])), bad[i][0] ? bad[i] : "(empty)"); }
    { char longn[200]; memset(longn, 'a', sizeof longn); check(!wp_name_ok(longn, 121) && wp_name_ok(longn, 120), "length limit"); }
    /* container */
    len = build(buf, good, 3, "hello");
    check(wp_parse(buf, len, e, WP_MAX_FILES, &count) == WP_OK && count == 3 && e[1].size == 5 && !memcmp(e[1].data, "hello", 5) && e[1].nameLen == 29, "a valid container parses");
    for (i = 0; i < (int)len; i++) { int r = wp_parse(buf, (size_t)i, e, WP_MAX_FILES, &count); check(r != WP_OK, "every truncation is refused"); }
    buf[len] = 0; check(wp_parse(buf, len + 1, e, WP_MAX_FILES, &count) == WP_TRAILING, "trailing bytes are refused");
    { const char* n[] = { "../evil.dll" }; len = build(buf, n, 1, "x"); check(wp_parse(buf, len, e, WP_MAX_FILES, &count) == WP_BAD_NAME, "a path-escaping name in the container is refused"); }
    { const char* n[] = { "A.txt", "a.TXT" }; len = build(buf, n, 2, "x"); check(wp_parse(buf, len, e, WP_MAX_FILES, &count) == WP_DUPLICATE, "names equal but for case are refused"); }
    { const char* n[] = { "a" }; len = build(buf, n, 1, "x"); memcpy(buf, "WCPAYLD2", 8); check(wp_parse(buf, len, e, WP_MAX_FILES, &count) == WP_BAD_MAGIC, "wrong magic"); }
    { const char* n[] = { "a" }; len = build(buf, n, 1, "x"); put32(buf + 8, 70); check(wp_parse(buf, len, e, WP_MAX_FILES, &count) == WP_TOO_MANY, "too many files"); put32(buf + 8, 0); check(wp_parse(buf, len, e, WP_MAX_FILES, &count) == WP_EMPTY, "no files"); }
    { const char* n[] = { "a" }; len = build(buf, n, 1, "x"); put32(buf + 12 + 2 + 1, 0xFFFFFFF0u); check(wp_parse(buf, len, e, WP_MAX_FILES, &count) == WP_TRUNCATED, "a size far past the end"); }
    /* hash */
    { const char* n[] = { "a" }; uint8_t d[32]; char hex[65]; len = build(buf, n, 1, "payload"); coop_sha256_buf(buf, len, d); coop_sha256_hex(d, hex);
      check(wp_hash_ok(buf, len, hex), "the right hash is accepted");
      buf[len - 1] ^= 1; check(!wp_hash_ok(buf, len, hex), "one changed byte changes the hash");
      check(!wp_hash_ok(buf, len, "") && !wp_hash_ok(buf, len, "abc"), "an empty or short expected hash never matches"); }
    /* where Setup runs from */
    check(wp_from_zip("C:\\Users\\x\\AppData\\Local\\Temp\\Temp1_witness-coop-v0.1.zip\\Witness-Coop-Setup.exe"), "Explorer's zip temp folder");
    check(wp_from_zip("C:\\Users\\x\\Downloads\\witness-coop.ZIP\\Witness-Coop-Setup.exe"), "a path through a .zip");
    check(!wp_from_zip("C:\\Users\\x\\Downloads\\witness-coop-v0.1\\Witness-Coop-Setup.exe") && !wp_from_zip("C:\\Users\\x\\Downloads\\Witness-Coop-Setup.exe"), "an unpacked folder or a lone download is not a zip");
    check(!wp_from_zip("C:\\Users\\x\\Downloads\\a.zip") , "a file called .zip at the end is the file, not a folder");
    if (argc > 1) {                                                           /* the real payload */
        FILE* f = fopen(argv[1], "rb"); uint8_t* big; long sz; check(f != NULL, "payload file"); if (f) {
            fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET); big = (uint8_t*)malloc((size_t)sz); check(fread(big, 1, (size_t)sz, f) == (size_t)sz, "read");
            check(wp_parse(big, (size_t)sz, e, WP_MAX_FILES, &count) == WP_OK && count >= 5, "the real payload parses"); fclose(f); free(big); } }
    free(buf);
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    printf("setup-core: all checks passed\n");
    return 0;
}
