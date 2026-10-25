// See lobby.h.
#include "lobby.h"
#include "core.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace wlobby {
namespace {
enum Type : uint8_t { T_JOIN = 1, T_WELCOME = 2, T_HELLO = 3, T_STATE = 4, T_CHAT = 5, T_LEAVE = 7 };
constexpr uint8_t kVersion = 1;
uint32_t Fnv(const uint8_t* d, size_t n) { uint32_t h = 2166136261u; for (size_t i = 0; i < n; ++i) { h ^= d[i]; h *= 16777619u; } return h; }
struct W {
    Bytes b; explicit W(Type t) { b = {'W', 'T', 'L', '1', (uint8_t)t}; }
    W& u8(unsigned v) { b.push_back(uint8_t(v)); return *this; }
    W& u16(unsigned v) { u8(v & 255); u8(v >> 8); return *this; }
    W& u32(uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); return *this; }
    W& str(const std::string& s, size_t max) { size_t n = std::min(s.size(), max); u8((unsigned)n); b.insert(b.end(), s.begin(), s.begin() + n); return *this; }      // (max < 256)
    W& lstr(const std::string& s, size_t max) { size_t n = std::min(s.size(), max); u16((unsigned)n); b.insert(b.end(), s.begin(), s.begin() + n); return *this; }
    Bytes done() { uint32_t h = Fnv(b.data(), b.size()); u32(h); return b; }
};
struct R {
    const uint8_t* d; size_t n, at = 5; bool bad = false;
    bool take(size_t k) { if (at + k > n) { bad = true; return false; } return true; }
    unsigned u8() { if (!take(1)) return 0; return d[at++]; }
    unsigned u16() { unsigned a = u8(); return a | u8() << 8; }
    uint32_t u32() { uint32_t v = 0; if (!take(4)) return 0; for (int i = 0; i < 4; ++i) v |= uint32_t(d[at + i]) << (8 * i); at += 4; return v; }
    std::string str() { unsigned k = u8(); if (!take(k)) return ""; std::string s((const char*)d + at, k); at += k; return s; }
    std::string lstr() { unsigned k = u16(); if (!take(k)) return ""; std::string s((const char*)d + at, k); at += k; return s; }
};
bool Open(const uint8_t* d, size_t n, Type* t) {
    if (n < 9 || memcmp(d, "WTL1", 4)) return false;
#ifndef WLB_MUT_NO_CHECKSUM
    uint32_t h = 0; for (int i = 0; i < 4; ++i) h |= uint32_t(d[n - 4 + i]) << (8 * i);
    if (Fnv(d, n - 4) != h) return false;
#endif
    *t = Type(d[4]); return true;
}
std::string Clean(const std::string& s, size_t max) { std::string r; for (char c : s) { if (c == '\r' || c == '\n' || (unsigned char)c < 32) continue; r += c; if (r.size() >= max) break; } return r; }
}  // namespace

std::string IpText(uint32_t ip) { char b[20]; snprintf(b, sizeof b, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255); return b; }

Lobby::Lobby(bool host, const std::string& name, uint16_t gamePort, const std::string& token, Send send) : host_(host), name_(Clean(name, 23)), token_(token.substr(0, 8)), gamePort_(gamePort), send_(std::move(send)) {
    if (name_.empty()) name_ = host ? "Host" : "Player";
    session_ = uint32_t(Fnv((const uint8_t*)name_.data(), name_.size()) ^ (uint32_t)gamePort ^ 0x9e3779b9u);      // (the shell gives the real one: SetSession)
    if (host_) { id_ = 1; joined_ = true; refused_ = kJoinOk; Rebuild(); st_.seq = 1; }
}
void Lobby::Join(const Addr& host) { hostAddr_ = host; }

