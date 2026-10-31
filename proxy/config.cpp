#include "bindings.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "config.h"
#include "../common/guide.h"
#include "../common/userdata.h"

extern HMODULE g_selfModule();

static std::wstring DllDir() {
    wchar_t p[MAX_PATH]; GetModuleFileNameW(g_selfModule(), p, MAX_PATH);
    wchar_t* s = wcsrchr(p, L'\\'); if (s) *(s + 1) = 0; return p;
}
// Where the settings and the run-time files live (common/userdata.h): %LOCALAPPDATA%\witness-coop\, or the game folder for an explicit per-instance cfg.
const std::wstring& DataDir() { static const std::wstring d = userdata::DataDir(DllDir()); return d; }

#include "../common/movedkeys.h"
void WriteMovedKeys() {
    const Cfg& c = GetCfg(); std::wstring dir = DataDir();      // (a test copy with an instance cfg keeps its files beside it)
    auto utf8 = [](const std::wstring& w) { int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr); std::string s(n > 0 ? n - 1 : 0, '\0'); if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr); return s; };
    movedkeys::WriteOnce(utf8(dir), c.movedKeys);
}
const Cfg& GetCfg() {
    static Cfg c = [] {
        Cfg r; FILE* f = _wfopen((DataDir() + L"witness_coop.cfg").c_str(), L"r"); if (!f) { r.movedKeys = guide::MovedNotes(r); return r; }
        char line[400];
        while (fgets(line, sizeof line, f)) {
            if (SetBindingLine(&r,line)) continue;
            int v; float fv; char s[300];
            if (!strncmp(line, "appearance_", 11)) { witness::SetAppearanceConfigLine(&r.appearance, line); continue; }
            if (sscanf(line, "cap_fps=%d", &v) == 1) r.capFps = v;
            else if (sscanf(line, "share_zip=%d", &v) == 1) r.shareZip = v != 0;
            else if (sscanf(line, "debug_depth=%d", &v) == 1) r.debugDepth = v != 0;
            else if (sscanf(line, "debug_sleep=%d", &v) == 1) r.debugSleep = v != 0;
            else if (sscanf(line, "debug_msgs=%d", &v) == 1) r.debugMsgs = v != 0;
            else if (sscanf(line, "force_active=%d", &v) == 1) r.forceActive = v != 0;
            else if (sscanf(line, "skip_entity_zero=%d", &v) == 1) r.skipEntityZero = v != 0;
            else if (sscanf(line, "dev_menu=%d", &v) == 1) r.devMenu = v != 0;
            else if (sscanf(line, "experimental_tv=%d", &v) == 1) r.experimentalTv = v != 0;
            else if (sscanf(line, "experimental_radio=%d", &v) == 1) r.experimentalRadio = v != 0;
            else if (sscanf(line, "pov_tv=%d", &v) == 1) r.povTv = v != 0;
            else if (sscanf(line, "pov_tv_consent=%d", &v) == 1) r.povTvConsent = v != 0;
            else if (sscanf(line, "partner_view=%d", &v) == 1) { r.partnerView = v != 0; r.noPartnerView = v == 0; }
            else if (sscanf(line, "no_partner_video=%d", &v) == 1) r.noPartnerVideo = v != 0;
            else if (sscanf(line, "no_restore_copy=%d", &v) == 1) r.noRestoreCopy = v != 0;
            else if (sscanf(line, "no_power_link=%d", &v) == 1) r.noPowerLink = v != 0;
            else if (sscanf(line, "no_target_dispatch=%d", &v) == 1) r.noTargetDispatch = v != 0;
            else if (sscanf(line, "old_projection=%d", &v) == 1) r.oldProjection = v != 0;
            else if (sscanf(line, "no_line_draw=%d", &v) == 1) r.noLineDraw = v != 0;
            else if (sscanf(line, "no_line_mirror=%d", &v) == 1) r.noLineMirror = v != 0;
            else if (sscanf(line, "no_name_tags=%d", &v) == 1) r.noNameTags = v != 0;
            else if (sscanf(line, "key_repeat_toggle=%d", &v) == 1) r.keyRepeatToggle = v != 0;
            else if (sscanf(line, "emote_third_person=%d", &v) == 1) r.emoteThirdPerson = v != 0;
            else if (sscanf(line, "tp_never_return=%d", &v) == 1) r.tpNeverReturn = v != 0;
            else if (sscanf(line, "tp_ignore_wall=%d", &v) == 1) r.tpIgnoreWall = v != 0;
            else if (sscanf(line, "ui_tight_atlas=%d", &v) == 1) r.uiTightAtlas = v != 0;
            else if (sscanf(line, "ui_font=%40[A-Za-z]", s) == 1) r.uiFont = s;
            else if (sscanf(line, "no_pov_request=%d", &v) == 1) r.noPovRequest = v != 0;
            else if (sscanf(line, "no_line_depth=%d", &v) == 1) r.noLineDepth = v != 0;
            else if (sscanf(line, "no_teleport_hold=%d", &v) == 1) r.noTeleportHold = v != 0;
            else if (sscanf(line, "panel_mouse_boost=%d", &v) == 1) r.panelMouseBoost = v != 0;
            else if (sscanf(line, "panel_mouse_gain=%f", &fv) == 1) r.panelMouseGain = fv;
            else if (sscanf(line, "depth_bias=%f", &fv) == 1) r.depthBias = fv;
            else if (sscanf(line, "depth_near=%f", &fv) == 1) r.depthNear = fv;
            else if (sscanf(line, "depth_view=%d", &v) == 1) r.depthView = v != 0;
            else if (sscanf(line, "model_height=%f", &fv) == 1) r.modelHeight = fv;
            else if (sscanf(line, "model_yaw=%f", &fv) == 1) r.modelYaw = fv;
            else if (sscanf(line, "model_speed=%f", &fv) == 1) r.modelStride = fv;
            else if (sscanf(line, "no_model_depth=%d", &v) == 1) r.noModelDepth = v != 0;
            else if (sscanf(line, "no_pause_hide=%d", &v) == 1) r.noPauseHide = v != 0;
            else if (!strncmp(line, "invite=", 7)) { std::string t = line + 7; while (!t.empty() && (t.back() == '\r' || t.back() == '\n')) t.pop_back(); r.invite = t; }
            else if (sscanf(line, "old_v_watch=%d", &v) == 1) r.oldVWatch = v != 0;
            else if (sscanf(line, "no_player_panel=%d", &v) == 1) r.noPlayerPanel = v != 0;
            else if (sscanf(line, "no_chat_ui=%d", &v) == 1) r.noChatUi = v != 0;
            else if (sscanf(line, "no_emote_anim=%d", &v) == 1) r.noEmoteAnim = v != 0;
            else if (sscanf(line, "no_seat_tiebreak=%d", &v) == 1) r.noSeatTiebreak = v != 0;
            else if (sscanf(line, "tv_probe_show=%d", &v) == 1) r.tvProbeShow = v != 0;
            else if (sscanf(line, "no_detail_fit=%d", &v) == 1) r.noDetailFit = v != 0;
            else if (sscanf(line, "no_terrain_probe=%d", &v) == 1) r.noTerrainProbe = v != 0;
            else if (sscanf(line, "no_emote_clips=%d", &v) == 1) r.noEmoteClips = v != 0;
            else if (sscanf(line, "no_emote_send=%d", &v) == 1) r.noEmoteSend = v != 0;
            else if (sscanf(line, "no_typing_dots=%d", &v) == 1) r.noTypingDots = v != 0;
            else if (sscanf(line, "no_ping_marks=%d", &v) == 1) r.noPingMarks = v != 0;
            else if (!strncmp(line, "start_progress=", 15)) { std::string t = line + 15; while (!t.empty() && (t.back() == '\r' || t.back() == '\n')) t.pop_back(); r.startProgress = t; }
            else if (sscanf(line, "no_radio_view=%d", &v) == 1) r.noRadioView = v != 0;
            else if (sscanf(line, "no_radio_pose=%d", &v) == 1) r.noRadioPose = v != 0;
            else if (sscanf(line, "radio_enabled=%d", &v) == 1) r.radioEnabled = v != 0;
            else if (sscanf(line, "voice_mode=%15s", s) == 1) r.voiceMode = !strcmp(s, "off") ? 0 : !strcmp(s, "open") ? 1 : !strcmp(s, "auto") ? 3 : 2;
            else if (sscanf(line, "pad_layer=%d", &v) == 1) r.padLayer = v != 0;
            else if (sscanf(line, "pad_voice=%15s", s) == 1) r.padVoice = !strcmp(s, "ls") ? 1 : !strcmp(s, "rs") ? 2 : 0;
            else if (sscanf(line, "pad_leak=%d", &v) == 1) r.padLeak = v != 0;
            else if (sscanf(line, "voice_volume=%d", &v) == 1) r.voiceVolume = std::max(0, std::min(100, v));
            else if (sscanf(line, "mic_volume=%d", &v) == 1) r.micVolume = std::max(0, std::min(100, v));
            else if (sscanf(line, "game_volume=%d", &v) == 1) r.gameVolume = std::max(0, std::min(100, v));
            else if (sscanf(line, "voice_range=%f", &fv) == 1) r.voiceRange = std::max(3.0f, fv);
            else if (sscanf(line, "voice_radio_range=%f", &fv) == 1) r.voiceRadioRange = std::max(0.0f, fv);
            else if (sscanf(line, "voice_radio=%d", &v) == 1) r.voiceRadio = v != 0;
            else if (sscanf(line, "game_mode=%15s", s) == 1) r.race = !strcmp(s, "race");
            else if (sscanf(line, "race_goal=%39s", s) == 1) r.raceGoal = s;
            else if (sscanf(line, "race_countdown_ms=%d", &v) == 1) r.raceCountdownMs = std::max(1000, std::min(600000, v));
            else if (sscanf(line, "no_race_hud=%d", &v) == 1) r.noRaceHud = v != 0;
            else if (sscanf(line, "pov_mode_zero=%d", &v) == 1) r.povModeZero = v != 0;
            else if (sscanf(line, "pov_needs_aim=%d", &v) == 1) r.povNeedsAim = v != 0;
            else if (sscanf(line, "run_unfocused_old_byte=%d", &v) == 1) r.runUnfocusedOldByte = v != 0;
            else if (sscanf(line, "run_unfocused=%d", &v) == 1) r.runUnfocused = v != 0;
            else if (sscanf(line, "show_launcher=%d", &v) == 1) r.showLauncher = v != 0;
            else if (sscanf(line, "line_old_anchor=%d", &v) == 1) r.lineOldAnchor = v != 0;
            else if (sscanf(line, "line_whole_edge=%d", &v) == 1) r.lineWholeEdge = v != 0;
            else if (sscanf(line, "see_through=%d", &v) == 1) r.noSeeThrough = v == 0;
            else if (sscanf(line, "ghost_alpha=%f", &fv) == 1) r.ghostAlpha = std::max(0.02f, std::min(1.0f, fv));
            else if (sscanf(line, "voice_channel=%31s", s) == 1) r.voiceChannel = s;
            else if (sscanf(line, "no_voice_channel=%d", &v) == 1) r.noVoiceChannel = v != 0;
            else if (sscanf(line, "voice_no_self=%d", &v) == 1) r.voiceNoSelf = v != 0;
            else if (sscanf(line, "no_voice_channel_key=%d", &v) == 1) r.noVoiceChannelKey = v != 0;
            else if (sscanf(line, "voice_test_tone=%299s", s) == 1) r.voiceTestTone = s;
            else if (sscanf(line, "voice_mute_output=%d", &v) == 1) r.voiceMuteOutput = v != 0;
            else if (sscanf(line, "no_voice_send=%d", &v) == 1) r.noVoiceSend = v != 0;
            else if (sscanf(line, "no_voice_play=%d", &v) == 1) r.noVoicePlay = v != 0;
            else if (sscanf(line, "voice_no_pos=%d", &v) == 1) r.voiceNoPos = v != 0;
            else if (sscanf(line, "no_speaking_flag=%d", &v) == 1) r.noSpeakingFlag = v != 0;
            else if (sscanf(line, "late_voice_lock=%d", &v) == 1) r.lateVoiceLock = v != 0;
            else if (sscanf(line, "no_game_volume=%d", &v) == 1) r.noGameVolume = v != 0;
            else if (sscanf(line, "mapgen_height=%f", &fv) == 1) r.mapgenHeight = std::max(30.0f, fv);
            else if (sscanf(line, "mapgen_res=%f", &fv) == 1) r.mapgenRes = std::max(0.1f, fv);
            else if (sscanf(line, "mapgen_settle_ms=%d", &v) == 1) r.mapgenSettleMs = std::max(100, v);
            else if (sscanf(line, "mapgen_bad_scale=%d", &v) == 1) r.mapgenBadScale = v != 0;
            else if (sscanf(line, "mapgen_mirror=%d", &v) == 1) r.mapgenMirror = v != 0;
            else if (sscanf(line, "mapgen_show_overlay=%d", &v) == 1) r.mapgenShowOverlay = v != 0;
            else if (sscanf(line, "no_underlay=%d", &v) == 1) r.noUnderlay = v != 0;
            else if (sscanf(line, "no_key_swallow=%d", &v) == 1) r.noKeySwallow = v != 0;
            else if (sscanf(line, "no_save_meta=%d", &v) == 1) r.noSaveMeta = v != 0;
            else if (!strncmp(line, "diag_dir=", 9)) { std::string t = line + 9; while (!t.empty() && (t.back() == '\r' || t.back() == '\n')) t.pop_back(); if (t == "instance") r.diagDir = DllDir() + L"witness-coop-logs"; else { int n = MultiByteToWideChar(CP_UTF8, 0, t.c_str(), -1, nullptr, 0); if (n > 1) { r.diagDir.resize(n - 1); MultiByteToWideChar(CP_UTF8, 0, t.c_str(), -1, &r.diagDir[0], n); } } }
            else if (sscanf(line, "max_log_mb=%d", &v) == 1) r.maxLogMb = std::max(5, std::min(2000, v));
            else if (sscanf(line, "crash_handler=%d", &v) == 1) r.noCrashHandler = v == 0;
            else if (sscanf(line, "test_crash=%d", &v) == 1) r.testCrash = v != 0;
            else if (sscanf(line, "diag_first_chance=%d", &v) == 1) r.diagFirstChance = v != 0;
            else if (sscanf(line, "no_diag_mark_send=%d", &v) == 1) r.noDiagMarkSend = v != 0;
            else if (sscanf(line, "no_diag_flight=%d", &v) == 1) r.noDiagFlight = v != 0;
            else if (sscanf(line, "no_start_progress=%d", &v) == 1) r.noStartProgress = v != 0;
            else if (sscanf(line, "progress_no_prereq=%d", &v) == 1) r.progressNoPrereq = v != 0;
            else if (sscanf(line, "no_draw_send=%d", &v) == 1) r.noDrawSend = v != 0;
            else if (sscanf(line, "no_draw_draw=%d", &v) == 1) r.noDrawDraw = v != 0;
            else if (sscanf(line, "no_draw_depth=%d", &v) == 1) r.noDrawDepth = v != 0;
            else if (sscanf(line, "pause_wrong_flag=%d", &v) == 1) r.pauseWrongFlag = v != 0;
            else if (sscanf(line, "no_group_avatar_state=%d", &v) == 1) r.noGroupAvatarState = v != 0;
            else if (sscanf(line, "no_modelstat_queue=%d", &v) == 1) r.noModelStatQueue = v != 0;
            else if (sscanf(line, "no_model_gait=%d", &v) == 1) r.noModelGait = v != 0;
            else if (sscanf(line, "no_model_life=%d", &v) == 1) r.noModelLife = v != 0;
            else if (sscanf(line, "no_model_normalmap=%d", &v) == 1) r.noModelNormalMap = v != 0;
            else if (sscanf(line, "no_model_matmap=%d", &v) == 1) r.noModelMaterialMap = v != 0;
            else if (sscanf(line, "footsteps=%d", &v) == 1) r.footsteps = v != 0;
            else if (sscanf(line, "footstep_volume=%d", &v) == 1) r.footstepVolume = v;
            else if (sscanf(line, "footstep_old_level=%d", &v) == 1) r.footstepOldLevel = v != 0;
            else if (sscanf(line, "model_sun=%f", &fv) == 1) r.modelSun = fv;
            else if (sscanf(line, "model_ambient=%f", &fv) == 1) r.modelAmbient = fv;
            else if (sscanf(line, "model_shadow=%f", &fv) == 1) r.modelShadow = fv;
            else if (sscanf(line, "model_shading=%d", &v) == 1) r.modelShading = v;
            else if (sscanf(line, "save_dir=%299[^\r\n]", s) == 1) {
                if (!strcmp(s, "instance")) r.saveDir = DllDir() + L"appdata";
                else { wchar_t w[300]; MultiByteToWideChar(CP_UTF8, 0, s, -1, w, 300); r.saveDir = w; }
            }
        }
        fclose(f); guide::MigrateAll(r, &r.keyConflicts); r.movedKeys = guide::MovedNotes(r); return r;
    }();
    return c;
}
