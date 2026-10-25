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
    bool noGroupAvatarState = false; // mutant: share one gait state across all remote players
    bool noModelGait = false;    // test mutant: the figure never walks, whatever the player does
    bool modelByPlayer = false;  // every remote player is drawn with a different model of witness_models\ (player 1 the selected one, 2 the next, ...)
    bool noModelDepth = false;   // test mutant: draw the model over walls
    bool pauseWrongFlag = false; // test mutant: the first, wrong pause flag (it flips with F6 / F7 and with entering a puzzle)
    unsigned keyChat = 'T', keyPing = 'P', keyEmote = 'G', keyDraw = 'X', keyChar = 'C', keyCanvas = 'Z', keyColor = 'B';   // key_chat= ... : a letter or F1..F12
    std::string character;       // character=<model name>: the model this player announces as its own (others draw this player with it); empty: none announced
    bool noEmoteAnim = false, noEmoteClips = false, noCharacterAnnounce = false, noEmoteSend = false;   // test mutants: emotes shown but not animated / never play a model's own clip / characters not announced or not honoured / emotes not sent
    bool noChatUi = false, noTypingDots = false, noPingMarks = false;      // test mutants: T does nothing; typing is not announced; pings are not drawn
    std::string startProgress;   // start_progress=<spec>: the next new game starts with these parts of the map completed (the launcher arms it; progress/progress.h says how a spec reads); disarmed once applied
    bool noSaveMeta = false;     // test mutant: no <save>.coop.txt is written
    bool noStartProgress = false, progressNoPrereq = false;      // test mutants: the setting is ignored / only the chosen panels are solved, not what the game needs before them
    int voiceMode = 2;           // voice_mode=off|open|ptt (0, 1, 2): voice chat is off / the microphone is open (a voice-activity gate) / push to talk with key_voice (default)
    int voiceVolume = 100, micVolume = 100, gameVolume = 100;      // voice_volume= mic_volume= game_volume= percent (the game's volume is set on its Windows audio session)
    float voiceRange = 25.0f, voiceRadioRange = 0.0f;              // voice_range= metres within which players are heard in the world; voice_radio_range= metres beyond which not even the radio carries (0: no limit)
    bool voiceRadio = true;      // voice_radio=0: a voice beyond the range is not heard at all
    bool noSeeThrough = false; float ghostAlpha = 0.22f;      // see_through=0: players stay solid when they are in the way of a puzzle; ghost_alpha=: how see-through they become (ghost/ghost.h)
    unsigned keyVoice = 'N';     // key_voice=: push to talk
    std::string voiceChannel = "proximity";      // voice_channel=proximity|radio|global|direct:<player id>: who hears this player and how (voice/voice.h)
    unsigned keyVoiceChannel = 'H';        // key_voice_channel=: next channel (proximity, radio, global, then one on one with each player in turn)
    std::string voiceTestTone;   // voice_test_tone=120,1000,5500: the transmitted voice is a sum of these tones instead of the microphone (tests)
    bool voiceMuteOutput = false;// voice_mute_output=1: the mixer runs but nothing is played (tests)
    bool noVoiceChannel = false, voiceNoSelf = false, noVoiceChannelKey = false;      // test mutants: the chosen channel is not sent / this listener does not know its own id (hears what was said to others) / the channel key does nothing
    bool noVoiceSend = false, noVoicePlay = false, voiceNoPos = false, noSpeakingFlag = false, noGameVolume = false;      // test mutants: nothing transmitted / received voices dropped / every player counted at distance 0 / the speaking flag never set / the game's volume never set
    float mapgenHeight = 70.0f, mapgenRes = 0.5f; int mapgenSettleMs = 1500;      // mapgen_height= metres above the ground, mapgen_res= metres per pixel, mapgen_settle_ms= wait per tile
    bool mapgenBadScale = false, mapgenMirror = false, mapgenShowOverlay = false;      // test mutants: the projection has the wrong field of view / is mirrored / the mod keeps drawing during the capture
    bool noUnderlay = false;     // test mutant: the map has no picture under it
    bool noKeySwallow = false;   // test mutant: a key the mod took (Esc closing the chat box or the character list) still reaches the game as raw input
    bool noDrawSend = false, noDrawDraw = false, noDrawDepth = false;      // test mutants: strokes are not sent / not drawn / put at a fixed distance instead of on the surface under the pen
    bool noPauseHide = false;    // test mutant: draw puppets, lines and name tags over the pause menu
    bool skipEntityZero = false; // test mutant: restore the old census that lost the second tutorial door
    bool devMenu = false;        // dev_menu=1: the F8 dev menu (map, teleport, nearby puzzles); off in a normal install
    bool partnerView = false;    // (old switch, ignored: POV pictures are on unless partner_view=0)
    bool noPartnerView = false;  // partner_view=0: no POV screens at all
    bool noPartnerVideo = false; // twin: send no picture
    bool noRestoreCopy = false;  // twin: a restore does not put the save file in place
    bool noPowerLink = false;    // twin: leave a powered panel's source back-link (+0x2C8) unset
    float panelMouseGain = 3.0f;   // panel_mouse_gain=<f>: factor on the REAL mouse motion while the panel boost is on (F3): the game's panel cursor moves ~1/3 pixel per count, the owner's pointer 1 pixel
    bool panelMouseBoost = false;  // panel_mouse_boost=1: start with the boost on
    bool noPovRequest = false;     // twin: this game watches a player but never asks for the pictures
    bool uiTightAtlas = false;     // twin: the old glyph atlas (no room under the glyphs, no clipping): '[' and ']' leave bits over 't' and 'v'
    bool noNameTags = false;       // twin: no name tags over the other players
    bool noLineDraw = false;       // twin: remote lines are received but not drawn
    bool noLineDepth = false;      // twin: remote lines are drawn without the depth test (they show through walls)
    bool noTeleportHold = false;   // twin: the menu teleport does not hold the player (the old 12-frame write): far panels end at the spawn point
    bool noTargetDispatch = false; // twin: a remote solve does not run the game's target dispatcher for other target types
};
const Cfg& GetCfg();
