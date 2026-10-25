// See social.h.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>
#include "social.h"
#include "coop.h"
#include "config.h"
#include "ui.h"
#include "input.h"
#include "model_gpu.h"
#include "playercolor.h"
#include <strings.h>

void Log(const char* msg);

namespace {
const float kVFovDeg = 60.0f;                       // the game's vertical field of view (as overlay.cpp)
const ULONGLONG kChatShowMs = 12000, kBubbleMs = 7000, kMarkMs = 25000;
const size_t kMaxInput = 200;

CRITICAL_SECTION* Lk() { static struct Init { CRITICAL_SECTION cs; Init() { InitializeCriticalSection(&cs); } } init; return &init.cs; }
struct Lock { Lock() { EnterCriticalSection(Lk()); } ~Lock() { LeaveCriticalSection(Lk()); } };

struct ChatEntry { unsigned from; std::string name, text; ULONGLONG at; };
struct Bubble { std::string text; ULONGLONG until = 0; };
struct Mark { unsigned from; uint8_t kind; float pos[3]; ULONGLONG at; };
std::deque<ChatEntry> g_log; std::map<unsigned, Bubble> g_bubbles; std::vector<Mark> g_marks;
bool g_open = false; std::string g_input;
bool g_pingReq = false, g_pingAsked = false; ULONGLONG g_pingAt = 0;
unsigned g_chatSeen = 0, g_marksSeen = 0;
std::string g_myChar; unsigned g_emoteSeq = 0; int g_emote = 0; ULONGLONG g_emoteAt = 0;          // my announced character; my emote
bool g_wheel = false; float g_wx = 0, g_wy = 0; bool g_pick = false; int g_pickSel = 0;
struct RemoteEmote { bool seen = false; uint8_t seq = 0; int id = 0; ULONGLONG start = 0; };
std::map<unsigned, RemoteEmote> g_remoteEmotes;
const ULONGLONG kEmoteMs = 2600;
const char* kEmoteNames[8] = {"Wave", "Dance", "Cheer", "Yes", "No", "Bow", "Spin", "Shrug"};
int WheelSlot() { float m = sqrtf(g_wx * g_wx + g_wy * g_wy); if (m < 30) return -1; float a = atan2f(g_wy, g_wx) + 1.5707963f; if (a < 0) a += 6.2831853f; return int(a / 0.7853982f + 0.5f) % 8; }
void TriggerEmote(int id, ULONGLONG now) { if (id < 1 || id > 8) return; g_emote = id; g_emoteAt = now; ++g_emoteSeq; }
// ---- free drawing: strokes of points in the world, put on the surface under the pen (the scene depth at the pen's pixel), seen by everybody
struct Stroke { unsigned from; uint32_t id; uint8_t color; std::vector<float> pts; };
std::deque<Stroke> g_strokes;      // everybody's, mine included
bool g_canvas = false, g_pen = false, g_shift = false; float g_ox = 0, g_oy = 0;      // the canvas: the mouse moves a pen (g_ox, g_oy: pixels from the middle of the screen); hold the draw key to put ink down
uint8_t g_penColor = 0; uint32_t g_strokeNext = 1, g_curId = 0; bool g_begun = false; std::vector<float> g_unsent; ULONGLONG g_sentAt = 0;
bool g_penAsked = false; ULONGLONG g_penAskAt = 0; float g_reqCam[3], g_reqAng[2], g_reqF[2]; float g_lastDist = 4.0f;
float g_camNow[3] = {}, g_angNow[2] = {}, g_scrW = 1920, g_scrH = 1080; unsigned g_segsDrawn = 0;
const size_t kMaxPlayerPts = 4000; const int kInks = 8;
void InkColor(unsigned from, uint8_t idx, float* c) {
    static const float p[kInks][3] = {{0, 0, 0}, {1, 1, 1}, {1.0f, 0.25f, 0.25f}, {1.0f, 0.62f, 0.15f}, {1.0f, 0.92f, 0.2f}, {0.35f, 0.95f, 0.4f}, {0.3f, 0.85f, 1.0f}, {0.95f, 0.4f, 0.95f}};
    if (idx % kInks == 0) PlayerColor(from, c); else memcpy(c, p[idx % kInks], 12);
}
void TrimPlayer(unsigned from) {      // a player's oldest strokes go when they hold too many points
    for (;;) { size_t n = 0; int first = -1; for (size_t i = 0; i < g_strokes.size(); ++i) if (g_strokes[i].from == from) { n += g_strokes[i].pts.size() / 3; if (first < 0) first = int(i); }
        if (n <= kMaxPlayerPts || first < 0) return; g_strokes.erase(g_strokes.begin() + first); }
}
Stroke* FindStroke(unsigned from, uint32_t id) { for (auto it = g_strokes.rbegin(); it != g_strokes.rend(); ++it) if (it->from == from && it->id == id) return &*it; return nullptr; }
void ApplyStroke(const witness::StrokeMsg& s) {
    if (s.clear) { g_strokes.erase(std::remove_if(g_strokes.begin(), g_strokes.end(), [&](const Stroke& t) { return t.from == s.from && (s.id == 0 || t.id == s.id); }), g_strokes.end()); return; }
    Stroke* t = s.begin ? nullptr : FindStroke(s.from, s.id);
    if (!t) { g_strokes.push_back({s.from, s.id, s.color, {}}); t = &g_strokes.back(); }
    t->pts.insert(t->pts.end(), s.pts.begin(), s.pts.end()); TrimPlayer(s.from);
}
void FlushStroke(bool end, ULONGLONG now) {      // the points not yet sent go out in chunks; `end` marks the last chunk (a bare end when nothing is left)
    g_sentAt = now; auto* core = CoopCore(); size_t total = g_unsent.size() / 3;
    if (!total && !(end && g_begun)) return;
    do {
        size_t n = std::min<size_t>(total, witness::kMaxStrokePoints); bool last = end && n == total;
        if (core && !GetCfg().noDrawSend) core->sendStroke(g_curId, !g_begun, last, g_penColor, g_unsent.data(), n);
        g_begun = true; g_unsent.erase(g_unsent.begin(), g_unsent.begin() + n * 3); total -= n;
    } while (total > 0);
}
void PenUp(ULONGLONG now) { if (!g_pen) return; g_pen = false; FlushStroke(true, now); g_unsent.clear(); g_begun = false; }
void PenDown(ULONGLONG now) { if (g_pen) return; g_pen = true; g_curId = g_strokeNext++; g_begun = false; g_unsent.clear(); g_penAsked = false; (void)now; }
void ClearMine() { g_strokes.erase(std::remove_if(g_strokes.begin(), g_strokes.end(), [](const Stroke& t) { return t.from == CoopMyId(); }), g_strokes.end()); if (auto* c = CoopCore()) if (!GetCfg().noDrawSend) c->sendStrokeClear(0); }
void UndoMine() {
    for (auto it = g_strokes.rbegin(); it != g_strokes.rend(); ++it) if (it->from == CoopMyId()) { uint32_t id = it->id; g_strokes.erase(std::next(it).base()); if (auto* c = CoopCore()) if (!GetCfg().noDrawSend) c->sendStrokeClear(id); return; }
}
void AddPenPoint(const float p[3], ULONGLONG now) {
    Stroke* t = FindStroke(CoopMyId(), g_curId);
    if (!t) { g_strokes.push_back({CoopMyId(), g_curId, g_penColor, {}}); t = &g_strokes.back(); }
    if (t->pts.size() >= 3) { const float* q = &t->pts[t->pts.size() - 3]; float d2 = (p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]); if (d2 < 0.04f * 0.04f) return; }
    t->pts.insert(t->pts.end(), p, p + 3); g_unsent.insert(g_unsent.end(), p, p + 3); TrimPlayer(CoopMyId());
    if (g_unsent.size() >= 36 || now - g_sentAt > 120) FlushStroke(false, now);
}
unsigned g_dotsDrawn = 0, g_bubblesDrawn = 0, g_marksDrawn = 0, g_arrowsDrawn = 0;      // what the last frame drew (for the tests)

