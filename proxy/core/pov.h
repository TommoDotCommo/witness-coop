// Game-free logic of the POV (point-of-view) screens: who is watching whom, and in what order the viewer cycles. The picture transport (video.cpp) and the keys
// (overlay.cpp) use these classes; they hold no Windows state, so core/tests/pov.cpp can pin them down without the game.
#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>

namespace witness {

// The sender's side: a game only captures and sends pictures while somebody asked for them, and only to those who asked. A request stays alive for
// kExpiryMs; viewers repeat theirs about once a second, so a viewer that leaves (or whose request is lost three times) stops costing bandwidth.
class PovSubs {
public:
    static constexpr uint64_t kExpiryMs = 3000;
    static constexpr size_t kMax = 16;
    explicit PovSubs(bool noExpiry = false) : noExpiry_(noExpiry) {}      // noExpiry: bug twin, a request never lapses
    void request(unsigned from, uint64_t now) { if (from && (at_.count(from) || at_.size() < kMax)) at_[from] = now; }
    void cancel(unsigned from) { at_.erase(from); }
    std::vector<unsigned> live(uint64_t now) {
        std::vector<unsigned> out;
        for (auto it = at_.begin(); it != at_.end();) { if (!noExpiry_ && now - it->second > kExpiryMs) it = at_.erase(it); else { out.push_back(it->first); ++it; } }
        return out;
    }
    bool wanted(uint64_t now) { return !live(now).empty(); }
private:
    std::map<unsigned, uint64_t> at_; bool noExpiry_;
};

// The viewer's side: which player's screen is on show (0: none). The player list is the ids currently in the game.
class PovView {
public:
    explicit PovView(bool noLeaveCheck = false, bool noOffAtEnd = false) : noLeaveCheck_(noLeaveCheck), noOffAtEnd_(noOffAtEnd) {}      // bug twins: a player who left stays selected; cycling never goes off
    unsigned current() const { return cur_; }
    void watch(unsigned id) { cur_ = id; }
    void clear() { cur_ = 0; }
    bool toggle(unsigned id) { if (cur_ == id) { cur_ = 0; return false; } cur_ = id; return true; }
    // next player in ascending id order; after the last one the screen goes off (0), after off it starts again at the first
    unsigned next(std::vector<unsigned> players) {
        std::sort(players.begin(), players.end()); players.erase(std::unique(players.begin(), players.end()), players.end());
        if (players.empty()) return cur_ = 0;
        if (cur_ == 0) return cur_ = players.front();
        auto it = std::upper_bound(players.begin(), players.end(), cur_);
        return cur_ = (it == players.end()) ? (noOffAtEnd_ ? players.front() : 0) : *it;
    }
    unsigned prev(std::vector<unsigned> players) {
        std::sort(players.begin(), players.end()); players.erase(std::unique(players.begin(), players.end()), players.end());
        if (players.empty()) return cur_ = 0;
        if (cur_ == 0) return cur_ = players.back();
        auto it = std::lower_bound(players.begin(), players.end(), cur_);
        return cur_ = (it == players.begin()) ? 0 : *(it - 1);
    }
    // the watched player left the game: nothing to show
    void update(const std::vector<unsigned>& players) { if (!noLeaveCheck_ && cur_ && std::find(players.begin(), players.end(), cur_) == players.end()) cur_ = 0; }
private:
    unsigned cur_ = 0; bool noLeaveCheck_, noOffAtEnd_;
};

}  // namespace witness
