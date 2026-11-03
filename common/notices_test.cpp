#include "notices.h"
#include <iostream>
static int failures;
static void check(bool ok, const char* what) { if (!ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; } }
using namespace wnotice;
int main() {
    check(Joined("Fox") == "Fox joined" && Left("Fox") == "Fox left", "joined / left");
    check(Lost("Fox") == "Lost Fox, waiting up to 30 s", "lost");
    check(Reconnecting("Fox") == "Reconnecting to Fox", "reconnecting");
    check(DifferentVersion("Fox", "v0.4.0") == "Fox has a different version (v0.4.0)", "different version");
    check(DifferentVersion("Fox", "") == "Fox has a different version", "different version, version unknown");
    Roster r; std::map<unsigned, Seen> m; uint64_t t = 1000;
    auto seen = [&](unsigned id, bool valid, uint64_t age, const char* nm) { m[id] = {valid, age, nm}; };
    check(r.Update(m, t).empty(), "nobody: nothing");
    seen(2, true, 100, "Fox"); auto n = r.Update(m, t += 1000); check(n.size() == 1 && n[0] == "Fox joined", "first pose: Fox joined");
    check(r.Update(m, t += 1000).empty(), "still there: nothing again");
    seen(3, false, 0, ""); check(r.Update(m, t += 1000).empty(), "an entry with no pose yet (loading) is not announced");
    seen(3, true, 50, ""); n = r.Update(m, t += 1000); check(n.size() == 1 && n[0] == "Player 3 joined", "an unnamed player is called Player N");
    seen(2, true, 6000, "Fox"); n = r.Update(m, t += 1000); check(n.size() == 1 && n[0] == "Lost Fox, waiting up to 30 s", "5 s of silence: Lost Fox");
    check(r.Update(m, t += 1000).empty(), "lost is said once");
    seen(2, true, 200, "Fox"); check(r.Update(m, t += 1000).empty(), "heard again: no second 'joined'");
    seen(2, true, 7000, "Fox"); r.Update(m, t += 1000);
    n = r.Update(m, t += 31000); check(n.size() == 1 && n[0] == "Fox left", "30 s of silence: Fox left");
    seen(2, true, 100, "Fox"); n = r.Update(m, t += 1000); check(n.size() == 1 && n[0] == "Fox joined", "back after leaving: joined again");
    m.erase(3); n = r.Update(m, t += 1000); check(n.size() == 1 && n[0] == "Player 3 left", "a player removed from the roster: left");
    std::cout << (failures ? "FAILED" : "ALL OK") << '\n'; return failures ? 1 : 0;
}