std::string NameOf(unsigned id) {
    if (id == CoopMyId()) return CoopMyName();
    for (const auto& r : CoopGetRemotes()) if (r.id == id && r.name[0]) return r.name;
    return "player " + std::to_string(id);
}
void AddChat(unsigned from, const std::string& text, ULONGLONG now) {
    g_log.push_back({from, NameOf(from), text, now}); while (g_log.size() > 80) g_log.pop_front();
    if (from != CoopMyId()) { Bubble b; b.text = text; b.until = now + kBubbleMs; g_bubbles[from] = b; }
    ++g_chatSeen;
}
void AddMark(unsigned from, uint8_t kind, const float pos[3], ULONGLONG now) {
    Mark m; m.from = from; m.kind = kind; memcpy(m.pos, pos, 12); m.at = now;
    int mine = 0; for (const auto& o : g_marks) if (o.from == from) ++mine;
    for (size_t i = 0; mine >= 4 && i < g_marks.size(); ++i) if (g_marks[i].from == from) { g_marks.erase(g_marks.begin() + i); --mine; break; }
    g_marks.push_back(m); ++g_marksSeen;
}
void SendInput(ULONGLONG now) {
    if (g_input.empty()) return;
    if (auto* core = CoopCore()) core->sendChat(g_input);
    AddChat(CoopMyId(), g_input, now); g_input.clear();
}
// camera space of a world point: r right, u up, f forward
void CamSpace(const float* cam, float th, float ph, const float* w, float* r, float* u, float* f) {
    float d[3] = {w[0] - cam[0], w[1] - cam[1], w[2] - cam[2]};
    float fwd[3] = {cosf(ph) * cosf(th), cosf(ph) * sinf(th), sinf(ph)}, right[3] = {sinf(th), -cosf(th), 0}, up[3] = {-sinf(ph) * cosf(th), -sinf(ph) * sinf(th), cosf(ph)};
    *f = d[0] * fwd[0] + d[1] * fwd[1] + d[2] * fwd[2]; *r = d[0] * right[0] + d[1] * right[1] + d[2] * right[2]; *u = d[0] * up[0] + d[1] * up[1] + d[2] * up[2];
}
bool ToScreen(const float* cam, float th, float ph, const float* w, float W, float H, float* sx, float* sy) {
    float r, u, f; CamSpace(cam, th, ph, w, &r, &u, &f); if (f < 0.1f) return false;
    float tv = tanf(kVFovDeg * 3.14159265f / 360.0f), th_ = tv * (W / H);
    float nx = (r / f) / th_, ny = (u / f) / tv; *sx = (nx * 0.5f + 0.5f) * W; *sy = (0.5f - ny * 0.5f) * H; return true;
}
std::vector<std::string> Wrap(const std::string& s, size_t width, size_t maxLines) {
    std::vector<std::string> out; std::string cur;
    size_t i = 0;
    while (i < s.size() && out.size() < maxLines) {
        size_t j = i; std::string word; while (j < s.size() && s[j] != ' ') word.push_back(s[j++]); while (j < s.size() && s[j] == ' ') ++j; i = j;
        while (word.size() > width) { if (!cur.empty()) { out.push_back(cur); cur.clear(); if (out.size() >= maxLines) return out; } out.push_back(word.substr(0, width)); word = word.substr(width); if (out.size() >= maxLines) return out; }
        if (!cur.empty() && cur.size() + 1 + word.size() > width) { out.push_back(cur); cur.clear(); if (out.size() >= maxLines) return out; }
        if (!cur.empty()) cur.push_back(' '); cur += word;
    }
    if (!cur.empty() && out.size() < maxLines) out.push_back(cur);
    return out;
}
void Bubbled(float cx, float bottom, const std::vector<std::string>& lines, bool dots, const float col[3], float k, ULONGLONG now) {
    float lh = UiLineHeight(0.9f * k), pad = 7 * k; float w = 0;
    if (dots) w = 46 * k; else for (const auto& l : lines) w = std::max(w, UiTextWidth(l.c_str(), 0.9f * k));
    float h = dots ? 22 * k : lh * lines.size(); float x = cx - w / 2 - pad, y = bottom - h - 2 * pad;
    UiRect(x - 1, y - 1, w + 2 * pad + 2, h + 2 * pad + 2, Rgba(col[0], col[1], col[2], 0.9f)); UiRect(x, y, w + 2 * pad, h + 2 * pad, Rgba(0.06f, 0.07f, 0.1f, 0.88f));
    UiTri(cx, bottom, 7 * k, 1.5707963f, Rgba(col[0], col[1], col[2], 0.9f));
    if (dots) { for (int i = 0; i < 3; ++i) { float ph = float(now % 1200) / 1200.0f * 6.2831853f - i * 0.9f; float bob = std::max(0.0f, sinf(ph)) * 6 * k; UiRect(cx - 14 * k + i * 14 * k - 3 * k, y + pad + 8 * k - bob, 6 * k, 6 * k, Rgba(col[0], col[1], col[2], 1)); } }
    else for (size_t i = 0; i < lines.size(); ++i) UiText(x + pad, y + pad + lh * i, lines[i].c_str(), Rgba(0.95f, 0.95f, 0.95f, 1), 0.9f * k);
}
}  // namespace

