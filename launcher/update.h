/* The launcher's self-update, game-free and portable (T0 tests: tools/test_launcher_update.py, built into update_cli.cpp).
 * Ported from the MDK2 co-op launcher's updater (MIT, same owner): GitHub release checker, SHA-256 / ZIP verifier, and a transactional apply.
 * What is different here: the apply goes through the installer's ownership receipts (dist/install-state.ps1, decisions in dist/install-policy.h):
 * a managed file is replaced only when its receipt exists and the file is what the receipt says the installer put there; the game's own
 * AnselSDK64 (AnselSDK64_real.dll and its .coop-original) is never touched. Pure functions and standard file operations only; no UI. */
#ifndef WITNESS_LAUNCHER_UPDATE_H
#define WITNESS_LAUNCHER_UPDATE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include "../dist/install-policy.h"

#ifdef _WIN32
#include <windows.h>
static inline int lu_rename_replace(const char *src, const char *dst)
{
    return MoveFileExA(src, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
#else
static inline int lu_rename_replace(const char *src, const char *dst)
{
    return rename(src, dst) == 0;
}
#endif

#define LU_INTERVAL_SEC     86400u
#define LU_MAX_JSON_BYTES   65536u
#define LU_MAX_ASSET_BYTES  (64u * 1024u * 1024u)
#define LU_NOTES_MAX        1024u
#define LU_URL_MAX          384u
#define LU_TAG_MAX          48u

enum lu_result {
    LU_OK = 0,
    LU_UP_TO_DATE,
    LU_UPDATE_AVAILABLE,
    LU_THROTTLED,
    LU_ERR_OFFLINE,
    LU_ERR_RATELIMIT,
    LU_ERR_HTTP,
    LU_ERR_JSON,
    LU_ERR_VERSION,
    LU_ERR_ASSET,
    LU_ERR_SIDECAR,
    LU_ERR_TRUNCATED,
    LU_ERR_CHECKSUM,
    LU_ERR_ARCHIVE,
    LU_ERR_PE,
    LU_ERR_IO,
    LU_ERR_ROLLBACK,
    LU_ERR_RECEIPT,     /* a managed file is not what the installer's receipt says (changed by hand, another build, a leftover of an interrupted setup, or no receipt: not installed by Setup) */
    LU_ERR_RUNNING,     /* the game is running from this folder: the proxy DLL is replaced only while it is not */
    LU_ERR_DOWNGRADE,   /* the package is not newer than the installed version */
    LU_ERR_DEV          /* the installed version is not a release version (a development build): never replaced */
};

struct lu_schedule {
    int auto_enabled;
    uint64_t last_check;
    uint64_t retry_after;
    int include_prereleases;
};

struct lu_release {
    char tag[LU_TAG_MAX];
    char zip_name[96];
    char zip_url[LU_URL_MAX];
    size_t zip_size;
    char sha_name[112];
    char sha_url[LU_URL_MAX];
    size_t sha_size;
    char notes[LU_NOTES_MAX];
    char published_at[36];
    int prerelease;
};

/* ---- FIPS 180-4 SHA-256 ---- */

static inline uint32_t lu_rotr32(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

static inline void lu_sha256_block(uint32_t h[8], const unsigned char b[64])
{
    static const uint32_t k[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfc,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffa,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
    };
    uint32_t w[64], a = h[0], b0 = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    int i;
    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)b[i*4]<<24) | ((uint32_t)b[i*4+1]<<16) | ((uint32_t)b[i*4+2]<<8) | (uint32_t)b[i*4+3];
    for (i = 16; i < 64; i++) {
        uint32_t s0 = lu_rotr32(w[i-15], 7) ^ lu_rotr32(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = lu_rotr32(w[i-2], 17) ^ lu_rotr32(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    for (i = 0; i < 64; i++) {
        uint32_t s1 = lu_rotr32(e, 6) ^ lu_rotr32(e, 11) ^ lu_rotr32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = hh + s1 + ch + k[i] + w[i];
        uint32_t s0 = lu_rotr32(a, 2) ^ lu_rotr32(a, 13) ^ lu_rotr32(a, 22);
        uint32_t maj = (a & b0) ^ (a & c) ^ (b0 & c);
        uint32_t t2 = s0 + maj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b0; b0 = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b0; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

static inline void lu_sha256_hex(const void *data, size_t len, char out_hex[65])
{
    static const char hex[] = "0123456789abcdef";
    uint32_t h[8] = {
        0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u
    };
    const unsigned char *p = (const unsigned char *)data;
    unsigned char tail[128];
    uint64_t bits = (uint64_t)len * 8u;
    size_t rem = len & 63u, pad_len = (rem < 56u) ? (64u - rem) : (128u - rem);
    int i;
    while (len >= 64u) {
        lu_sha256_block(h, p);
        p += 64u; len -= 64u;
    }
    memset(tail, 0, sizeof(tail));
    if (rem) memcpy(tail, p, rem);
    tail[rem] = 0x80u;
    for (i = 0; i < 8; i++)
        tail[rem + pad_len - 1 - i] = (unsigned char)(bits >> (i * 8));
    lu_sha256_block(h, tail);
    if (rem + pad_len > 64u)
        lu_sha256_block(h, tail + 64u);
    for (i = 0; i < 8; i++) {
        out_hex[i*8+0] = hex[(h[i] >> 28) & 15];
        out_hex[i*8+1] = hex[(h[i] >> 24) & 15];
        out_hex[i*8+2] = hex[(h[i] >> 20) & 15];
        out_hex[i*8+3] = hex[(h[i] >> 16) & 15];
        out_hex[i*8+4] = hex[(h[i] >> 12) & 15];
        out_hex[i*8+5] = hex[(h[i] >> 8) & 15];
        out_hex[i*8+6] = hex[(h[i] >> 4) & 15];
        out_hex[i*8+7] = hex[h[i] & 15];
    }
    out_hex[64] = 0;
}

static inline uint32_t lu_crc32(const unsigned char *data, size_t len)
{
    uint32_t crc = 0xffffffffu;
    size_t i;
    int b;
    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/* ---- Version comparison ---- */

struct lu_semver {
    unsigned major, minor, patch;
    char pre[32];
};

static inline int lu_tag_safe(const char *s)
{
    size_t i, n;
    if (!s || !*s) return 0;
    n = strlen(s);
    if (n >= LU_TAG_MAX || !isalnum((unsigned char)s[0])) return 0;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!isalnum(c) && c != '.' && c != '_' && c != '+' && c != '-') return 0;
    }
    return 1;
}

static inline int lu_parse_semver(const char *tag, struct lu_semver *v)
{
    const char *p = tag;
    unsigned parts[3] = {0, 0, 0};
    int k;
    memset(v, 0, sizeof(*v));
    if (!lu_tag_safe(tag)) return 0;
    if (*p == 'v' || *p == 'V') p++;
    for (k = 0; k < 3; k++) {
        unsigned val = 0;
        if (!isdigit((unsigned char)*p)) return 0;
        while (isdigit((unsigned char)*p)) {
            unsigned d = (unsigned)(*p - '0');
            if (val > (1000000u - d) / 10u) return 0;
            val = val * 10u + d;
            p++;
        }
        parts[k] = val;
        if (k < 2) {
            if (*p != '.') return 0;
            p++;
        }
    }
    if (*p == '-') {
        size_t n = 0;
        p++;
        if (!*p || *p == '+') return 0;
        while (*p && *p != '+') {
            if (n + 1 >= sizeof(v->pre)) return 0;
            v->pre[n++] = *p++;
        }
        v->pre[n] = 0;
    }
    if (*p == '+') {
        p++;
        if (!*p) return 0;
        while (*p) p++;
    }
    if (*p != 0) return 0;
    v->major = parts[0]; v->minor = parts[1]; v->patch = parts[2];
    return 1;
}

/* semver 2.0 pre-release order: dot-separated identifiers, numeric ones compared as numbers and lower than alphanumeric ones; fewer identifiers is lower */
static inline int lu_pre_cmp(const char *a, const char *b)
{
    for (;;) {
        char ia[32], ib[32];
        size_t na = 0, nb = 0;
        int numa = 1, numb = 1;
        if (!*a && !*b) return 0;
        if (!*a) return -1;
        if (!*b) return 1;
        while (*a && *a != '.' && na + 1 < sizeof(ia)) { if (!isdigit((unsigned char)*a)) numa = 0; ia[na++] = *a++; }
        while (*b && *b != '.' && nb + 1 < sizeof(ib)) { if (!isdigit((unsigned char)*b)) numb = 0; ib[nb++] = *b++; }
        ia[na] = 0; ib[nb] = 0;
        while (*a && *a != '.') a++;
        while (*b && *b != '.') b++;
        if (numa && numb) {
            size_t sa = 0, sb = 0;
            while (ia[sa] == '0' && ia[sa + 1]) sa++;
            while (ib[sb] == '0' && ib[sb + 1]) sb++;
            if (strlen(ia + sa) != strlen(ib + sb)) return strlen(ia + sa) > strlen(ib + sb) ? 1 : -1;
            { int c = strcmp(ia + sa, ib + sb); if (c) return c > 0 ? 1 : -1; }
        } else if (numa != numb) {
            return numa ? -1 : 1;
        } else {
            int c = strcmp(ia, ib);
            if (c) return c > 0 ? 1 : -1;
        }
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
}

static inline int lu_semver_cmp(const struct lu_semver *a, const struct lu_semver *b)
{
    if (a->major != b->major) return a->major > b->major ? 1 : -1;
    if (a->minor != b->minor) return a->minor > b->minor ? 1 : -1;
    if (a->patch != b->patch) return a->patch > b->patch ? 1 : -1;
    if (!a->pre[0] && b->pre[0]) return 1;
    if (a->pre[0] && !b->pre[0]) return -1;
    return lu_pre_cmp(a->pre, b->pre);
}

/* A development build: "git describe" after a tag (v0.1.0-3-gabc1234), a dirty tree, a bare commit id or "dev". Such a build is never offered a replacement
 * (it is not a release; the installer's receipts would refuse it anyway). */
static inline int lu_is_dev_build(const char *v)
{
    struct lu_semver sv;
    size_t n;
    const char *g, *p;
    if (!v || !lu_parse_semver(v, &sv)) return 1;
    n = strlen(v);
    if (n > 6 && !strcmp(v + n - 6, "-dirty")) return 1;
    for (g = v; (g = strstr(g, "-g")) != NULL; g += 2) {
        p = g + 2;
        while (isxdigit((unsigned char)*p)) p++;
        if (p - (g + 2) >= 7 && *p == 0) return 1;
    }
    return 0;
}

/* 1: the candidate is a valid release tag strictly newer than current. 0: not newer (equal, older, or current is not a release version: a development
 * build, "dev" or a bare commit id, is never offered a replacement). -1: the candidate tag is not a valid version. */
static inline int lu_version_is_newer(const char *current, const char *candidate)
{
    struct lu_semver cur, cand;
    if (!lu_parse_semver(candidate, &cand)) return -1;
    if (lu_is_dev_build(current) || !lu_parse_semver(current, &cur)) return 0;
    return lu_semver_cmp(&cand, &cur) > 0 ? 1 : 0;
}

/* Is the installed version a pre-release (its tag has a pre-release part: v0.2.0-beta.1, or a build ahead of a tag: v0.1.0-3-gabc123, or not a release at all)? */
static inline int lu_version_is_prerelease(const char *current)
{
    struct lu_semver cur;
    if (lu_is_dev_build(current) || !lu_parse_semver(current, &cur)) return 1;
    return cur.pre[0] ? 1 : 0;
}

/* ---- Check throttling & rate-limit policy ---- */

static inline int lu_can_check(const struct lu_schedule *s, uint64_t now, int manual)
{
    if (now < s->retry_after) return 0;
    if (manual) return 1;
    if (!s->auto_enabled) return 0;
    if (s->last_check && now < s->last_check + LU_INTERVAL_SEC) return 0;
    return 1;
}

static inline enum lu_result lu_http_classify(int status_code, int ratelimit_remaining,
                                              uint32_t retry_after_sec, uint64_t now,
                                              struct lu_schedule *s)
{
    if (status_code <= 0) return LU_ERR_OFFLINE;
    if (status_code == 429 || (status_code == 403 && ratelimit_remaining == 0)) {
        uint32_t wait_s = retry_after_sec ? retry_after_sec : 3600u;
        if (wait_s > 86400u) wait_s = 86400u;
        s->retry_after = now + wait_s;
        return LU_ERR_RATELIMIT;
    }
    if (status_code != 200) return LU_ERR_HTTP;
    s->last_check = now;
    return LU_OK;
}

/* ---- JSON release parser & asset selector ---- */

static inline const char *lu_json_str(const char *p, const char *end, char *out, size_t cap, int allow_newlines)
{
    size_t n = 0;
    if (p >= end || *p != '"') return NULL;
    p++;
    while (p < end && *p != '"') {
        unsigned char c = (unsigned char)*p++;
        if (c == '\\') {
            if (p >= end) return NULL;
            c = (unsigned char)*p++;
            if (c == 'n') c = allow_newlines ? '\n' : ' ';
            else if (c == 'r') { if (allow_newlines) continue; c = ' '; }
            else if (c == 't') c = ' ';
            else if (c == 'u') {
                if (end - p < 4) return NULL;
                p += 4;
                c = '?';
            }
        }
        if (c == '\n' && allow_newlines) {
            if (n + 1 < cap) out[n++] = '\n';
        } else if (c >= 32 && c <= 126) {
            if (n + 1 < cap) out[n++] = (char)c;
        }
    }
    if (p >= end || *p != '"') return NULL;
    out[n] = 0;
    return p + 1;
}

static inline const char *lu_json_find_key(const char *start, const char *end, const char *key)
{
    size_t klen = strlen(key);
    const char *p = start;
    while (p < end) {
        if (*p == '"') {
            char tmp[64];
            const char *after = lu_json_str(p, end, tmp, sizeof(tmp), 0);
            if (!after) return NULL;
            p = after;
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
            if (p < end && *p == ':' && strlen(tmp) == klen && !strcmp(tmp, key)) {
                p++;
                while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
                return p;
            }
        } else {
            p++;
        }
    }
    return NULL;
}

static inline int lu_json_uint(const char *p, const char *end, size_t *out)
{
    size_t v = 0;
    if (p >= end || !isdigit((unsigned char)*p)) return 0;
    while (p < end && isdigit((unsigned char)*p)) {
        unsigned d = (unsigned)(*p - '0');
        if (v > (LU_MAX_ASSET_BYTES - d) / 10u) return 0;
        v = v * 10u + d;
        p++;
    }
    *out = v;
    return 1;
}

static inline int lu_url_valid(const char *url)
{
    size_t i, n = strlen(url);
    if (n < 10 || n >= LU_URL_MAX) return 0;
    if (strncmp(url, "https://", 8) && strncmp(url, "http://127.0.0.1:", 17) && strncmp(url, "http://localhost:", 17))
        return 0;
    for (i = 0; i < n; i++)
        if ((unsigned char)url[i] <= 32 || (unsigned char)url[i] > 126) return 0;
    return 1;
}

static inline int lu_json_bool(const char *p, const char *end)
{
    if (!p || p >= end) return 0;
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    return (end - p >= 4 && !strncmp(p, "true", 4)) ? 1 : 0;
}

static inline enum lu_result lu_parse_single_release(const char *start, const char *end,
                                                     const char *prefix, int include_prereleases,
                                                     struct lu_release *out, struct lu_semver *out_sv)
{
    const char *v, *assets, *p;
    memset(out, 0, sizeof(*out));
    memset(out_sv, 0, sizeof(*out_sv));

    v = lu_json_find_key(start, end, "draft");
    if (v && lu_json_bool(v, end))
        return LU_UP_TO_DATE;

    v = lu_json_find_key(start, end, "prerelease");
    if (v && lu_json_bool(v, end)) {
        out->prerelease = 1;
        if (!include_prereleases) return LU_UP_TO_DATE;
    }

    v = lu_json_find_key(start, end, "published_at");
    if (v && *v == '"') {
        char pub[36] = {0};
        if (lu_json_str(v, end, pub, sizeof(pub), 0))
            snprintf(out->published_at, sizeof(out->published_at), "%s", pub);
    }

    v = lu_json_find_key(start, end, "tag_name");
    if (!v || !lu_json_str(v, end, out->tag, sizeof(out->tag), 0))
        return LU_ERR_JSON;
    if (!lu_parse_semver(out->tag, out_sv))
        return LU_ERR_VERSION;

    v = lu_json_find_key(start, end, "body");
    if (v && *v == '"')
        lu_json_str(v, end, out->notes, sizeof(out->notes), 1);

    snprintf(out->zip_name, sizeof(out->zip_name), "%s-%s.zip", prefix, out->tag);
    snprintf(out->sha_name, sizeof(out->sha_name), "%s-%s.zip.sha256", prefix, out->tag);

    assets = lu_json_find_key(start, end, "assets");
    if (!assets || *assets != '[')
        return LU_ERR_ASSET;
    p = assets + 1;
    while (p < end && *p != ']') {
        if (*p == '{') {
            const char *obj_start = p + 1, *obj_end = p + 1;
            int depth = 1;
            while (obj_end < end && depth > 0) {
                if (*obj_end == '"') {
                    char skip[4];
                    const char *next = lu_json_str(obj_end, end, skip, sizeof(skip), 1);
                    if (!next) return LU_ERR_JSON;
                    obj_end = next;
                    continue;
                }
                if (*obj_end == '{') depth++;
                else if (*obj_end == '}') depth--;
                if (depth > 0) obj_end++;
            }
            if (depth != 0) return LU_ERR_JSON;
            {
                char name[112] = {0}, url[LU_URL_MAX] = {0};
                size_t sz = 0;
                const char *nk = lu_json_find_key(obj_start, obj_end, "name");
                const char *uk = lu_json_find_key(obj_start, obj_end, "browser_download_url");
                const char *sk = lu_json_find_key(obj_start, obj_end, "size");
                if (nk && uk && sk && lu_json_str(nk, obj_end, name, sizeof(name), 0) &&
                    lu_json_str(uk, obj_end, url, sizeof(url), 0) && lu_json_uint(sk, obj_end, &sz) && sz > 0 && lu_url_valid(url)) {
                    if (!strcmp(name, out->zip_name)) {
                        strcpy(out->zip_url, url);
                        out->zip_size = sz;
                    } else if (!strcmp(name, out->sha_name) && sz <= 4096u) {
                        strcpy(out->sha_url, url);
                        out->sha_size = sz;
                    }
                }
            }
            p = obj_end + 1;
        } else {
            p++;
        }
    }
    if (!out->zip_url[0] || !out->zip_size || !out->sha_url[0] || !out->sha_size)
        return LU_ERR_ASSET;
    return LU_OK;
}

static inline enum lu_result lu_parse_release(const char *json, size_t len, const char *prefix,
                                              int include_prereleases, struct lu_release *out)
{
    const char *end = json + len, *p;
    int have_best = 0;
    struct lu_semver best_sv;
    enum lu_result last_err = LU_ERR_JSON;
    memset(out, 0, sizeof(*out));

    if (!json || !len || len >= LU_MAX_JSON_BYTES || memchr(json, 0, len))
        return LU_ERR_JSON;

    p = json;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    if (p >= end) return LU_ERR_JSON;

    if (*p == '{') {
        return lu_parse_single_release(p + 1, end, prefix, include_prereleases, out, &best_sv);
    }

    if (*p != '[') return LU_ERR_JSON;

    p++;
    while (p < end && *p != ']') {
        if (*p == '{') {
            const char *obj_start = p + 1, *obj_end = p + 1;
            int depth = 1;
            while (obj_end < end && depth > 0) {
                if (*obj_end == '"') {
                    char skip[4];
                    const char *next = lu_json_str(obj_end, end, skip, sizeof(skip), 1);
                    if (!next) return LU_ERR_JSON;
                    obj_end = next;
                    continue;
                }
                if (*obj_end == '{') depth++;
                else if (*obj_end == '}') depth--;
                if (depth > 0) obj_end++;
            }
            if (depth != 0) return LU_ERR_JSON;
            {
                struct lu_release cand;
                struct lu_semver cand_sv;
                enum lu_result r = lu_parse_single_release(obj_start, obj_end, prefix, include_prereleases, &cand, &cand_sv);
                if (r == LU_OK) {
                    if (!have_best) {
                        *out = cand;
                        best_sv = cand_sv;
                        have_best = 1;
                    } else {
                        int cmp = lu_semver_cmp(&cand_sv, &best_sv);
                        if (cmp > 0 || (cmp == 0 && strcmp(cand.published_at, out->published_at) > 0)) {
                            *out = cand;
                            best_sv = cand_sv;
                        }
                    }
                } else if (r != LU_UP_TO_DATE) {
                    last_err = r;
                }
            }
            p = obj_end + 1;
        } else {
            p++;
        }
    }
    if (p >= end || *p != ']') return LU_ERR_JSON;
    if (have_best) return LU_OK;
    if (last_err == LU_ERR_ASSET || last_err == LU_ERR_VERSION) return last_err;
    return LU_UP_TO_DATE;
}

/* ---- Sidecar & download verification ---- */

static inline enum lu_result lu_parse_sidecar(const char *text, size_t len, const char *expected_zip_name, char out_hash[65])
{
    size_t i = 0, name_start, name_len;
    if (!text || len < 66 || len > 4096 || memchr(text, 0, len)) return LU_ERR_SIDECAR;
    for (i = 0; i < 64; i++) {
        unsigned char c = (unsigned char)text[i];
        if (!isxdigit(c)) return LU_ERR_SIDECAR;
        out_hash[i] = (char)tolower(c);
    }
    out_hash[64] = 0;
    if (text[i] != ' ' && text[i] != '\t') return LU_ERR_SIDECAR;
    while (i < len && (text[i] == ' ' || text[i] == '\t')) i++;
    if (i < len && text[i] == '*') i++;
    name_start = i;
    while (i < len && text[i] != '\r' && text[i] != '\n' && text[i] != ' ' && text[i] != '\t') i++;
    name_len = i - name_start;
    while (i < len && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) i++;
    if (i != len || name_len != strlen(expected_zip_name) || strncmp(text + name_start, expected_zip_name, name_len))
        return LU_ERR_SIDECAR;
    return LU_OK;
}

static inline enum lu_result lu_verify_download(const unsigned char *bytes, size_t actual_size,
                                                size_t declared_size, const char *expected_sha256)
{
    char actual_hash[65];
    if (!bytes || !declared_size || actual_size != declared_size || actual_size > LU_MAX_ASSET_BYTES)
        return LU_ERR_TRUNCATED;
    lu_sha256_hex(bytes, actual_size, actual_hash);
    if (strcmp(actual_hash, expected_sha256) != 0)
        return LU_ERR_CHECKSUM;
    return LU_OK;
}

/* ---- RFC 1951 Inflate + ZIP extractor + PE check ---- */

struct lu_bits {
    const unsigned char *src;
    size_t size, pos;
    uint32_t bitbuf;
    int bitcnt;
};

static inline int lu_getbits(struct lu_bits *b, int n, unsigned *out)
{
    while (b->bitcnt < n) {
        if (b->pos >= b->size) return 0;
        b->bitbuf |= (uint32_t)b->src[b->pos++] << b->bitcnt;
        b->bitcnt += 8;
    }
    *out = (unsigned)(b->bitbuf & ((1u << n) - 1u));
    b->bitbuf >>= n;
    b->bitcnt -= n;
    return 1;
}

struct lu_huff {
    uint16_t count[16];
    uint16_t symbol[288];
};

static inline int lu_huff_build(struct lu_huff *h, const unsigned char *lengths, int nsyms)
{
    uint16_t offs[16];
    int i, left = 1;
    memset(h->count, 0, sizeof(h->count));
    for (i = 0; i < nsyms; i++) {
        if (lengths[i] > 15) return 0;
        h->count[lengths[i]]++;
    }
    if (h->count[0] == nsyms) return 1;
    for (i = 1; i <= 15; i++) {
        left <<= 1;
        left -= h->count[i];
        if (left < 0) return 0;
    }
    offs[1] = 0;
    for (i = 1; i < 15; i++) offs[i + 1] = (uint16_t)(offs[i] + h->count[i]);
    for (i = 0; i < nsyms; i++)
        if (lengths[i]) h->symbol[offs[lengths[i]]++] = (uint16_t)i;
    return 1;
}

static inline int lu_huff_decode(struct lu_bits *b, const struct lu_huff *h, int *sym)
{
    int len, code = 0, first = 0, index = 0;
    for (len = 1; len <= 15; len++) {
        unsigned bit;
        if (!lu_getbits(b, 1, &bit)) return 0;
        code |= (int)bit;
        if (code - h->count[len] < first) {
            *sym = h->symbol[index + (code - first)];
            return 1;
        }
        index += h->count[len];
        first = (first + h->count[len]) << 1;
        code <<= 1;
    }
    return 0;
}

static inline int lu_inflate(const unsigned char *src, size_t src_len, unsigned char *dst, size_t dst_cap, size_t *out_len)
{
    static const uint16_t lbase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const uint8_t lextra[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const uint16_t dbase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const uint8_t dextra[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    static const uint8_t clorder[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    struct lu_bits b = { src, src_len, 0, 0, 0 };
    size_t out = 0;
    unsigned bfinal = 0;
    do {
        unsigned btype;
        if (!lu_getbits(&b, 1, &bfinal) || !lu_getbits(&b, 2, &btype)) return 0;
        if (btype == 0) {
            uint16_t len, nlen;
            b.bitbuf = 0; b.bitcnt = 0;
            if (b.pos + 4 > b.size) return 0;
            len = (uint16_t)(b.src[b.pos] | (b.src[b.pos+1] << 8));
            nlen = (uint16_t)(b.src[b.pos+2] | (b.src[b.pos+3] << 8));
            b.pos += 4;
            if ((uint32_t)nlen + (uint32_t)len != 0xffffu || b.pos + len > b.size || out + len > dst_cap) return 0;
            memcpy(dst + out, b.src + b.pos, len);
            b.pos += len; out += len;
        } else if (btype == 1 || btype == 2) {
            struct lu_huff lit_huff, dist_huff;
            unsigned char lengths[320];
            int i, nlit = 288, ndist = 32;
            if (btype == 1) {
                for (i = 0; i < 144; i++) lengths[i] = 8;
                for (; i < 256; i++) lengths[i] = 9;
                for (; i < 280; i++) lengths[i] = 7;
                for (; i < 288; i++) lengths[i] = 8;
                for (; i < 320; i++) lengths[i] = 5;
            } else {
                unsigned hlit, hdist, hclen;
                unsigned char clen[19] = {0};
                struct lu_huff chuff;
                if (!lu_getbits(&b, 5, &hlit) || !lu_getbits(&b, 5, &hdist) || !lu_getbits(&b, 4, &hclen)) return 0;
                nlit = (int)hlit + 257; ndist = (int)hdist + 1;
                if (nlit > 286 || ndist > 30) return 0;
                for (i = 0; i < (int)hclen + 4; i++) {
                    unsigned v;
                    if (!lu_getbits(&b, 3, &v)) return 0;
                    clen[clorder[i]] = (unsigned char)v;
                }
                if (!lu_huff_build(&chuff, clen, 19)) return 0;
                i = 0;
                while (i < nlit + ndist) {
                    int sym;
                    if (!lu_huff_decode(&b, &chuff, &sym)) return 0;
                    if (sym < 16) {
                        lengths[i++] = (unsigned char)sym;
                    } else {
                        unsigned rep = 0;
                        unsigned char prev = 0;
                        if (sym == 16) {
                            if (i == 0 || !lu_getbits(&b, 2, &rep)) return 0;
                            prev = lengths[i - 1]; rep += 3;
                        } else if (sym == 17) {
                            if (!lu_getbits(&b, 3, &rep)) return 0;
                            rep += 3;
                        } else if (sym == 18) {
                            if (!lu_getbits(&b, 7, &rep)) return 0;
                            rep += 11;
                        } else return 0;
                        if (i + (int)rep > nlit + ndist) return 0;
                        while (rep--) lengths[i++] = prev;
                    }
                }
            }
            if (!lu_huff_build(&lit_huff, lengths, nlit) || !lu_huff_build(&dist_huff, lengths + nlit, ndist)) return 0;
            for (;;) {
                int sym;
                if (!lu_huff_decode(&b, &lit_huff, &sym)) return 0;
                if (sym < 256) {
                    if (out >= dst_cap) return 0;
                    dst[out++] = (unsigned char)sym;
                } else if (sym == 256) {
                    break;
                } else {
                    unsigned len, dist, eb;
                    int dsym;
                    sym -= 257;
                    if (sym >= 29 || !lu_getbits(&b, lextra[sym], &eb)) return 0;
                    len = lbase[sym] + eb;
                    if (!lu_huff_decode(&b, &dist_huff, &dsym) || dsym >= 30 || !lu_getbits(&b, dextra[dsym], &eb)) return 0;
                    dist = dbase[dsym] + eb;
                    if (dist > out || out + len > dst_cap) return 0;
                    while (len--) {
                        dst[out] = dst[out - dist];
                        out++;
                    }
                }
            }
        } else return 0;
    } while (!bfinal);
    *out_len = out;
    return 1;
}

static inline uint16_t lu_u16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t lu_u32(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }

static inline int lu_zip_path_safe(const char *path, size_t len)
{
    size_t i;
    if (!len || len >= 240 || path[0] == '/' || path[0] == '\\') return 0;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)path[i];
        if (c < 32 || c > 126 || c == '\\' || c == ':') return 0;
        if (c == '.' && i + 1 < len && path[i+1] == '.') return 0;
    }
    return 1;
}

static inline enum lu_result lu_zip_extract(const unsigned char *zip, size_t zip_len, const char *member_name,
                                            unsigned char **out_buf, size_t *out_len)
{
    size_t eocd = 0, cd_off, cd_size, i, pos;
    uint16_t cd_entries;
    *out_buf = NULL; *out_len = 0;
    if (!zip || zip_len < 22 || zip_len > LU_MAX_ASSET_BYTES) return LU_ERR_ARCHIVE;
    for (i = zip_len - 22; ; i--) {
        if (zip[i] == 'P' && zip[i+1] == 'K' && zip[i+2] == 5 && zip[i+3] == 6) {
            eocd = i; break;
        }
        if (i == 0 || zip_len - i > 65557u) return LU_ERR_ARCHIVE;
    }
    cd_entries = lu_u16(zip + eocd + 10);
    cd_size = lu_u32(zip + eocd + 12);
    cd_off = lu_u32(zip + eocd + 16);
    if (cd_off + cd_size > eocd) return LU_ERR_ARCHIVE;
    pos = cd_off;
    for (i = 0; i < cd_entries; i++) {
        uint16_t method, nlen, elen, clen;
        uint32_t crc, comp_sz, uncomp_sz, lhoff;
        const char *name;
        if (pos + 46 > cd_off + cd_size || memcmp(zip + pos, "PK\x01\x02", 4)) return LU_ERR_ARCHIVE;
        method = lu_u16(zip + pos + 10);
        crc = lu_u32(zip + pos + 16);
        comp_sz = lu_u32(zip + pos + 20);
        uncomp_sz = lu_u32(zip + pos + 24);
        nlen = lu_u16(zip + pos + 28);
        elen = lu_u16(zip + pos + 30);
        clen = lu_u16(zip + pos + 32);
        lhoff = lu_u32(zip + pos + 42);
        if (pos + 46 + nlen + elen + clen > cd_off + cd_size) return LU_ERR_ARCHIVE;
        name = (const char *)(zip + pos + 46);
        if (!lu_zip_path_safe(name, nlen)) return LU_ERR_ARCHIVE;
        if (nlen == strlen(member_name) && !memcmp(name, member_name, nlen)) {
            uint16_t lnlen, lelen;
            size_t data_off, actual_uncomp = 0;
            unsigned char *buf;
            if (!uncomp_sz || uncomp_sz > LU_MAX_ASSET_BYTES || lhoff + 30 > cd_off) return LU_ERR_ARCHIVE;
            if (memcmp(zip + lhoff, "PK\x03\x04", 4)) return LU_ERR_ARCHIVE;
            lnlen = lu_u16(zip + lhoff + 26);
            lelen = lu_u16(zip + lhoff + 28);
            data_off = lhoff + 30u + lnlen + lelen;
            if (data_off + comp_sz > cd_off) return LU_ERR_ARCHIVE;
            buf = (unsigned char *)malloc(uncomp_sz + 1);
            if (!buf) return LU_ERR_IO;
            if (method == 0) {
                if (comp_sz != uncomp_sz) { free(buf); return LU_ERR_ARCHIVE; }
                memcpy(buf, zip + data_off, uncomp_sz);
                actual_uncomp = uncomp_sz;
            } else if (method == 8) {
                if (!lu_inflate(zip + data_off, comp_sz, buf, uncomp_sz, &actual_uncomp) || actual_uncomp != uncomp_sz) {
                    free(buf); return LU_ERR_ARCHIVE;
                }
            } else {
                free(buf); return LU_ERR_ARCHIVE;
            }
            if (lu_crc32(buf, uncomp_sz) != crc) {
                free(buf); return LU_ERR_ARCHIVE;
            }
            buf[uncomp_sz] = 0;
            *out_buf = buf; *out_len = uncomp_sz;
            return LU_OK;
        }
        pos += 46u + nlen + elen + clen;
    }
    return LU_ERR_ARCHIVE;
}

/* Every entry name of the archive is checked, not only the ones the update takes: a package that carries a path climbing out of its folder is refused whole. */
static inline enum lu_result lu_zip_check_paths(const unsigned char *zip, size_t zip_len)
{
    size_t eocd = 0, cd_off, cd_size, i, pos;
    uint16_t cd_entries;
    if (!zip || zip_len < 22 || zip_len > LU_MAX_ASSET_BYTES) return LU_ERR_ARCHIVE;
    for (i = zip_len - 22; ; i--) {
        if (zip[i] == 'P' && zip[i+1] == 'K' && zip[i+2] == 5 && zip[i+3] == 6) { eocd = i; break; }
        if (i == 0 || zip_len - i > 65557u) return LU_ERR_ARCHIVE;
    }
    cd_entries = lu_u16(zip + eocd + 10); cd_size = lu_u32(zip + eocd + 12); cd_off = lu_u32(zip + eocd + 16);
    if (cd_off + cd_size > eocd) return LU_ERR_ARCHIVE;
    pos = cd_off;
    for (i = 0; i < cd_entries; i++) {
        uint16_t nlen, elen, clen;
        if (pos + 46 > cd_off + cd_size || memcmp(zip + pos, "PK\x01\x02", 4)) return LU_ERR_ARCHIVE;
        nlen = lu_u16(zip + pos + 28); elen = lu_u16(zip + pos + 30); clen = lu_u16(zip + pos + 32);
        if (pos + 46 + nlen + elen + clen > cd_off + cd_size) return LU_ERR_ARCHIVE;
        if (!lu_zip_path_safe((const char *)(zip + pos + 46), nlen)) return LU_ERR_ARCHIVE;
        pos += 46u + nlen + elen + clen;
    }
    return LU_OK;
}

/* A Windows executable of the given machine type (0x8664: x64). The launcher and the proxy are 64-bit. */
static inline int lu_valid_pe(const unsigned char *bytes, size_t len, unsigned machine)
{
    uint32_t pe_off;
    if (!bytes || len < 256 || bytes[0] != 'M' || bytes[1] != 'Z') return 0;
    pe_off = lu_u32(bytes + 60);
    if (pe_off < 64 || (size_t)pe_off + 24 > len) return 0;
    if (memcmp(bytes + pe_off, "PE\0\0", 4) != 0) return 0;
    if (lu_u16(bytes + pe_off + 4) != machine) return 0;
    return 1;
}


/* ---- Files, receipts and the transactional apply ---- */

static inline int lu_read_bytes(const char *path, unsigned char **out_buf, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    long sz;
    unsigned char *buf;
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) < 0 || (size_t)sz > LU_MAX_ASSET_BYTES || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f); return 0;
    }
    buf = (unsigned char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return 0; }
    if (sz > 0 && fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf); fclose(f); return 0;
    }
    fclose(f);
    buf[sz] = 0;
    *out_buf = buf; *out_len = (size_t)sz;
    return 1;
}

static inline int lu_file_sha256(const char *path, char out_hash[65])
{
    unsigned char *buf = NULL;
    size_t len = 0;
    if (!lu_read_bytes(path, &buf, &len)) return 0;
    lu_sha256_hex(buf, len, out_hash);
    free(buf);
    return 1;
}

static inline int lu_write_verified(const char *path, const unsigned char *bytes, size_t len, const char expected_hash[65])
{
    FILE *f = fopen(path, "wb");
    char check[65];
    if (!f) return 0;
    if (len > 0 && fwrite(bytes, 1, len, f) != len) { fclose(f); remove(path); return 0; }
    if (fclose(f) != 0) { remove(path); return 0; }
    if (!lu_file_sha256(path, check) || strcmp(check, expected_hash) != 0) {
        remove(path); return 0;
    }
    return 1;
}

static inline int lu_copy_verified(const char *src, const char *dst, char out_hash[65])
{
    unsigned char *buf = NULL;
    size_t len = 0;
    int ok;
    if (!lu_read_bytes(src, &buf, &len)) return 0;
    lu_sha256_hex(buf, len, out_hash);
    ok = lu_write_verified(dst, buf, len, out_hash);
    free(buf);
    return ok;
}

static inline int lu_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static inline int lu_hex64(const char *s)
{
    int i;
    for (i = 0; i < 64; i++) if (!s[i] || !isxdigit((unsigned char)s[i]) || isupper((unsigned char)s[i])) return 0;
    return s[64] == 0 || s[64] == '"';
}

/* ---- the installer's ownership receipt: {"schema":1,"original":<hex64|null>,"installed":<hex64>,"pending":<hex64|null>}  (dist/install-state.ps1) ---- */

struct lu_receipt {
    int present;        /* the file exists */
    int valid;          /* schema 1 with well-formed hashes */
    char original[65], installed[65], pending[65];      /* "" for null */
};

static inline int lu_receipt_field(const char *text, const char *key, char out[65])
{
    char pat[32];
    const char *p;
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    out[0] = 0;
    p = strstr(text, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ':') return 0;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (!strncmp(p, "null", 4)) return 1;
    if (*p != '"' || !lu_hex64(p + 1) || p[65] != '"') return 0;
    memcpy(out, p + 1, 64);
    out[64] = 0;
    return 1;
}

static inline void lu_read_receipt(const char *target, struct lu_receipt *r)
{
    char path[700], inst[65];
    unsigned char *buf = NULL;
    size_t len = 0;
    memset(r, 0, sizeof(*r));
    snprintf(path, sizeof(path), "%s.coop-receipt.json", target);
    if (!lu_read_bytes(path, &buf, &len)) return;
    r->present = 1;
    if (len > 2048 || strstr((const char *)buf, "\"schema\":1") == NULL) {
        if (!(len <= 2048 && strstr((const char *)buf, "\"schema\": 1"))) { free(buf); return; }
    }
    if (lu_receipt_field((const char *)buf, "installed", inst) && inst[0] &&
        lu_receipt_field((const char *)buf, "original", r->original) &&
        lu_receipt_field((const char *)buf, "pending", r->pending)) {
        memcpy(r->installed, inst, 65);
        r->valid = 1;
    }
    free(buf);
}

/* Written like the installer: a temporary file, then a replacing rename. */
static inline int lu_write_receipt(const char *target, const char *original, const char *installed, const char *pending)
{
    char tmp[700], path[700], text[512], o[80], p[80];
    FILE *f;
    size_t n;
    if (original && *original) snprintf(o, sizeof(o), "\"%s\"", original); else snprintf(o, sizeof(o), "null");
    if (pending && *pending) snprintf(p, sizeof(p), "\"%s\"", pending); else snprintf(p, sizeof(p), "null");
    snprintf(text, sizeof(text), "{\"schema\":1,\"original\":%s,\"installed\":\"%s\",\"pending\":%s}", o, installed, p);
    snprintf(tmp, sizeof(tmp), "%s.coop-receipt.json.tmp", target);
    snprintf(path, sizeof(path), "%s.coop-receipt.json", target);
    n = strlen(text);
    f = fopen(tmp, "wb");
    if (!f) return 0;
    if (fwrite(text, 1, n, f) != n || fclose(f) != 0) { remove(tmp); return 0; }
    if (!lu_rename_replace(tmp, path)) { remove(tmp); return 0; }
    return 1;
}

/* ---- what the package replaces: the files Setup manages, but never the game's own AnselSDK64 (AnselSDK64_real.dll, AnselSDK64.dll.coop-original) ---- */

struct lu_target { const char *name; int exe; int launcher; };
#define LU_NTARGETS 4
static const struct lu_target lu_targets[LU_NTARGETS] = {
    { "AnselSDK64.dll", 1, 0 },
    { "witness_launcher.exe", 1, 1 },
    { "witness_models/adventurer.glb", 0, 0 },
    { "witness_models/adventurer.glb.txt", 0, 0 }
};

static inline void lu_path(char *out, size_t cap, const char *dir, const char *name, const char *suffix)
{
    snprintf(out, cap, "%s/%s%s", dir, name, suffix);
}

/* The state of one managed file against the package's hash: the installer's own decision (dist/install-policy.h), plus its refusal of leftovers. */
static inline int lu_file_state(const char *dir, const struct lu_target *t, const char *expected, struct lu_receipt *rc, char observed[65])
{
    char path[640], aux[640], backup[65] = {0};
    int unknown = 0;
    lu_path(path, sizeof(path), dir, t->name, "");
    lu_read_receipt(path, rc);
    observed[0] = 0;
    if (lu_exists(path) && !lu_file_sha256(path, observed)) unknown = 1;
    lu_path(aux, sizeof(aux), dir, t->name, ".coop-original");
    if (lu_exists(aux) && !lu_file_sha256(aux, backup)) unknown = 1;
    lu_path(aux, sizeof(aux), dir, t->name, ".coop-stage");
    if (lu_exists(aux)) return CI_CONFLICT;
    lu_path(aux, sizeof(aux), dir, t->name, ".coop-receipt.json.tmp");
    if (lu_exists(aux)) return CI_CONFLICT;
    if (rc->present && !rc->valid) return CI_UNKNOWN;
    return ci_file(expected, observed, rc->present ? 1 : 0, rc->installed, rc->original, rc->pending, backup, unknown);
}

#define LU_JOURNAL "witness_update.state"

static inline int lu_journal_status(const char *dir, char *ver, size_t cap)
{
    char path[640];
    unsigned char *buf = NULL;
    size_t len = 0;
    const char *p;
    int pending;
    lu_path(path, sizeof(path), dir, LU_JOURNAL, "");
    if (!lu_read_bytes(path, &buf, &len)) return 0;
    pending = strstr((const char *)buf, "status=pending") != NULL;
    if (ver && cap) {
        size_t i = 0;
        ver[0] = 0;
        p = strstr((const char *)buf, "previous_version=");
        if (p) { p += 17; while (p[i] && p[i] != '\r' && p[i] != '\n' && i + 1 < cap) { ver[i] = p[i]; i++; } ver[i] = 0; }
    }
    free(buf);
    return pending ? 1 : 2;
}

/* Put every file back from its .coop-prev (verified against the journal) and the receipts with them. Idempotent; leaves the artifacts when a backup does not verify. */
static inline enum lu_result lu_rollback(const char *dir)
{
    char jpath[640], hashkey[48];
    unsigned char *jb = NULL;
    size_t jl = 0;
    int i, failed = 0;
    lu_path(jpath, sizeof(jpath), dir, LU_JOURNAL, "");
    if (!lu_read_bytes(jpath, &jb, &jl)) return LU_ERR_ROLLBACK;
    for (i = 0; i < LU_NTARGETS; i++) {
        char target[640], prev[640], stage[640], old[640], rprev[640], rpath[640], want[65] = {0}, got[65], tmp[65];
        const char *p;
        lu_path(target, sizeof(target), dir, lu_targets[i].name, "");
        lu_path(prev, sizeof(prev), dir, lu_targets[i].name, ".coop-prev");
        lu_path(stage, sizeof(stage), dir, lu_targets[i].name, ".coop-stage");
        lu_path(old, sizeof(old), dir, lu_targets[i].name, ".coop-old");
        lu_path(rprev, sizeof(rprev), dir, lu_targets[i].name, ".coop-receipt.json.coop-prev");
        lu_path(rpath, sizeof(rpath), dir, lu_targets[i].name, ".coop-receipt.json");
        snprintf(hashkey, sizeof(hashkey), "file%d_prev=", i);
        p = strstr((const char *)jb, hashkey);
        if (p && strlen(p + strlen(hashkey)) >= 64) memcpy(want, p + strlen(hashkey), 64);
        if (!want[0]) continue;      /* this file was not part of the update */
        remove(stage);
        if (!lu_file_sha256(prev, got) || strcmp(got, want) != 0) {
            /* no backup: fine only when the file itself is still the old one (the update never reached it) */
            if (lu_file_sha256(target, got) && !strcmp(got, want)) { remove(prev); if (lu_exists(rprev)) { lu_rename_replace(rprev, rpath); } continue; }
            failed = 1; continue;
        }
        if (!lu_file_sha256(target, got) || strcmp(got, want) != 0) {
            if (!lu_copy_verified(prev, stage, tmp) || !lu_rename_replace(stage, target)) { remove(stage); failed = 1; continue; }
        }
        if (!lu_file_sha256(target, got) || strcmp(got, want) != 0) { failed = 1; continue; }
        if (lu_exists(rprev) && !lu_rename_replace(rprev, rpath)) { failed = 1; continue; }
        remove(prev);
        remove(old);
    }
    free(jb);
    if (failed) return LU_ERR_ROLLBACK;
    remove(jpath);
    return LU_OK;
}

/* Startup recovery: an update that was interrupted (status=pending) is rolled back. 1: rolled back, 0: nothing to do, -1: could not. */
static inline int lu_recover_interrupted(const char *dir)
{
    char old[640];
    if (lu_journal_status(dir, NULL, 0) == 1) return lu_rollback(dir) == LU_OK ? 1 : -1;
    /* the launcher that was replaced while it ran is kept as .coop-old until the next start */
    lu_path(old, sizeof(old), dir, "witness_launcher.exe", ".coop-old");
    remove(old);
    return 0;
}

/* The apply: the package (already verified against the release's .sha256) is opened, every file is checked against its receipt first, then all
 * files are staged and backed up, and only then replaced one by one, each through the installer's own receipt steps (pending, replace, installed).
 * Any failure puts everything back. game_running: the proxy DLL cannot be replaced while the game runs. fail_step (tests): fail after replacing
 * that many files (1..); crash (tests): then stop dead without rolling back, as a killed process would. */
static inline enum lu_result lu_apply_package(const char *dir, const unsigned char *zip, size_t zip_len, const char *prefix, const char *current_ver,
                                              const char *target_tag, int game_running, int fail_step, int crash)
{
    unsigned char *payload[LU_NTARGETS] = {0};
    size_t plen[LU_NTARGETS] = {0};
    char newh[LU_NTARGETS][65], oldh[LU_NTARGETS][65];
    struct lu_receipt rc[LU_NTARGETS];
    int change[LU_NTARGETS] = {0}, nchange = 0, i, k, ver;
    enum lu_result r = LU_OK;
    struct lu_semver curv;
    char member[256], jpath[640];
    FILE *jf;

    if (!lu_tag_safe(target_tag)) return LU_ERR_VERSION;
    ver = lu_version_is_newer(current_ver, target_tag);
    if (ver < 0) return LU_ERR_VERSION;
    if (lu_is_dev_build(current_ver) || !lu_parse_semver(current_ver, &curv)) return LU_ERR_DEV;
    if (ver == 0) return LU_ERR_DOWNGRADE;
    if (game_running) return LU_ERR_RUNNING;
    if (lu_journal_status(dir, NULL, 0)) return LU_ERR_RECEIPT;      /* an earlier update has not been settled: recovery first */

    r = lu_zip_check_paths(zip, zip_len);
    if (r != LU_OK) goto done;
    for (i = 0; i < LU_NTARGETS; i++) {
        snprintf(member, sizeof(member), "%s-%s/%s", prefix, target_tag, lu_targets[i].name);
        r = lu_zip_extract(zip, zip_len, member, &payload[i], &plen[i]);
        if (r != LU_OK) goto done;
        if (lu_targets[i].exe && !lu_valid_pe(payload[i], plen[i], 0x8664u)) { r = LU_ERR_PE; goto done; }
        lu_sha256_hex(payload[i], plen[i], newh[i]);
    }
    for (i = 0; i < LU_NTARGETS; i++) {
        int st = lu_file_state(dir, &lu_targets[i], newh[i], &rc[i], oldh[i]);
        if (st == CI_OUTDATED) { change[i] = 1; nchange++; }
        else if (st != CI_CURRENT) { r = LU_ERR_RECEIPT; goto done; }
    }
    if (!nchange) { r = LU_UP_TO_DATE; goto done; }

    /* stage, back up, journal */
    for (i = 0; i < LU_NTARGETS; i++) {
        char stage[640], target[640], prev[640], rpath[640], rprev[640], chk[65];
        if (!change[i]) continue;
        lu_path(stage, sizeof(stage), dir, lu_targets[i].name, ".coop-stage");
        lu_path(target, sizeof(target), dir, lu_targets[i].name, "");
        lu_path(prev, sizeof(prev), dir, lu_targets[i].name, ".coop-prev");
        lu_path(rpath, sizeof(rpath), dir, lu_targets[i].name, ".coop-receipt.json");
        lu_path(rprev, sizeof(rprev), dir, lu_targets[i].name, ".coop-receipt.json.coop-prev");
        if (!lu_write_verified(stage, payload[i], plen[i], newh[i]) || !lu_copy_verified(target, prev, chk) || strcmp(chk, oldh[i]) != 0 ||
            !lu_copy_verified(rpath, rprev, chk)) { r = LU_ERR_IO; goto fail_clean; }
    }
    lu_path(jpath, sizeof(jpath), dir, LU_JOURNAL, "");
    jf = fopen(jpath, "wb");
    if (!jf) { r = LU_ERR_IO; goto fail_clean; }
    fprintf(jf, "[update]\nstatus=pending\nprevious_version=%s\ntarget_version=%s\n", current_ver, target_tag);
    for (i = 0; i < LU_NTARGETS; i++)
        if (change[i]) fprintf(jf, "file%d_prev=%s\nfile%d_new=%s\n", i, oldh[i], i, newh[i]);
    if (fclose(jf) != 0) { remove(jpath); r = LU_ERR_IO; goto fail_clean; }

    k = 0;
    for (i = 0; i < LU_NTARGETS; i++) {
        char stage[640], target[640], old[640], chk[65];
        if (!change[i]) continue;
        lu_path(stage, sizeof(stage), dir, lu_targets[i].name, ".coop-stage");
        lu_path(target, sizeof(target), dir, lu_targets[i].name, "");
        lu_path(old, sizeof(old), dir, lu_targets[i].name, ".coop-old");
        if (!lu_write_receipt(target, rc[i].original, rc[i].installed, newh[i])) { r = LU_ERR_IO; goto fail_rollback; }
        if (lu_targets[i].launcher && !lu_rename_replace(target, old)) { r = LU_ERR_IO; goto fail_rollback; }      /* the running launcher can be renamed, not overwritten */
        if (!lu_rename_replace(stage, target)) { r = LU_ERR_IO; goto fail_rollback; }
        if (!lu_file_sha256(target, chk) || strcmp(chk, newh[i]) != 0) { r = LU_ERR_IO; goto fail_rollback; }
        if (!lu_write_receipt(target, rc[i].original, newh[i], NULL)) { r = LU_ERR_IO; goto fail_rollback; }
        k++;
        if (fail_step && k == fail_step) {
            if (crash) { r = LU_ERR_IO; goto done; }
            r = LU_ERR_IO; goto fail_rollback;
        }
    }
    /* settled: the backups and the journal go (the old launcher image stays until the next start) */
    for (i = 0; i < LU_NTARGETS; i++) {
        char prev[640], rprev[640];
        if (!change[i]) continue;
        lu_path(prev, sizeof(prev), dir, lu_targets[i].name, ".coop-prev");
        lu_path(rprev, sizeof(rprev), dir, lu_targets[i].name, ".coop-receipt.json.coop-prev");
        remove(prev); remove(rprev);
    }
    remove(jpath);
    r = LU_OK;
    goto done;
fail_rollback:
    if (lu_rollback(dir) != LU_OK) r = LU_ERR_ROLLBACK;
fail_clean:
    for (i = 0; i < LU_NTARGETS; i++) {
        char stage[640], prev[640], rprev[640];
        lu_path(stage, sizeof(stage), dir, lu_targets[i].name, ".coop-stage");
        remove(stage);
        if (r != LU_ERR_ROLLBACK) {      /* (after a failed rollback the backups are all that is left: kept) */
            lu_path(prev, sizeof(prev), dir, lu_targets[i].name, ".coop-prev");
            lu_path(rprev, sizeof(rprev), dir, lu_targets[i].name, ".coop-receipt.json.coop-prev");
            if (change[i]) { remove(prev); remove(rprev); }
        }
    }
done:
    for (i = 0; i < LU_NTARGETS; i++) free(payload[i]);
    return r;
}

/* ---- the settings of the check, in a small file next to the launcher (key=value lines) ---- */

static inline void lu_cfg_get(const char *text, const char *key, char *out, size_t cap, const char *def)
{
    size_t kl = strlen(key);
    const char *p = text;
    snprintf(out, cap, "%s", def);
    while (p && *p) {
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            size_t n = 0;
            const char *v = p + kl + 1;
            while (v[n] && v[n] != '\r' && v[n] != '\n' && n + 1 < cap) { out[n] = v[n]; n++; }
            out[n] = 0;
            return;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
}

/* update_check=0 turns the check off; update_prereleases=auto|1|0; last_check / retry_after are written by the launcher */
static inline void lu_load_schedule(const char *text, const char *current_ver, struct lu_schedule *s)
{
    char v[64];
    lu_cfg_get(text, "update_check", v, sizeof(v), "1");
    s->auto_enabled = strcmp(v, "0") != 0;
    lu_cfg_get(text, "update_prereleases", v, sizeof(v), "auto");
    s->include_prereleases = !strcmp(v, "1") ? 1 : !strcmp(v, "0") ? 0 : lu_version_is_prerelease(current_ver);
    lu_cfg_get(text, "update_last_check", v, sizeof(v), "0");
    s->last_check = strtoull(v, NULL, 10);
    lu_cfg_get(text, "update_retry_after", v, sizeof(v), "0");
    s->retry_after = strtoull(v, NULL, 10);
}

#endif
