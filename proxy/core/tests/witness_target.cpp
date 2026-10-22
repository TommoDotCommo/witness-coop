// Fuzz target for the Witness sync core (proxy/core/sync_core.cpp): two machines, each a SyncCore over a FakeWorld, joined by the
// hub's lossy link (delay, jitter, loss, duplicates, one-bit corruption), driven by random sessions (solves, restarts, leaves, stale
// replays, bad links) and checked by the hub's oracles plus three of our own. No game, no Windows: runs natively in seconds.
//
// The fake world reproduces what the real game does that went wrong in practice:
//  - a loading phase: the camera is valid before the entity table is complete (a baseline taken then reported 39 phantom opens);
//  - doors that are open by default, and one that powers up late, after any reasonable "stable" gate (found by the real suite);
//  - a solve opens a door a moment later, and applying a remote door-open takes a moment to show (the echo guard's race).
// Machine restarts are new games: a fresh world and a new session; the survivor must resend its whole state.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include "coopfuzz/fuzz.h"
#include "../sync_core.h"

namespace coopfuzz {
namespace {

using witness::SyncCore;
constexpr int kPanels = 12, kDoors = 8;
unsigned panelId(int i) { return 0x40u + 3u * unsigned(i); }
unsigned doorId(int j) { return 0x1000u + 5u * unsigned(j); }
enum class Cause : uint8_t { None, Startup, Player, Remote };

class FakeWorld : public witness::World {
public:
    FakeWorld(uint64_t seed, uint64_t createdAt) : rng_(seed), created_(createdAt) {
        loadMs_ = rng_.range(2500, 6500); lateMs_ = rng_.range(7000, 22000);
        for (int i = 0; i < kPanels; ++i) ents_.push_back({panelId(i), false, false, false, createdAt + 300 + loadMs_ * uint64_t(i) / kPanels, Cause::None});
        for (int j = 0; j < kDoors; ++j) ents_.push_back({doorId(j), true, false, false, createdAt + 300 + loadMs_ * uint64_t(j) / kDoors, Cause::None});
        timers_.push_back({createdAt + loadMs_ + lateMs_, doorId(kDoors - 1), Cause::Startup});     // powers up late, by itself; no panel opens this door
    }
    void advance(uint64_t now) {
        now_ = now;
        for (auto& e : ents_) if (!e.present && now >= e.appearAt) {
            e.present = true; changedAt_ = now;
            if (e.door && (e.id == doorId(0) || e.id == doorId(1))) { e.state = true; e.cause = Cause::Startup; }   // open by default
        }
        for (size_t i = 0; i < timers_.size();) {
            if (timers_[i].at <= now) { Ent* e = find(timers_[i].id); if (getenv("WTRACE") && timers_[i].cause == Cause::Startup) fprintf(stderr, "%6llu world: startup timer fires for #%x, door %s\n", (unsigned long long)now, timers_[i].id, (e && e->present && !e->state) ? "opens" : "was already open"); if (e && e->present && !e->state) { e->state = true; e->cause = timers_[i].cause; changedAt_ = now; } timers_.erase(timers_.begin() + long(i)); }
            else ++i;
        }
    }
    bool localSolve(int i) {                     // the player solves a panel; its door opens a little later
        Ent* e = find(panelId(i)); if (!e || !e->present || e->state) return false;
        e->state = true; e->cause = Cause::Player; changedAt_ = now_; lastSolveAt_ = now_; anySolve_ = true;
        Ent* d = find(doorId(i % (kDoors - 1))); if (d && !d->state) timers_.push_back({now_ + rng_.range(200, 800), d->id, Cause::Player});
        return true;
    }
    Cause cause(unsigned id) { Ent* e = find(id); return e ? e->cause : Cause::None; }
    bool pending() const { return !timers_.empty(); }
    bool complete() const { for (auto& e : ents_) if (!e.present) return false; return true; }
    uint64_t changedAt() const { return changedAt_; }
    bool solvedWithin(uint64_t now, uint64_t ms) const { return anySolve_ && now < lastSolveAt_ + ms; }
    void setPose(float x, float y) { px_ = x; py_ = y; }
    // witness::World
    bool cameraValid() override { return now_ >= created_ + 1500; }
    bool scan(std::vector<unsigned>& p, std::vector<unsigned>& d, uint64_t* tableId) override {
        for (auto& e : ents_) if (e.present) (e.door ? d : p).push_back(e.id);
        *tableId = 1; return true;
    }
    bool entityExists(unsigned id) override { Ent* e = find(id); return e && e->present; }
    bool isPanel(unsigned id) override { Ent* e = find(id); return e && e->present && !e->door; }
    bool isDoor(unsigned id) override { Ent* e = find(id); return e && e->present && e->door; }
    bool panelSolved(unsigned id) override { Ent* e = find(id); return e && e->present && !e->door && e->state; }
    bool doorOpen(unsigned id) override { Ent* e = find(id); return e && e->present && e->door && e->state; }
    void applyPanelSolved(unsigned id) override { Ent* e = find(id); if (e && e->present && !e->state) { e->state = true; e->cause = Cause::Remote; changedAt_ = now_; } }
    bool applyDoorOpen(unsigned id) override { timers_.push_back({now_ + rng_.range(50, 450), id, Cause::Remote}); return true; }     // shows a moment later
    bool pose(float pos[3], float ang[2]) override { if (!cameraValid()) return false; pos[0] = px_; pos[1] = py_; pos[2] = 3.7f; ang[0] = ang[1] = 0; return true; }
    struct Ent { unsigned id; bool door, state, present; uint64_t appearAt; Cause cause; };
    const std::vector<Ent>& ents() const { return ents_; }
private:
    struct Timer { uint64_t at; unsigned id; Cause cause; };
    Ent* find(unsigned id) { for (auto& e : ents_) if (e.id == id) return &e; return nullptr; }
    Rng rng_; uint64_t lastSolveAt_ = 0; bool anySolve_ = false; uint64_t created_, now_ = 0, changedAt_ = 0, loadMs_ = 0, lateMs_ = 0; float px_ = 0, py_ = 0;
    std::vector<Ent> ents_; std::vector<Timer> timers_;
};

struct Machine {
    std::unique_ptr<FakeWorld> world; std::unique_ptr<SyncCore> core;
    bool up = false; uint64_t nextPoll = 0, nextApply = 0, startedAt = 0; int restarts = 0; uint64_t resyncsBefore = 0;
    std::map<uint64_t, uint32_t> lastAccepted;     // per peer-stream epoch: the last sequence taken in order
};

class WitnessTarget : public TargetBase {
public:
    WitnessTarget(const Scenario& s, witness::Flags flags) : net_(2, s.seed * 131 + 7), seed_(s.seed), flags_(flags), rng_(s.seed * 977 + 5) {
        LinkParams link = s.link;
        if (!s.profile.empty()) linkProfile(s.profile, link);
        net_.setLink(link);
        for (int m = 0; m < 2; ++m) start(m);
    }
    uint64_t now() const override { return now_; }
    int machines() const override { return 2; }
    bool connected(int m) const override { return machines_[m].up; }
    uint64_t repairs(int m) const override { return machines_[m].resyncsBefore + (machines_[m].up ? machines_[m].core->stats().resyncs : 0); }

