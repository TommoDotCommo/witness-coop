// See pad.h. The layer's state lives here under one lock: the game's thread calls PadGameGetState, the render thread PadFrame, the command thread PadCommand.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include "pad.h"
#include "../common/padlayer.h"
#include "config.h"
#include "guide_ui.h"
#include "povui.h"
#include "social.h"
#include "steam_net.h"
#include "../common/guide.h"

void Log(const char* msg);
void PovPadPanel(bool open);                 // overlay.cpp: the player panel opened / closed by the pad
void PovPadWatch(int dir);                   // overlay.cpp: watch the previous (-1) / next (+1) player
void PovPanelMove(int dx, int dy);
bool PovPanelClick();
bool DevMenuOpen();

namespace {
struct XPad { WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; };
struct XState { DWORD packet; XPad pad; };
typedef DWORD(WINAPI* XGetStateFn)(DWORD, XState*);
XGetStateFn g_gameReal, g_own; bool g_ownTried;
CRITICAL_SECTION* Lk() { static struct Init { CRITICAL_SECTION cs; Init() { InitializeCriticalSection(&cs); } } init; return &init.cs; }
struct Lock { Lock() { EnterCriticalSection(Lk()); } ~Lock() { LeaveCriticalSection(Lk()); } };
padlayer::Layer g_layer; padlayer::Glyphs g_glyphs; padlayer::Osk g_osk;
padlayer::Pad g_fake; bool g_fakeOn = false; DWORD g_fakePacket = 1;
int g_slot = -1; ULONGLONG g_scanAt = 0;
padlayer::Out g_last; padlayer::Pad g_lastRaw; bool g_listWas = false, g_guideWas = false, g_talkWas = false; padlayer::Wheel g_wheelWas = padlayer::Wheel::None;
std::atomic<unsigned> g_gameCalls{0}, g_frames{0}, g_pings{0}, g_chats{0}, g_watches{0}, g_wheels{0}, g_steamKb{0}, g_ownKb{0};
std::atomic<unsigned> g_gameButtons{0}; std::atomic<int> g_gameLy{0}, g_gameRx{0};      // the last pad the game got (after the layer)
std::atomic<bool> g_padGlyphs{false};
DWORD g_frameTid = 0, g_gameTid = 0;

padlayer::Pad FromX(const XState& s) { padlayer::Pad p; p.connected = true; p.buttons = s.pad.buttons; p.lt = s.pad.lt; p.rt = s.pad.rt; p.lx = s.pad.lx; p.ly = s.pad.ly; p.rx = s.pad.rx; p.ry = s.pad.ry; return p; }
void ToX(const padlayer::Pad& p, XState* s) { s->pad.buttons = p.buttons; s->pad.lt = p.lt; s->pad.rt = p.rt; s->pad.lx = p.lx; s->pad.ly = p.ly; s->pad.rx = p.rx; s->pad.ry = p.ry; }
XGetStateFn Real() {
    if (g_gameReal) return g_gameReal;
    if (!g_ownTried) { g_ownTried = true; HMODULE x = LoadLibraryW(L"xinput1_4.dll"); if (!x) x = LoadLibraryW(L"xinput9_1_0.dll"); if (x) g_own = (XGetStateFn)GetProcAddress(x, "XInputGetState"); }
    return g_own;
}
// the pad of `slot` as Windows has it, or the test's stand-in on slot 0
DWORD ReadRaw(DWORD slot, XState* s, XGetStateFn real) {
    { Lock l; if (g_fakeOn && slot == 0) { memset(s, 0, sizeof *s); s->packet = g_fakePacket; ToX(g_fake, s); return ERROR_SUCCESS; } }
    return real ? real(slot, s) : ERROR_DEVICE_NOT_CONNECTED;
}
DWORD WINAPI PadGameGetState(DWORD slot, XState* s) {
    g_gameTid = GetCurrentThreadId(); g_gameCalls.fetch_add(1);
    if (!s) return ERROR_BAD_ARGUMENTS;
    DWORD r = ReadRaw(slot, s, g_gameReal);
    if (r != ERROR_SUCCESS) return r;
    padlayer::Pad raw = FromX(*s), game;
    const Cfg& c = GetCfg();
    if (c.padLeak && c.padLayer) { game = raw; game.buttons &= uint16_t(~padlayer::View); }      // mutant: the layer takes only View
    else { Lock l; game = g_layer.Filter(raw); }
    if (c.padLayer) game.buttons &= uint16_t(~padlayer::PadVoiceButton(c.padVoice));      // a stick click chosen for push to talk is the mod's
    ToX(game, s);
    g_gameButtons = game.buttons; g_gameLy = game.ly; g_gameRx = game.rx;
    return ERROR_SUCCESS;
}
}  // namespace

