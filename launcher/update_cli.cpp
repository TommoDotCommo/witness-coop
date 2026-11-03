// A command line over update.h for the game-free tests (tools/test_launcher_update.py): every command prints "key=value" lines and exits 0; the test reads them.
#include "update.h"
#include <string>
static std::string slurp(const char* path) { unsigned char* b = nullptr; size_t n = 0; if (!lu_read_bytes(path, &b, &n)) return std::string("\0MISSING", 8); std::string s((char*)b, n); free(b); return s; }
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::string c = argv[1];
    if (c == "sha256" && argc == 3) { std::string d = slurp(argv[2]); char h[65]; lu_sha256_hex(d.data(), d.size(), h); printf("sha=%s\n", h); return 0; }
    if (c == "newer" && argc == 4) { printf("newer=%d\n", lu_version_is_newer(argv[2], argv[3])); return 0; }
    if (c == "prerelease" && argc == 3) { printf("prerelease=%d\n", lu_version_is_prerelease(argv[2])); return 0; }
    if (c == "classify" && argc == 6) { lu_schedule s = {1, 0, 0, 1}; auto r = lu_http_classify(atoi(argv[2]), atoi(argv[3]), (uint32_t)atoi(argv[4]), (uint64_t)atoll(argv[5]), &s); printf("result=%d last_check=%llu retry_after=%llu\n", (int)r, (unsigned long long)s.last_check, (unsigned long long)s.retry_after); return 0; }
    if (c == "check" && argc == 6) {      // check <json file> <prefix> <include_pre> <current>
        std::string j = slurp(argv[2]); lu_release rel; auto r = lu_parse_release(j.data(), j.size(), argv[3], atoi(argv[4]), &rel);
        printf("result=%d tag=%s zip=%s zip_url=%s zip_size=%zu sha=%s sha_url=%s prerelease=%d newer=%d\n", (int)r, rel.tag, rel.zip_name, rel.zip_url, rel.zip_size, rel.sha_name, rel.sha_url, rel.prerelease, r == LU_OK ? lu_version_is_newer(argv[5], rel.tag) : -9);
        printf("notes=%s\n", rel.notes); return 0; }
    if (c == "sidecar" && argc == 4) { std::string t = slurp(argv[2]); char h[65] = {0}; auto r = lu_parse_sidecar(t.data(), t.size(), argv[3], h); printf("result=%d hash=%s\n", (int)r, h); return 0; }
    if (c == "verify" && argc == 6) {      // verify <zip> <sha> <declared size> -> result
        std::string z = slurp(argv[2]); auto r = lu_verify_download((const unsigned char*)z.data(), z.size(), (size_t)atoll(argv[4]), argv[3]); printf("result=%d\n", (int)r); (void)argv[5]; return 0; }
    if (c == "schedule" && argc == 6) {      // schedule <cfg file> <current> <now> <manual>
        std::string t = slurp(argv[2]); lu_schedule s; lu_load_schedule(t.c_str(), argv[3], &s); printf("auto=%d pre=%d last=%llu retry=%llu can=%d\n", s.auto_enabled, s.include_prereleases, (unsigned long long)s.last_check, (unsigned long long)s.retry_after, lu_can_check(&s, (uint64_t)atoll(argv[4]), atoi(argv[5]))); return 0; }
    if (c == "apply" && argc == 10) {      // apply <dir> <zip> <prefix> <current> <tag> <running> <fail_step> <crash>
        std::string z = slurp(argv[3]); auto r = lu_apply_package(argv[2], (const unsigned char*)z.data(), z.size(), argv[4], argv[5], argv[6], atoi(argv[7]), atoi(argv[8]), atoi(argv[9])); printf("result=%d\n", (int)r); return 0; }
    if (c == "owner" && argc == 3) { printf("owner=%d\n", lu_owner(argv[2])); return 0; }
    if (c == "recover" && argc == 3) { printf("recovered=%d\n", lu_recover_interrupted(argv[2])); return 0; }
    return 2;
}