    void apply(const Op& op) override {
        int m = std::max(0, std::min(1, op.m));
        switch (op.kind) {
            case OpKind::Activate: {
                if (!machines_[m].up || !machines_[m].world->complete() || now_ < machines_[m].startedAt + 20000) return;     // a player acts once the world has loaded and settled in
                int panel = int((unsigned(op.a) * 7u + op.t / 97u) % kPanels);
                if (machines_[m].world->localSolve(panel)) { ++counters_["op_solve"]; trace("SOLVE", m, panelId(panel)); } else ++counters_["op_solve_noop"];
                break;
            }
            case OpKind::Move: if (machines_[m].up) machines_[m].world->setPose(float(op.a), float(op.b)); break;
            case OpKind::Leave: if (m == 1 && machines_[m].up && now_ >= machines_[m].startedAt + 45000) { stop(m); ++counters_["op_leave"]; } break;   // a game is not quit within a minute of starting
            case OpKind::Join: if (m == 1 && !machines_[m].up) { start(m); ++counters_["op_join"]; } break;
            case OpKind::SetLink: { LinkParams l = net_.link(); l.minLatencyMs = uint32_t(op.a); l.maxLatencyMs = uint32_t(std::max(op.a, op.b)); l.loss = op.c / 1000.0; l.duplicate = op.d / 1000.0; net_.setLink(l); ++counters_["op_setlink"]; break; }
            case OpKind::Stale: {   // replay a recorded packet (old events, old acks, old poses) towards machine m
                std::vector<const Bytes*> from; for (auto& h : hist_) if (h.from == op.a % 2 && h.to == m) from.push_back(&h.data);
                if (!from.empty()) { net_.send(op.a % 2, m, *from[size_t(op.b) % from.size()]); ++counters_["op_stale"]; }
                break;
            }
            default: break;   // fights, items, spawns, saves: nothing in this core
        }
    }

