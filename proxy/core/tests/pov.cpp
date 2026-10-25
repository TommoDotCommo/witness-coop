// POV screen logic (game-free): a sender streams only while someone asked and only to them; the viewer cycles through players in order, can clear,
// toggle the player it looks at, and loses a player that left.
#include "../pov.h"
#include <cstdio>

using namespace witness;

static bool subs(bool noExpiry = false) {
    PovSubs s(noExpiry);
    if (s.wanted(0)) return false;
    s.request(3, 1000); s.request(5, 1500);
    if (s.live(2000).size() != 2) return false;
    s.request(3, 3000);                               // 3 repeats its request, 5 does not
    auto l = s.live(4800);                            // 5's request is 3.3 s old
    if (l.size() != 1 || l[0] != 3) return false;
    if (!s.wanted(5000)) return false;
    if (s.wanted(9000)) return false;                 // nobody asked for a while: stop capturing
    s.request(7, 10000); s.cancel(7); return !s.wanted(10001);
}
static bool bounded() { PovSubs s; for (unsigned i = 1; i <= 40; ++i) s.request(i, 100); return s.live(200).size() == PovSubs::kMax; }
static bool cycle(bool noOffAtEnd = false) {
    PovView v(false, noOffAtEnd); std::vector<unsigned> ps = {5, 2, 9, 2};
    bool a = v.next(ps) == 2 && v.next(ps) == 5 && v.next(ps) == 9 && v.next(ps) == 0 && v.next(ps) == 2;     // ascending, off after the last, then round again
    PovView w; bool b = w.prev(ps) == 9 && w.prev(ps) == 5 && w.prev(ps) == 2 && w.prev(ps) == 0;
    PovView e; bool c = e.next({}) == 0;
    return a && b && c;
}
static bool clearing() {
    PovView v; v.watch(4); if (v.current() != 4) return false; v.clear(); if (v.current() != 0) return false;
    bool t1 = v.toggle(6), t2 = v.toggle(6);          // looking at 6: watch; again: stop
    return t1 && !t2 && v.current() == 0 && v.toggle(6) && v.toggle(8) && v.current() == 8;
}
static bool leaving(bool twin = false) { PovView v(twin); v.watch(5); v.update({2, 5}); if (v.current() != 5) return false; v.update({2, 9}); return v.current() == 0; }

int main() {
    bool a = subs(), b = bounded(), c = cycle(), d = clearing(), e = leaving(), f = !subs(true), g = !cycle(true), h = !leaving(true);
    std::printf("%s  pov: requests keep a sender streaming, lapse after 3 s, and can be cancelled\n", a ? "PASS" : "FAIL");
    std::printf("%s  pov: at most 16 viewers\n", b ? "PASS" : "FAIL");
    std::printf("%s  pov: cycling goes through the players in id order, then off\n", c ? "PASS" : "FAIL");
    std::printf("%s  pov: clear and toggle\n", d ? "PASS" : "FAIL");
    std::printf("%s  pov: a player who left is dropped\n", e ? "PASS" : "FAIL");
    std::printf("%s  twin no-expiry caught\n", f ? "PASS" : "FAIL");
    std::printf("%s  twin cycle-never-off caught\n", g ? "PASS" : "FAIL");
    std::printf("%s  twin no-leave-check caught\n", h ? "PASS" : "FAIL");
    bool ok = a && b && c && d && e && f && g && h; std::printf("%s\n", ok ? "ALL OK" : "FAILED"); return ok ? 0 : 1;
}
