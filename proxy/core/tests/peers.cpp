// The roster and its receive filter (peers.h) for both transports, game-free. Three production sync cores talk over a fake Steam link that loses,
// duplicates and reorders messages, addressed by SteamID; a fourth Steam user who is not in the roster sends the same protocol, once as itself and once
// claiming to be member 3, and a member claims to be another member. Only roster members, each as itself, may change a world.
// Mutants (build flags): WP_MUT_STEAM_ANY_SENDER (a Steam datagram is taken from anybody), WP_MUT_STEAM_ANY_MEMBER (a member may speak as another member).
#include "../peers.h"
#include "../sync_core.h"
#include <array>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <vector>
static int fails = 0, checks = 0;
static void check(const char* what, bool ok) { ++checks; if (!ok) { ++fails; printf("FAILED: %s\n", what); } }

struct World : witness::World {
    std::array<bool, 32> solved{}, doors{}, power{}, cables{}; unsigned player = 0;
    bool cameraValid() override { return true; }
    bool scan(std::vector<unsigned>& p, std::vector<unsigned>& d, uint64_t* t) override { for (unsigned i = 1; i <= 8; ++i) { p.push_back(i); d.push_back(i + 8); } *t = 1; return true; }
    bool scanCables(std::vector<unsigned>& c) override { c = {17, 18}; return true; }
    bool entityExists(unsigned id) override { return id > 0 && id <= 18; }
    bool isPanel(unsigned id) override { return id > 0 && id <= 8; }
    bool isDoor(unsigned id) override { return id > 8 && id <= 16; }
    bool isCable(unsigned id) override { return id == 17 || id == 18; }
    bool panelSolved(unsigned id) override { return solved[id]; }
    bool doorOpen(unsigned id) override { return doors[id]; }
    bool panelPowered(unsigned id) override { return power[id]; }
    bool cableLit(unsigned id) override { return cables[id]; }
    void applyPanelSolved(unsigned id) override { solved[id] = true; }
    bool applyDoorOpen(unsigned id) override { doors[id] = true; return true; }
    bool applyPanelPower(unsigned id) override { power[id] = true; return true; }
    bool applyCableLit(unsigned id) override { cables[id] = true; return true; }
    bool applyCableOff(unsigned id) override { cables[id] = false; return true; }
    bool pose(float p[3], float a[2]) override { p[0] = float(player); p[1] = p[2] = a[0] = a[1] = 0; return true; }
};

constexpr uint64_t kSteamBase = 76561197960265728ull;      // individual accounts start here
uint64_t Sid(unsigned n) { return kSteamBase + 1000 + n; }

// One node: a sync core with its world, its roster (the cfg the launcher wrote) and its own SteamID. A node may lie about its id (the impostor).
struct Node { World w; std::unique_ptr<witness::SyncCore> core; wpeers::PeerTable table; uint64_t steam = 0; unsigned rejected = 0, taken = 0; };
struct Msg { uint64_t at; uint64_t from, to; witness::Bytes b; };
struct SteamNet {
    std::vector<std::unique_ptr<Node>> nodes; std::vector<Msg> q; uint64_t now = 0; unsigned seed = 11, lossPct = 20, dupPct = 10, jitter = 150;
    unsigned rnd(unsigned m) { seed = seed * 1103515245u + 12345u; return m ? (seed >> 16) % m : 0; }
    // id: the player id this node's core uses; roster: player id -> SteamID of the others (what its cfg says)
    Node* add(unsigned id, uint64_t steam, const std::map<unsigned, uint64_t>& roster) {
        auto n = std::make_unique<Node>(); Node* p = n.get(); p->steam = steam; p->w.player = id;
        std::string cfg; for (const auto& kv : roster) { cfg = "peer=" + std::to_string(kv.first) + ",steam:" + steamids::Text(kv.second) + "\n"; p->table.ParseLine(cfg.c_str(), id, [](const char*) { return 0xffffffffu; }); }
        p->table.CheckSelf(id, steam);
        witness::Hooks h;
        h.sendTo = [this, p](unsigned to, const witness::Bytes& b) {      // the Steam transport: only roster members have an address
            const wpeers::Endpoint* e = p->table.Find(to); if (!e) return;
            if (rnd(100) < lossPct) return;
            unsigned copies = rnd(100) < dupPct ? 2 : 1; for (unsigned k = 0; k < copies; ++k) q.push_back({now + 20 + rnd(jitter), p->steam, e->steam, b});
        };
        p->core = std::make_unique<witness::SyncCore>(id, 1, &p->w, h);
        std::vector<unsigned> ids; for (const auto& kv : roster) ids.push_back(kv.first); if (!p->core->setPeers(ids)) std::abort();
        nodes.push_back(std::move(n)); return p;
    }
    void advance(unsigned ms) {
        uint64_t end = now + ms;
        while (now < end) {
            now += 50;
            for (auto& n : nodes) { n->core->pollState(now); n->core->tick(now); }
            auto cur = std::move(q); q.clear();
            for (auto& m : cur) {
                if (m.at > now) { q.push_back(std::move(m)); continue; }
                for (auto& n : nodes) if (n->steam == m.to) {
                    if (n->table.Accept(wpeers::Steam(m.from), m.b.data(), m.b.size(), wpeers::Endpoint())) { ++n->taken; n->core->onPacket(m.b.data(), m.b.size(), now); }
                    else ++n->rejected;
                }
            }
            for (auto& n : nodes) n->core->applyRemote(now);
        }
    }
};

