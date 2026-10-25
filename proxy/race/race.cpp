// See race.h.
#include "race.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace wrace {
static uint32_t Fnv(const uint8_t* d, size_t n) { uint32_t h = 2166136261u; for (size_t i = 0; i < n; ++i) { h ^= d[i]; h *= 16777619u; } return h; }
bool ParseGoal(const char* t, Goal* g) {
    unsigned v = 0;
    if (sscanf(t, "panel:%x", &v) == 1 && v > 0 && v < 0x40000) { g->kind = Goal::kPanel; g->panel = v; return true; }
    if (sscanf(t, "count:%u", &v) == 1 && v > 0 && v < 1000) { g->kind = Goal::kCount; g->count = v; return true; }
    return false;
}
std::string GoalText(const Goal& g) { char b[40]; if (g.kind == Goal::kPanel) snprintf(b, sizeof b, "panel:%X", g.panel); else snprintf(b, sizeof b, "count:%u", g.count); return b; }
uint32_t GoalHash(const Goal& g) { std::string s = GoalText(g); return Fnv((const uint8_t*)s.data(), s.size()); }

std::vector<uint8_t> MakePacket(const Report& r) {
    std::vector<uint8_t> b = {'W', 'T', 'R', '1'}; auto put = [&](uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); };
    put(r.from); put(r.session); put(r.seq); b.push_back(r.phase); b.push_back(0); b.push_back(uint8_t(r.solved)); b.push_back(uint8_t(r.solved >> 8)); put(r.finishMs); put(uint32_t(r.msToGo)); put(r.goal); put(Fnv(b.data(), b.size()));
    return b;
}
bool ParsePacket(const uint8_t* d, size_t n, Report* r) {
    if (n != 36 || memcmp(d, "WTR1", 4)) return false;
    auto get = [&](size_t at) { return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8 | uint32_t(d[at + 2]) << 16 | uint32_t(d[at + 3]) << 24; };
#ifndef WR_MUT_NO_CHECKSUM
    if (Fnv(d, n - 4) != get(n - 4)) return false;
#endif
    if (d[16] > kFinished) return false;
    r->from = get(4); r->session = get(8); r->seq = get(12); r->phase = Phase(d[16]); r->solved = uint16_t(d[18] | d[19] << 8); r->finishMs = get(20); r->msToGo = int32_t(get(24)); r->goal = get(28); return true;
}

Race::Race(unsigned self, const Goal& goal, uint32_t session, std::function<void(const std::vector<uint8_t>&)> send, uint32_t countdownMs) : self_(self), goal_(goal), session_(session), countdownMs_(countdownMs), send_(std::move(send)) {}
void Race::SetRoster(const std::vector<unsigned>& others) { for (unsigned id : others) if (id != self_) peers_[id]; }

void Race::OnPacket(const uint8_t* d, size_t n, uint64_t now) {
    Report r; if (!ParsePacket(d, n, &r) || r.from == self_) return;
    auto it = peers_.find(r.from); if (it == peers_.end()) return;      // not in the roster
    Peer& p = it->second;
    if (p.any && (r.session < p.r.session || (r.session == p.r.session && r.seq <= p.r.seq))) return;      // an older report: the newest wins
    bool restarted = p.any && r.session != p.r.session; p.r = r; p.at = now; p.any = true;
    if (restarted && phase_ >= kCountdown) { /* a player that restarted in the middle of a race is simply listed again from the start */ }
    if (r.msToGo > 0) { uint64_t est = now + uint64_t(r.msToGo); p.goAt = (p.goAt && !restarted && p.goAt > now && est > p.goAt - 1000 && est < p.goAt + 1000) ? std::min(p.goAt, est) : est; } else p.goAt = 0;
    if (r.phase == kFinished && !firstFinishAt_) firstFinishAt_ = now;
}

void Race::Send(uint64_t now) {
    Report r; r.from = self_; r.session = session_; r.seq = ++seq_; r.phase = phase_; r.solved = uint16_t(std::min<unsigned>(solvedDelta_, 65535)); r.finishMs = finishMs_;
    r.msToGo = phase_ == kCountdown && goAt_ > now ? int32_t(std::min<uint64_t>(goAt_ - now, 0x7fffffff)) : 0; r.goal = GoalHash(goal_);
    send_(MakePacket(r)); nextSend_ = now + kReportMs; sentPhase_ = phase_; sentSolved_ = solvedDelta_;
}