bool SocialCapturing() { Lock l; return g_open || g_pick; }
bool SocialMouseCaptured() { Lock l; return g_open || g_pick || g_wheel || g_canvas; }
void SocialRawMove(int dx, int dy) { Lock l; if (g_canvas && !g_wheel && !g_open && !g_pick) { g_ox += dx * 1.6f; g_oy += dy * 1.6f; return; } if (g_wheel) { g_wx += float(dx); g_wy += float(dy); float m = sqrtf(g_wx * g_wx + g_wy * g_wy); if (m > 140) { g_wx *= 140 / m; g_wy *= 140 / m; } } }
const char* SocialEmoteName(int e) { return e >= 1 && e <= 8 ? kEmoteNames[e - 1] : ""; }
bool SocialEmoteOf(unsigned id, int* emote, float* seconds) {
    Lock l; auto it = g_remoteEmotes.find(id); if (it == g_remoteEmotes.end() || !it->second.id) return false;
    ULONGLONG age = GetTickCount64() - it->second.start; if (age >= kEmoteMs) return false; *emote = it->second.id; *seconds = float(age) / 1000.0f; return true;
}
bool SocialKey(unsigned vk, bool down) {
    const Cfg& c = GetCfg(); Lock l; ULONGLONG now = GetTickCount64();
    if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) { g_shift = down; return false; }
    if (!down && vk == c.keyDraw) { bool was = g_pen; PenUp(now); return was; }
    if (g_open) {
        if (!down) return false;
        if (vk == VK_ESCAPE) { g_open = false; g_input.clear(); return true; }
        if (vk == VK_RETURN) { SendInput(now); g_open = false; return true; }
        if (vk == VK_BACK) { if (!g_input.empty()) g_input.pop_back(); return true; }
        return true;      // every other key belongs to the box (its characters arrive as WM_CHAR)
    }
    if (g_wheel) {      // the emote wheel: the key's release picks the slot the mouse points to (none: cancel)
        if (vk == c.keyEmote && !down) { int sl = WheelSlot(); g_wheel = false; if (sl >= 0) TriggerEmote(sl + 1, now); return false; }
        if (vk == VK_ESCAPE && down) { g_wheel = false; return true; }
        return false;
    }
    if (g_pick) {       // the character list
        if (!down) return false;
        int n = ModelGpuCount() + 1;      // (default) and every model
        if (vk == VK_UP) { g_pickSel = (g_pickSel + n - 1) % n; return true; }
        if (vk == VK_DOWN) { g_pickSel = (g_pickSel + 1) % n; return true; }
        if (vk == VK_RETURN) { g_myChar = g_pickSel == 0 ? "" : ModelGpuName(g_pickSel - 1); g_pick = false; return true; }
        if (vk == VK_ESCAPE || vk == c.keyChar) { g_pick = false; return true; }
        return true;
    }
    if (!down || GamePaused()) return false;
    if (vk == c.keyEmote) { g_wheel = true; g_wx = g_wy = 0; return true; }
    if (vk == c.keyChar) { g_pick = true; g_pickSel = 0; for (int i = 0; i < ModelGpuCount(); ++i) if (!_stricmp(ModelGpuName(i), g_myChar.c_str())) g_pickSel = i + 1; return true; }
    if (vk == c.keyChat && !c.noChatUi) { g_open = true; g_input.clear(); return true; }
    if (vk == c.keyDraw) { if (g_shift) ClearMine(); else PenDown(now); return true; }
    if (vk == c.keyCanvas) { g_canvas = !g_canvas; g_ox = g_oy = 0; if (!g_canvas) PenUp(now); return true; }
    if (vk == c.keyColor && (g_canvas || g_pen)) { g_penColor = uint8_t((g_penColor + 1) % kInks); return true; }
    if (vk == VK_BACK && g_canvas) { UndoMine(); return true; }
    if (vk == c.keyPing) { g_pingReq = true; g_pingAsked = false; g_pingAt = now; return true; }
    return false;
}
bool SocialChar(unsigned ch) {
    Lock l; if (!g_open) return false;
    if (ch >= 32 && ch < 127 && g_input.size() < kMaxInput) g_input.push_back(char(ch));
    return true;
}