    void advance(uint64_t toMs) override {
        while (now_ < toMs) {
            now_ = std::min<uint64_t>(now_ + 10, toMs);
            net_.advance(now_);
            for (int m = 0; m < 2; ++m) {
                Machine& M = machines_[m]; int from = 0; Bytes d;
                while (net_.poll(m, from, d)) if (M.up) M.core->onPacket(d.data(), d.size(), now_);
                if (!M.up) continue;
                M.world->advance(now_);
                M.core->tick(now_);
                if (now_ >= M.nextPoll) { M.nextPoll = now_ + 100; M.core->pollState(now_); }
                if (now_ >= M.nextApply) { M.nextApply = now_ + 33; M.core->applyRemote(now_); }
            }
        }
    }

    void heal() override {
        frozen_ = true; net_.setLink(LinkParams());
        for (int m = 0; m < 2; ++m) if (!machines_[m].up) start(m);     // everyone reconnects: a restarted machine must get the whole state back
    }
    bool quiescent() const override {
        if (getenv("WDEBUG") && now_ % 10000 == 0) {
            fprintf(stderr, "t=%llu frozen=%d idle=%d", (unsigned long long)now_, frozen_, net_.idle());
            for (int m = 0; m < 2; ++m) { const Machine& M = machines_[m]; if (!M.up) { fprintf(stderr, " | m%d DOWN", m); continue; }
                fprintf(stderr, " | m%d ready=%d complete=%d pending=%d unacked=%zu queued=%zu sess=%u peerSess=%u quietFor=%llu", m, M.core->ready(), M.world->complete(), M.world->pending(), M.core->unacked(), M.core->queuedRemote(), M.core->session(), M.core->peerSession(), (unsigned long long)(now_ - M.world->changedAt())); }
            fprintf(stderr, "\n"); }
        if (!frozen_ || !net_.idle()) return false;
        for (int m = 0; m < 2; ++m) {
            const Machine& M = machines_[m];
            if (!M.up || !M.core->ready() || !M.world->complete() || M.world->pending() || M.core->unacked() || M.core->queuedRemote()) return false;
            if (now_ < M.world->changedAt() + 600) return false;
            const Machine& O = machines_[1 - m];
            if (M.core->peerSession() != O.core->session()) return false;      // has seen the peer's CURRENT run, so the restart handling ran
        }
        return true;
    }
    std::vector<Thing> view(int m) const override {
        std::vector<Thing> v; if (!machines_[m].up) return v;
        for (auto& e : machines_[m].world->ents()) {
            if (!e.present) continue;
            Thing t; t.netId = e.id; t.type = e.door ? 3 : 1; t.open = e.door && e.state; t.hp = (!e.door && e.state) ? 1 : 0; v.push_back(t);
        }
        std::sort(v.begin(), v.end(), [](const Thing& a, const Thing& b) { return a.netId < b.netId; });
        return v;
    }
    uint64_t digest(int m, int cat) const override {   // 0: solved panels, 1: open doors, 2 and 3: nothing
        uint64_t h = 1469598103934665603ull + uint64_t(cat);
        if (cat > 1 || !machines_[m].up) return h;
        for (auto& e : machines_[m].world->ents()) if (e.present && e.door == (cat == 1) && e.state) { h ^= e.id; h *= 1099511628211ull; }
        return h;
    }
    std::vector<Violation> nativeViolations() override { return viol_; }

private:
    struct Sent { int from, to; Bytes data; };
    void trace(const char* what, int m, unsigned id = 0, uint64_t a = 0, uint64_t b = 0) const { if (getenv("WTRACE")) fprintf(stderr, "%6llu m%d %-9s #%x %llx %llu\n", (unsigned long long)now_, m, what, id, (unsigned long long)a, (unsigned long long)b); }
    void viol(const std::string& what, const std::string& detail) { viol_.push_back({what, detail, now_}); }