void Race::Tick(uint64_t now, bool worldReady, const std::set<unsigned>& solved) {
    const uint32_t myGoal = GoalHash(goal_);
    if (phase_ == kWaiting && worldReady) { baseline_ = solved; phase_ = kReady; }
    unsigned host = self_; for (const auto& kv : peers_) host = std::min(host, kv.first);      // the lowest id sets the moment of GO; the others take it from its reports
    if (phase_ == kReady) {
        if (host == self_) {
            bool all = true;
            for (auto& kv : peers_) { const Peer& p = kv.second; bool fresh = p.any && now - p.at < kStaleMs;
#ifdef WR_MUT_NO_GOAL_CHECK
                bool same = true;
#else
                bool same = p.r.goal == myGoal;
#endif
                if (!(fresh && same && p.r.phase >= kReady)) all = false; }
#ifdef WR_MUT_NO_GATE
            all = true;
#endif
            if (all) { goAt_ = now + countdownMs_; phase_ = kCountdown; }
        } else {
            const Peer& h = peers_[host]; bool fresh = h.any && now - h.at < kStaleMs && h.r.goal == myGoal;
#ifdef WR_MUT_NO_GOAL_CHECK
            fresh = h.any && now - h.at < kStaleMs;
#endif
            if (fresh && h.goAt) { goAt_ = h.goAt; phase_ = kCountdown; }                                   // the host has scheduled GO
            else if (fresh && h.r.phase >= kRunning) { goAt_ = now; phase_ = kCountdown; }                  // joined late: the race is on already
#ifdef WR_MUT_NO_HOST
            else if (fresh) { bool all = true; for (auto& kv : peers_) if (!(kv.second.any && now - kv.second.at < kStaleMs && kv.second.r.phase >= kReady)) all = false; if (all) { goAt_ = now + countdownMs_; phase_ = kCountdown; } }
#endif
        }
    }
    if (phase_ == kCountdown) {
        if (host != self_) { const Peer& h = peers_[host]; if (h.any && h.goAt && h.goAt < goAt_) goAt_ = h.goAt; }      // (the earliest estimate is the one with the least delay in it)
        if (now >= goAt_) {
            phase_ = kRunning; baseline_ = solved;      // what is solved at GO does not count
#ifdef WR_MUT_NO_BASELINE
            baseline_.clear();
#endif
        }
    }
    if (phase_ == kRunning) {
        unsigned n = 0; for (unsigned id : solved) if (!baseline_.count(id)) ++n; solvedDelta_ = n;
        bool done = goal_.kind == Goal::kCount ? n >= goal_.count : (solved.count(goal_.panel) && !baseline_.count(goal_.panel));
        if (done) { phase_ = kFinished; finishAt_ = now; finishMs_ = uint32_t(now - goAt_); if (!firstFinishAt_) firstFinishAt_ = now; }
    }
    if (phase_ != kWaiting && (phase_ != sentPhase_ || solvedDelta_ != sentSolved_ || now >= nextSend_)) Send(now);
    else if (phase_ == kWaiting && now >= nextSend_) Send(now);      // (a waiting player is announced too, so the others can list it)
}

uint32_t Race::elapsedMs(uint64_t now) const { if (phase_ == kFinished) return finishMs_; if (phase_ == kRunning && now >= goAt_) return uint32_t(now - goAt_); return 0; }

std::vector<Standing> Race::standings(uint64_t now) const {
    std::vector<Standing> v; { Standing s; s.id = self_; s.me = true; s.phase = phase_; s.solved = solvedDelta_; s.finishMs = finishMs_; v.push_back(s); }
    for (const auto& kv : peers_) { Standing s; s.id = kv.first; s.gone = !kv.second.any || now - kv.second.at >= kStaleMs; if (kv.second.any) { s.phase = kv.second.r.phase; s.solved = kv.second.r.solved; s.finishMs = kv.second.r.finishMs; } v.push_back(s); }
    std::sort(v.begin(), v.end(), [](const Standing& a, const Standing& b) {
        bool fa = a.phase == kFinished, fb = b.phase == kFinished; if (fa != fb) return fa; if (fa) return a.finishMs != b.finishMs ? a.finishMs < b.finishMs : a.id < b.id;
        return a.solved != b.solved ? a.solved > b.solved : a.id < b.id; });
    for (size_t i = 0; i < v.size(); ++i) v[i].place = int(i) + 1;
    return v;
}
unsigned Race::winner() const { auto s = standings(UINT64_MAX / 2); return !s.empty() && s[0].phase == kFinished ? s[0].id : 0; }
bool Race::decided(uint64_t now) const {
    if (!winner()) return false;
    bool all = phase_ == kFinished; for (const auto& kv : peers_) if (!kv.second.any || kv.second.r.phase != kFinished) all = false;
    if (all) return true;
#ifdef WR_MUT_NO_GRACE
    return true;
#endif
    return firstFinishAt_ && now - firstFinishAt_ >= kGraceMs;
}
unsigned Race::waitingFor(uint64_t now) const { unsigned n = 0; for (const auto& kv : peers_) if (!kv.second.any || now - kv.second.at >= kStaleMs || kv.second.r.phase < kReady) ++n; return n; }
}  // namespace wrace
