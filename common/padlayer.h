#pragma once
// The shared co-op pad layer (co-op standard K4-K7, H6), game-free: XInput states in, co-op actions and the pad the game may see out.
// The proxy (proxy/pad.cpp) feeds it the pad every frame and the game's own XInputGetState answers from Filter(); the guide and the
// launcher's Controls page list kLines (H5: one table). padlayer_test.cpp drives it with recorded pad states (T0, with mutants).
//   Tap View                  ping what the crosshair is on
//   Hold View                 player list (right stick moves its cursor, RT or A clicks: mute, volume, watch)
//   View + D-pad up           emote wheel: the right stick picks, letting go of View plays (LB: next page)
//   View + D-pad down         chat: Steam's floating keyboard when Steam can show it, else one on screen (Menu sends)
//   View + D-pad left/right   watch the previous / next player
//   View + RB                 quick chat wheel (Ready, Wait for me, Over here, Come look at this, Thanks)
//   View + Y                  the quick guide while held
// While View is held, a co-op wheel is open or the on-screen keyboard is up, the game sees no pad at all (no button, no stick, no trigger),
// and a button pressed then stays the mod's until it is released. The Witness never uses View/Back in its own pad hints.
// Drawing stays on the keyboard: it aims with the camera, so a pad chord would have to leave the right stick to the game while holding View.
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
namespace padlayer {
enum : uint16_t { Up = 0x0001, Down = 0x0002, Left = 0x0004, Right = 0x0008, Start = 0x0010, View = 0x0020, LS = 0x0040, RS = 0x0080,
                  LB = 0x0100, RB = 0x0200, A = 0x1000, B = 0x2000, X = 0x4000, Y = 0x8000 };
struct Pad { bool connected = false; uint16_t buttons = 0; uint8_t lt = 0, rt = 0; int16_t lx = 0, ly = 0, rx = 0, ry = 0; };
constexpr uint32_t kHoldMs = 250;          // View held this long (or used with another button) is a hold, not a tap
constexpr int kStickDead = 8000;           // of 32767
constexpr int kTrigger = 40;               // of 255
inline bool StickOut(int16_t x, int16_t y) { return std::abs(int(x)) > kStickDead || std::abs(int(y)) > kStickDead; }

// The layer's lines, for every place that lists them (the quick guide, the launcher's Controls page); the same wording as the MDK2 mod.
struct Line { const char* buttons; const char* id; const char* text; };
inline const Line kLines[] = {
    {"Tap View", "ping", "Ping what you aim at: a marker for everyone"},
    {"Hold View", "list", "Player list (right stick + RT: mute, volume, watch)"},
    {"View + D-pad up", "emote", "Emote wheel: right stick picks, let go of View plays"},
    {"View + D-pad down", "chat", "Chat: Steam's keyboard, or one on screen (Menu sends)"},
    {"View + D-pad left / right", "watch", "Watch the previous / next player"},
    {"View + RB", "quick", "Quick chat: Ready, Wait for me, Over here, Thanks..."},
    {"View + Y", "guide", "This quick guide (while held)"},
    {"Stick click (Controls)", "voice", "Push to talk if you choose one (voice is open mic on a pad otherwise)"},
};
// The quick-chat wheel: the same phrases, in the same order, as the MDK2 mod.
inline const char* const kQuickChat[] = {"Ready", "Wait for me", "Over here", "Come look at this", "Thanks"};
constexpr int kQuickCount = 5;
// the slot a wheel of n slots points at (slot 0 straight up, clockwise), from an aim in screen pixels (y down); -1 in the middle
inline int WheelSlot(float x, float y, int n, float dead = 40) {
    if (x * x + y * y < dead * dead || n <= 0) return -1;
    float a = 0; { const float pi = 3.14159265f; a = std::atan2(x, -y); if (a < 0) a += 2 * pi; a = a / (2 * pi) * n + 0.5f; }
    return int(a) % n;
}

enum class Wheel { None, Emote, Quick };
struct Out {
    bool ping = false;          // this frame: ping what the crosshair is on
    bool chat = false;          // this frame: open the chat line and a keyboard
    bool watchPrev = false, watchNext = false;      // this frame
    bool list = false;          // the player list is held open
    Wheel wheel = Wheel::None;  // a wheel is open (closing it chooses: closed = this frame's wheel None after a frame with one)
    bool guide = false;         // the quick guide is held
    bool emotePage = false;     // this frame: the emote wheel's next page (LB)
    bool click = false;         // this frame: click in the player list (RT or A)
    float aimX = 0, aimY = 0;   // the right stick, -1..1 with y down (screen), 0 inside the dead zone
};

class Layer {
public:
    bool enabled = true;        // pad_layer=0: the game gets the whole pad, the mod none of it
    bool osk = false;           // the proxy's on-screen keyboard is up: the pad is the mod's
    Out Step(const Pad& p, uint32_t nowMs) {
        Out o; uint16_t b = p.connected ? p.buttons : 0, edge = uint16_t(b & ~prev_);
        bool rtNow = p.connected && p.rt > kTrigger, rtEdge = rtNow && !rtPrev_; rtPrev_ = rtNow;
        if (StickOut(p.rx, p.ry)) { o.aimX = p.rx / 32767.f; o.aimY = -p.ry / 32767.f; }
        if (!enabled) { down_ = false; wheel_ = Wheel::None; prev_ = b; owned_ = 0; return o; }
        if (b & View) {
            if (!down_) { down_ = true; since_ = nowMs; used_ = false; wheel_ = Wheel::None; }
            if (edge & Up) { wheel_ = Wheel::Emote; used_ = true; }
            if (edge & RB) { wheel_ = Wheel::Quick; used_ = true; }
            if (edge & Down) { o.chat = true; used_ = true; wheel_ = Wheel::None; }
            if (edge & Left) { o.watchPrev = true; used_ = true; }
            if (edge & Right) { o.watchNext = true; used_ = true; }
            if (b & Y) { o.guide = true; used_ = true; }
            if ((edge & LB) && wheel_ == Wheel::Emote) o.emotePage = true;
            o.wheel = wheel_;
            if (wheel_ == Wheel::None && !o.chat && !o.guide && (used_ || nowMs - since_ >= kHoldMs)) {
                o.list = true;
                if (rtEdge || (edge & A)) { o.click = true; used_ = true; }
            }
        } else if (down_) {      // let go: a short press with nothing else is the ping; an open wheel closes (and chooses)
            if (!used_ && nowMs - since_ < kHoldMs) o.ping = true;
            down_ = false; wheel_ = Wheel::None;
        }
        // what the game may see: nothing while the layer is busy, and a button pressed then stays the mod's until it is released
        bool busy = (b & View) || osk;
        owned_ = busy ? b : uint16_t(owned_ & b);
        busy_ = busy; prev_ = b;
        return o;
    }
    // the pad as the game gets it this frame (the game's own XInputGetState answers with this)
    Pad Filter(const Pad& raw) const {
        Pad g = raw; if (!enabled) return g;
#ifdef PAD_MUT_LEAK
        g.buttons &= uint16_t(~View);      // mutant: only View is taken, the D-pad and the face buttons still reach the game while View is held
        return g;
#else
        bool busy = busy_ || (raw.buttons & View) || osk;
        if (busy) { g.buttons = 0; g.lt = g.rt = 0; g.lx = g.ly = g.rx = g.ry = 0; return g; }
        g.buttons &= uint16_t(~(owned_ | View));
        return g;
#endif
    }
    bool held() const { return down_; }
private:
    bool down_ = false, used_ = false, busy_ = false, rtPrev_ = false; uint32_t since_ = 0; uint16_t prev_ = 0, owned_ = 0; Wheel wheel_ = Wheel::None;
};

// K7: which hints to show. The pad's buttons from the first pad input (a button pressed, a stick or trigger moved), the keys again from the
// first key or mouse button. Mouse motion alone does not switch (a resting hand on a mouse must not flicker the hints).
inline bool PadActive(const Pad& p, uint16_t prevButtons) {
    if (!p.connected) return false;
    return (p.buttons & ~prevButtons) || StickOut(p.lx, p.ly) || StickOut(p.rx, p.ry) || p.lt > kTrigger || p.rt > kTrigger;
}
class Glyphs {
public:
    void Pad(const padlayer::Pad& p) { if (PadActive(p, prev_)) pad_ = true; prev_ = p.connected ? p.buttons : 0; }
    void Key() {
#ifndef PAD_MUT_STUCK_GLYPHS
        pad_ = false;
#endif
    }
    bool pad() const { return pad_; }
private:
    bool pad_ = false; uint16_t prev_ = 0;
};
inline const char* Hint(bool pad, const char* keys, const char* buttons) { return pad && buttons && buttons[0] ? buttons : keys; }

// Voice on a pad (K5): voice_mode=auto is push to talk after keyboard input and open mic (with the voice gate) after pad input, unless the player
// chose a stick click for push to talk (pad_voice=ls|rs), which then talks. Returns the mode in force: 0 off, 1 open, 2 push to talk.
inline int VoiceMode(int cfgMode, bool padLast, int padVoice) {
    if (cfgMode != 3) return cfgMode;
    return padLast && !padVoice ? 1 : 2;
}
inline uint16_t PadVoiceButton(int padVoice) { return padVoice == 1 ? LS : padVoice == 2 ? RS : 0; }

// The on-screen keyboard (K6) when Steam's cannot be shown: a grid; D-pad or left stick moves, A types, B deletes (on an empty line: closes),
// Y a space, Menu sends.
inline const char* const kOskRows[] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL'", "ZXCVBNM,.?"};
constexpr int kOskRowsN = 4, kOskCols = 10;
enum class OskKey { None, Char, Space, Back, Send, Close };
struct OskOut { OskKey key = OskKey::None; char ch = 0; };
class Osk {
public:
    int row = 1, col = 0;
    void Reset(const Pad& now) { row = 1; col = 0; prev_ = Dirs(now); dir_ = -1; if (prev_ & (Up | Down | Left | Right)) dir_ = -2; }      // what is held when it opens (View + D-pad down) does not type or move
    // one frame; `emptyLine`: B closes instead of deleting
    OskOut Step(const Pad& p, uint32_t nowMs, bool emptyLine) {
        uint16_t b = Dirs(p);
        uint16_t edge = uint16_t(b & ~prev_); prev_ = b; OskOut o;
        int d = (b & Up) ? 0 : (b & Down) ? 1 : (b & Left) ? 2 : (b & Right) ? 3 : -1;
        bool move = false;
        if (dir_ == -2) { if (d < 0) dir_ = -1; }      // (the direction held at the start must be let go first)
        else if (d != dir_) { dir_ = d; since_ = last_ = nowMs; move = d >= 0; }
        else if (d >= 0 && nowMs - since_ >= 400 && nowMs - last_ >= 120) { last_ = nowMs; move = true; }
        if (move) {
            if (d == 0) row = (row + kOskRowsN - 1) % kOskRowsN; else if (d == 1) row = (row + 1) % kOskRowsN;
            else if (d == 2) col = (col + kOskCols - 1) % kOskCols; else col = (col + 1) % kOskCols;
        }
        if (edge & A) { o.key = OskKey::Char; o.ch = kOskRows[row][col]; if (o.ch >= 'A' && o.ch <= 'Z') o.ch = char(o.ch - 'A' + 'a'); }
        else if (edge & Y) o.key = OskKey::Space;
        else if (edge & B) o.key = emptyLine ? OskKey::Close : OskKey::Back;
        else if (edge & Start) o.key = OskKey::Send;
        return o;
    }
private:
    static uint16_t Dirs(const Pad& p) {
        uint16_t b = p.connected ? p.buttons : 0;
        if (p.connected) { if (p.lx < -16000) b |= Left; if (p.lx > 16000) b |= Right; if (p.ly > 16000) b |= Up; if (p.ly < -16000) b |= Down; }
        return b;
    }
    uint16_t prev_ = 0; int dir_ = -1; uint32_t since_ = 0, last_ = 0;
};
}  // namespace padlayer