    void start(int m) {
        Machine& M = machines_[m];
        M.world = std::make_unique<FakeWorld>(seed_ * 7919 + uint64_t(m) * 104729 + uint64_t(M.restarts) * 15485863, now_);
        M.world->advance(now_);
        witness::Hooks h;
        h.send = [this, m](const witness::Bytes& b) {
            net_.send(m, 1 - m, b);
            bool pose = b.size() == 40;     // poses go out 30 times a second and would drown the rare events and acks: record one in 30
            if (!pose || ++poseCount_ % 30 == 0) { hist_.push_back({m, 1 - m, b}); if (hist_.size() > 200) hist_.erase(hist_.begin()); }
        };
        h.log = [this, m](const char* text) { if (getenv("WTRACE")) fprintf(stderr, "%6llu m%d LOG       %s\n", (unsigned long long)now_, m, text); };
        h.onEmit = [this, m](uint8_t kind, unsigned id, bool resync) {
            trace(resync ? "emit-rsync" : "emit", m, id, kind);
            if (resync) { ++counters_["emit_resync"]; return; }
            Cause c = machines_[m].world->cause(id);
            if (c == Cause::Remote) viol("echo_sent", "m" + std::to_string(m) + " sent an event for #" + std::to_string(id) + " (kind " + std::to_string(kind) + "), a change its peer had caused");
            else if (c == Cause::Startup && machines_[m].world->solvedWithin(now_, 5500)) ++counters_["startup_flip_beside_a_solve"];     // indistinguishable from the cascade of that solve: the documented limit
            else if (c == Cause::Startup) viol("spurious_event", "m" + std::to_string(m) + " sent an event for #" + std::to_string(id) + " (kind " + std::to_string(kind) + "), a start-up change, not a player action");
            else ++counters_["emit_local"];
        };
        h.onApplied = [this, m](uint8_t kind, unsigned id, uint64_t epoch, uint32_t seq) {
            trace("APPLIED", m, id, epoch, seq); ++counters_["applied"]; ++counters_[kind == witness::EV_DOOR_OPENED ? "applied_door" : "applied_panel"];
            logApply(m, (epoch << 20) ^ (uint64_t(seq) << 1) ^ 1, int(id), uint32_t(epoch), seq, 1 - m, ApplyKind::Door, true, 0, "");
        };
        h.onRejected = [this, m](uint8_t, unsigned id, uint64_t epoch, uint32_t seq, const char* why) {
            logApply(m, (epoch << 20) ^ (uint64_t(seq) << 1) ^ 1, int(id), uint32_t(epoch), seq, 1 - m, ApplyKind::Door, false, 0, why);
        };
        h.onAccepted = [this, m](uint64_t epoch, uint32_t seq) {   // an event taken in order: the sequence must be exactly the next one in its epoch
            uint32_t& last = machines_[m].lastAccepted[epoch];
            if (seq != last + 1) viol("out_of_order_apply", "m" + std::to_string(m) + " took seq " + std::to_string(seq) + " after " + std::to_string(last) + " in epoch " + std::to_string(epoch));
            last = seq;
        };
        h.onRestartSeen = [this, m] { trace("sees-restart", m); ++counters_["peer_restarts_seen"]; };
        uint32_t session = uint32_t(1 + now_ / 10 + uint64_t(M.restarts) * 1000);      // grows with every start, like the clock in seconds
        M.core = std::make_unique<SyncCore>(m, session, M.world.get(), h, flags_);
        trace("START", m, 0, session); M.lastAccepted.clear(); M.up = true; M.nextPoll = now_; M.nextApply = now_; M.startedAt = now_; ++M.restarts;
        net_.setConnected(m, true);
        if (M.restarts > 1) { ++counters_["restarts"]; logApply(m, 0, -1, 0, 0, m, ApplyKind::Reset, true, 0, "restart"); }
    }
    void stop(int m) {
        Machine& M = machines_[m];
        trace("STOP", m); M.resyncsBefore += M.core->stats().resyncs; M.core.reset(); M.world.reset(); M.up = false;
        net_.setConnected(m, false);
    }

    LossyLink net_; uint64_t seed_; witness::Flags flags_; Rng rng_; Machine machines_[2];
    uint64_t now_ = 0, poseCount_ = 0; bool frozen_ = false; std::vector<Sent> hist_; std::vector<Violation> viol_;
};

std::unique_ptr<ITarget> makeWitness(const Scenario& s, std::string* err) {
    witness::Flags f;
    for (auto& b : s.bugs) {
        if (b == "no_resend") f.noResend = true;
        else if (b == "no_echo_guard") f.noEchoGuard = true;
        else if (b == "no_dedupe") f.noDedupe = true;
        else if (b == "no_epoch_reset") f.noEpochReset = true;
        else if (b == "no_resync_on_restart") f.noResyncOnRestart = true;
        else if (b == "no_gate") f.noGate = true;
        else if (b == "no_settle") f.noSettle = true;
        else if (b == "wrong_ack_epoch") f.wrongAckEpoch = true;
        else if (b == "apply_out_of_order") f.applyOutOfOrder = true;
        else if (b == "no_checksum") f.noChecksum = true;
        else { if (err) *err = "unknown bug class for target witness: " + b; return nullptr; }
    }
    return std::make_unique<WitnessTarget>(s, f);
}

const TargetRegistrar kRegisterWitness(TargetInfo{"witness", "The Witness sync core: two players, solved panels and open doors, restarts", {"no_resend", "no_echo_guard", "no_dedupe", "no_epoch_reset", "no_resync_on_restart", "no_gate", "no_settle", "wrong_ack_epoch", "apply_out_of_order", "no_checksum"}, makeWitness});

}  // namespace
}  // namespace coopfuzz
