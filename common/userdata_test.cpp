// Game-free test of common/userdata.h (run as a Windows program: under WSL interop or on Windows). Usage: userdata-test.exe [scratch folder]
//  - which cfg is an explicit per-instance override (an active instance key) and which is a player's own;
//  - DataDir: a per-instance cfg keeps everything beside the exe; no cfg or a player's cfg gives %LOCALAPPDATA%\witness-coop\;
//  - a player's cfg is moved once (copied, compared, then removed from the game folder), never overwriting a per-user cfg, and an override is never moved.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <windows.h>
#include "userdata.h"

static int fails;
#define CHECK(c, what) do { if (!(c)) { printf("FAIL: %s\n", what); fails++; } } while (0)

static void put(const std::wstring& p, const std::string& t) { FILE* f = _wfopen(p.c_str(), L"wb"); if (f) { fwrite(t.data(), 1, t.size(), f); fclose(f); } }
static std::string get(const std::wstring& p) { std::string s; userdata::ReadAll(p, &s); return s; }

int main(int argc, char** argv) {
    CHECK(userdata::IsInstanceCfg("id=1\nlisten=47001\nsave_dir=instance\n"), "save_dir=instance is an override");
    CHECK(userdata::IsInstanceCfg("name=A\r\nforce_active=1\r\n"), "force_active is an override");
    CHECK(userdata::IsInstanceCfg("  run_unfocused = 1\n"), "run_unfocused with spaces is an override");
    CHECK(userdata::IsInstanceCfg("diag_dir=instance"), "diag_dir without a newline is an override");
    CHECK(!userdata::IsInstanceCfg("name=Tom\nkey_ping=V\nvoice_mode=ptt\n"), "a player's cfg is not an override");
    CHECK(!userdata::IsInstanceCfg("# save_dir=instance\n; force_active=1\nname=A\n"), "commented lines do not count");
    CHECK(!userdata::IsInstanceCfg("name=save_dir=instance\n"), "a value does not count");
    CHECK(!userdata::IsInstanceCfg(""), "an empty cfg is not an override");

    std::wstring base;
    if (argc > 1) { int n = MultiByteToWideChar(CP_ACP, 0, argv[1], -1, nullptr, 0); base.resize(n - 1); MultiByteToWideChar(CP_ACP, 0, argv[1], -1, &base[0], n); }
    else { wchar_t t[MAX_PATH]; GetTempPathW(MAX_PATH, t); base = t; }
    base = userdata::WithSlash(base) + L"userdata-test-" + std::to_wstring(GetCurrentProcessId()) + L"\\";
    CreateDirectoryW(base.c_str(), nullptr);
    std::wstring appdata = base + L"appdata"; CreateDirectoryW(appdata.c_str(), nullptr);
    SetEnvironmentVariableW(L"LOCALAPPDATA", appdata.c_str());
    std::wstring user = appdata + L"\\witness-coop\\";

    // 1. a game folder with a per-instance cfg: everything stays beside the exe, nothing is created per user, the cfg is untouched
    std::wstring g1 = base + L"g1"; CreateDirectoryW(g1.c_str(), nullptr); put(g1 + L"\\witness_coop.cfg", "save_dir=instance\nname=Lab\n");
    CHECK(userdata::DataDir(g1) == g1 + L"\\", "an instance cfg keeps the data folder beside the exe");
    CHECK(!userdata::Exists(user + L"witness_coop.cfg"), "an instance cfg is not moved");
    CHECK(get(g1 + L"\\witness_coop.cfg") == "save_dir=instance\nname=Lab\n", "an instance cfg is left as it was");

    // 2. no cfg at all: the per-user folder, created; nothing is written into the game folder
    std::wstring g2 = base + L"g2"; CreateDirectoryW(g2.c_str(), nullptr);
    CHECK(userdata::DataDir(g2) == user, "no cfg: the data folder is %LOCALAPPDATA%\\witness-coop\\");
    CHECK(userdata::Exists(user), "the per-user folder is created");
    CHECK(!userdata::Exists(g2 + L"\\witness_coop.cfg"), "nothing is created in the game folder");

    // 3. a player's own cfg beside the exe (an earlier version): moved once, identical, and gone from the game folder
    std::wstring g3 = base + L"g3"; CreateDirectoryW(g3.c_str(), nullptr); const std::string mine = "name=Tom\nkey_ping=V\nvoice_mode=ptt\n";
    put(g3 + L"\\witness_coop.cfg", mine);
    CHECK(userdata::DataDir(g3) == user, "a player's cfg: the data folder is per user");
    CHECK(get(user + L"witness_coop.cfg") == mine, "the player's cfg arrived unchanged");
    CHECK(!userdata::Exists(g3 + L"\\witness_coop.cfg"), "the player's cfg left the game folder");

    // 4. a per-user cfg already there wins and the old file is not overwritten into it
    std::wstring g4 = base + L"g4"; CreateDirectoryW(g4.c_str(), nullptr); put(g4 + L"\\witness_coop.cfg", "name=Old\n");
    CHECK(userdata::DataDir(g4) == user, "both cfgs: still per user");
    CHECK(get(user + L"witness_coop.cfg") == mine, "the per-user cfg is not overwritten by an old one");
    CHECK(get(g4 + L"\\witness_coop.cfg") == "name=Old\n", "the old file is kept when a per-user cfg exists");

    std::error_code ec; std::filesystem::remove_all(std::filesystem::path(base), ec);
    printf(fails ? "userdata: %d FAILED\n" : "userdata: all checks passed\n", fails);
    return fails ? 1 : 0;
}
