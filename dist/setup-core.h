/* The portable decisions of the one-file setup (no Windows calls): the payload container, its hash, the names it may hold, and
 * where Setup was started from. installer.c does the Windows work; tools/test_setup_core.sh runs this file natively with
 * AddressSanitizer and mutants.
 *
 * Container "WCPAYLD1": 8 magic bytes, u32 file count, then per file: u16 name length, the name (relative, '/' separated),
 * u32 size, the bytes. All integers little endian. tools/pack_payload.py writes it. */
#ifndef COOP_SETUP_CORE_H
#define COOP_SETUP_CORE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../common/sha256.h"

#define WP_MAX_FILES 64
#define WP_MAX_NAME 120
typedef struct { const char* name; size_t nameLen; const uint8_t* data; size_t size; } wp_entry;
enum { WP_OK = 0, WP_BAD_MAGIC, WP_TRUNCATED, WP_BAD_NAME, WP_TOO_MANY, WP_TRAILING, WP_DUPLICATE, WP_EMPTY, WP_BAD_HASH };

static inline int wp_lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* A name that is safe to create below the extraction folder: relative, '/' separators, plain characters, no "." or ".." component,
 * no component that Windows would read differently (trailing dot or space, a device name, a stream colon). */
static inline int wp_name_ok(const char* n, size_t len)
{
    static const char* const devices[] = { "con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
                                           "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9", 0 };
    size_t i, start = 0;
    if (!len || len > WP_MAX_NAME) return 0;
    for (i = 0; i <= len; i++) {
        if (i < len) {
            unsigned char c = (unsigned char)n[i];
            if (c == '/') { /* component ends */ }
            else if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' || c == '+')) return 0;
            if (c != '/') continue;
        }
        { /* the component n[start..i) */
            size_t clen = i - start, k, base = 0; int d;
            if (!clen) return 0;
            if (n[start] == '.' && (clen == 1 || (clen == 2 && n[start + 1] == '.'))) return 0;
            if (n[i - 1] == '.') return 0;
            while (base < clen && n[start + base] != '.') base++;
            for (d = 0; devices[d]; d++) {
                size_t dl = strlen(devices[d]);
                if (base == dl) { for (k = 0; k < dl && wp_lower(n[start + k]) == devices[d][k]; k++) {} if (k == dl) return 0; }
            }
        }
        start = i + 1;
    }
    return 1;
}
static inline int wp_names_equal_ci(const char* a, size_t al, const char* b, size_t bl)
{
    size_t i; if (al != bl) return 0;
    for (i = 0; i < al; i++) if (wp_lower(a[i]) != wp_lower(b[i])) return 0;
    return 1;
}
static inline uint32_t wp_rd32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* Is the payload what the setup was built with? expectedHex is the 64 hex digits compiled in next to the payload. */
static inline int wp_hash_ok(const uint8_t* buf, size_t len, const char* expectedHex)
{
    uint8_t d[32]; char hex[65];
    coop_sha256_buf(buf, len, d); coop_sha256_hex(d, hex);
    return strlen(expectedHex) == 64 && !strcmp(hex, expectedHex);
}

/* Split the container into entries. Every name is checked; nothing is read past len. */
static inline int wp_parse(const uint8_t* buf, size_t len, wp_entry* out, int max, int* count)
{
    size_t at = 12, i, j; uint32_t n;
    *count = 0;
    if (len < 12) return WP_TRUNCATED;
    if (memcmp(buf, "WCPAYLD", 7) || buf[7] != '1') return WP_BAD_MAGIC;      /* (written in two parts so the exe holds the magic only in the payload itself) */
    n = wp_rd32(buf + 8);
    if (!n) return WP_EMPTY;
    if (n > (uint32_t)max || n > WP_MAX_FILES) return WP_TOO_MANY;
    for (i = 0; i < n; i++) {
        size_t nl, sz;
        if (len - at < 2) return WP_TRUNCATED;
        nl = (size_t)buf[at] | (size_t)buf[at + 1] << 8; at += 2;
        if (len - at < nl || len - at - nl < 4) return WP_TRUNCATED;
        out[i].name = (const char*)buf + at; out[i].nameLen = nl; at += nl;
#ifndef SETUP_MUT_ANY_NAME
        if (!wp_name_ok(out[i].name, nl)) return WP_BAD_NAME;
#endif
        sz = wp_rd32(buf + at); at += 4;
        if (len - at < sz) return WP_TRUNCATED;
        out[i].data = buf + at; out[i].size = sz; at += sz;
        for (j = 0; j < i; j++) if (wp_names_equal_ci(out[j].name, out[j].nameLen, out[i].name, nl)) return WP_DUPLICATE;
        *count = (int)i + 1;
    }
    return at == len ? WP_OK : WP_TRAILING;
}

/* Where was Setup started from? Explorer opens "Setup.exe" from inside a zip by unpacking only that file to
 * %TEMP%\Temp1_<zip name>.zip\ ; the files next to it are not there. path: the exe's own path, ASCII or UTF-8. */
static inline int wp_from_zip(const char* path)
{
    size_t i, len = strlen(path), start = 0;
    for (i = 0; i <= len; i++) {
        if (i == len || path[i] == '\\' || path[i] == '/') {
            size_t clen = i - start;
            if (i < len && clen >= 4 && wp_lower(path[i - 4]) == '.' && wp_lower(path[i - 3]) == 'z' && wp_lower(path[i - 2]) == 'i' && wp_lower(path[i - 1]) == 'p') return 1;   /* a folder called *.zip */
            start = i + 1;
        }
    }
    return 0;
}

#define WP_MSG_FROM_ZIP \
    "Setup was started from inside a zip file.\n\n" \
    "Windows lets you open a zip like a folder, but the other files Setup needs are not unpacked next to it.\n\n" \
    "Either:\n" \
    "  1. close this, right-click the zip, choose Extract All, and run Witness-Coop-Setup.exe from the new folder; or\n" \
    "  2. download the single file Witness-Coop-Setup.exe from the releases page. It carries everything and needs no unpacking:\n" \
    "     https://github.com/TommoDotCommo/witness-coop/releases"
#define WP_MSG_MISSING \
    "Setup cannot find its files next to itself.\n\n" \
    "Unpack the whole zip first and run Witness-Coop-Setup.exe from the unpacked folder, or download the single file " \
    "Witness-Coop-Setup.exe from the releases page. It carries everything and needs no unpacking:\n" \
    "https://github.com/TommoDotCommo/witness-coop/releases"
#define WP_MSG_DAMAGED \
    "This Setup file is damaged, or was changed after it was built: the files inside it do not match their checksum.\n\n" \
    "Nothing was installed. Download Witness-Coop-Setup.exe again from the releases page: https://github.com/TommoDotCommo/witness-coop/releases"
#endif