void Lobby::SetReady(bool r) { ready_ = r; if (host_) Refresh(); }
void Lobby::SetInGame(bool b) { inGame_ = b; if (b) seenInGame_ = true; if (host_) Refresh(); }
void Lobby::SendChat(const std::string& text) {
    std::string t = Clean(text, 200); if (t.empty()) return;
    if (host_) { PushChat(1, t); return; }
    out_.push_back({++outSeq_, t});
}
void Lobby::PushChat(unsigned from, const std::string& text) {      // host: a chat line, numbered, to everybody (and to its own screen)
    ChatLine c; c.seq = chatNext_++; c.from = from; c.text = text; chatIn_.push_back(c);
    for (auto& kv : peers_) { kv.second.outbox[c.seq] = c; kv.second.outboxAt = 0; }
}
void Lobby::SetSettings(const Settings& s) { if (!host_) return; st_.settings = s; st_.settings.raceGoal = Clean(s.raceGoal, 23); st_.settings.progress = Clean(s.progress, 160); Refresh(); }
void Lobby::SetResult(const std::string& t) { if (!host_) return; st_.lastResult = Clean(t, 200); Refresh(); }
bool Lobby::Start(uint64_t now) {
    if (!host_ || st_.phase != kLobby) return false;
    if (!twins.startWithoutReady) for (const auto& kv : peers_) if (!kv.second.m.ready) return false;
    st_.phase = kStarting; startAt_ = now + kStartMs; startedAt_ = now; launched_ = false; seenInGame_ = false; backSince_ = 0;
    for (auto& kv : peers_) kv.second.m.playing = true;      // the roster is fixed now: whoever joins later is only in the lobby
    Refresh(); return true;
}
void Lobby::Leave() { if (host_ || !joined_) return; W w(T_LEAVE); w.u8(id_); send_(hostAddr_, w.done()); joined_ = false; }
std::vector<ChatLine> Lobby::TakeChat() { auto r = std::move(chatIn_); chatIn_.clear(); return r; }

void Lobby::Rebuild() {      // host: the member list from the peers (the host first, then by id)
    Member me; me.id = 1; me.name = name_; me.host = true; me.ready = true; me.inGame = inGame_; me.speaking = speaking_; me.gamePort = gamePort_; me.playing = st_.phase != kLobby;
    std::vector<Member> v = {me}; for (auto& kv : peers_) v.push_back(kv.second.m);
    std::sort(v.begin() + 1, v.end(), [](const Member& a, const Member& b) { return a.id < b.id; });
    for (auto& m : v) if (m.host) m.ready = true;
    for (auto& kv : peers_) for (auto& m : v) if (m.id == kv.second.m.id) m.ackedChat = kv.second.chatTaken;
    st_.members = v;
}
void Lobby::Refresh() { if (!host_) return; Rebuild(); ++st_.seq; nextState_ = 0; }

void Lobby::SendVoice(const wvoice::Packet& p) {
    Bytes b = wvoice::MakePacket(p);
    if (host_) { if (twins.noVoiceRelay) return; for (auto& kv : peers_) send_(kv.first, b); return; }
    if (joined_) send_(hostAddr_, b);
}

void Lobby::SendHello(uint64_t now) {
    W w(T_HELLO); w.u8(id_).u8(ready_ ? 1 : 0).u8(inGame_ ? 1 : 0).u8(speaking_ ? 1 : 0).u32(hostSession_).u32(seenChat_);
    std::string text; uint32_t seq = 0; for (auto& o : out_) if (o.first > outAcked_ || twins.noChatAck) { seq = o.first; text = o.second; break; }      // the oldest line the host has not taken yet
    w.u32(seq).str(text, 200);
    send_(hostAddr_, w.done()); nextHello_ = now + kHelloMs;
}

