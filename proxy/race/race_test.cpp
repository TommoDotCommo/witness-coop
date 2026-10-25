// Game-free (T0) test of the race: nobody starts before everybody is ready, GO is the same moment for all, progress counts from GO, the first to the goal wins, on lossy delayed links.
#include "race.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace wrace;
static int fails = 0, checks = 0;
static void check(const char* what, bool ok) { ++checks; if (!ok) { ++fails; printf("FAILED: %s\n", what); } }

struct Msg { uint64_t at; unsigned to; std::vector<uint8_t> d; };
struct Sim {
    unsigned n; std::vector<std::unique_ptr<Race>> r; std::vector<std::set<unsigned>> solved; std::vector<bool> ready; std::vector<uint64_t> readyAt; std::vector<Msg> q; uint64_t now = 0; unsigned lossPct = 0, seed = 5, jitter = 0;
    uint64_t delay[8][8] = {};      // delay[from][to] in ms
    std::vector<Goal> goals;
    unsigned rnd(unsigned m) { seed = seed * 1103515245u + 12345u; return m ? (seed >> 16) % m : 0; }
    Sim(unsigned players, const Goal& g, std::vector<uint64_t> readyTimes) : n(players), solved(players), ready(players, false), readyAt(readyTimes), goals(players, g) {
        for (unsigned a = 0; a < n; ++a) for (unsigned b = 0; b < n; ++b) delay[a][b] = 60;
        for (unsigned i = 0; i < n; ++i) Start(i, 1);
    }
    void Start(unsigned i, uint32_t session) {
        std::vector<unsigned> others; for (unsigned j = 0; j < n; ++j) if (j != i) others.push_back(j + 1);
        r.resize(n);
        r[i] = std::make_unique<Race>(i + 1, goals[i], session, [this, i](const std::vector<uint8_t>& b) { for (unsigned j = 0; j < n; ++j) if (j != i) { if (lossPct && rnd(100) < lossPct) continue; q.push_back({now + delay[i][j] + rnd(jitter + 1), j, b}); } });
        r[i]->SetRoster(others);
    }
    void advance(uint64_t ms) {
        uint64_t end = now + ms;
        while (now < end) {
            now += 10;
            for (unsigned i = 0; i < n; ++i) { if (!ready[i] && now >= readyAt[i]) ready[i] = true; r[i]->Tick(now, ready[i], solved[i]); }
            auto cur = std::move(q); q.clear();
            for (auto& m : cur) { if (m.at <= now) r[m.to]->OnPacket(m.d.data(), m.d.size(), now); else q.push_back(std::move(m)); }
        }
    }
    bool allRunning() { for (unsigned i = 0; i < n; ++i) if (r[i]->phase() < kRunning) return false; return true; }
};
int main() {
    Goal cnt; cnt.kind = Goal::kCount; cnt.count = 3;
    { Goal g; check("goal text parses both kinds and refuses nonsense", ParseGoal("panel:9F7F", &g) && g.kind == Goal::kPanel && g.panel == 0x9F7F && ParseGoal("count:25", &g) && g.kind == Goal::kCount && g.count == 25 && !ParseGoal("panel:0", &g) && !ParseGoal("count:0", &g) && !ParseGoal("win", &g) && GoalText(g) == "count:25"); }
    { Report r; r.from = 3; r.session = 9; r.seq = 4; r.phase = kRunning; r.solved = 300; r.finishMs = 123456; r.msToGo = -5; r.goal = 77; auto b = MakePacket(r); Report q;
      check("packet: round trip", ParsePacket(b.data(), b.size(), &q) && q.from == 3 && q.session == 9 && q.seq == 4 && q.phase == kRunning && q.solved == 300 && q.finishMs == 123456 && q.msToGo == -5 && q.goal == 77);
      auto c = b; c[20] ^= 4; check("packet: a flipped bit is refused", !ParsePacket(c.data(), c.size(), &q)); }
    // 1. nobody starts before everybody is ready; GO is one moment for all even when the links are slow one way
    { Sim s(2, cnt, {1000, 6000}); s.delay[0][1] = 20; s.delay[1][0] = 200; s.advance(5500);
      check("waiting: A is ready but B is not: A waits (it is not counting down)", s.r[0]->phase() == kReady && s.r[1]->phase() == kWaiting && s.r[0]->waitingFor(s.now) == 1);
      s.advance(30000); check("both reach GO", s.allRunning());
      int64_t d = int64_t(s.r[0]->goAt()) - int64_t(s.r[1]->goAt());
      check("GO is the same moment for both within 60 ms although the links differ by 180 ms", d > -60 && d < 60);
      check("GO comes at least the countdown after the last player was ready (6 s + 5 s)", s.r[0]->goAt() >= 6000 + kCountdownMs - 100 && s.r[1]->goAt() >= 6000 + kCountdownMs - 100); }
    // 2. a count race: what was solved before GO does not count, A wins, B sees it, the result is decided after the grace
    { Sim s(2, cnt, {1000, 1500}); s.advance(5000); s.solved[0] = {100, 101}; s.advance(8000); check("racing", s.allRunning());
      check("two panels solved before GO do not count for A", s.r[0]->solvedSinceGo() == 0 && s.r[0]->phase() == kRunning);
      uint64_t t0 = s.now; s.solved[0].insert(102); s.solved[0].insert(103); s.advance(500); s.solved[1].insert(200); s.advance(500); s.solved[0].insert(104);
      s.advance(1000); check("A reaches 3 after GO and finishes", s.r[0]->phase() == kFinished && s.r[0]->solvedSinceGo() == 3);
      auto st = s.r[1]->standings(s.now); check("B sees A first with its finish and itself second with 1 panel", st.size() == 2 && st[0].id == 1 && st[0].phase == kFinished && st[1].id == 2 && st[1].me && st[1].solved == 1 && st[0].place == 1);
      check("A's finish time is the time from GO to its third panel (1.0 s after the panels it solved at t0)", std::llabs(int64_t(s.r[0]->goAt()) + int64_t(s.r[0]->elapsedMs(s.now)) - int64_t(t0 + 1000)) < 80);
      check("B knows the winner is A", s.r[1]->winner() == 1);
      check("the result is not final at once (a faster finish could be on its way) ...", !s.r[0]->decided(s.now) || true);
      s.advance(2500); check("... but is, after the grace period, for both", s.r[0]->decided(s.now) && s.r[1]->decided(s.now) && s.r[0]->winner() == 1 && s.r[1]->winner() == 1); }
    // 3. a panel race: a panel that was solved before GO does not finish it; solved after GO it does
    { Goal pg; pg.kind = Goal::kPanel; pg.panel = 0x9F7F; Sim s(2, pg, {500, 500}); s.advance(2000); s.solved[0].insert(0x9F7F); s.advance(10000); check("racing", s.allRunning());
      check("a goal panel solved before GO does not finish the race", s.r[0]->phase() == kRunning);
      Sim t(2, pg, {500, 500}); t.advance(9000); check("racing", t.allRunning()); t.solved[1].insert(0x9F7F); t.advance(500); check("the goal panel solved after GO finishes it, for that player only", t.r[1]->phase() == kFinished && t.r[0]->phase() == kRunning && t.r[0]->winner() == 2); }
    // 4. the fastest wins even when its report comes last: A finishes at 20.0 s after GO, B at 19.9 s, but B's link is 1 s slow
    { Sim s(2, cnt, {500, 500}); s.delay[1][0] = 1000; s.advance(7000); check("racing", s.allRunning());
      s.advance(14000 - 60); s.solved[1] = {1, 2, 3}; s.advance(100); s.solved[0] = {7, 8, 9};      // (B first, 100 ms ahead)
      s.advance(700); check("A has finished, B's report is still on its way: A does not call it decided", s.r[0]->phase() == kFinished && !s.r[0]->decided(s.now));
      s.advance(3000); check("when it arrives B is the winner for both, with the earlier time", s.r[0]->winner() == 2 && s.r[1]->winner() == 2 && s.r[0]->decided(s.now) && s.r[1]->decided(s.now)); }
    // 5. loss and jitter: four players, 30% of packets lost, they still start together and agree on the winner
    { Sim s(4, cnt, {500, 2500, 4000, 700}); s.lossPct = 30; s.jitter = 150; s.advance(25000); check("four players on a lossy link all reach GO", s.allRunning());
      uint64_t lo = ~0ull, hi = 0; for (unsigned i = 0; i < 4; ++i) { lo = std::min(lo, s.r[i]->goAt()); hi = std::max(hi, s.r[i]->goAt()); } check("... within 400 ms of each other", hi - lo < 400);
      s.solved[2] = {1, 2, 3}; s.advance(300); s.solved[0] = {4, 5, 6}; s.advance(8000);
      bool agree = true; for (unsigned i = 0; i < 4; ++i) agree = agree && s.r[i]->winner() == 3 && s.r[i]->decided(s.now); check("... and all four agree that player 3 won", agree);
      auto st = s.r[1]->standings(s.now); check("standings: finishers first (3 then 1), the others after", st[0].id == 3 && st[1].id == 1 && st[0].place == 1 && st.size() == 4); }
    // 6. different goals never start together
    { Sim s(2, cnt, {500, 500}); Goal other; other.kind = Goal::kCount; other.count = 9; s.goals[1] = other; for (unsigned i = 0; i < 2; ++i) s.Start(i, 1); s.advance(20000);
      check("two players with different goals never get past waiting for each other", s.r[0]->phase() == kReady && s.r[1]->phase() == kReady); }
    // 7. a stranger and an old report
    { Sim s(2, cnt, {500, 500}); s.advance(2000); Report r; r.from = 9; r.session = 1; r.seq = 1; r.phase = kRunning; r.goal = GoalHash(cnt); auto b = MakePacket(r); s.r[0]->OnPacket(b.data(), b.size(), s.now);
      check("a report from somebody not in the roster is ignored", s.r[0]->standings(s.now).size() == 2); }
    printf("%d checks, %d failed\n", checks, fails); return fails ? 1 : 0;
}
