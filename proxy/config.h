#pragma once
#include <string>
// Settings from witness_coop.cfg (next to the DLL), read once on first use, so it is safe to call from a hook that runs
// before any thread of ours has started.
struct Cfg {
    std::wstring saveDir;   // save_dir=instance -> <dll folder>\appdata ; save_dir=<path> ; empty: the game's real save folder
    int capFps = 0;         // cap_fps=N: limit the game to N frames per second (0 = unlimited, the game's own behaviour)
    bool forceActive = false; // force_active=1: keep the game's own 'window is active' byte set, so it does not throttle to ~10 fps or ignore input in the background
    bool debugMsgs = false;   // debug_msgs=1: log the window messages the game's loop receives, once per message id (diagnostic)
    bool debugDepth = false;  // debug_depth=1: log which depth buffers the game binds and clears in one frame (diagnostic)
    bool debugSleep = false;  // debug_sleep=1: log every distinct Sleep(arg) call and its caller (diagnostic)
    bool shareZip = false;  // share_zip=1: open data-pc.zip with FILE_SHARE_READ so hard-linked copies can run side by side
    std::wstring modelPath = L"cesium-man"; // model=<name or file>: the other player's character: a bundled name (witness_models\<name>.glb) or a .glb path (next to the DLL or absolute); model=none: the flat puppet
    float modelHeight = 0;       // model_height=<metres>: the figure is scaled to this height (0: the model's own <file>.glb.txt, else 1.8)
    float modelYaw = 0;          // model_yaw=<degrees>: turn a model that faces another way than +Z (added to the model's own yaw=)
    float modelStride = 0;       // model_speed=<m/s>: the walking speed the clip was made for; the clip plays faster or slower to match the player's speed (0: the model's own speed=, else 1.6)
    float depthBias = 0.02f;     // depth_bias=<fraction>: how much nearer than a wall the model may be before the wall hides it (2%)
    float depthNear = 0.0597f;   // depth_near=<m>: the game stores depth as near / distance (reversed, no far plane); 0.0597 was measured, see NOTES
    bool depthView = false;      // depth_view=1: draw the game's depth buffer instead of the model (diagnostic)
    bool noModelSelect = false;  // test mutant: F6 / F7 and the model command do nothing
    bool noModelGait = false;    // test mutant: the figure never walks, whatever the player does
    bool noModelDepth = false;   // test mutant: draw the model over walls
    bool skipEntityZero = false; // test mutant: restore the old census that lost the second tutorial door
};
const Cfg& GetCfg();
