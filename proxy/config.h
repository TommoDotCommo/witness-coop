#pragma once
#include <string>
// Settings from witness_coop.cfg (next to the DLL), read once on first use, so it is safe to call from a hook that runs
// before any thread of ours has started.
struct Cfg {
    std::wstring saveDir;   // save_dir=instance -> <dll folder>\appdata ; save_dir=<path> ; empty: the game's real save folder
    int capFps = 0;         // cap_fps=N: limit the game to N frames per second (0 = unlimited, the game's own behaviour)
    bool shareZip = false;  // share_zip=1: open data-pc.zip with FILE_SHARE_READ so hard-linked copies can run side by side
};
const Cfg& GetCfg();
