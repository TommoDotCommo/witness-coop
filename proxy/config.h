#pragma once
#include <string>
#include <vector>
#include "core/appearance.h"
// Settings from witness_coop.cfg (next to the DLL), read once on first use, so it is safe to call from a hook that runs
// before any thread of ours has started.
struct Cfg {
    bool experimentalTv = false;              // experimental_tv=1: the TV features (back TV, sky TV, audience seats, TV camera consent) run; 0: none of them runs, draws, is sent or listens, and their keys are free
    bool povTv = false, povTvConsent = false; // pov_tv=1: show the nearest back TV; pov_tv_consent=1: allow sending snapshots
    witness::Appearance appearance;
    std::wstring saveDir;   // save_dir=instance -> <dll folder>\appdata ; save_dir=<path> ; empty: the game's real save folder
    int capFps = 0;         // cap_fps=N: limit the game to N frames per second (0 = unlimited, the game's own behaviour)
    bool forceActive = false; // force_active=1: keep the game's own 'window is active' byte set, so it does not throttle to ~10 fps or ignore input in the background
    bool debugMsgs = false;   // debug_msgs=1: log the window messages the game's loop receives, once per message id (diagnostic)
    bool debugDepth = false;  // debug_depth=1: log which depth buffers the game binds and clears in one frame (diagnostic)
    bool debugSleep = false;  // debug_sleep=1: log every distinct Sleep(arg) call and its caller (diagnostic)
    bool shareZip = false;  // share_zip=1: open data-pc.zip with FILE_SHARE_READ so hard-linked copies can run side by side
    float modelHeight = 0;       // model_height=<metres>: adventurer height override (0: the model's own adventurer.glb.txt)
    float modelYaw = 0;          // model_yaw=<degrees>: extra facing adjustment
    float modelStride = 0;       // model_speed=<m/s>: walking-clip reference speed override
    float depthBias = 0.02f;     // depth_bias=<fraction>: how much nearer than a wall the model may be before the wall hides it (2%)
    float depthNear = 0.0597f;   // depth_near=<m>: the game stores depth as near / distance (reversed, no far plane); 0.0597 was measured, see NOTES
    bool depthView = false;      // depth_view=1: draw the game's depth buffer instead of the model (diagnostic)
    bool noGroupAvatarState = false; // mutant: share one gait state across all remote players
    bool noModelStatQueue = false; // test mutant: read render-owned model statistics on the command thread
    bool noModelGait = false;    // test mutant: the figure never walks, whatever the player does
    bool noModelDepth = false;   // test mutant: draw the model over walls
    float modelSun = 0.55f, modelAmbient = 0.30f, modelShadow = 0.55f;   // model_sun= model_ambient= (linear light on the adventurer: the sun and the sky/ground hemisphere), model_shadow= (how dark its sun shadow is, 0: none)
    int modelShading = 1;        // model_shading=0: the old flat shading (no normal map, no specular, no shadow), for comparison
    bool noModelLife = false;    // test mutant: no blinks, glances, breathing, cloth or fidgets
    bool noModelNormalMap = false, noModelMaterialMap = false;   // diagnostics: ignore the baked normal map / the material map (flat relief; roughness 0.6, no metal)
    bool footsteps = true; int footstepVolume = 100;             // footsteps=0: the other players walk silently (a test mutant); footstep_volume= percent
    bool pauseWrongFlag = false; // test mutant: the first, wrong pause flag (it flips with F6 / F7 and with entering a puzzle)
#define BIND(name, member, key, text, label) unsigned member = key;
#include "../common/bindings.def"
#undef BIND
    bool noDetailFit = false;      // test mutant: the map page keeps the old detail-pane layout (stacked chain titles, fixed pane height) that cut off "Fed by:" at 1920x1080
    bool noTerrainProbe = false;   // test mutant: the audience screen and stools assume flat ground (the wearer's) instead of measuring it
    bool tvProbeShow = false;      // test mutant: measuring the ground for a screen is seen (the picture is not held, the others are sent the jumps)
    bool noSeatTiebreak = false;   // test mutant: two players who take the same audience stool in the same instant both stay seated
    bool noEmoteAnim = false, noEmoteClips = false, noEmoteSend = false;   // test mutants: emotes shown but not animated / never play a model's own clip / emotes not sent
    bool noChatUi = false, noTypingDots = false, noPingMarks = false;      // test mutants: T does nothing; typing is not announced; pings are not drawn
    std::string startProgress;   // start_progress=<spec>: the next new game starts with these parts of the map completed (the launcher arms it; progress/progress.h says how a spec reads); disarmed once applied
    bool noSaveMeta = false;     // test mutant: no <save>.coop.txt is written
    bool noStartProgress = false, progressNoPrereq = false;      // test mutants: the setting is ignored / only the chosen panels are solved, not what the game needs before them
    int voiceMode = 3;           // voice_mode=off|open|ptt|auto (0, 1, 2, 3): voice chat is off / the microphone is open (a voice-activity gate) / push to talk with key_voice / automatic (default): push to talk after keyboard input, open mic after pad input (common/padlayer.h)
    bool padLayer = true;        // pad_layer=0: the co-op pad layer on View is off (the game gets the whole pad)
    int padVoice = 0;            // pad_voice=none|ls|rs: a stick click held is push to talk (the game's own hints use both stick clicks, so none by default)
    bool padLeak = false;        // test mutant pad_leak=1: only View is taken from the game, the D-pad, the face buttons and the sticks still reach it while View is held
    int voiceVolume = 100, micVolume = 100, gameVolume = 100;      // voice_volume= mic_volume= game_volume= percent (the game's volume is set on its Windows audio session)
    float voiceRange = 25.0f, voiceRadioRange = 0.0f;              // voice_range= metres within which players are heard in the world; voice_radio_range= metres beyond which not even the radio carries (0: no limit)
    bool voiceRadio = true;      // voice_radio=0: a voice beyond the range is not heard at all
    bool race = false; std::string raceGoal = "panel:9F7F"; bool noRaceHud = false; int raceCountdownMs = 5000;      // game_mode=race: nothing of the world is shared and the players race (race/race.h); race_goal=panel:<hex>|count:<n>; no_race_hud=1: nothing drawn (test)
    bool lineOldAnchor = false, lineWholeEdge = false;      // test mutants: lines are sent as offsets from the panel's position now (the old way), the tip of a line being drawn is the end of its edge
    bool povModeZero = false;      // test mutant pov_mode_zero=1: the other player's screen is never drawn unless F9 was pressed (the old default)
    bool povNeedsAim = false;      // test mutant pov_needs_aim=1: V does nothing unless the crosshair is on a player (the old behaviour)
    bool runUnfocusedOldByte = false;      // test mutant run_unfocused_old_byte=1: run_unfocused sets the game's 'window is active' byte (it then grabs the real mouse in every window: the way it was first done)
    bool runUnfocused = false;      // run_unfocused=1: the game keeps running at full speed (not the ~10 fps of a background window) when its window is not active; the keyboard and mouse still only reach the window that has the focus
    bool noSeeThrough = false; float ghostAlpha = 0.22f;      // see_through=0: players stay solid when they are in the way of a puzzle; ghost_alpha=: how see-through they become (ghost/ghost.h)
    bool showLauncher = true;      // show_launcher=0: Play in Steam starts the game itself instead of the co-op launcher (hold Shift while it starts to get the launcher; common/handover.h)
    bool experimentalRadio = false; // experimental_radio=1: the handheld radio (key R, prop, antenna light, Radio channel, RADIO mark) exists; 0: none of it runs, draws, is sent or listens; the far-voice radio sound is not part of it
    bool radioEnabled = true; // radio_enabled=0: also switch the handheld radio off when experimental_radio is on (test and old configs)
    bool RadioOn() const { return experimentalRadio && radioEnabled; }
    bool noRadioView = false, noRadioPose = false; // test mutants: omit the view model / right-arm overlay
    std::string voiceChannel = "proximity";      // voice_channel=proximity|radio|global|direct:<player id>: who hears this player and how (voice/voice.h)
    std::string voiceTestTone;   // voice_test_tone=120,1000,5500: the transmitted voice is a sum of these tones instead of the microphone (tests)
    bool voiceMuteOutput = false;// voice_mute_output=1: the mixer runs but nothing is played (tests)
    bool noVoiceChannel = false, voiceNoSelf = false, noVoiceChannelKey = false;      // test mutants: the chosen channel is not sent / this listener does not know its own id (hears what was said to others) / the channel key does nothing
    bool noVoiceSend = false, noVoicePlay = false, voiceNoPos = false, noSpeakingFlag = false, noGameVolume = false;      // test mutants: nothing transmitted / received voices dropped / every player counted at distance 0 / the speaking flag never set / the game's volume never set
    bool lateVoiceLock = false;      // test mutant (tools/solo.sh): the voice lock exists only from VoiceStart, as before 2026-10-31: a start without a roster then crashes in the overlay
    float mapgenHeight = 70.0f, mapgenRes = 0.5f; int mapgenSettleMs = 1500;      // mapgen_height= metres above the ground, mapgen_res= metres per pixel, mapgen_settle_ms= wait per tile
    bool mapgenBadScale = false, mapgenMirror = false, mapgenShowOverlay = false;      // test mutants: the projection has the wrong field of view / is mirrored / the mod keeps drawing during the capture
    bool noUnderlay = false;     // test mutant: the map has no picture under it
    bool noKeySwallow = false;   // test mutant: a key the mod took (Esc closing the chat box) still reaches the game as raw input
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
    bool oldProjection = false;    // twin: world drawing assumes a 60 degree vertical field of view instead of the game's own
    bool noLineDraw = false;       // twin: remote lines are received but not drawn
    bool noLineDepth = false;      // twin: remote lines are drawn without the depth test (they show through walls)
    bool noTeleportHold = false;   // twin: the menu teleport does not hold the player (the old 12-frame write): far panels end at the spawn point
    bool noTargetDispatch = false; // twin: a remote solve does not run the game's target dispatcher for other target types
    std::wstring diagDir;        // diag_dir=instance -> <dll folder>\witness-coop-logs (not "logs": the game keeps its own logs there) ; diag_dir=<path> ; empty: %LOCALAPPDATA%\witness-coop\logs (diag.cpp)
    int maxLogMb = 100;          // max_log_mb=N: the logs folder is pruned to this size (a crashed session is never pruned)
    bool noCrashHandler = false; // crash_handler=0: no crash marker, no dump (a test mutant: the injected crash must then leave nothing)
    bool testCrash = false;      // test_crash=1: the witness_cmd.txt command 'testcrash' really crashes this game (access violation on the render thread); 'testhandled' raises an exception that is caught
    bool diagFirstChance = false; // test mutant diag_first_chance=1: the dump is written from the first-chance (vectored) handler, so exceptions the game handles itself would produce one
    bool noDiagMarkSend = false; // test mutant: the marker is not sent to the other players
    bool noDiagFlight = false;   // test mutant: no flight recorder samples
    std::vector<std::string> movedKeys, keyConflicts;   // the one-time list of keys whose default moved (common/movedkeys.h writes it), and the shared keys a player's own binding kept (logged)
    std::string invite;          // invite=<text>: the launcher's invite for this session (the player panel offers Copy invite when it is set)
    bool oldVWatch = false;      // test mutant old_v_watch=1: V watches the player (the pre-2026-11 key) instead of pinging
    bool noPlayerPanel = false;  // test mutant no_player_panel=1: holding Tab does not open the player panel
    bool emoteThirdPerson = true; // emote_third_person=0: the view stays first person while this player emotes (the launcher's "Third-person view when emoting")
    bool tpIgnoreWall = false;    // test mutant tp_ignore_wall=1: the measured room behind the player is ignored (the camera may stand in the wall)
    bool tpNeverReturn = false;   // test mutant tp_never_return=1: the third-person view never swings back to first person
};
const Cfg& GetCfg();
const std::wstring& DataDir();      // trailing backslash: %LOCALAPPDATA%\witness-coop\, or the game folder when its witness_coop.cfg is an explicit per-instance override (common/userdata.h)
void WriteMovedKeys();      // once per user: the list of keys whose default moved, for the launcher's What's new (common/movedkeys.h); not from DllMain
