// Game-free (T0) test of the progression data and the completion of parts of the map. Facts are ones the game itself decides (a door's panel, a gate), checked on the real data.
#include "progress.h"
#include <cstdio>
#include <set>
using namespace wprog;
static int fails = 0, checks = 0;
static void check(const char* what, bool ok) { ++checks; if (!ok) { ++fails; printf("FAILED: %s\n", what); } }
static bool has(const std::vector<unsigned>& v, unsigned id) { for (unsigned x : v) if (x == id) return true; return false; }
static bool closedAndOrdered(const Data& d, const std::vector<unsigned>& v) {      // every panel's prerequisites are in the set and come before it
    std::set<unsigned> seen;
    for (unsigned id : v) { for (unsigned n : d.panels[d.index.at(id)].needs) if (!seen.count(n)) return false; seen.insert(id); }
    return true;
}
static std::set<int> areasOf(const Data& d, const std::vector<unsigned>& v) { std::set<int> s; for (unsigned id : v) s.insert(d.panels[d.index.at(id)].area); return s; }
static size_t lasersIn(const Data& d, const std::vector<unsigned>& v) { size_t n = 0; for (unsigned l : d.lasers) if (has(v, l)) ++n; return n; }

int main() {
    const Data& d = Builtin();
    check("the data parses: 24 areas, hundreds of panels, 11 lasers", d.areas.size() == 24 && d.panels.size() > 500 && d.lasers.size() == 11);
    // the data itself: no self-need, needs are strict subsets (so the order "fewer prerequisites first" is a valid order), acyclic by that
    bool selfok = true, subset = true;
    for (const auto& p : d.panels) { std::set<unsigned> mine(p.needs.begin(), p.needs.end()); if (mine.count(p.id)) selfok = false; for (unsigned n : p.needs) { std::set<unsigned> theirs(d.panels[d.index.at(n)].needs.begin(), d.panels[d.index.at(n)].needs.end()); for (unsigned x : theirs) if (!mine.count(x)) subset = false; if (theirs.size() >= mine.size()) subset = false; } }
    check("no panel needs itself", selfok); check("a prerequisite's own prerequisites are all prerequisites too (transitive, no loop)", subset);
    int sym = d.AreaByName("symmetry island"), tin = d.AreaByName("Tutorial (Inside)"), tout = d.AreaByName("tutorial_outside"), gf = d.AreaByName("Glass Factory"), mo = d.AreaByName("Mountain (Outside)"), des = d.AreaByName("Desert");
    check("areas are found by name, whatever the case or punctuation", sym >= 0 && tin >= 0 && tout >= 0 && gf >= 0 && mo >= 0 && des >= 0 && d.AreaByName("nowhere") < 0);
    // the first area: the hallway, the bend, the gate; nothing else
    auto t = Complete(d, {tin});
    check("the first area is its own panels only, the first hallway panel first", t.size() == d.AreaCount(tin) && t.front() == 0x64 && closedAndOrdered(d, t));
    // symmetry island: its entry door is opened by a panel that needs the glass factory's last panel, which needs the tutorial gate open
    auto s = Complete(d, {sym});
    check("Symmetry Island needs the Glass Factory's last panel (0x343A), the island's lower door panel (0xB0) and the tutorial gate (0x3629)", has(s, 0x343A) && has(s, 0xB0) && has(s, 0x3629));
    check("... and nothing from the other areas", [&] { auto a = areasOf(d, s); for (int x : a) if (x != sym && x != gf && x != tin && x != tout) return false; return true; }());
    check("the result is closed and prerequisites come first", closedAndOrdered(d, s));
    auto ds = Complete(d, {des});
    check("the desert needs the tutorial gate but not Symmetry Island", has(ds, 0x3629) && !has(ds, 0x22) && closedAndOrdered(d, ds));
    // the mountain opens with seven lasers
    auto m = Complete(d, {mo});
    check("the way up the mountain needs at least seven lasers lit", lasersIn(d, m) >= 7 && closedAndOrdered(d, m));
    // lasers
    for (int n : {0, 1, 3, 7, 11}) { auto l = Lasers(d, n); check("n lasers: at least n laser panels, closed, ordered", lasersIn(d, l) >= (size_t)n && closedAndOrdered(d, l) && (n || l.empty())); }
    check("lasers are cheapest first: fewer panels than the first n lasers in the list", [&] { for (int n : {3, 5, 7}) { std::set<unsigned> first; for (int i = 0; i < n; ++i) { first.insert(d.lasers[i]); for (unsigned x : d.panels[d.index.at(d.lasers[i])].needs) first.insert(x); } if (!(Lasers(d, n).size() < first.size())) { printf("  n %d: %zu vs %zu\n", n, Lasers(d, n).size(), first.size()); return false; } } return true; }());
    check("seven lasers cost less than everything", Lasers(d, 7).size() < d.panels.size());
    // monotone and union
    auto a = Complete(d, {sym}), b = Complete(d, {des}), ab = Complete(d, {sym, des});
    check("two parts together contain both", [&] { for (unsigned x : a) if (!has(ab, x)) return false; for (unsigned x : b) if (!has(ab, x)) return false; return closedAndOrdered(d, ab); }());
    // text
    std::string err; auto r = Resolve(d, "area:Symmetry Island", &err);
    check("a choice written as text gives the same as asking directly", r == s && err.empty());
    check("lasers and presets in text", Resolve(d, "lasers:7").size() == Lasers(d, 7).size() && Resolve(d, "preset:seven-lasers") == Resolve(d, "lasers:7") && Resolve(d, "preset:tutorial", &err) == Complete(d, {tin, tout}));
    check("a single panel brings its prerequisites", Resolve(d, "panel:0x22") .size() > 5 && has(Resolve(d, "panel:0x22"), 0x343A));
    size_t opt = 0; for (const auto& p : d.panels) if (p.optional) ++opt;
    check("everything is every panel but the optional ones", opt >= 1 && Resolve(d, "area:all").size() + opt == d.panels.size());
    check("an optional panel asked for by itself brings its long list of prerequisites", Resolve(d, "panel:0x3505").size() > 100);
    check("errors are said", Resolve(d, "area:Nowhere", &err).empty() && !err.empty() && Resolve(d, "lasers:99", &err).empty() && Resolve(d, "panel:0xFFFFF", &err).empty() && Resolve(d, "preset:x", &err).empty() && Resolve(d, "colour:red", &err).empty());
    for (const auto& n : PresetNames()) check("every preset resolves", !Resolve(d, PresetSpec(n)).empty() || n == "");
    Data bad; std::string e;
    check("garbage does not parse", !Parse("panel zz 0 x\n", &bad, &e) && !Parse("area 0 A\npanel 1 5 x\n", &bad, &e) && !Parse("area 0 A\npanel 1 0 x 2\n", &bad, &e));
    printf("%d checks, %d failed\n", checks, fails); return fails ? 1 : 0;
}
