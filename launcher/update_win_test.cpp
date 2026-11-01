// A headless run of the Windows updater against a local fake release server (tools/test_launcher_update_win.py): no window, no game.
//   update_win_test.exe <game dir> <version> <api url> <check|apply|both|auto>   prints "key=value" lines
#include "update_win.h"
#include <cstdio>
int main(int argc, char** argv) {
    if (argc < 5) return 2;
    int n; wchar_t** w = CommandLineToArgvW(GetCommandLineW(), &n); (void)w;
    std::wstring dir; { int len = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, nullptr, 0); dir.resize(len - 1); MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, &dir[0], len); }
    wup::Updater u; u.Init(dir, argv[2], [](const std::string& s) { printf("log: %s\n", s.c_str()); });
    u.SetApiUrl(argv[3]); std::string cmd = argv[4];
    int r = 0;
    if (cmd == "auto") { u.StartAuto(); for (int i = 0; i < 200 && (u.Busy() || i < 3); ++i) Sleep(50); }
    else { r = u.CheckSync(cmd != "auto"); printf("check=%d\n", r); if ((cmd == "apply" || cmd == "both") && u.Available()) { r = u.ApplySync(); printf("apply=%d\n", r); } }
    wup::Snapshot s = u.Get(); printf("state=%d\ntag=%s\nmessage=%s\nnotes=%s\n", (int)s.state, s.tag.c_str(), s.message.c_str(), s.notes.substr(0, 40).c_str());
    return 0;
}