void* PadHookGetState(void* real) {
    g_gameReal = (XGetStateFn)real; Log("pad: the game resolved XInputGetState -> the co-op pad layer answers it");
    return (void*)PadGameGetState;
}
bool PadGlyphs() { return g_padGlyphs.load(); }
void PadNoteKeyboard() {
    if (SocialPadChatOpen()) return;      // Steam's floating keyboard types as keys: the pad is still the player's hand
    Lock l; g_glyphs.Key(); g_padGlyphs = g_glyphs.pad();
}
int VoiceModeNow() { const Cfg& c = GetCfg(); return padlayer::VoiceMode(c.voiceMode, PadGlyphs(), c.padLayer ? c.padVoice : 0); }
const char* PadVoiceButtonName() { int v = GetCfg().padVoice; return !GetCfg().padLayer ? "" : v == 1 ? "LS" : v == 2 ? "RS" : ""; }

void PadFrame() {
    g_frameTid = GetCurrentThreadId(); g_frames.fetch_add(1);
    XGetStateFn real = Real(); ULONGLONG now = GetTickCount64();
    XState s = {}; padlayer::Pad p;
    if (g_slot < 0 && now >= g_scanAt) {      // (an absent pad is looked for every 2 s: XInputGetState on an empty slot is slow)
        g_scanAt = now + 2000;
        for (DWORD i = 0; i < 4; ++i) if (ReadRaw(i, &s, real) == ERROR_SUCCESS) { g_slot = (int)i; break; }
    }
    if (g_slot >= 0) { if (ReadRaw((DWORD)g_slot, &s, real) == ERROR_SUCCESS) p = FromX(s); else g_slot = -1; }
    { Lock l; if (g_fakeOn && g_slot != 0) { g_slot = 0; p = g_fake; } }
    const Cfg& c = GetCfg(); padlayer::Out o;
    bool oskUp = SocialOskOpen();
    {
        Lock l; g_glyphs.Pad(p); g_padGlyphs = g_glyphs.pad();
        g_layer.enabled = c.padLayer; g_layer.osk = oskUp;
        o = g_layer.Step(p, (uint32_t)now); g_last = o; g_lastRaw = p;
    }
    if (!c.padLayer) return;
    if (o.ping) { SocialPadPing(); g_pings.fetch_add(1); }
    if (o.chat) {
        if (g_listWas) { PovPadPanel(false); g_listWas = false; }
        // Steam's floating keyboard when Steam can show it (Big Picture, Steam Deck), else ours; the line sits where the chat box is
        RECT cr = {0, 0, 1280, 720}; extern HWND g_gameWnd; if (g_gameWnd) GetClientRect(g_gameWnd, &cr);
        int W = cr.right - cr.left, H = cr.bottom - cr.top;
        if (SocialPadChat(false)) {
            g_chats.fetch_add(1);
            if (wsteam::FloatingKeyboard(24, H - 140, W / 2, 40)) { g_steamKb.fetch_add(1); SocialPadUseSteamKeyboard(); }
            else { Lock l; g_osk.Reset(p); g_ownKb.fetch_add(1); }
        }
        (void)W;
    }
    if (o.watchPrev || o.watchNext) { PovPadWatch(o.watchPrev ? -1 : 1); g_watches.fetch_add(1); }
    // the player list while View is held; the right stick moves its cursor, RT or A clicks
    if (o.list != g_listWas) { PovPadPanel(o.list); g_listWas = o.list; }
    if (o.list) {
        if (o.aimX || o.aimY) PovPanelMove(int(o.aimX * 14), int(o.aimY * 14));
        if (o.click) PovPanelClick();
    }
    // the wheels: open while View is held, the right stick aims, closing chooses
    if (o.wheel != g_wheelWas) {
        if (g_wheelWas != padlayer::Wheel::None) SocialPadWheel(g_wheelWas == padlayer::Wheel::Emote ? 1 : 2, false);
        if (o.wheel != padlayer::Wheel::None) { if (g_listWas) { PovPadPanel(false); g_listWas = false; } SocialPadWheel(o.wheel == padlayer::Wheel::Emote ? 1 : 2, true); g_wheels.fetch_add(1); }
        g_wheelWas = o.wheel;
    }
    if (o.wheel != padlayer::Wheel::None) SocialPadAim(o.aimX, o.aimY);
    if (o.emotePage) SocialPadEmotePage();
    if (o.guide != g_guideWas) { GuideKey(guide::Help, o.guide); g_guideWas = o.guide; }
    // our on-screen keyboard
    if (oskUp) {
        padlayer::OskOut k; { Lock l; k = g_osk.Step(p, (uint32_t)now, SocialInputEmpty()); }
        if (k.key != padlayer::OskKey::None) SocialOskKey((int)k.key, k.ch);
    }
    // push to talk on a stick click (pad_voice=ls|rs)
    bool talk = (p.buttons & padlayer::PadVoiceButton(c.padVoice)) != 0;
    if (talk != g_talkWas) { unsigned vk = c.keyVoice ? c.keyVoice : c.keyVoiceAlt; VoiceKey(vk, talk); g_talkWas = talk; }
}

