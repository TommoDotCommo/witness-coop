// The voice display's rules (voice_hud.h), game-free. Mutants: VH_MUT_ALWAYS_HINT (the old permanent push-to-talk hint), VH_MUT_HEAR_ALL (players out of earshot listed).
#include "voice_hud.h"
#include <cstdio>
static int failures;
static void check(bool ok, const char* what) { if (!ok) { std::printf("FAILED: %s\n", what); ++failures; } }
int main() {
    using namespace wvhud;
    Heard far{2, "Owl", true, 0.0f, 0.0f}, near{3, "Fox", true, 0.4f, 0.0f}, radio{4, "Wren", true, 0.0f, 0.6f}, quiet{5, "Moth", false, 0.5f, 0.0f}, muted{6, "Lynx", true, 0.5f, 0.0f, 0.0f};
    check(Rows(2, false, "Proximity", {}).empty(), "push to talk, not talking, nobody talking: nothing at all");
    check(Rows(0, false, "Proximity", {}).empty(), "voice off, nobody talking: nothing");
    auto r = Rows(2, true, "Proximity", {}); check(r.size() == 1 && r[0].me && r[0].lit && r[0].text == "Mic on", "push to talk while talking: Mic on, lit");
    r = Rows(1, false, "Proximity", {}); check(r.size() == 1 && r[0].me && !r[0].lit && r[0].text == "Open mic", "open mic: shown while it is the mode, not lit when silent");
    r = Rows(1, true, "Proximity", {}); check(r.size() == 1 && r[0].lit, "open mic: lit while my voice goes out");
    r = Rows(2, true, "Radio", {}); check(r.size() == 1 && r[0].text == "Mic on  -  Radio", "another channel than proximity is named");
    r = Rows(2, false, "Proximity", {far, near, radio, quiet, muted});
    check(r.size() == 2 && !r[0].me && r[0].id == 3 && r[0].text == "Fox" && r[1].id == 4 && r[1].text == "Wren  (radio)", "only players talking whose voice reaches me (not out of range, not silent, not muted), radio marked");
    r = Rows(0, false, "Proximity", {near}); check(r.size() == 1 && r[0].id == 3, "voice off for me: I still see who I hear");
    std::printf("%s\n", failures ? "FAILED" : "voice hud: ALL OK"); return failures ? 1 : 0;
}
