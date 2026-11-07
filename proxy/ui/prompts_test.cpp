#include "prompts.h"
#include <cstdio>
static int failures;
static void check(bool ok, const char* what) { if (!ok) { std::printf("FAILED: %s\n", what); ++failures; } }
static bool has(const std::string& s, const char* w) { return s.find(w) != std::string::npos; }
int main() {
    using namespace wprompt;
    std::string c = Canvas("X", "Mouse wheel", "Z");
    check(has(c, "Mouse wheel: colour") && !has(c, "B colour") && has(c, "Hold X: draw") && has(c, "Z: close") && has(c, "Shift+X: clear"), "the canvas hint names the keys the player has");
    check(has(Canvas("Q", "C", "U"), "Hold Q: draw") && has(Canvas("Q", "C", "U"), "C: colour") && has(Canvas("Q", "C", "U"), "U: close"), "changed keys appear in the canvas hint");
    check(Seat("K", false) == "K: sit and watch" && SkyTv("J", "Beta", true) == "J: take down Beta's sky TV", "TV prompts in the same style");
    std::printf("%s\n", failures ? "FAILED" : "prompts: ALL OK"); return failures ? 1 : 0;
}
