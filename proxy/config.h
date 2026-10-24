#pragma once
#include <string>
// Settings from witness_coop.cfg (next to the DLL), read once on first use, so it is safe to call from a hook that runs
// before any thread of ours has started.
struct Cfg {
    std::wstring saveDir;   // save_dir=instance -> <dll folder>\appdata ; save_dir=<path> ; empty: the game's real save folder
    int capFps = 0;         // cap_fps=N: limit the game to N frames per second (0 = unlimited, the game's own behaviour)
    bool forceActive = false; // force_active=1: keep the game's own 'window is active' byte set, so it does not throttle to ~10 fps or ignore input in the background
    bool debugMsgs = false;   // debug_msgs=1: log the window messages the game's loop receives, once per message id (diagnostic)
    bool debugSleep = false;  // debug_sleep=1: log every distinct Sleep(arg) call and its caller (diagnostic)
    bool shareZip = false;  // share_zip=1: open data-pc.zip with FILE_SHARE_READ so hard-linked copies can run side by side
    bool skipEntityZero = false; // test mutant: restore the old census that lost the second tutorial door
    bool devMenu = false;        // dev_menu=1: the F8 dev menu (map, teleport, nearby puzzles); off in a normal install
    bool partnerView = false;    // partner_view=1: send a small picture of this screen to the peer and show the peer's (F9)
    bool noPartnerVideo = false; // twin: send no picture
    bool noRestoreCopy = false;  // twin: a restore does not put the save file in place
    bool noPowerLink = false;    // twin: leave a powered panel's source back-link (+0x2C8) unset
    bool noTargetDispatch = false; // twin: a remote solve does not run the game's target dispatcher for other target types
};
const Cfg& GetCfg();
