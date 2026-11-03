// Game-free (T0) test of the launcher's logic.
#include "core.h"
#include "../common/buildid_fixture.h"
#include <cstdio>
#include <set>
#include <cstring>
#include <windows.h>
using namespace wl;
static int fails = 0, checks = 0;
static void check(const char* what, bool ok) { ++checks; if (!ok) { ++fails; printf("FAILED: %s\n", what); } }
static std::vector<uint8_t> FakeSave() {      // the layout of the real save: name, NUL, value (4 bytes), a 4-byte number; with a lookalike name earlier in the file
    std::vector<uint8_t> b = {1, 2, 3}; auto rec = [&](const char* n, const void* v, int32_t tail) { b.push_back(0); for (const char* p = n; *p; ++p) b.push_back((uint8_t)*p); b.push_back(0); const uint8_t* q = (const uint8_t*)v; b.insert(b.end(), q, q + 4); const uint8_t* t = (const uint8_t*)&tail; b.insert(b.end(), t, t + 4); };
    float f8 = 8.0f, f1 = 1.0f, f54 = 54.0f; int32_t z = 0, one = 1;
    for (const char* n : {"mouse_speed_3d_extra", "old_mouse_speed_3d"}) rec(n, &f1, 0);      // not the option: its name only starts or ends the same
    rec("mouse_speed_3d", &f8, 1); rec("mouse_speed_2d", &f8, 0); rec("invert_y_axis", &z, 0); rec("invert_x_axis", &z, 1); rec("mouse_smoothing", &z, 0);
    rec("user_brightness", &f1, 0); rec("user_fov_vertical", &f54, 0); rec("user_reticle", &z, 0); rec("user_vignetting", &one, 0); rec("look_magnetism_enabled", &one, 1);
    b.push_back(9); return b;
}
static float Val(const std::vector<uint8_t>& b, const char* n) { for (const auto& o : ReadSaveOptions(b)) if (o.name == n) return o.value; return -999; }