void Lobby::Tick(uint64_t now) {
    if (host_) {
        if (!twins.noTimeout) { bool dropped = false; for (auto it = peers_.begin(); it != peers_.end();) { if (now - it->second.at > kStaleMs) { it = peers_.erase(it); dropped = true; } else ++it; } if (dropped) Refresh(); }
        if (st_.phase == kStarting && now >= startAt_) { st_.phase = kInGame; Refresh(); }
        if (st_.phase == kInGame) {      // back to the lobby when every game has ended (or none ever started)
            bool any = inGame_; for (auto& kv : peers_) any = any || kv.second.m.inGame;
            if (any) { seenInGame_ = true; backSince_ = 0; }
            else if (seenInGame_ && !backSince_) backSince_ = now;
            bool back = !any && seenInGame_ && backSince_ && now - backSince_ > 1500, never = !any && !seenInGame_ && now - startedAt_ > 90000;
            if (back || never) { st_.phase = kLobby; ready_ = false; for (auto& kv : peers_) { kv.second.m.ready = false; kv.second.m.playing = false; } Refresh(); }
        }
        if (now >= nextState_) {
            st_.startInMs = st_.phase == kStarting ? int32_t(std::max<int64_t>(0, int64_t(startAt_) - int64_t(now))) : 0; Rebuild();
            W w(T_STATE); w.u32(session_).u32(st_.seq).u8(st_.phase).u32((uint32_t)st_.startInMs).u8(st_.settings.mode).u8(st_.settings.newGame ? 1 : 0).str(st_.settings.raceGoal, 23).lstr(st_.settings.progress, 160).lstr(st_.lastResult, 200).u8((unsigned)st_.members.size());
            for (const auto& m : st_.members) w.u8(m.id).u8((m.host ? 1 : 0) | (m.ready ? 2 : 0) | (m.inGame ? 4 : 0) | (m.speaking ? 8 : 0) | (m.playing ? 16 : 0)).u16(m.gamePort).u32(m.ip).u32(m.ackedChat).str(m.name, 23);
            Bytes b = w.done(); for (auto& kv : peers_) send_(kv.first, b); nextState_ = now + kStateMs;
        }
        for (auto& kv : peers_) {      // chat the client has not acknowledged yet, oldest first, a few at a time
            Peer& p = kv.second; if (p.outbox.empty() || now < p.outboxAt) continue; int k = 0;
            for (auto& o : p.outbox) { W w(T_CHAT); w.u32(o.second.seq).u8(o.second.from).str(o.second.text, 200); send_(kv.first, w.done()); if (++k >= 4) break; }
            p.outboxAt = now + 400;
        }
    } else {
        if (hostAddr_.port == 0) return;
        if (!joined_) { if (now >= nextHello_) { W w(T_JOIN); w.u8(kVersion).str(token_, 8).str(name_, 23).u16(gamePort_); send_(hostAddr_, w.done()); nextHello_ = now + kHelloMs; } return; }
        if (!twins.noTimeout && now - lastHost_ > kStaleMs) { joined_ = false; refused_ = kPending; id_ = 0; hostSession_ = 0; nextHello_ = 0; return; }      // the host is gone (or dropped us): ask again
        if (now >= nextHello_) SendHello(now);
        if (st_.phase == kStarting && !startAt_) startAt_ = now + uint64_t(std::max(0, st_.startInMs));
    }
}

bool Lobby::TakeLaunch(uint64_t now, Launch* out) {
    if (launched_ || st_.phase == kLobby) return false;
    bool playing = false; for (const auto& m : st_.members) if (m.id == id_) playing = m.playing;
    if (!playing) return false;
    if (st_.phase == kStarting && now < startAt_) return false;
    if (host_ && st_.phase == kStarting) return false;      // (the host's phase moves on to in-game on its own tick, at the same moment)
    launched_ = true; out->myId = id_; out->state = st_; return true;
}

