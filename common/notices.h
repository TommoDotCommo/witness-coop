#pragma once
// The words for join / leave / lost / reconnect / version notices: one table, one file (the co-op mod standard I6, the same strings in every co-op mod).
// The Roster tracker turns "who is heard from, and how recently" into those notices, once per change. Game-free (notices_test.cpp).
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace wnotice {
inline std::string Joined(const std::string& name) { return name + " joined"; }
inline std::string Left(const std::string& name) { return name + " left"; }
inline std::string Lost(const std::string& name) {
#ifdef NOTICE_MUT_WORDS
    return "Lost " + name;      // mutant: not the standard's sentence
#endif
    return "Lost " + name + ", waiting up to 30 s";
}
inline std::string Reconnecting(const std::string& name) { return "Reconnecting to " + name; }
inline std::string DifferentVersion(const std::string& name, const std::string& version) {
    return name + " has a different version" + (version.empty() ? std::string() : " (" + version + ")");
}
// The built-in randomizer (proxy/randomizer): another player's puzzles are not this player's (the randomizer's fingerprints differ).
inline std::string PuzzlesDiffer(const std::string& name) { return name + " has different puzzles: start again from the launcher's lobby so everyone gets the host's randomizer"; }
inline std::string PuzzlesFailed() { return "The randomizer could not make this world's puzzles: you have the game's own puzzles"; }
constexpr uint64_t kLostAfterMs = 5000, kLeftAfterMs = 30000;      // silent this long: "Lost"; and "waiting up to 30 s" is over: "left"
struct Seen { bool valid = false; uint64_t ageMs = 0; std::string name; };
class Roster {
public:
    // Call about once a second with every remote player the core knows (`now`: id -> what we last heard) and the clock. Returns the notices to show, oldest first.
    std::vector<std::string> Update(const std::map<unsigned, Seen>& now, uint64_t t) {
        std::vector<std::string> out;
        for (auto& kv : now) {
            const Seen& s = kv.second; State& st = state_[kv.first]; std::string nm = s.name.empty() ? "Player " + std::to_string(kv.first) : s.name; st.name = nm;
            bool heard = s.valid && s.ageMs < kLostAfterMs;
            if (heard) {
                if (st.phase == Absent || st.phase == Gone) out.push_back(Joined(nm));
#ifdef NOTICE_MUT_REPEAT
                else if (st.phase == Present) out.push_back(Joined(nm));
#endif
                st.phase = Present;
            } else if (s.valid && st.phase == Present) { out.push_back(Lost(nm)); st.phase = Silent; st.since = t; }
        }
        for (auto& kv : state_) {
            State& st = kv.second;
            bool listed = now.count(kv.first) != 0;
#ifndef NOTICE_MUT_NEVER_LEFT
            if ((st.phase == Silent && t - st.since >= kLeftAfterMs) || (!listed && (st.phase == Present || st.phase == Silent))) { out.push_back(Left(st.name)); st.phase = Gone; }
#endif
        }
        return out;
    }
private:
    enum Phase { Absent, Present, Silent, Gone };
    struct State { Phase phase = Absent; uint64_t since = 0; std::string name; };
    std::map<unsigned, State> state_;
};
}  // namespace wnotice