void SocialFrame(float W, float H, const float* cam, const float* ang) {
    ULONGLONG now = GetTickCount64(); witness::SyncCore* core = CoopCore(); const Cfg& cfg = GetCfg();
    Lock l;
    if (core) {
        for (const auto& m : core->takeChat()) AddChat(m.from, m.text, now);
        for (const auto& m : core->takeMarks()) AddMark(m.from, m.kind, m.pos, now);
        for (const auto& s : core->takeStrokes()) ApplyStroke(s);
        witness::Presence p; p.typing = g_open && !cfg.noTypingDots; p.character = cfg.noCharacterAnnounce ? "" : (g_myChar.empty() ? cfg.character : g_myChar);
        if (g_emote && now - g_emoteAt >= kEmoteMs) g_emote = 0;
        if (!cfg.noEmoteSend) { p.emote = uint8_t(g_emote); p.emoteSeq = uint8_t(g_emoteSeq); }
        core->setPresence(p);
    }
    g_marks.erase(std::remove_if(g_marks.begin(), g_marks.end(), [&](const Mark& m) { return now - m.at > kMarkMs; }), g_marks.end());
    for (auto it = g_bubbles.begin(); it != g_bubbles.end();) { if (it->second.until < now) it = g_bubbles.erase(it); else ++it; }
    g_dotsDrawn = g_bubblesDrawn = g_marksDrawn = g_arrowsDrawn = 0;
    if (GamePaused()) return;
    float k = std::max(1.0f, H / 1080.0f);
    memcpy(g_camNow, cam, 12); g_angNow[0] = ang[0]; g_angNow[1] = ang[1]; g_scrW = W; g_scrH = H;
    g_ox = std::min(std::max(g_ox, -W / 2 + 6), W / 2 - 6); g_oy = std::min(std::max(g_oy, -H / 2 + 6), H / 2 - 6);
    if (g_pen && !g_open) {      // the pen is down: ask for the depth under it; the answer (next frame) puts one point of ink on that surface, along the ray of the frame the question was asked in
        float d = 0;
        if (g_penAsked && ModelGpuTakeCenterDepth(&d)) {
            g_penAsked = false; float th = g_reqAng[0], ph = g_reqAng[1];
            if (d < 0.05f || d > 300.0f) d = std::min(std::max(g_lastDist, 3.0f), 60.0f); else g_lastDist = d;
            if (cfg.noDrawDepth) d = 4.0f;
            float tv = tanf(kVFovDeg * 3.14159265f / 360.0f), tw = tv * (W / H), rr = (g_reqF[0] * 2 - 1) * tw, uu = (1 - g_reqF[1] * 2) * tv;
            float fwd[3] = {cosf(ph) * cosf(th), cosf(ph) * sinf(th), sinf(ph)}, rt[3] = {sinf(th), -cosf(th), 0}, up[3] = {-sinf(ph) * cosf(th), -sinf(ph) * sinf(th), cosf(ph)}, p[3];
            for (int i = 0; i < 3; ++i) p[i] = g_reqCam[i] + (fwd[i] + rt[i] * rr + up[i] * uu) * d;
            AddPenPoint(p, now);
        } else if (g_penAsked && now - g_penAskAt > 400) g_penAsked = false;
        if (!g_penAsked) {
            g_reqF[0] = (W / 2 + g_ox) / W; g_reqF[1] = (H / 2 + g_oy) / H; memcpy(g_reqCam, cam, 12); g_reqAng[0] = ang[0]; g_reqAng[1] = ang[1];
            ModelGpuRequestDepthAt(g_reqF[0], g_reqF[1]); g_penAsked = true; g_penAskAt = now;
        }
    }
    // a location ping asked for: the next frame answers with the scene depth under the cross-hair
    if (g_pingReq) {
        if (!g_pingAsked) { ModelGpuRequestCenterDepth(); g_pingAsked = true; }
        float d = 0; bool got = ModelGpuTakeCenterDepth(&d);
        if (got || now - g_pingAt > 700) {
            if (!got || d < 0) d = 10.0f; d = std::min(d, 250.0f);
            float th = ang[0], ph = ang[1], pos[3] = {cam[0] + cosf(ph) * cosf(th) * d, cam[1] + cosf(ph) * sinf(th) * d, cam[2] + sinf(ph) * d};
            AddMark(CoopMyId(), 0, pos, now); if (core) core->sendMark(0, pos); g_pingReq = false;
        }
    }
    std::vector<RemotePose> rem = CoopGetRemotes();
    for (const auto& rp : rem) {      // a change of a player's emote counter starts that emote now (the first sight only records it)
        RemoteEmote& re = g_remoteEmotes[rp.id];
        if (!re.seen) { re.seen = true; re.seq = rp.emoteSeq; }
        else if (rp.emoteSeq != re.seq) { re.seq = rp.emoteSeq; if (rp.emote) { re.id = rp.emote; re.start = now; } }
    }
    // bubbles over the players: three dots while typing, else the last chat line for a few seconds
    for (const auto& rp : rem) {
        if (rp.ageMs > 2000) continue;
        float head[3] = {rp.pos[0], rp.pos[1], rp.pos[2] + 0.38f}, sx, sy;
        if (!ToScreen(cam, ang[0], ang[1], head, W, H, &sx, &sy)) continue;
        float col[3]; PlayerColor(rp.id, col); float dist = sqrtf((rp.pos[0] - cam[0]) * (rp.pos[0] - cam[0]) + (rp.pos[1] - cam[1]) * (rp.pos[1] - cam[1]) + (rp.pos[2] - cam[2]) * (rp.pos[2] - cam[2]));
        float kk = k * std::min(1.0f, std::max(0.7f, 10.0f / std::max(dist, 1.0f))); float bottom = sy - UiLineHeight(kk) - 14 * kk;
        auto bi = g_bubbles.find(rp.id);
        if (rp.typing) { Bubbled(sx, bottom, {}, true, col, kk, now); ++g_dotsDrawn; }
        else if (bi != g_bubbles.end()) { Bubbled(sx, bottom, Wrap(bi->second.text, 28, 4), false, col, kk, now); ++g_bubblesDrawn; }
    }
    for (const auto& rp : rem) {      // the name of the emote a player is playing, over their head
        auto it = g_remoteEmotes.find(rp.id); if (it == g_remoteEmotes.end() || !it->second.id || now - it->second.start >= kEmoteMs || rp.ageMs > 2000) continue;
        float head[3] = {rp.pos[0], rp.pos[1], rp.pos[2] + 0.8f}, sx, sy; if (!ToScreen(cam, ang[0], ang[1], head, W, H, &sx, &sy)) continue;
        float col[3]; PlayerColor(rp.id, col); char b[40]; snprintf(b, sizeof b, "~ %s ~", kEmoteNames[it->second.id - 1]); float tw = UiTextWidth(b, 1.0f * k);
        float up = float(now - it->second.start) / 1000.0f * 14 * k; UiText(sx - tw / 2, sy - 26 * k - up, b, Rgba(col[0], col[1], col[2], 1), 1.0f * k);
    }
    // pings: a marker over the point with the sender's name and the distance; off screen, an arrow at the edge of the screen pointing the way
    if (!cfg.noPingMarks) for (const auto& m : g_marks) {
        float col[3]; PlayerColor(m.from, col); float r, u, f; CamSpace(cam, ang[0], ang[1], m.pos, &r, &u, &f);
        float dx = m.pos[0] - cam[0], dy = m.pos[1] - cam[1], dz = m.pos[2] - cam[2], dist = sqrtf(dx * dx + dy * dy + dz * dz);
        float age = float(now - m.at) / 1000.0f, pulse = 0.75f + 0.25f * sinf(age * 5.0f);
        char lab[80]; snprintf(lab, sizeof lab, "%s  %.0f m", NameOf(m.from).c_str(), dist);
        float sx, sy;
        if (ToScreen(cam, ang[0], ang[1], m.pos, W, H, &sx, &sy) && sx > 0 && sx < W && sy > 0 && sy < H) {
            ++g_marksDrawn; UiRect(sx - 1.5f * k, sy - 70 * k, 3 * k, 70 * k, Rgba(col[0], col[1], col[2], 0.8f * pulse));      // a pole standing on the spot
            UiTri(sx, sy - 4 * k, 20 * k, 1.5707963f, Rgba(col[0], col[1], col[2], 1)); UiTri(sx, sy - 4 * k, 12 * k, 1.5707963f, Rgba(0, 0, 0, 0.6f));
            float tw = UiTextWidth(lab, 0.9f * k), th = UiLineHeight(0.9f * k);
            UiRect(sx - tw / 2 - 6 * k, sy - 70 * k - th - 10 * k, tw + 12 * k, th + 8 * k, Rgba(0, 0, 0, 0.6f)); UiText(sx - tw / 2, sy - 70 * k - th - 6 * k, lab, Rgba(col[0], col[1], col[2], 1), 0.9f * k);
        } else {      // the direction on screen, from the middle: the pointer sits on a rounded margin
            float ang2 = atan2f(-u, r); float cx = W / 2, cy = H / 2, mx = W / 2 - 50 * k, my = H / 2 - 50 * k;
            float ex = cosf(ang2), ey = sinf(ang2), t = std::min(fabsf(ex) > 1e-4f ? mx / fabsf(ex) : 1e9f, fabsf(ey) > 1e-4f ? my / fabsf(ey) : 1e9f);
            float px = cx + ex * t, py = cy + ey * t;
            ++g_arrowsDrawn; UiTri(px, py, 22 * k, ang2, Rgba(col[0], col[1], col[2], 1)); float tw = UiTextWidth(lab, 0.8f * k);
            float lx = std::min(std::max(px - tw / 2, 8 * k), W - tw - 8 * k), ly = std::min(std::max(py + (ey > 0 ? -34 * k : 20 * k), 8 * k), H - 30 * k);
            UiRect(lx - 4 * k, ly - 2 * k, tw + 8 * k, UiLineHeight(0.8f * k) + 4 * k, Rgba(0, 0, 0, 0.6f)); UiText(lx, ly, lab, Rgba(col[0], col[1], col[2], 1), 0.8f * k);
        }
    }
    g_segsDrawn = 0;      // the ink: every stroke as a glowing line through its points, thinner the farther away; nothing behind the camera
    if (!cfg.noDrawDraw) for (const auto& s : g_strokes) {
        float col[3]; InkColor(s.from, s.color, col); bool havePrev = false; float px = 0, py = 0, pd = 0;
        for (size_t i = 0; i + 2 < s.pts.size(); i += 3) {
            float sx, sy; const float* w = &s.pts[i];
            bool ok = ToScreen(cam, ang[0], ang[1], w, W, H, &sx, &sy); float dx = w[0] - cam[0], dy = w[1] - cam[1], dz = w[2] - cam[2], dist = sqrtf(dx * dx + dy * dy + dz * dz);
            if (ok && havePrev && dist < 160 && pd < 160 && fabsf(sx - px) < W * 2 && fabsf(sy - py) < H * 2) {
                float t = std::min(std::max(5.0f * k * 5.0f / std::max(std::min(dist, pd), 2.0f), 2.0f * k), 7.0f * k);
                UiLine(px, py, sx, sy, Rgba(col[0], col[1], col[2], 0.28f), t * 2.2f); UiLine(px, py, sx, sy, Rgba(col[0], col[1], col[2], 0.95f), t); ++g_segsDrawn;
            }
            havePrev = ok; px = sx; py = sy; pd = dist;
        }
    }
    if (g_canvas || g_pen) {      // the pen: a ring in the ink's colour where the ink goes down, and the keys
        float col[3]; InkColor(CoopMyId(), g_penColor, col); float cx = W / 2 + g_ox, cy = H / 2 + g_oy, R = (g_pen ? 7.0f : 10.0f) * k;
        for (int i = 0; i < 16; ++i) { float a0 = i * 0.3926991f, a1 = (i + 1) * 0.3926991f; UiLine(cx + cosf(a0) * R, cy + sinf(a0) * R, cx + cosf(a1) * R, cy + sinf(a1) * R, Rgba(col[0], col[1], col[2], 1), 2.5f * k); }
        UiRect(cx - 1.5f * k, cy - 1.5f * k, 3 * k, 3 * k, Rgba(1, 1, 1, 1));
        if (g_canvas) {
            for (int i = 0; i < kInks; ++i) { float c2[3]; InkColor(CoopMyId(), uint8_t(i), c2); float sw = 22 * k, x0 = W / 2 - kInks * sw / 2 + i * sw; UiRect(x0, H - 78 * k, sw - 4 * k, 14 * k, Rgba(c2[0], c2[1], c2[2], 1)); if (i == g_penColor) UiFrame(x0 - 2, H - 80 * k, sw, 18 * k, Rgba(1, 1, 1, 1), 2); }
            const char* hint = "canvas: move the mouse, hold X to draw, B colour, Backspace undo, Shift+X clear all, Z leaves"; float hw = UiTextWidth(hint, 0.8f * k); UiText(W / 2 - hw / 2, H - 56 * k, hint, Rgba(0.85f, 0.9f, 1, 1), 0.8f * k);
        }
    }
    if (g_wheel) {      // the emote wheel: eight slots round the middle of the screen, the one the mouse points to lit
        float cx = W / 2, cy = H / 2, R = 150 * k; int sel = WheelSlot(); float col[3]; PlayerColor(CoopMyId(), col);
        UiRect(cx - R - 70 * k, cy - R - 40 * k, 2 * R + 140 * k, 2 * R + 80 * k, Rgba(0, 0, 0, 0.35f));
        for (int i = 0; i < 8; ++i) {
            float a = -1.5707963f + i * 0.7853982f, px = cx + cosf(a) * R, py = cy + sinf(a) * R; bool on = i == sel;
            float bw = 112 * k, bh = 34 * k; UiRect(px - bw / 2 - 2, py - bh / 2 - 2, bw + 4, bh + 4, on ? Rgba(col[0], col[1], col[2], 1) : Rgba(0.4f, 0.45f, 0.55f, 0.8f));
            UiRect(px - bw / 2, py - bh / 2, bw, bh, on ? Rgba(0.2f, 0.24f, 0.4f, 0.97f) : Rgba(0.06f, 0.07f, 0.1f, 0.9f));
            float tw = UiTextWidth(kEmoteNames[i], 1.0f * k); UiText(px - tw / 2, py - UiLineHeight(1.0f * k) / 2, kEmoteNames[i], on ? Rgba(1, 1, 1, 1) : Rgba(0.8f, 0.85f, 0.95f, 1), 1.0f * k);
        }
        UiLine(cx, cy, cx + g_wx * R / 140.0f, cy + g_wy * R / 140.0f, Rgba(col[0], col[1], col[2], 1), 3 * k); UiRect(cx - 5 * k, cy - 5 * k, 10 * k, 10 * k, Rgba(col[0], col[1], col[2], 1));
        const char* hint = "move the mouse toward an emote, release to play it"; float hw = UiTextWidth(hint, 0.8f * k); UiText(cx - hw / 2, cy + R + 40 * k, hint, Rgba(0.8f, 0.85f, 0.95f, 1), 0.8f * k);
    }
    if (g_pick) {       // the character list: the model the others see you as
        int n = ModelGpuCount() + 1; float lh = UiLineHeight(1.0f * k) + 8 * k, x = W / 2 - 220 * k, y = H / 2 - lh * (n + 1) / 2, w = 440 * k;
        UiRect(x, y, w, lh * (n + 2) + 10 * k, Rgba(0.05f, 0.06f, 0.09f, 0.92f)); UiText(x + 12 * k, y + 8 * k, "Your character: Up/Down, Enter to choose, Esc to close", Rgba(0.75f, 0.8f, 0.95f, 1), 0.85f * k);
        for (int i = 0; i < n; ++i) {
            float ry = y + 10 * k + lh * (i + 1.3f); bool on = i == g_pickSel; const char* nm = i == 0 ? "(the default)" : ModelGpuName(i - 1); bool mine = (i == 0 && g_myChar.empty()) || (i > 0 && !_stricmp(nm, g_myChar.c_str()));
            if (on) UiRect(x + 6 * k, ry - 3 * k, w - 12 * k, lh - 2 * k, Rgba(0.25f, 0.28f, 0.45f, 0.95f));
            char b[100]; snprintf(b, sizeof b, "%s%s", nm, mine ? "   (you are this)" : ""); UiText(x + 20 * k, ry, b, mine ? Rgba(0.6f, 1, 0.7f, 1) : Rgba(0.92f, 0.92f, 0.95f, 1), 1.0f * k);
        }
    }
    // the chat log (bottom left) and the box
    float lh = UiLineHeight(0.95f * k) + 4 * k, x = 24 * k, base = H - (g_open ? 150 : 120) * k; int shown = 0;
    std::vector<const ChatEntry*> vis;
    for (auto it = g_log.rbegin(); it != g_log.rend() && vis.size() < (g_open ? 12u : 8u); ++it) if (g_open || now - it->at < kChatShowMs) vis.push_back(&*it);
    for (size_t i = 0; i < vis.size(); ++i) {
        const ChatEntry& e = *vis[i]; float col[3]; PlayerColor(e.from, col); float y = base - lh * i;
        float fade = g_open ? 1.0f : std::min(1.0f, float(kChatShowMs - (now - e.at)) / 2000.0f); std::string who = e.name + ": ";
        float ww = UiTextWidth(who.c_str(), 0.95f * k), tw = UiTextWidth(e.text.c_str(), 0.95f * k);
        UiRect(x - 6 * k, y - 2 * k, ww + tw + 12 * k, lh, Rgba(0, 0, 0, 0.45f * fade));
        UiText(x, y, who.c_str(), Rgba(col[0], col[1], col[2], fade), 0.95f * k); UiText(x + ww, y, e.text.c_str(), Rgba(0.95f, 0.95f, 0.95f, fade), 0.95f * k); ++shown;
    }
    if (g_open) {
        float col[3]; PlayerColor(CoopMyId(), col); float bw = std::min(W - 48 * k, 760 * k), by = H - 112 * k, bh = UiLineHeight(1.0f * k) + 16 * k;
        UiRect(x - 2, by - 2, bw + 4, bh + 4, Rgba(col[0], col[1], col[2], 0.95f)); UiRect(x, by, bw, bh, Rgba(0.05f, 0.06f, 0.09f, 0.94f));
        std::string shownText = g_input; if ((now / 450) % 2 == 0) shownText += "_";
        UiText(x + 10 * k, by + 8 * k, shownText.c_str(), Rgba(1, 1, 1, 1), 1.0f * k);
        UiText(x, by + bh + 6 * k, "Enter: send   Esc: cancel", Rgba(0.7f, 0.75f, 0.9f, 1), 0.75f * k);
    }
    (void)shown;
}

