#pragma once
// The launcher's self-update on Windows: GitHub's release list over WinINet on a worker thread (the window stays responsive), the verified download, and the apply
// through update.h (the installer's receipts). No telemetry: one GET of the public release list a day at most, one GET per asset when the player says Update; nothing
// about the player or the PC is sent. A failure of the automatic check is only logged. Settings (a small key=value file in the mod's data folder, witness_launcher_update.cfg: %LOCALAPPDATA%\witness-coop, or beside the launcher for a per-instance copy):
//   update_check=1|0           the automatic check when the launcher starts (once a day at most)
//   update_prereleases=auto|1|0   auto: pre-releases are offered only to an installed pre-release
//   update_api_url=...         tests only: https://, or http://127.0.0.1:<port> / http://localhost:<port>
#include <windows.h>
#include <wininet.h>
#include <ctime>
#include <functional>
#include <string>
#include <tlhelp32.h>
#include "update.h"

namespace wup {
#define WUP_DEFAULT_API "https://api.github.com/repos/TommoDotCommo/witness-coop/releases?per_page=10"
#define WUP_PREFIX "witness-coop"
enum State { kIdle, kChecking, kCurrent, kAvailable, kDownloading, kApplied, kError };
struct Snapshot { State state = kIdle; std::string message, tag, notes, version; bool prerelease = false; int lastError = 0; bool autoCheck = true; int prereleases = -1; bool manual = false; };

inline std::string Narrow(const std::wstring& w) { if (w.empty()) return ""; int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr); std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr); return s; }

// Is a game started from this folder running? (The proxy DLL is replaced only while it is not.)
inline bool GameRunningIn(const std::wstring& dir) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); if (snap == INVALID_HANDLE_VALUE) return true;      // unsure: do not replace
    PROCESSENTRY32W pe = {}; pe.dwSize = sizeof pe; bool running = false; std::wstring want = dir; for (auto& c : want) c = towlower(c); while (!want.empty() && (want.back() == L'\\' || want.back() == L'/')) want.pop_back();
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !running; ok = Process32NextW(snap, &pe)) {
        std::wstring exe = pe.szExeFile; for (auto& c : exe) c = towlower(c); if (exe != L"witness64_d3d11.exe" && exe != L"witness_d3d11.exe") continue;
        HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID); if (!p) { running = true; continue; }      // cannot look: assume it is ours
        wchar_t path[MAX_PATH * 2]; DWORD n = MAX_PATH * 2; if (QueryFullProcessImageNameW(p, 0, path, &n)) { std::wstring img(path, n); for (auto& c : img) { c = towlower(c); if (c == L'/') c = L'\\'; } for (auto& c : want) if (c == L'/') c = L'\\'; if (img.compare(0, want.size(), want) == 0) running = true; } else running = true;
        CloseHandle(p);
    }
    CloseHandle(snap); return running;
}

