// Game-free (T0) test of the save sidecar: progress counts from the real data, the text round trip, players of earlier sessions merged in.
#include "savemeta.h"
#include <cstdio>
using namespace wprog;
static int fails = 0, checks = 0;
static void check(const char* what, bool ok) { ++checks; if (!ok) { ++fails; printf("FAILED: %s\n", what); } }
int main() {
    const Data& d = Builtin();
    // the first area solved, and the lasers' panels of two lasers
    int tin = d.AreaByName("Tutorial (Inside)"), sym = d.AreaByName("Symmetry Island");
    std::set<unsigned> solved; for (const auto& p : d.panels) if (p.area == tin && !p.optional) solved.insert(p.id);
    SaveMeta m; FillProgress(d, solved, 3, 106, &m);
    unsigned total = 0; for (const auto& p : d.panels) if (!p.optional) ++total;
    check("progress: solved is the first area, total every non-optional panel", m.solved == d.AreaCount(tin) && m.total == total);
    check("areas: only those with panels, the first area full, Symmetry Island empty", [&] { bool a = false, b = false; for (const auto& x : m.areas) { if (x.total == 0) return false; if (x.name == d.areas[tin]) a = x.solved == x.total && x.total == d.AreaCount(tin); if (x.name == d.areas[sym]) b = x.solved == 0 && x.total == d.AreaCount(sym); } return a && b; }());
    check("doors are passed through", m.doorsOpen == 3 && m.doorsTotal == 106);
    check("lasers: none lit yet, 11 in all", m.lasers == 0 && m.lasersTotal == 11);
    solved.insert(d.lasers[0]); solved.insert(d.lasers[4]); FillProgress(d, solved, 0, 0, &m);
    check("lasers: two lit when two laser panels are solved", m.lasers == 2);
    // the text
    m.savedAt = "2026-10-26 01:17:36"; m.sessionSeconds = 90; m.totalSeconds = 4000;
    m.players = {{1, "Alpha Fox", "fox", 3600, 2}, {2, "Beta", "", 400, 1}};
    SaveMeta back; std::string t = FormatMeta(m);
    check("the text parses back to the same", ParseMeta(t, &back) && back.savedAt == m.savedAt && back.solved == m.solved && back.total == m.total && back.lasers == 2 && back.areas.size() == m.areas.size() && back.players.size() == 2);
    check("players keep names with spaces, characters and times", back.players[0].name == "Alpha Fox" && back.players[0].character == "fox" && back.players[0].seconds == 3600 && back.players[0].sessions == 2 && back.players[1].character.empty());
    check("an area name with spaces survives", [&] { for (const auto& a : back.areas) if (a.name == "Symmetry Island") return true; return false; }());
    check("not a sidecar: refused", !ParseMeta("hello\n", &back) && !ParseMeta("coop-save 2\n", &back) && !ParseMeta("coop-save 1\nplayer\t1\tx\n", &back) && !ParseMeta("coop-save 1\nwhat 3\n", &back));
    check("a tab or newline in a name cannot break the file", [&] { SaveMeta x = m; x.players[0].name = "A\tB\nC"; SaveMeta y; return ParseMeta(FormatMeta(x), &y) && y.players.size() == 2 && y.players[0].name == "A B C"; }());
    // earlier sessions
    std::vector<MetaPlayer> now = {{1, "Alpha Fox", "fox", 100, 1}}, earlier = {{7, "Alpha Fox", "frog", 3600, 2}, {8, "Gamma", "", 50, 1}};
    MergePlayers(&now, earlier);
    check("merge: the same name adds up, a new name joins", now.size() == 2 && now[0].seconds == 3700 && now[0].sessions == 3 && now[0].character == "fox" && now[1].name == "Gamma");
    std::string line = OneLine(m);
    check("one line for a list: percentage, lasers, players, time", line.find("% done") != std::string::npos && line.find("2 lasers") != std::string::npos && line.find("Alpha Fox + Beta") != std::string::npos && line.find("1 h 6 min") != std::string::npos);
    printf("%d checks, %d failed\n", checks, fails); return fails ? 1 : 0;
}