void SocialCommand(const char* line) {
    ULONGLONG now = GetTickCount64(); char a[8] = {};
    if (!strncmp(line, "chat ", 5)) { Lock l; std::string t = line + 5; while (!t.empty() && (t.back() == '\r' || t.back() == '\n')) t.pop_back(); if (auto* c = CoopCore()) c->sendChat(t); AddChat(CoopMyId(), t, now); }
    else if (sscanf(line, "typing %7s", a) == 1) { Lock l; g_open = !strcmp(a, "on"); if (!g_open) g_input.clear(); }
    else if (!strncmp(line, "typetext ", 9)) { for (const char* p = line + 9; *p && *p != '\r' && *p != '\n'; ++p) InputChar((unsigned char)*p); }
    else if (!strncmp(line, "ping", 4) && (line[4] == 0 || line[4] == '\r' || line[4] == '\n')) { Lock l; g_pingReq = true; g_pingAsked = false; g_pingAt = now; }
    else if (!strncmp(line, "pingat ", 7)) { float p[3]; unsigned kind = 0; if (sscanf(line, "pingat %f %f %f %u", &p[0], &p[1], &p[2], &kind) >= 3) { Lock l; AddMark(CoopMyId(), (uint8_t)kind, p, now); if (auto* c = CoopCore()) c->sendMark((uint8_t)kind, p); } }
    else if (!strncmp(line, "emote ", 6)) { int id = atoi(line + 6); Lock l; TriggerEmote(id, now); }
    else if (!strncmp(line, "character ", 10)) { char nm[64] = {}; sscanf(line + 10, "%63s", nm); Lock l; g_myChar = strcmp(nm, "none") ? nm : ""; }
    else if (!strncmp(line, "wheel ", 6)) { Lock l; g_wheel = !strncmp(line + 6, "open", 4); g_wx = g_wy = 0; }
    else if (!strncmp(line, "canvas ", 7)) { Lock l; g_canvas = !strncmp(line + 7, "on", 2); g_ox = g_oy = 0; if (!g_canvas) PenUp(now); }
    else if (!strncmp(line, "reticle ", 8)) { float fx = 0.5f, fy = 0.5f; if (sscanf(line + 8, "%f %f", &fx, &fy) == 2) { Lock l; g_ox = (fx - 0.5f) * g_scrW; g_oy = (fy - 0.5f) * g_scrH; } }
    else if (!strncmp(line, "drawclear", 9)) { Lock l; ClearMine(); }
    else if (!strncmp(line, "drawstat", 8)) {
        Lock l; char m[300]; size_t total = 0; for (const auto& s : g_strokes) total += s.pts.size() / 3;
        snprintf(m, sizeof m, "drawstat: strokes %zu points %zu segs %u pen %d canvas %d reticle %.0f %.0f color %u", g_strokes.size(), total, g_segsDrawn, g_pen ? 1 : 0, g_canvas ? 1 : 0, g_scrW / 2 + g_ox, g_scrH / 2 + g_oy, g_penColor); Log(m);
        for (const auto& s : g_strokes) {
            size_t n = s.pts.size() / 3; if (!n) continue; const float* a = &s.pts[0]; const float* b = &s.pts[(n - 1) * 3]; float sx = -1, sy = -1; ToScreen(g_camNow, g_angNow[0], g_angNow[1], b, g_scrW, g_scrH, &sx, &sy);
            float dx = b[0] - g_camNow[0], dy = b[1] - g_camNow[1], dz = b[2] - g_camNow[2];
            snprintf(m, sizeof m, "drawstat: stroke from %u id %u color %u points %zu first %.3f %.3f %.3f last %.3f %.3f %.3f lastscreen %.0f %.0f lastdist %.3f", s.from, s.id, s.color, n, a[0], a[1], a[2], b[0], b[1], b[2], sx, sy, sqrtf(dx * dx + dy * dy + dz * dz)); Log(m);
        }
    }
    else if (!strncmp(line, "socialstat", 10)) {
        Lock l; char m[300]; snprintf(m, sizeof m, "socialstat: open %d input_len %zu log %zu chat_seen %u marks %zu marks_seen %u bubbles %zu drawn dots %u bubbles %u marks %u arrows %u", g_open ? 1 : 0, g_input.size(), g_log.size(), g_chatSeen, g_marks.size(), g_marksSeen, g_bubbles.size(), g_dotsDrawn, g_bubblesDrawn, g_marksDrawn, g_arrowsDrawn); Log(m);
        for (const auto& e : g_log) { snprintf(m, sizeof m, "socialstat: chat from %u name %s text %s", e.from, e.name.c_str(), e.text.c_str()); Log(m); }
        for (const auto& mk : g_marks) { snprintf(m, sizeof m, "socialstat: mark from %u kind %u pos %.2f %.2f %.2f", mk.from, mk.kind, mk.pos[0], mk.pos[1], mk.pos[2]); Log(m); }
        snprintf(m, sizeof m, "socialstat: me character %s emote %d seq %u wheel %d picker %d", g_myChar.c_str(), g_emote, g_emoteSeq, g_wheel ? 1 : 0, g_pick ? 1 : 0); Log(m);
        for (const auto& r : CoopGetRemotes()) { int e = 0; float t = 0; auto it = g_remoteEmotes.find(r.id); bool act = it != g_remoteEmotes.end() && it->second.id && now - it->second.start < kEmoteMs; if (act) { e = it->second.id; t = float(now - it->second.start) / 1000.0f; }
            snprintf(m, sizeof m, "socialstat: remote %u typing %d speaking %d emote %u character %s playing %d t %.2f", r.id, r.typing ? 1 : 0, r.speaking ? 1 : 0, r.emote, r.character, e, t); Log(m); }
    }
}