static void ParseTests() {
    auto bad = [](const char*) { return 0xffffffffu; };
    auto ip = [](const char* a) -> uint32_t { unsigned x[4]; if (sscanf(a, "%u.%u.%u.%u", &x[0], &x[1], &x[2], &x[3]) != 4) return 0xffffffffu; return x[0] | x[1] << 8 | x[2] << 16 | x[3] << 24; };
    { wpeers::PeerTable t; t.ParseLine("peer=2,10.0.0.2,47002\n", 1, ip); t.ParseLine("peer=3,10.0.0.3,47003\r\n", 1, ip); t.CheckSelf(1, 0);
      check("cfg: the UDP form (peer=<id>,<ip>,<port>) still parses", t.ok() && !t.steam() && t.peers().size() == 2 && t.Find(3) && t.Find(3)->port == 47003 && t.Find(3)->ip == ip("10.0.0.3")); }
    { wpeers::PeerTable t; t.ParseLine(("peer=2,steam:" + steamids::Text(Sid(2)) + "\n").c_str(), 1, bad); t.ParseLine(("peer=3,steam:" + steamids::Text(Sid(3))).c_str(), 1, bad); t.CheckSelf(1, Sid(1));
      check("cfg: the Steam form (peer=<id>,steam:<steamid64>) parses", t.ok() && t.steam() && t.Find(2) && t.Find(2)->steam == Sid(2) && t.Find(3)->steam == Sid(3)); }
    { wpeers::PeerTable t; check("cfg: the old two-player line (peer=<port>) is not a group line", !t.ParseLine("peer=47002\n", 1, ip) && t.ok() && t.empty()); }
    auto refused = [&](std::vector<std::string> lines, unsigned me, uint64_t mySteam) { wpeers::PeerTable t; for (auto& l : lines) t.ParseLine(l.c_str(), me, ip); t.CheckSelf(me, mySteam); return !t.ok(); };
    std::string s2 = "peer=2,steam:" + steamids::Text(Sid(2)), s3 = "peer=3,steam:" + steamids::Text(Sid(3));
    check("cfg: UDP and Steam peers in one cfg are refused (no silent fallback)", refused({s2, "peer=3,10.0.0.3,47003"}, 1, 0));
    check("cfg: a lobby id or a malformed SteamID is refused", refused({"peer=2,steam:109775240000000001"}, 1, 0) && refused({"peer=2,steam:12x"}, 1, 0) && refused({"peer=2,steam:"}, 1, 0));
    check("cfg: two peers with one SteamID are refused", refused({s2, "peer=3,steam:" + steamids::Text(Sid(2))}, 1, 0));
    check("cfg: this game's own SteamID in the roster is refused", refused({s2, s3}, 1, Sid(3)));
    check("cfg: this game's own id in the roster is refused", refused({s2}, 2, 0));
    std::vector<std::string> many; for (unsigned i = 2; i <= 17; ++i) many.push_back("peer=" + std::to_string(i) + ",steam:" + steamids::Text(Sid(i)));
    check("cfg: at most 15 peers (16 players)", refused(many, 1, 0) && !refused(std::vector<std::string>(many.begin(), many.begin() + 15), 1, 0));
    uint64_t lob = 0; check("ids: +connect_lobby on a command line is found, a player's id is not a lobby", steamids::ParseConnect("\"C:\\x\\witness64_d3d11.exe\" +connect_lobby 109775241234567890 -foo", &lob) && lob == 109775241234567890ull && !steamids::ParseConnect("+connect_lobby " + steamids::Text(Sid(1)), &lob) && !steamids::ParseConnect("x+connect_lobby 109775241234567890", &lob));
    check("ids: a typed lobby id", steamids::ParseLobby(" 109775241234567890 ", &lob) && lob == 109775241234567890ull && !steamids::ParseLobby("abc", &lob) && !steamids::ParseLobby(steamids::Text(Sid(4)), &lob));
}

