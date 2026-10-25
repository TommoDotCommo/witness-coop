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

bool SocialCapturing() { Lock l; return g_open; }
bool SocialKey(unsigned vk, bool down) {
    const Cfg& c = GetCfg(); Lock l; ULONGLONG now = GetTickCount64();
    if (g_open) {
        if (!down) return false;
        if (vk == VK_ESCAPE) { g_open = false; g_input.clear(); return true; }
        if (vk == VK_RETURN) { SendInput(now); g_open = false; return true; }
        if (vk == VK_BACK) { if (!g_input.empty()) g_input.pop_back(); return true; }
        return true;      // every other key belongs to the box (its characters arrive as WM_CHAR)
    }
    if (!down || GamePaused()) return false;
    if (vk == c.keyChat && !c.noChatUi) { g_open = true; g_input.clear(); return true; }
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
        witness::Presence p; p.typing = g_open && !cfg.noTypingDots; core->setPresence(p);
    }
    g_marks.erase(std::remove_if(g_marks.begin(), g_marks.end(), [&](const Mark& m) { return now - m.at > kMarkMs; }), g_marks.end());
    for (auto it = g_bubbles.begin(); it != g_bubbles.end();) { if (it->second.until < now) it = g_bubbles.erase(it); else ++it; }
    g_dotsDrawn = g_bubblesDrawn = g_marksDrawn = g_arrowsDrawn = 0;
    if (GamePaused()) return;
    float k = std::max(1.0f, H / 1080.0f);
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
    else if (!strncmp(line, "socialstat", 10)) {
        Lock l; char m[300]; snprintf(m, sizeof m, "socialstat: open %d input_len %zu log %zu chat_seen %u marks %zu marks_seen %u bubbles %zu drawn dots %u bubbles %u marks %u arrows %u", g_open ? 1 : 0, g_input.size(), g_log.size(), g_chatSeen, g_marks.size(), g_marksSeen, g_bubbles.size(), g_dotsDrawn, g_bubblesDrawn, g_marksDrawn, g_arrowsDrawn); Log(m);
        for (const auto& e : g_log) { snprintf(m, sizeof m, "socialstat: chat from %u name %s text %s", e.from, e.name.c_str(), e.text.c_str()); Log(m); }
        for (const auto& mk : g_marks) { snprintf(m, sizeof m, "socialstat: mark from %u kind %u pos %.2f %.2f %.2f", mk.from, mk.kind, mk.pos[0], mk.pos[1], mk.pos[2]); Log(m); }
        for (const auto& r : CoopGetRemotes()) { snprintf(m, sizeof m, "socialstat: remote %u typing %d speaking %d emote %u character %s", r.id, r.typing ? 1 : 0, r.speaking ? 1 : 0, r.emote, r.character); Log(m); }
    }
}