void PadOskCursor(int* row, int* col) { Lock l; *row = g_osk.row; *col = g_osk.col; }

static void WriteStat() {
    std::wstring path = DataDir() + L"witness_pad.json", tmp = path + L".tmp";
    FILE* f = _wfopen(tmp.c_str(), L"w"); if (!f) return;
    padlayer::Out o; padlayer::Pad raw; bool fake; int slot; { Lock l; o = g_last; raw = g_lastRaw; fake = g_fakeOn; slot = g_slot; }
    char kb[16]; snprintf(kb, sizeof kb, "%s", SocialOskOpen() ? "own" : SocialPadChatOpen() ? "steam" : "none");
    fprintf(f, "{\"layer\":%s,\"fake\":%s,\"slot\":%d,\"raw_buttons\":%u,\"game_buttons\":%u,\"game_ly\":%d,\"game_rx\":%d,\"game_calls\":%u,\"frames\":%u,"
               "\"list\":%s,\"wheel\":%d,\"guide\":%s,\"glyphs\":\"%s\",\"voice_mode\":%d,\"keyboard\":\"%s\","
               "\"panel_open\":%s,\"social_busy\":%s,\"pings\":%u,\"chats\":%u,\"watches\":%u,\"wheels\":%u,\"steam_keyboard\":%u,\"own_keyboard\":%u,\"same_thread\":%s}\n",
            GetCfg().padLayer ? "true" : "false", fake ? "true" : "false", slot, (unsigned)raw.buttons, g_gameButtons.load(), g_gameLy.load(), g_gameRx.load(), g_gameCalls.load(), g_frames.load(),
            o.list ? "true" : "false", (int)o.wheel, o.guide ? "true" : "false", PadGlyphs() ? "pad" : "keys", VoiceModeNow(), kb,
            PovRosterOpen() ? "true" : "false", SocialBusy() ? "true" : "false", g_pings.load(), g_chats.load(), g_watches.load(), g_wheels.load(), g_steamKb.load(), g_ownKb.load(),
            (!g_gameTid || g_gameTid == g_frameTid) ? "true" : "false");
    fclose(f); MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}
void PadCommand(const char* line) {
    if (!strncmp(line, "padstat", 7)) { WriteStat(); return; }
    if (!strncmp(line, "pad off", 7)) { Lock l; g_fakeOn = false; g_fake = padlayer::Pad(); return; }
    unsigned b = 0; int lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
    if (sscanf(line, "pad %x %d %d %d %d %d %d", &b, &lx, &ly, &rx, &ry, &lt, &rt) >= 1) {
        Lock l; g_fakeOn = true; g_fake.connected = true; g_fake.buttons = (uint16_t)b;
        g_fake.lx = (int16_t)lx; g_fake.ly = (int16_t)ly; g_fake.rx = (int16_t)rx; g_fake.ry = (int16_t)ry; g_fake.lt = (uint8_t)lt; g_fake.rt = (uint8_t)rt; ++g_fakePacket;
    }
}