class Updater {
public:
    // dir: the game folder (the launcher's own); version: the stamped mod version; log: where failures go
    void Init(const std::wstring& dir, const std::wstring& dataDir, const std::string& version, std::function<void(const std::string&)> log) {
        dir_ = dir; dirA_ = Narrow(dir); for (auto& c : dirA_) if (c == '\\') c = '/'; version_ = version; log_ = log; cfgPath_ = dataDir + L"\\witness_launcher_update.cfg";
        InitializeCriticalSection(&cs_); inited_ = true; snap_.version = version; apiUrl_ = WUP_DEFAULT_API;
        LoadSettings(); int rec = lu_recover_interrupted(dirA_.c_str());
        if (rec > 0) { Set(kError, "An interrupted update was rolled back to the previous version."); Log("update: an interrupted update was rolled back"); }
        else if (rec < 0) { Set(kError, "An interrupted update could not be rolled back; run Setup again."); Log("update: ROLLBACK FAILED, kept the backups"); }
    }
    void SetApiUrl(const std::string& u) { if (lu_url_valid(u.c_str())) apiUrl_ = u; }
    // the automatic check at start: only when due; never says anything on failure
    void StartAuto() { if (!inited_) return; lu_schedule s; { Lock l(&cs_); s = sched_; } if (!lu_can_check(&s, (uint64_t)time(nullptr), 0)) return; Start(false); }
    void StartManual() { if (inited_) Start(true); }
    void StartApply() { if (!inited_) return; { Lock l(&cs_); if (snap_.state != kAvailable) return; snap_.state = kDownloading; snap_.message = std::string("Downloading and verifying ") + rel_.zip_name + " ..."; } if (InterlockedCompareExchange(&busy_, 1, 0) != 0) return; HANDLE t = CreateThread(nullptr, 0, ApplyThread, this, 0, nullptr); if (t) CloseHandle(t); else InterlockedExchange(&busy_, 0); }
    void Later() { Lock l(&cs_); if (snap_.state == kAvailable) { snap_.state = kCurrent; snap_.message = "Update " + snap_.tag + " postponed; it is offered again at the next check."; } }
    Snapshot Get() { Lock l(&cs_); Snapshot s = snap_; s.autoCheck = sched_.auto_enabled != 0; s.prereleases = prePref_; return s; }
    void SetAutoCheck(bool on) { { Lock l(&cs_); sched_.auto_enabled = on; } SaveSettings(); }
    void SetPrereleases(int pref) { { Lock l(&cs_); prePref_ = pref; sched_.include_prereleases = pref < 0 ? lu_version_is_prerelease(version_.c_str()) : pref; } SaveSettings(); }
    bool Busy() const { return busy_ != 0; }
    // synchronous forms (tests, and the workers): return the lu_result
    int CheckSync(bool manual) { return DoCheck(manual); }
    int ApplySync() { return DoApply(); }
    bool Available() { Lock l(&cs_); return snap_.state == kAvailable; }
private:
    struct Lock { CRITICAL_SECTION* c; explicit Lock(CRITICAL_SECTION* x) : c(x) { EnterCriticalSection(c); } ~Lock() { if (c) LeaveCriticalSection(c); } };
    void Log(const std::string& s) { if (log_) log_(s); }
    void Set(State st, const std::string& msg) { Lock l(&cs_); snap_.state = st; snap_.message = msg; }
    std::string ReadCfg() { unsigned char* b = nullptr; size_t n = 0; std::string path = Narrow(cfgPath_); if (!lu_read_bytes(path.c_str(), &b, &n)) return ""; std::string s((char*)b, n); free(b); return s; }
    void LoadSettings() {
        std::string t = ReadCfg(); Lock l(&cs_); lu_load_schedule(t.c_str(), version_.c_str(), &sched_);
        char v[64]; lu_cfg_get(t.c_str(), "update_prereleases", v, sizeof v, "auto"); prePref_ = !strcmp(v, "1") ? 1 : !strcmp(v, "0") ? 0 : -1;
        char api[LU_URL_MAX]; lu_cfg_get(t.c_str(), "update_api_url", api, sizeof api, ""); if (api[0] && lu_url_valid(api)) apiUrl_ = api;
    }
    static std::string SetKey(const std::string& text, const std::string& key, const std::string& val) {
        std::string out, line; size_t i = 0; bool done = false; while (i <= text.size()) { size_t e = text.find('\n', i); if (e == std::string::npos) e = text.size(); line = text.substr(i, e - i); if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.compare(0, key.size() + 1, key + "=") == 0) { if (!done) out += key + "=" + val + "\n"; done = true; } else if (!(i == text.size() || (e == text.size() && line.empty()))) out += line + "\n"; i = e + 1; }
        if (!done) out += key + "=" + val + "\n";
        return out; }
    void SaveSettings() {
        Lock l(&cs_); std::string t = ReadCfg(); t = SetKey(t, "update_check", sched_.auto_enabled ? "1" : "0"); t = SetKey(t, "update_prereleases", prePref_ < 0 ? "auto" : prePref_ ? "1" : "0");
        t = SetKey(t, "update_last_check", std::to_string(sched_.last_check)); t = SetKey(t, "update_retry_after", std::to_string(sched_.retry_after));
        std::string path = Narrow(cfgPath_), tmp = path + ".tmp"; FILE* f = fopen(tmp.c_str(), "wb"); if (!f) return; bool ok = fwrite(t.data(), 1, t.size(), f) == t.size(); ok = fclose(f) == 0 && ok; if (ok) lu_rename_replace(tmp.c_str(), path.c_str()); else remove(tmp.c_str());
    }
    static bool Get(const char* url, int* status, int* remaining, uint32_t* retry, unsigned char** out, size_t* outLen, size_t maxBytes) {
        *status = 0; *remaining = -1; *retry = 0; *out = nullptr; *outLen = 0;
        HINTERNET net = InternetOpenA("witness-coop-launcher-updater/1", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0); if (!net) return false;
        DWORD to = 20000; InternetSetOptionA(net, INTERNET_OPTION_CONNECT_TIMEOUT, &to, sizeof to); InternetSetOptionA(net, INTERNET_OPTION_RECEIVE_TIMEOUT, &to, sizeof to);
        HINTERNET req = InternetOpenUrlA(net, url, "Accept: application/vnd.github+json\r\n", (DWORD)-1L, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI | INTERNET_FLAG_NO_AUTH | INTERNET_FLAG_NO_COOKIES, 0);
        if (!req) { InternetCloseHandle(net); return false; }
        DWORD code = 0, sz = sizeof code, idx = 0; if (HttpQueryInfoA(req, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &code, &sz, &idx)) *status = (int)code;
        char hdr[64]; strcpy(hdr, "X-RateLimit-Remaining"); sz = sizeof hdr; idx = 0; if (HttpQueryInfoA(req, HTTP_QUERY_CUSTOM, hdr, &sz, &idx)) *remaining = atoi(hdr);
        strcpy(hdr, "Retry-After"); sz = sizeof hdr; idx = 0; if (HttpQueryInfoA(req, HTTP_QUERY_CUSTOM, hdr, &sz, &idx)) *retry = (uint32_t)atoi(hdr);
        size_t cap = 8192, len = 0; unsigned char* buf = (unsigned char*)malloc(cap + 1); bool ok = buf != nullptr;
        while (ok) { if (len + 4096 > cap) { size_t nc = cap * 2; if (nc > maxBytes + 8192) nc = maxBytes + 8192; if (len + 4096 > nc) { ok = false; break; } unsigned char* nb = (unsigned char*)realloc(buf, nc + 1); if (!nb) { ok = false; break; } buf = nb; cap = nc; }
            DWORD got = 0; if (!InternetReadFile(req, buf + len, 4096, &got)) { ok = false; break; } if (!got) break; len += got; if (len > maxBytes) { ok = false; break; } }
        InternetCloseHandle(req); InternetCloseHandle(net);
        if (!ok) { free(buf); return false; } buf[len] = 0; *out = buf; *outLen = len; return true;
    }
    void Start(bool manual) {
        if (InterlockedCompareExchange(&busy_, 1, 0) != 0) return;
        { Lock l(&cs_); snap_.state = kChecking; snap_.message = "Checking GitHub releases ..."; snap_.manual = manual; }
        HANDLE t = CreateThread(nullptr, 0, manual ? CheckThreadManual : CheckThreadAuto, this, 0, nullptr); if (t) CloseHandle(t); else InterlockedExchange(&busy_, 0);
    }
    static DWORD WINAPI CheckThreadManual(LPVOID p) { ((Updater*)p)->DoCheck(true); InterlockedExchange(&((Updater*)p)->busy_, 0); return 0; }
    static DWORD WINAPI CheckThreadAuto(LPVOID p) { ((Updater*)p)->DoCheck(false); InterlockedExchange(&((Updater*)p)->busy_, 0); return 0; }
    static DWORD WINAPI ApplyThread(LPVOID p) { ((Updater*)p)->DoApply(); InterlockedExchange(&((Updater*)p)->busy_, 0); return 0; }
    int DoCheck(bool manual) {
        if (lu_is_dev_build(version_.c_str())) {      // a development build is never replaced: it does not even ask
            Set(manual ? kCurrent : kIdle, manual ? "This is a development build; it is not updated automatically." : ""); return LU_UP_TO_DATE; }
        uint64_t now = (uint64_t)time(nullptr); lu_schedule s; std::string url; { Lock l(&cs_); s = sched_; url = apiUrl_; }
        if (!lu_can_check(&s, now, manual)) { Set(manual ? kError : kIdle, manual ? "The update check is on hold (GitHub's rate limit); try again later." : ""); return LU_THROTTLED; }
        int status = 0, rem = -1; uint32_t retry = 0; unsigned char* body = nullptr; size_t len = 0; lu_release rel;
        Get(url.c_str(), &status, &rem, &retry, &body, &len, LU_MAX_JSON_BYTES);
        int r = lu_http_classify(status, rem, retry, now, &s);
        if (r == LU_OK) r = lu_parse_release((const char*)body, len, WUP_PREFIX, s.include_prereleases, &rel);
        free(body);
        { Lock l(&cs_); sched_.last_check = s.last_check; sched_.retry_after = s.retry_after; snap_.lastError = r; }
        SaveSettings();
        if (r == LU_OK) {
            int newer = lu_version_is_newer(version_.c_str(), rel.tag);
            Lock l(&cs_);
            if (newer > 0) { rel_ = rel; snap_.tag = rel.tag; snap_.notes = rel.notes; snap_.prerelease = rel.prerelease != 0; snap_.state = kAvailable; snap_.message = std::string("Update available: ") + rel.tag; Log("update: available " + std::string(rel.tag)); }
            else { snap_.state = kCurrent; snap_.message = lu_is_dev_build(version_.c_str()) ? "This is a development build; it is not updated automatically." : "You have the newest version (" + version_ + ")."; }
            return LU_OK;
        }
        char b[96]; snprintf(b, sizeof b, "update: check failed, code %d, http %d", r, status); Log(b);
        if (!manual) { Set(kIdle, ""); return r; }      // (the automatic check is silent)
        Set(kError, r == LU_ERR_OFFLINE ? "GitHub could not be reached (offline?)." : r == LU_ERR_RATELIMIT ? "GitHub's rate limit is reached; try again later." : r == LU_UP_TO_DATE ? "No release is published yet." : "The release list could not be read (code " + std::to_string(r) + ").");
        return r;
    }
    int DoApply() {
        lu_release rel; { Lock l(&cs_); rel = rel_; }
        int status = 0, rem = -1; uint32_t retry = 0; unsigned char *sha = nullptr, *zip = nullptr; size_t shaLen = 0, zipLen = 0; char expect[65] = {0}; int r;
        if (!Get(rel.sha_url, &status, &rem, &retry, &sha, &shaLen, 4096) || status != 200 || shaLen != rel.sha_size) r = LU_ERR_SIDECAR; else r = lu_parse_sidecar((const char*)sha, shaLen, rel.zip_name, expect);
        free(sha);
        if (r == LU_OK) {
            if (!Get(rel.zip_url, &status, &rem, &retry, &zip, &zipLen, LU_MAX_ASSET_BYTES) || status != 200) r = LU_ERR_TRUNCATED;
            else { r = lu_verify_download(zip, zipLen, rel.zip_size, expect);
                   if (r == LU_OK) r = lu_apply_package(dirA_.c_str(), zip, zipLen, WUP_PREFIX, version_.c_str(), rel.tag, GameRunningIn(dir_) ? 1 : 0, 0, 0); }
            free(zip);
        }
        char b[96]; snprintf(b, sizeof b, "update: apply %s -> code %d", rel.tag, r); Log(b);
        Lock l(&cs_); snap_.lastError = r;
        switch (r) {
        case LU_OK: snap_.state = kApplied; snap_.message = std::string("Updated to ") + rel.tag + ". Close this launcher and open it again (the game, if it is running, after it ends)."; break;
        case LU_UP_TO_DATE: snap_.state = kCurrent; snap_.message = "Already up to date."; break;
        case LU_ERR_CHECKSUM: snap_.state = kError; snap_.message = "Update refused: the download does not match its SHA-256. Nothing was changed."; break;
        case LU_ERR_TRUNCATED: snap_.state = kError; snap_.message = "Update refused: the download was interrupted or incomplete. Nothing was changed."; break;
        case LU_ERR_SIDECAR: snap_.state = kError; snap_.message = "Update refused: the release has no usable .sha256 file. Nothing was changed."; break;
        case LU_ERR_RUNNING: snap_.state = kAvailable; snap_.message = "Close The Witness first, then press Update again."; break;
        case LU_ERR_RECEIPT: snap_.state = kError; snap_.message = "This copy was not installed by Setup (or a file was changed): download the new Setup from the release page instead. Nothing was changed."; break;
        case LU_ERR_LEGACY: snap_.state = kError; snap_.message = "An older co-op install is in the game folder and its AnselSDK64 files are not the game's own: run the new Setup, which explains what to do. Nothing was changed."; break;
        case LU_ERR_DOWNGRADE: case LU_ERR_DEV: snap_.state = kError; snap_.message = "Update refused: not a newer release for this build. Nothing was changed."; break;
        default: snap_.state = kError; snap_.message = "Update failed (code " + std::to_string(r) + "); the previous version was put back."; break;
        }
        return r;
    }
    std::wstring dir_, cfgPath_; std::string dirA_, version_, apiUrl_; std::function<void(const std::string&)> log_; CRITICAL_SECTION cs_; bool inited_ = false; volatile LONG busy_ = 0;
    lu_schedule sched_ = {1, 0, 0, 0}; int prePref_ = -1; lu_release rel_ = {}; Snapshot snap_;
};
}  // namespace wup
