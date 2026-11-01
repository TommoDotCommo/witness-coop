#pragma once
// The game's roster and who it takes datagrams from, game-free (tests/peers.cpp). Used by coop.cpp for both transports.
// A peer is either a UDP endpoint or a SteamID64. witness_coop.cfg lines:
//   peer=<id>,<IPv4>,<port>     a group member over UDP (LAN, or a forwarded port)
//   peer=<id>,steam:<steamid64> a group member over Steam (relayed by Steam: no port forwarding); the launcher writes these for a Steam lobby
//   peer=<port> [peer_host=<IPv4>]  the old two-player form (no ids)
// One game uses one transport: UDP and Steam peers in the same cfg are refused (and nothing ever falls back from Steam to UDP).
// Every datagram of the group protocol names its sender at bytes 4..7; it is taken only when it arrives from that sender's own endpoint,
// so a Steam user who is not in the roster, or a member who claims to be another member, is never heard.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include "../../common/steam_ids.h"

namespace wpeers {
constexpr unsigned kMaxPeers = 15;      // with this game: 16 players (witness::kMaxPlayers)
struct Endpoint {
    uint32_t ip = 0; uint16_t port = 0; uint64_t steam = 0;      // ip: as in in_addr.s_addr; port: host order; steam: the SteamID64 (then ip/port are 0)
    bool isSteam() const { return steam != 0; }
    bool operator==(const Endpoint& o) const { return ip == o.ip && port == o.port && steam == o.steam; }
};
inline Endpoint Udp(uint32_t ip, uint16_t port) { Endpoint e; e.ip = ip; e.port = port; return e; }
inline Endpoint Steam(uint64_t id) { Endpoint e; e.steam = id; return e; }

// A Steam session with a player is worth telling the player about when Steam gave up on it or it ended after it had worked
// (states as in ESteamNetworkingConnectionState: 0 none, 1 connecting, 2 finding route, 3 connected, 4 closed by peer, 5 problem locally).
inline bool SessionLost(int was, int now) {
#ifdef WP_MUT_SILENT_LOSS
    return false;      // mutant: a lost session is never said
#endif
    return now == 4 || now == 5 || (now == 0 && was == 3);
}
class PeerTable {
public:
    // Reads one cfg line. Returns true when it was a group peer line (good or bad: bad ones set error()). `resolve` turns "a.b.c.d" into in_addr.s_addr (0xffffffff: bad).
    template <typename Resolve> bool ParseLine(const char* line, unsigned myId, Resolve resolve) {
        if (strncmp(line, "peer=", 5) || !strchr(line, ',')) return false;
        unsigned id = 0; char rest[96] = {}; char tail[8] = {};
        if (sscanf(line, "peer=%u,%95[^\r\n]", &id, rest) != 2) { Fail("bad peer line"); return true; }
        if (!strncmp(rest, "steam:", 6)) {
            char digits[32] = {}; if (sscanf(rest + 6, "%31[0-9]%7s", digits, tail) != 1) { Fail("bad steam peer"); return true; }
            uint64_t sid = 0; if (!steamids::ParseU64(digits, &sid) || !steamids::IsIndividual(sid)) { Fail("not a player's SteamID"); return true; }
            Add(id, Steam(sid), myId); return true;
        }
        char host[64] = {}; int port = 0;
        if (sscanf(rest, "%63[^,],%d%7s", host, &port, tail) != 2 || port < 1 || port > 65535) { Fail("bad udp peer"); return true; }
        uint32_t ip = resolve(host); if (ip == 0xffffffffu) { Fail("bad peer address"); return true; }
        Add(id, Udp(ip, (uint16_t)port), myId); return true;
    }
    void Add(unsigned id, const Endpoint& e, unsigned myId) {
        if (!id || id > 0x7fffffffU || id == myId) { Fail("bad peer id"); return; }
        if (byId_.size() >= kMaxPeers) { Fail("too many peers"); return; }
        if (!byId_.empty() && byId_.begin()->second.isSteam() != e.isSteam()) { Fail("UDP and Steam peers mixed"); return; }
        for (const auto& kv : byId_) if (kv.second == e) { Fail("duplicate peer endpoint"); return; }
        if (!byId_.emplace(id, e).second) Fail("duplicate peer id");
    }
    void CheckSelf(unsigned myId, uint64_t mySteam) {      // after the cfg: the roster must not contain this game (its id, or its own SteamID)
        if (byId_.count(myId)) Fail("own id in the roster");
        if (mySteam) for (const auto& kv : byId_) if (kv.second.steam == mySteam) Fail("own SteamID in the roster");
    }
    bool ok() const { return error_.empty(); }
    const std::string& error() const { return error_; }
    bool empty() const { return byId_.empty(); }
    bool steam() const { return !byId_.empty() && byId_.begin()->second.isSteam(); }
    const std::map<unsigned, Endpoint>& peers() const { return byId_; }
    const Endpoint* Find(unsigned id) const { auto it = byId_.find(id); return it == byId_.end() ? nullptr : &it->second; }
    // A received datagram: taken only from the endpoint of the member it names. (The old two-player form has no ids: `single` is its one peer.)
    bool Accept(const Endpoint& from, const uint8_t* d, size_t n, const Endpoint& single) const {
        if (byId_.empty()) return from == single;
        if (n < 8) return false;
        unsigned sender = unsigned(d[4]) | unsigned(d[5]) << 8 | unsigned(d[6]) << 16 | unsigned(d[7]) << 24;
#ifdef WP_MUT_STEAM_ANY_SENDER
        if (from.isSteam()) return byId_.count(sender) != 0;          // mutant: a Steam datagram is taken for whatever member it names, from anybody
#endif
#ifdef WP_MUT_STEAM_ANY_MEMBER
        if (from.isSteam()) { for (const auto& kv : byId_) if (kv.second == from) return byId_.count(sender) != 0; return false; }      // mutant: any member may speak as any other
#endif
        const Endpoint* e = Find(sender); return e && *e == from;
    }
private:
    void Fail(const char* why) { if (error_.empty()) error_ = why; }
    std::map<unsigned, Endpoint> byId_; std::string error_;
};
}  // namespace wpeers