int main() {
    ParseTests();
    SteamNet net;
    std::map<unsigned, uint64_t> all = {{1, Sid(1)}, {2, Sid(2)}, {3, Sid(3)}};
    Node* nodes[3]; for (unsigned i = 1; i <= 3; ++i) { std::map<unsigned, uint64_t> r = all; r.erase(i); nodes[i - 1] = net.add(i, Sid(i), r); }
    // the impostor: a Steam user who is not in the lobby, running the same protocol, claiming to be player 3 (its roster names the real members)
    Node* impostor = net.add(3, Sid(99), {{1, Sid(1)}, {2, Sid(2)}});
    // a stranger who sends as itself (player id 4, unknown to the roster)
    Node* stranger = net.add(4, Sid(98), {{1, Sid(1)}, {2, Sid(2)}, {3, Sid(3)}});
    net.advance(32000);
    nodes[0]->w.solved[1] = true; nodes[1]->w.doors[10] = true; nodes[2]->w.power[3] = true;
    impostor->w.solved[5] = true; stranger->w.solved[6] = true;
    net.advance(45000);
    for (Node* n : nodes) if (getenv("PEERS_DEBUG")) printf("node %u: solved1 %d door10 %d power3 %d taken %u rejected %u ready %d\n", n->w.player, (int)n->w.solved[1], (int)n->w.doors[10], (int)n->w.power[3], n->taken, n->rejected, (int)n->core->ready());
    bool synced = true; for (Node* n : nodes) synced = synced && n->w.solved[1] && n->w.doors[10] && n->w.power[3];
    check("steam: a solve, a door and a power change reach all three members over a link losing 20%, duplicating 10% and reordering", synced);
    bool poses = true; for (Node* n : nodes) poses = poses && n->core->remotePoses(net.now).size() == 2;
    check("steam: every member sees both other members", poses);
    bool clean = true; for (Node* n : nodes) clean = clean && !n->w.solved[5] && !n->w.solved[6];
    check("steam: nothing a non-member sends changes a member's world (as itself or claiming a member's id)", clean);
    unsigned rejected = 0; for (Node* n : nodes) rejected += n->rejected; check("steam: the non-members' datagrams were seen and refused", rejected > 50);
    // a member who claims to be another member: player 2's SteamID sending a datagram that names player 3
    { witness::Bytes b(24, 0); b[4] = 3; check("steam: a member speaking as another member is refused", !nodes[0]->table.Accept(wpeers::Steam(Sid(2)), b.data(), b.size(), wpeers::Endpoint()));
      b[4] = 2; check("steam: a member speaking as itself is taken", nodes[0]->table.Accept(wpeers::Steam(Sid(2)), b.data(), b.size(), wpeers::Endpoint())); }
    // UDP keeps today's rule
    { wpeers::PeerTable t; t.Add(2, wpeers::Udp(0x0200000a, 47002), 1); witness::Bytes b(24, 0); b[4] = 2;
      check("udp: a member's datagram from its own address is taken, from another port it is not", t.Accept(wpeers::Udp(0x0200000a, 47002), b.data(), b.size(), {}) && !t.Accept(wpeers::Udp(0x0200000a, 47009), b.data(), b.size(), {}));
      wpeers::PeerTable one; check("udp: the old two-player form takes only its one peer", one.Accept(wpeers::Udp(1, 2), b.data(), b.size(), wpeers::Udp(1, 2)) && !one.Accept(wpeers::Udp(1, 3), b.data(), b.size(), wpeers::Udp(1, 2))); }
    // a Steam session that fails mid-game is said in the game (connected -> closed by peer, problem locally, or gone); a session still being set up is not
    check("steam: a lost session is said (closed by peer, a local problem, or gone after it worked)", wpeers::SessionLost(3, 4) && wpeers::SessionLost(3, 5) && wpeers::SessionLost(3, 0) && wpeers::SessionLost(1, 5));
    check("steam: connecting, finding a route and connected are not alarms", !wpeers::SessionLost(0, 1) && !wpeers::SessionLost(1, 2) && !wpeers::SessionLost(2, 3) && !wpeers::SessionLost(3, 3) && !wpeers::SessionLost(0, 0));
    printf("%d checks, %d failed\n", checks, fails); return fails ? 1 : 0;
}
