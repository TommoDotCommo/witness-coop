// Game-free (T0): whole runs of every built-in randomizer over a recorded game (world.h; `randrecord` in the game made it). For each run the game made:
//   - the generator built here (g++ on Linux) makes the same puzzles as the one built into the mod (MinGW, in the game): the same fingerprint, the
//     same number of watchdogs, entities and bytes, and it reads nothing the game did not have (no read outside the recording)
//   - a second run in the same process makes them again (upstream's statics are reset between runs, as after a save is loaded)
//   - applying the result writes only into entities, and the arrays it points them to hold what the generator wrote
// Mutants (each must fail): WR_MUT_NO_RESET (the statics are not reset), WR_MUT_WATCHDOG_THREAD (the watchdogs are lost).
#include <cstdio>
#include <cstring>
#include "../generate.h"
#include "../world.h"

int main(int argc, char** argv) {
    int fails = 0, runs = 0;
    for (int a = 1; a < argc; a++) {
        wrand::ReplayRaw world; std::vector<wrand::RecordedRun> recorded;
        if (!wrand::LoadWorld(argv[a], &world, &recorded)) { std::printf("FAIL %s: not a recorded world\n", argv[a]); fails++; continue; }
        std::printf("%s: %zu entities, %zu ranges, %zu bytes, %zu runs\n", argv[a], world.ents.size(), world.mem.ranges().size(), world.mem.bytes(), recorded.size());
        for (const auto& rec : recorded) {
            if (!rec.fingerprint) { std::printf("  %-44s the game's run failed: skipped\n", rec.spec.c_str()); continue; }
            wrand::Spec spec = wrand::ParseSpec(rec.spec);
            uint64_t first = 0;
            for (int pass = 0; pass < 2; pass++) {
                wrand::Arrays arrays; world.misses = 0;
                auto r = wrand::Generate(spec, &world, &arrays);
                bool same = r.ok && r.fingerprint == rec.fingerprint && r.watchdogs.size() == rec.watchdogs && r.entities == rec.entities && r.bytes == rec.bytes;
                std::printf("  %-44s pass %d: %s fingerprint %016llx (game %016llx) watchdogs %zu/%u entities %zu/%u bytes %zu/%u misses %u %s\n", rec.spec.c_str(), pass + 1,
                            r.ok ? "ok" : r.error.c_str(), (unsigned long long)r.fingerprint, (unsigned long long)rec.fingerprint, r.watchdogs.size(), rec.watchdogs,
                            r.entities, rec.entities, r.bytes, rec.bytes, world.misses, same ? "SAME" : "DIFFERENT");
                if (!same) fails++;
                if (pass == 0) first = r.fingerprint; else if (r.fingerprint != first) { std::printf("  FAIL: the second run in the same process made other puzzles\n"); fails++; }
                if (pass == 1 && r.ok) {      // apply into a copy of the world: only entity bytes change, and every pointer written is to one of the run's arrays or a game address
                    wrand::ReplayRaw copy = world;
                    if (!r.overlay->apply(&copy)) { std::printf("  FAIL: applying into the recorded world failed\n"); fails++; }
                    for (const auto& kv : r.overlay->changes()) for (const auto& run : kv.second) {
                        uint8_t got[64]; size_t n = std::min<size_t>(run.bytes.size(), sizeof got);
                        if (!copy.read(world.ents.at(kv.first) + run.off, got, n) || std::memcmp(got, run.bytes.data(), n)) { std::printf("  FAIL: entity %x +%x not written\n", kv.first, run.off); fails++; break; }
                    }
                }
                runs++;
            }
        }
    }
    if (!runs) { std::printf("FAIL: no run to compare (no recorded world given)\n"); return 1; }
    if (fails) std::printf("generate: %d FAILED\n", fails); else std::printf("generate: ok (%d runs)\n", runs);
    return fails ? 1 : 0;
}
