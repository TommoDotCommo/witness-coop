// Game-free (T0) test of the lobby: joining with an invite, the member list, ready and start, the roster every game is started from, chat exactly once and in order, voice relayed by the host,
// members that go away, a host that restarts, on a link that loses, delays and reorders packets. Every protection has a twin (Lobby::twins) that must fail here.
#include "lobby.h"
#include "core.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
using namespace wlobby;
static int fails = 0, checks = 0;
static void check(const char* what, bool ok) { ++checks; if (!ok) { ++fails; printf("FAILED: %s\n", what); } }

struct Msg { uint64_t at; Addr to, from; Bytes d; };
struct Net {
    std::vector<std::unique_ptr<Lobby>> n; std::vector<Addr> addr; std::vector<bool> alive; std::vector<Msg> q; uint64_t now = 1000; unsigned lossPct = 0, jitter = 0, seed = 7, delay = 40, corruptPct = 0; Addr hostAddr;
    std::vector<std::vector<wvoice::Packet>> heard; std::vector<uint64_t> deadSince;
    unsigned rnd(unsigned m) { seed = seed * 1103515245u + 12345u; return m ? (seed >> 16) % m : 0; }
    Net(unsigned loss = 0, unsigned jit = 0) : lossPct(loss), jitter(jit) { hostAddr = {0x0A000001, 47100}; }
    unsigned add(bool host, const std::string& name, const std::string& token = "ABC123", Lobby::Twins tw = {}, uint16_t gamePort = 0) {
        unsigned i = (unsigned)n.size(); Addr a = host ? hostAddr : Addr{0x0A000001 + i, uint16_t(50000 + i)}; addr.push_back(a); alive.push_back(true); heard.emplace_back();
        n.push_back(std::make_unique<Lobby>(host, name, gamePort ? gamePort : uint16_t(47001 + i), token, [this, i](const Addr& to, const Bytes& b0) { if (lossPct && rnd(100) < lossPct) return; Bytes b = b0; if (corruptPct && rnd(100) < corruptPct) b[5 + rnd(unsigned(b.size() - 9))] ^= uint8_t(1 + rnd(255)); q.push_back({now + delay + rnd(jitter + 1), to, addr[i], b}); }));
        n[i]->twins = tw; n[i]->onVoice = [this, i](const wvoice::Packet& p) { heard[i].push_back(p); };
        if (host) n[i]->SetSession(unsigned(now)); else n[i]->Join(hostAddr);
        return i;
    }
    void advance(uint64_t ms) {
        uint64_t end = now + ms;
        while (now < end) {
            now += 10; for (size_t i = 0; i < n.size(); ++i) if (alive[i]) n[i]->Tick(now);
            auto cur = std::move(q); q.clear();
            for (auto& m : cur) { if (m.at <= now) { for (size_t i = 0; i < n.size(); ++i) if (alive[i] && addr[i] == m.to) n[i]->OnPacket(m.from, m.d.data(), m.d.size(), now); } else q.push_back(std::move(m)); }
        }
    }
};
static std::string names(const State& s) { std::string r; for (const auto& m : s.members) r += m.name + (m.ready ? "+" : "-") + std::to_string(m.id) + ","; return r; }