void Lobby::OnPacket(const Addr& from, const uint8_t* d, size_t n, uint64_t now) {
    if (n >= 4 && !memcmp(d, "WTO5", 4)) {      // a voice packet
        wvoice::Packet p; if (!wvoice::ParsePacket(d, n, &p)) return;
        if (host_) {
            auto it = peers_.find(from); if (it == peers_.end()) return;
            if (!twins.noVoiceFromCheck && p.from != it->second.m.id) return;      // a member speaks as itself only
            it->second.at = now; if (onVoice) onVoice(p);
            if (twins.noVoiceRelay) return;
            for (auto& kv : peers_) if (twins.voiceEcho || !(kv.first == from)) send_(kv.first, Bytes(d, d + n));
        } else if (joined_ && from == hostAddr_) { lastHost_ = now; if (onVoice) onVoice(p); }
        return;
    }
    Type t; if (!Open(d, n, &t)) return;
    R r{d, n - 4};
    if (host_) {
        if (t == T_JOIN) {
            unsigned ver = r.u8(); std::string tok = r.str(), nm = r.str(); unsigned port = r.u16(); if (r.bad) return;
            auto reply = [&](JoinResult res, unsigned id) { W w(T_WELCOME); w.u8(res).u8(id); send_(from, w.done()); };
            if (ver != kVersion) { reply(kBadVersion, 0); return; }
            if (!twins.noToken && tok != token_) { reply(kBadToken, 0); return; }
            auto it = peers_.find(from);
            if (it == peers_.end()) {
                if (st_.members.size() >= kMaxMembers) { reply(kFull, 0); return; }
                Peer p; p.addr = from; p.m.id = nextId_++; p.m.name = Clean(nm, 23); if (p.m.name.empty()) p.m.name = "Player"; p.m.gamePort = (uint16_t)port; p.m.ip = from.ip; p.at = now;
                it = peers_.emplace(from, p).first; Refresh();
            }
            it->second.at = now; reply(kJoinOk, it->second.m.id); return;
        }
        auto it = peers_.find(from); if (it == peers_.end()) return;
        Peer& p = it->second;
        if (t == T_LEAVE) { peers_.erase(it); Refresh(); return; }
        if (t == T_HELLO) {
            unsigned id = r.u8(), ready = r.u8(), inGame = r.u8(), speaking = r.u8(); r.u32(); uint32_t chatAck = r.u32(), cseq = r.u32(); std::string text = r.str(); if (r.bad || id != p.m.id) return;
            p.at = now; bool changed = false;
            if ((ready != 0) != p.m.ready) { p.m.ready = ready != 0; changed = true; } if ((inGame != 0) != p.m.inGame) { p.m.inGame = inGame != 0; changed = true; if (inGame) seenInGame_ = true; } if ((speaking != 0) != p.m.speaking) { p.m.speaking = speaking != 0; changed = true; }
            if (chatAck > p.chatSeen) { p.chatSeen = chatAck; for (auto o = p.outbox.begin(); o != p.outbox.end();) { if (o->first <= chatAck) o = p.outbox.erase(o); else ++o; } }
            if (cseq && (cseq == p.chatTaken + 1 || (twins.noChatDedupe && cseq <= p.chatTaken + 1))) { p.chatTaken = cseq; PushChat(p.m.id, Clean(text, 200)); changed = true; }
            if (changed) Refresh(); return;
        }
        return;
    }
    // a client
    if (!(from == hostAddr_)) return;
    if (t == T_WELCOME) { unsigned res = r.u8(), id = r.u8(); if (r.bad) return; if (res == kJoinOk) { id_ = id; joined_ = true; refused_ = kJoinOk; lastHost_ = now; nextHello_ = 0; out_.clear(); outAcked_ = 0; outSeq_ = 0; seenChat_ = 0; pendingIn_.clear(); } else { refused_ = JoinResult(res); } return; }
    if (!joined_) return;
    if (t == T_STATE) {
        State s; uint32_t session = r.u32(); s.seq = r.u32(); s.phase = Phase(r.u8()); s.startInMs = int32_t(r.u32()); s.settings.mode = Mode(r.u8()); s.settings.newGame = r.u8() != 0; s.settings.raceGoal = r.str(); s.settings.progress = r.lstr(); s.lastResult = r.lstr();
        unsigned cnt = r.u8(); for (unsigned i = 0; i < cnt && !r.bad; ++i) { Member m; m.id = r.u8(); unsigned f = r.u8(); m.host = f & 1; m.ready = f & 2; m.inGame = f & 4; m.speaking = f & 8; m.playing = f & 16; m.gamePort = (uint16_t)r.u16(); m.ip = r.u32(); m.ackedChat = r.u32(); m.name = r.str(); s.members.push_back(m); }
        if (r.bad || s.phase > kInGame) return;
        bool newHost = hostSession_ != session;
        if (!twins.noStateSeq && !newHost && s.seq <= stateSeq_) { lastHost_ = now; return; }      // an older state arrived late
        bool mine = false; for (const auto& m : s.members) if (m.id == id_) { mine = true; if (m.ackedChat > outAcked_ && !twins.noChatAck) { outAcked_ = m.ackedChat; out_.erase(std::remove_if(out_.begin(), out_.end(), [&](const std::pair<uint32_t, std::string>& o) { return o.first <= outAcked_; }), out_.end()); } }
        if (!mine) { joined_ = false; refused_ = kPending; hostSession_ = 0; nextHello_ = 0; return; }      // the host does not know us (it timed us out or restarted)
        if (newHost) { hostSession_ = session; seenChat_ = 0; pendingIn_.clear(); }
        if (st_.phase != kLobby && s.phase == kLobby) { ready_ = false; launched_ = false; startAt_ = 0; }
        if (s.phase == kStarting && st_.phase != kStarting) { startAt_ = now + uint64_t(std::max(0, s.startInMs)); launched_ = false; }
        if (s.phase == kLobby) startAt_ = 0;
        stateSeq_ = s.seq; lastHost_ = now; st_ = s; return;
    }
    if (t == T_CHAT) {
        ChatLine c; c.seq = r.u32(); c.from = r.u8(); c.text = r.str(); if (r.bad) return; lastHost_ = now;
        if (c.seq == seenChat_ + 1 || (twins.noChatDedupe && c.seq <= seenChat_ + 1)) { chatIn_.push_back(c); seenChat_ = std::max(seenChat_, c.seq); while (pendingIn_.count(seenChat_ + 1)) { chatIn_.push_back(pendingIn_[seenChat_ + 1]); pendingIn_.erase(seenChat_ + 1); ++seenChat_; } }
        else if (c.seq > seenChat_ + 1) pendingIn_[c.seq] = c;
        return;
    }
}

