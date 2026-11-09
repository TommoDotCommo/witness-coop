// One run of one randomizer over a recorded game (world.h), for tools/randomizer_sweep.py: prints "ok <fingerprint>", "failed <why>" or
// "missed <why>" (the run read what the recording does not have: no verdict). A crash or a hang is the process's, seen by the sweep.
#include <cstdio>
#include "../generate.h"
#include "../world.h"
int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: one-run <world> <spec>\n"); return 2; }
    wrand::ReplayRaw world; std::vector<wrand::RecordedRun> rec;
    if (!wrand::LoadWorld(argv[1], &world, &rec)) { std::printf("failed not a recorded world\n"); return 2; }
    wrand::Arrays arrays;
    auto r = wrand::Generate(wrand::ParseSpec(argv[2]), &world, &arrays);
    if (r.ok) std::printf("ok %016llx\n", (unsigned long long)r.fingerprint);
    else std::printf("%s %s\n", world.misses ? "missed" : "failed", r.error.c_str());
    return 0;
}