int main() {
    {   // the build check: a synthetic exe stands in for the game
        Fixture ok = Make(0x5c48cf09, 0x1100, 1), newer = Make(0x5c48cf0a, 0x1100, 1);
        check("build: the supported build gives no problem", GameBuildProblem(ok.file, ok.spec).empty());
        std::string p = GameBuildProblem(newer.file, ok.spec);
        check("build: another build is a problem that says what it needs and what to do", p.find("Steam build 11370412") != std::string::npos && p.find("Verify integrity") != std::string::npos && p.find("Found: exe built 5c48cf0a") != std::string::npos && p.find("#faq-not-the-supported-build") != std::string::npos);
        check("build: an empty (missing) file is a problem", !GameBuildProblem({}, ok.spec).empty());
        std::vector<uint8_t> cut(ok.file.begin(), ok.file.begin() + 0x300); check("build: a cut-off exe is a problem", !GameBuildProblem(cut, ok.spec).empty());
        Fixture patched = ok; patched.file[0x200 + 9] ^= 4; check("build: same header, other code is a problem", !GameBuildProblem(patched.file, ok.spec).empty());
        check("build: a file that does not exist is a problem", !GameBuildProblemOfFile(L"does-not-exist.exe").empty());
    }
    // invite links: the handler's argument becomes the text of the join box, nothing else does
    check("link: scheme and slashes are dropped, the invite stays", InviteFromLink("witness-coop://192.168.1.5:47000/ABCD") == "192.168.1.5:47000/ABCD");
    check("link: percent escapes are decoded and a closing slash dropped", InviteFromLink("WITNESS-COOP://10.0.0.2%3A47000%2FXY12/") == "10.0.0.2:47000/XY12");
    check("link: a Steam lobby number", InviteFromLink("\"witness-coop:109775241234567890\"") == "109775241234567890");
    check("link: text that is not a witness-coop link is refused", InviteFromLink("https://example.invalid/x").empty() && InviteFromLink("calc.exe").empty() && InviteFromLink("").empty());
    check("link: a control character is refused", InviteFromLink("witness-coop://a%0Ab").empty());
    // cfg
    CfgFile c = CfgFile::Parse("# my settings\nid=1\nname=Tom\npeer=2,10.0.0.5,47002\n\npeer=3,10.0.0.6,47003\nweird line\nmodel=fox\n");
    check("cfg: values are read", c.Get("name") == "Tom" && c.Get("id") == "1" && c.Get("nothing", "dflt") == "dflt" && c.Has("model") && !c.Has("mode"));
    check("cfg: a comment is not a setting", !CfgFile::Parse("#name=x\n").Has("name"));
    c.Set("name", "Alice"); c.Set("cap_fps", "60");
    check("cfg: set replaces in place, a new key is appended, everything else is kept", c.Get("name") == "Alice" && c.Get("cap_fps") == "60" && c.Text().find("# my settings\nid=1\nname=Alice\n") == 0 && c.Text().find("weird line\n") != std::string::npos);
    check("cfg: repeated keys", c.GetAll("peer").size() == 2 && c.GetAll("peer")[1] == "3,10.0.0.6,47003");
    c.SetAll("peer", {"4,1.2.3.4,5"}); check("cfg: peers replaced by exactly the new list, at the first one's place", c.GetAll("peer").size() == 1 && c.Text().find("name=Alice\npeer=4,1.2.3.4,5\n\n") != std::string::npos);
    c.Unset("model"); check("cfg: unset removes", !c.Has("model") && c.Text().find("model") == std::string::npos);
    check("cfg: round trip keeps the text", CfgFile::Parse("a=1\n\n# x\nb=2\n").Text() == "a=1\n\n# x\nb=2\n" && CfgFile::Parse("a=1\r\nb=2\r\n").Get("b") == "2");
    // video: the real file as the game wrote it
    const char* real = ":/launcher\nrender_profile 2\nfullscreen false\nvsync false\nhighdpi false\nresolution 4\nmultisampling 3\ncustom_width 2560\ncustom_height 1440\nshading_quality 2\nreflection_quality 2\ntexture_quality 2\ntexture_detail 2\ngeometry_detail 2\n";
    Video v; check("video: the game's file parses", ParseVideo(real, &v) && v.resolution == 4 && v.multisampling == 3 && v.customW == 2560 && v.customH == 1440 && !v.fullscreen && !v.vsync && v.geometry == 2);
    check("video: written back byte for byte", FormatVideo(v) == real);
    v.fullscreen = true; v.vsync = true; v.shading = 0; Video w; check("video: changes survive a write and a read", ParseVideo(FormatVideo(v), &w) && w.fullscreen && w.vsync && w.shading == 0 && w.customW == 2560);
    check("video: not that file", !ParseVideo("hello 1\n", &w));
    check("video: values the game does not know are refused", CheckVideo(v).empty() && !([&] { Video x = v; x.resolution = 9; return CheckVideo(x); }()).empty() && !([&] { Video x = v; x.multisampling = -1; return CheckVideo(x); }()).empty() && !([&] { Video x = v; x.customW = 10; return CheckVideo(x); }()).empty() && !([&] { Video x = v; x.shading = 3; return CheckVideo(x); }()).empty());
    check("video: a custom size matters only for the custom resolution", [&] { Video x = v; x.resolution = 1; x.customW = 5; return CheckVideo(x).empty(); }());
    // save options
    auto s = FakeSave();
    check("save: the options are found with their values (a lookalike name is not one)", Val(s, "mouse_speed_3d") == 8.0f && Val(s, "user_fov_vertical") == 54.0f && Val(s, "user_brightness") == 1.0f && Val(s, "user_vignetting") == 1.0f && Val(s, "invert_y_axis") == 0.0f && ReadSaveOptions(s).size() == SaveOptionDefs().size());
    auto orig = s; check("save: a float is written as a float", WriteSaveOption(&s, "mouse_speed_3d", 12.5f) && Val(s, "mouse_speed_3d") == 12.5f && Val(s, "mouse_speed_2d") == 8.0f);
    check("save: a yes / no is written as the integer", WriteSaveOption(&s, "invert_y_axis", 1) && Val(s, "invert_y_axis") == 1.0f && WriteSaveOption(&s, "user_vignetting", 0) && Val(s, "user_vignetting") == 0.0f);
    int diff = 0; for (size_t i = 0; i < s.size(); ++i) diff += s[i] != orig[i];
    check("save: nothing but the edited values changed (the file keeps its size, the numbers after each value stay)", s.size() == orig.size() && diff > 0 && diff <= 3 + 3);
    check("save: out-of-range and unknown options are refused", !WriteSaveOption(&s, "mouse_speed_3d", 99) && !WriteSaveOption(&s, "user_fov_vertical", 5) && !WriteSaveOption(&s, "no_such_option", 1) && Val(s, "mouse_speed_3d") == 12.5f);
    std::vector<uint8_t> empty = {1, 2, 3}; check("save: a file without the options reads as none and refuses writes", ReadSaveOptions(empty).empty() && !WriteSaveOption(&empty, "mouse_speed_3d", 5));
    // progress spec
    check("new game: the spec text", ProgressSpec({"Desert", "Symmetry Island"}, 3, {"tutorial"}) == "area:Desert;area:Symmetry Island;lasers:3;preset:tutorial" && ProgressSpec({}, 0, {}).empty());
    check("launch: skips the game's own dialog (the launcher has written its settings), a new game only when asked", LaunchArgs(c, false) == "-skip_config_dialog" && LaunchArgs(c, true) == "-skip_config_dialog -new_game");
    c.Set("launcher_no_sound", "1"); check("launch: -no_sound when the sound is switched off", LaunchArgs(c, false) == "-skip_config_dialog -no_sound");
    // saves on disk
    wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp); std::wstring dir = std::wstring(tmp) + L"wl_test_saves"; CreateDirectoryW(dir.c_str(), nullptr);
    auto put = [&](const wchar_t* name, const char* data) { FILE* f = _wfopen((dir + L"\\" + name).c_str(), L"wb"); fputs(data, f); fclose(f); };
    put(L"2026.10.26__time_01.00.00.witness_campaign", "old"); put(L"2026.10.26__time_01.00.00.png", "png");
    Sleep(50); put(L"2026.10.26__time_02.00.00.witness_campaign", "newer"); put(L"2026.10.26__time_02.00.00.coop.txt", "coop-save 1\nsaved 2026-10-26 02:00:00\nprogress 5 100\nlasers 1 11\nplayer\t1\tAlpha\tfox\t60\t1\n");
    auto l = ListSaves(dir);
    check("saves: newest first, with the picture and the co-op sidecar", l.size() == 2 && l[0].stem.find("02.00.00") != std::string::npos && l[0].hasMeta && l[0].meta.solved == 5 && l[0].meta.players.size() == 1 && !l[0].hasPng && l[1].hasPng && !l[1].hasMeta);
    for (auto& f : l) { DeleteFileW(f.path.c_str()); } DeleteFileW((dir + L"\\2026.10.26__time_01.00.00.png").c_str()); DeleteFileW((dir + L"\\2026.10.26__time_02.00.00.coop.txt").c_str()); RemoveDirectoryW(dir.c_str());
    check("saves: a folder that is not there lists nothing", ListSaves(L"C:\\no\\such\\folder").empty());
    // the banner's art: the newest save whose picture loads, else the drawn panel
    {
        auto save = [](const wchar_t* stem, bool png) { SaveInfo s; s.path = std::wstring(L"S:\\saves\\") + stem + L".witness_campaign"; s.hasPng = png; return s; };
        std::set<std::wstring> good = {L"S:\\saves\\b.png", L"S:\\saves\\c.png"}; std::vector<std::wstring> tried;
        auto loads = [&](const std::wstring& p) { tried.push_back(p); return good.count(p) > 0; };
        check("art: no saves (first start) draws the panel", !PickArt({}, loads).picture);
        check("art: saves without pictures draw the panel", !PickArt({save(L"a", false), save(L"x", false)}, loads).picture);
        ArtPick a = PickArt({save(L"c", true), save(L"b", true)}, loads); check("art: the newest save's picture", a.picture && a.png == L"S:\\saves\\c.png");
        a = PickArt({save(L"a", false), save(L"b", true)}, loads); check("art: the newest save has no picture: the next save's picture", a.picture && a.png == L"S:\\saves\\b.png");
        tried.clear(); a = PickArt({save(L"x", true), save(L"b", true)}, loads); check("art: a picture that does not load is passed over", a.picture && a.png == L"S:\\saves\\b.png" && tried.size() == 2);
        check("art: no picture loads: the panel", !PickArt({save(L"x", true), save(L"y", true)}, loads).picture);
    }
    printf("%d checks, %d failed\n", checks, fails); return fails ? 1 : 0;
}