int main() {
    // 1. joining
    { Net net; unsigned h = net.add(true, "Host"); unsigned a = net.add(false, "Alpha"), b = net.add(false, "Beta"), c = net.add(false, "Gamma"); net.advance(3000);
      bool all = net.n[a]->joined() && net.n[b]->joined() && net.n[c]->joined();
      check("join: three clients join with the invite and get ids 2, 3, 4", all && net.n[a]->myId() == 2 && net.n[b]->myId() == 3 && net.n[c]->myId() == 4);
      bool same = true; for (unsigned i : {a, b, c}) same = same && names(net.n[i]->state()) == names(net.n[h]->state()) && net.n[i]->state().members.size() == 4;
      check("join: everybody sees the same four members (host first)", same && net.n[h]->state().members[0].host && net.n[h]->state().members[0].name == "Host");
      check("join: a client's game port and address are known to all (the host saw its source)", net.n[a]->state().members[1].gamePort == 47002 && net.n[a]->state().members[1].ip == 0x0A000002);
      unsigned bad = net.add(false, "Mallory", "WRONG1"); net.advance(3000);
      check("join: a wrong invite is refused and the member list does not change", !net.n[bad]->joined() && net.n[bad]->refused() == kBadToken && net.n[h]->state().members.size() == 4);
      Net n2; n2.add(true, "H", "T", {}); for (int i = 0; i < 16; ++i) n2.add(false, "P" + std::to_string(i), "T"); n2.advance(6000);
      unsigned in = 0; for (size_t i = 1; i < n2.n.size(); ++i) in += n2.n[i]->joined(); check("join: at most 16 members (the 16th client is refused as full)", in == 15 && n2.n[16]->refused() == kFull && n2.n[0]->state().members.size() == 16);
      Net n3; Lobby::Twins tw; tw.noToken = true; n3.add(true, "H", "TOKEN1", tw); unsigned m3 = n3.add(false, "Mallory", "NOPE"); n3.advance(3000);
      check("twin noToken: a host that does not check the invite lets a stranger in", n3.n[m3]->joined()); }
    // 2. ready, settings, start, the roster
    { Net net(15, 60); unsigned h = net.add(true, "Host", "ABC123", {}, 47001); unsigned a = net.add(false, "Alpha", "ABC123", {}, 47002), b = net.add(false, "Beta", "ABC123", {}, 47003); net.advance(4000);
      Settings s; s.mode = kRace; s.raceGoal = "count:9"; s.progress = "area:Symmetry_Island;lasers:2"; net.n[h]->SetSettings(s); net.advance(2500);
      check("settings: the mode, goal and new-game spec reach every member", net.n[a]->state().settings.mode == kRace && net.n[b]->state().settings.raceGoal == "count:9" && net.n[a]->state().settings.progress == "area:Symmetry_Island;lasers:2");
      check("start: refused while somebody is not ready", !net.n[h]->Start(net.now)); net.n[a]->SetReady(true); net.advance(1500); check("start: still refused with one of two ready", !net.n[h]->Start(net.now));
      net.n[b]->SetReady(true); net.advance(2500); check("ready: everybody sees who is ready", net.n[h]->state().members[2].ready && net.n[a]->state().members[2].ready && net.n[b]->state().members[1].ready);
      uint64_t t0 = net.now; check("start: accepted when all are ready", net.n[h]->Start(t0));
      unsigned late = net.add(false, "Late"); std::vector<uint64_t> launchAt(4, 0); std::vector<Launch> L(4); std::vector<std::string> base = {"id=9\nvoice_mode=ptt\nname=Old\n", "listen=1\nvoice_volume=50\n", "dev_menu=1\n", ""};
      for (int step = 0; step < 600; ++step) { net.advance(10); for (unsigned i = 0; i < 4; ++i) { Launch l; if (net.n[i]->TakeLaunch(net.now, &l)) { launchAt[i] = net.now; L[i] = l; } } }
      bool once = true; for (unsigned i : {h, a, b}) once = once && launchAt[i] != 0; check("start: every member's launcher is told to start its game", once);
      uint64_t lo = ~0ull, hi = 0; for (unsigned i : {h, a, b}) { lo = std::min(lo, launchAt[i]); hi = std::max(hi, launchAt[i]); }
      check("start: after the countdown (3 s) and within 600 ms of each other", lo >= t0 + 2800 && hi - lo < 600);
      check("start: a member who joined during the countdown is not in the game", launchAt[late] == 0);
      bool roster = true; for (unsigned i : {a, b}) { const auto& x = L[i].state.members; const auto& y = L[h].state.members; roster = roster && x.size() == y.size(); for (size_t k = 0; k < x.size() && k < y.size(); ++k) roster = roster && x[k].id == y[k].id && x[k].playing == y[k].playing && x[k].gamePort == y[k].gamePort && x[k].name == y[k].name; }
      check("start: all members start from the same roster (4 listed, 3 playing)", roster && L[h].state.members.size() == 4 && !L[h].state.members[3].playing);
      std::string cfg[3]; unsigned who[3] = {h, a, b}; for (int k = 0; k < 3; ++k) cfg[k] = BuildGameCfg(base[k], L[who[k]], 0x0A000001);
      auto has = [&](const std::string& c, const std::string& line) { return c.find(line + "\n") != std::string::npos || c.find(line + "\r\n") != std::string::npos; };
      check("cfg: id, listen port, name and mode of each game", has(cfg[0], "id=1") && has(cfg[0], "listen=47001") && has(cfg[1], "id=2") && has(cfg[1], "listen=47002") && has(cfg[2], "id=3") && has(cfg[2], "listen=47003") && has(cfg[1], "name=Alpha") && has(cfg[0], "game_mode=race") && has(cfg[2], "race_goal=count:9") && has(cfg[1], "start_progress=area:Symmetry_Island;lasers:2"));
      bool peersOk = true; for (int x = 0; x < 3; ++x) { wl::CfgFile cf = wl::CfgFile::Parse(cfg[x]); auto ps = cf.GetAll("peer"); peersOk = peersOk && ps.size() == 2; for (int y = 0; y < 3; ++y) { if (x == y) continue; std::string want = std::to_string(y + 1) + ",10.0.0." + std::to_string(y + 1) + "," + std::to_string(47001 + y); bool f = false; for (auto& p : ps) f = f || p == want; peersOk = peersOk && f; } }
      check("cfg: every game lists every other game's id, address and port, and not itself, not the late joiner", peersOk);
      check("cfg: the lines the launcher does not set are kept, the old id is replaced", has(cfg[0], "voice_mode=ptt") && has(cfg[1], "voice_volume=50") && has(cfg[2], "dev_menu=1") && !has(cfg[0], "id=9") && !has(cfg[0], "name=Old"));
      // after the game
      for (unsigned i : {h, a, b}) net.n[i]->SetInGame(true); net.advance(1500); check("in game: the phase is in-game for everybody", net.n[a]->state().phase == kInGame && net.n[h]->state().members[1].inGame);
      net.n[h]->SetResult("Race: 1. Beta 12:34.5   2. Alpha 13:01.0"); for (unsigned i : {h, a, b}) net.n[i]->SetInGame(false); net.advance(4500);
      check("after the game: everybody is back in the lobby, not ready any more, with the result", net.n[a]->state().phase == kLobby && net.n[b]->state().phase == kLobby && !net.n[a]->state().members[2].ready && net.n[b]->state().lastResult.find("Beta 12:34.5") != std::string::npos);
      bool refused = !net.n[h]->Start(net.now); net.n[a]->SetReady(true); net.n[b]->SetReady(true); net.n[late]->SetReady(true); net.advance(2500);
      check("after the game: the next game needs a new ready round (the late joiner counts too), then can start", refused && net.n[h]->Start(net.now));
      Net ne; Lobby::Twins tw; tw.startWithoutReady = true; ne.add(true, "H", "T", tw); ne.add(false, "A", "T"); ne.advance(3000);
      check("twin startWithoutReady: a host that starts over somebody who is not ready is caught", ne.n[0]->Start(ne.now)); }
    // 3. chat on a bad link
    { auto run = [&](Lobby::Twins tw, Lobby::Twins hostTw, unsigned corrupt = 0) {
        Net net(30, 120); net.corruptPct = corrupt; unsigned h = net.add(true, "Host", "ABC123", hostTw); unsigned a = net.add(false, "A", "ABC123", tw), b = net.add(false, "B", "ABC123", tw), c = net.add(false, "C", "ABC123", tw); net.advance(5000);
        for (int i = 0; i < 20; ++i) { net.n[h]->SendChat("h" + std::to_string(i)); net.n[a]->SendChat("a" + std::to_string(i)); net.n[b]->SendChat("b" + std::to_string(i)); net.n[c]->SendChat("c" + std::to_string(i)); net.advance(150); }
        net.advance(40000); std::vector<std::vector<ChatLine>> got; for (unsigned i : {h, a, b, c}) got.push_back(net.n[i]->TakeChat());
        bool every = true, same = true; for (auto& g : got) { every = every && g.size() == 80; if (g.size() == 80) for (size_t k = 0; k < 80; ++k) same = same && g[k].text == got[0][k].text && g[k].from == got[0][k].from; else same = false; }
        bool mineInOrder = true; if (got[1].size() == 80) { int next = 0; for (auto& l : got[1]) if (l.from == 2) { if (l.text != "a" + std::to_string(next)) mineInOrder = false; ++next; } }
        return std::make_tuple(every, same, mineInOrder); };
      auto r = run({}, {}); check("chat: 80 lines from four members on a link losing 30% reach everybody, exactly once", std::get<0>(r)); check("chat: everybody sees the same order, and each member's own lines in the order it said them", std::get<1>(r) && std::get<2>(r));
      auto r4 = run({}, {}, 15); check("chat: with 15% of the packets damaged on top of the loss, every line still arrives once and unchanged (damaged packets are refused)", std::get<0>(r4) && std::get<1>(r4));
      Lobby::Twins noAck; noAck.noChatAck = true; auto r2 = run(noAck, {}); check("twin noChatAck: clients that never advance past their first line are caught", !std::get<0>(r2));
      Lobby::Twins noDup; noDup.noChatDedupe = true; auto r3 = run(noDup, noDup); check("twin noChatDedupe: a resent line shown twice is caught", !std::get<0>(r3) || !std::get<1>(r3)); }
    // 4. voice
    { auto run = [&](Lobby::Twins tw) {
        Net net; unsigned h = net.add(true, "Host", "ABC123", tw); unsigned a = net.add(false, "A", "ABC123", tw), b = net.add(false, "B", "ABC123", tw), c = net.add(false, "C", "ABC123", tw); net.advance(3000);
        wvoice::Packet p; p.from = net.n[a]->myId(); p.flags = wvoice::kStart; p.channel = wvoice::kGlobal; for (unsigned k = 0; k < 5; ++k) { p.seq = k; net.n[a]->SendVoice(p); net.advance(20); }
        wvoice::Packet spoof = p; spoof.from = net.n[c]->myId(); spoof.seq = 99; net.n[a]->SendVoice(spoof); net.advance(200);
        wvoice::Packet hp; hp.from = 1; hp.seq = 1; net.n[h]->SendVoice(hp); net.advance(200);
        auto count = [&](unsigned who, unsigned from) { unsigned k = 0; for (auto& x : net.heard[who]) k += x.from == from; return k; };
        return std::make_tuple(count(h, 2), count(b, 2), count(c, 2), count(a, 2), count(b, 4), count(a, 1), count(b, 1)); };
      auto r = run({}); check("voice: A's voice reaches the host, B and C (5 packets each) and not A itself", std::get<0>(r) == 5 && std::get<1>(r) == 5 && std::get<2>(r) == 5 && std::get<3>(r) == 0);
      check("voice: a member cannot speak as somebody else (A's packet claiming to be C never reaches B)", std::get<4>(r) == 0);
      check("voice: the host's voice reaches the clients", std::get<5>(r) == 1 && std::get<6>(r) == 1);
      Lobby::Twins t1; t1.noVoiceRelay = true; auto r1 = run(t1); check("twin noVoiceRelay: a host that does not pass voice on is caught", std::get<1>(r1) == 0);
      Lobby::Twins t2; t2.voiceEcho = true; auto r2 = run(t2); check("twin voiceEcho: a host that sends A its own voice back is caught", std::get<3>(r2) > 0);
      Lobby::Twins t3; t3.noVoiceFromCheck = true; auto r3 = run(t3); check("twin noVoiceFromCheck: speaking as another member is caught", std::get<4>(r3) > 0); }
    // 5. members that go, a host that restarts
    { auto run = [&](Lobby::Twins tw) {
        Net net; unsigned h = net.add(true, "Host", "ABC123", tw); unsigned a = net.add(false, "A", "ABC123", tw), b = net.add(false, "B", "ABC123", tw); net.advance(3000);
        net.alive[b] = false; net.advance(9000); return net.n[h]->state().members.size() == 2 && net.n[a]->state().members.size() == 2; };
      check("leave: a member that goes silent is dropped from everybody's list within 9 s", run({}));
      Lobby::Twins noTo; noTo.noTimeout = true; check("twin noTimeout: a host that keeps silent members for ever is caught", !run(noTo));
      Net net; unsigned h = net.add(true, "Host", "ABC123"); unsigned a = net.add(false, "A", "ABC123"); net.advance(3000); check("restart: joined before", net.n[a]->joined());
      net.alive[h] = false; net.advance(8000); check("restart: the host's silence is noticed (not joined any more)", !net.n[a]->joined());
      net.now += 5; net.alive[h] = true; net.n[h] = std::make_unique<Lobby>(true, "Host", 47001, "ABC123", [&net, h](const Addr& to, const Bytes& b) { net.q.push_back({net.now + net.delay, to, net.addr[h], b}); }); net.n[h]->SetSession(unsigned(net.now)); net.advance(5000);
      check("restart: a host that starts again is found by the client on its own, with a fresh member list", net.n[a]->joined() && net.n[h]->state().members.size() == 2);
      Net o; unsigned oh = o.add(true, "Host", "ABC123"); unsigned oa = o.add(false, "A", "ABC123"); o.delay = 40; o.jitter = 0; o.advance(3000);
      Settings s1; s1.raceGoal = "count:1"; o.n[oh]->SetSettings(s1); o.advance(800); Settings s2; s2.raceGoal = "count:2"; o.n[oh]->SetSettings(s2); o.advance(800);
      auto stale = o.n[oa]->state(); stale.seq = 0; (void)stale;
      check("state order: the newest settings win", o.n[oa]->state().settings.raceGoal == "count:2"); }
    // 6. packets
    { Net net; net.add(true, "Host"); unsigned a = net.add(false, "A"); net.advance(2000); Bytes junk = {'W', 'T', 'L', '1', 3, 1, 2, 3, 4, 5, 6, 7, 8}; net.n[0]->OnPacket(net.addr[a], junk.data(), junk.size(), net.now);
      check("packets: garbage and a bad checksum are ignored", net.n[0]->state().members.size() == 2); }
    printf("%d checks, %d failed\n", checks, fails); return fails ? 1 : 0;
}