std::string BuildGameCfg(const std::string& base, const Launch& l, uint32_t hostDialIp) {
    wl::CfgFile cfg = wl::CfgFile::Parse(base); const Member* me = nullptr; for (const auto& m : l.state.members) if (m.id == l.myId) me = &m;
    if (!me) return base;
    cfg.Set("id", std::to_string(me->id)); cfg.Set("listen", std::to_string(me->gamePort)); cfg.Set("name", me->name);
    std::vector<std::string> peers; for (const auto& m : l.state.members) { if (m.id == me->id || !m.playing) continue; uint32_t ip = m.host ? (me->host ? m.ip : hostDialIp) : m.ip; if (m.host && me->host) continue; peers.push_back(std::to_string(m.id) + "," + IpText(ip) + "," + std::to_string(m.gamePort)); }
    cfg.SetAll("peer", peers); cfg.Unset("peer_host");
    cfg.Set("game_mode", l.state.settings.mode == kRace ? "race" : "coop");
    if (l.state.settings.mode == kRace) cfg.Set("race_goal", l.state.settings.raceGoal); else cfg.Unset("race_goal");
    if (l.state.settings.newGame && !l.state.settings.progress.empty()) cfg.Set("start_progress", l.state.settings.progress); else cfg.Unset("start_progress");
    return cfg.Text();
}
}  // namespace wlobby
