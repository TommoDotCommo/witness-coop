#include "sync_core.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace witness {
namespace {

// Protocol v2 adds panel power-on. Distinct magics prevent a v1 peer from ACKing an incomplete vocabulary.
constexpr uint32_t kMagicPose = 0x32505457, kMagicEv = 0x32455457, kMagicAck = 0x32415457;   // "WTP2" "WTE2" "WTA2"
constexpr size_t kPoseLen = 40, kEvLen = 32, kAckLen = 24;                                    // with the 4-byte checksum
constexpr uint64_t kSuppressMs = 5000, kSettleMs = 15000, kCascadeMs = 5000;

uint32_t sum32(const uint8_t* p, size_t n) { uint32_t h = 2166136261u; for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; } return h; }
void put32(Bytes& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); }
void put64(Bytes& b, uint64_t v) { put32(b, uint32_t(v)); put32(b, uint32_t(v >> 32)); }
uint32_t get32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint64_t get64(const uint8_t* p) { return uint64_t(get32(p)) | uint64_t(get32(p + 4)) << 32; }
void putF(Bytes& b, float f) { uint32_t v; std::memcpy(&v, &f, 4); put32(b, v); }
float getF(const uint8_t* p) { uint32_t v = get32(p); float f; std::memcpy(&f, &v, 4); return f; }
Bytes finish(Bytes b) { put32(b, sum32(b.data(), b.size())); return b; }

}  // namespace

struct SyncCore::Guard {
    const Hooks& h;
    explicit Guard(const Hooks& hooks) : h(hooks) { if (h.lock) h.lock(); }
    ~Guard() { if (h.unlock) h.unlock(); }
};

SyncCore::SyncCore(int id, uint32_t session, World* world, Hooks hooks, Flags flags)
    : id_(id), session_(session), w_(world), h_(std::move(hooks)), f_(flags) { epoch_ = uint64_t(session) << 16; }

void SyncCore::log(const char* m) { if (h_.log) h_.log(m); }
void SyncCore::send(Bytes b) { if (h_.send) h_.send(b); }

void SyncCore::emit(uint8_t kind, unsigned id, bool resync) {
    if (sent_.size() < 4096) { sent_.push_back({nextSeq_++, kind, id, false, false}); st_.sent++; }
    if (h_.onEmit) h_.onEmit(kind, id, resync);
}

// ------------------------------------------------------------------------------------------------ network side
void SyncCore::tick(uint64_t now) {
    Guard g(h_);
    if (now >= nextPose_) {
        nextPose_ = now + 33;
        float pos[3], ang[2];
        if (w_->pose(pos, ang)) {
            Bytes b; put32(b, kMagicPose); put32(b, uint32_t(id_)); put32(b, ++poseSeq_);
            for (float f : pos) putF(b, f);
            for (float f : ang) putF(b, f);
            put32(b, session_); send(finish(std::move(b)));
        }
    }
    if (now >= nextResend_) {   // resend unacknowledged events, oldest first, at most 32 per round
        nextResend_ = now + 150; int n = 0;
        for (auto& s : sent_) {
            if (n >= 32) break;
            if (s.acked) continue;
            if (f_.noResend && s.sentOnce) continue;                        // test: never resend
            bool drop = f_.dropFirstEvent && !s.sentOnce;                   // test: the first send of each event is lost
            if (!drop) {
                Bytes b; put32(b, kMagicEv); put32(b, uint32_t(id_)); put64(b, epoch_); put32(b, s.seq); put32(b, s.kind); put32(b, s.entity);
                send(finish(std::move(b)));
            }
            n++;
            if (s.sentOnce) st_.resent++;
            s.sentOnce = true;
        }
    }
}

void SyncCore::onPacket(const uint8_t* d, size_t n, uint64_t now) {
    if (n < 8 || (!f_.noChecksum && sum32(d, n - 4) != get32(d + n - 4))) return;   // corrupted or foreign: drop, the sender resends
    uint32_t magic = get32(d);
    Guard g(h_);
    if (magic == kMagicEv && n == kEvLen) {
        uint32_t from = get32(d + 4), seq = get32(d + 16), kind = get32(d + 20), entity = get32(d + 24); uint64_t epoch = get64(d + 8);
        if (int(from) == id_ || (kind != EV_PANEL_SOLVED && kind != EV_DOOR_OPENED && kind != EV_PANEL_POWERED) || entity >= kMaxEntityId) return;
        if (epoch < peerEpoch_) { st_.ooo++; if (h_.onRejected) h_.onRejected(uint8_t(kind), entity, epoch, seq, "stale epoch"); return; }   // a delayed packet of an earlier run
        if (epoch > peerEpoch_) { peerEpoch_ = epoch; if (!f_.noEpochReset) peerApplied_ = 0; }
        bool ordered = false, dup = false;
        if (seq == peerApplied_ + 1 && recvQ_.size() < 1024) { recvQ_.push_back({uint8_t(kind), entity, epoch, seq}); peerApplied_ = seq; ordered = true; }
        else if (f_.applyOutOfOrder && seq > peerApplied_ + 1 && recvQ_.size() < 1024) { recvQ_.push_back({uint8_t(kind), entity, epoch, seq}); peerApplied_ = seq; ordered = true; }
        else if (seq <= peerApplied_) {
            dup = true;
            if (f_.noDedupe && recvQ_.size() < 1024) recvQ_.push_back({uint8_t(kind), entity, epoch, seq});    // twin: duplicates reach the apply step
        }
        if (ordered) { st_.inOrder++; if (h_.onAccepted) h_.onAccepted(epoch, seq); } else if (dup) st_.dup++; else st_.ooo++;
        if (!ordered && h_.onRejected) h_.onRejected(uint8_t(kind), entity, epoch, seq, dup ? "duplicate" : "out of order");
        Bytes a; put32(a, kMagicAck); put32(a, uint32_t(id_)); put64(a, peerEpoch_); put32(a, peerApplied_);
        send(finish(std::move(a)));
        return;
    }
    if (magic == kMagicAck && n == kAckLen) {
        uint32_t from = get32(d + 4), ack = get32(d + 16); uint64_t epoch = get64(d + 8);
        if (int(from) == id_ || (epoch != epoch_ && !f_.wrongAckEpoch)) return;
        for (auto& s : sent_) if (s.seq <= ack && !s.acked) { s.acked = true; st_.acked++; }
        return;
    }
    if (magic == kMagicPose && n == kPoseLen) {
        uint32_t from = get32(d + 4), seq = get32(d + 8), session = get32(d + 32);   // pose: magic id seq pos[3] ang[2] session checksum
        if (int(from) == id_) return;
        if (session < peerSession_) return;     // a delayed packet from an earlier run of the peer: ignore it completely
        if (session > peerSession_) {           // the peer (re)started: begin a new stream epoch and resend the full state
            peerSession_ = session;
            epoch_ = (uint64_t(session_) << 16) | ++restartsSeen_; nextSeq_ = 1; sent_.clear();
            if (!f_.noResyncOnRestart) resync_ = true;
            st_.resyncs++;
            if (h_.onRestartSeen) h_.onRestartSeen();
            char l[120]; std::snprintf(l, sizeof l, "coop: peer session %08x seen, new event epoch %llx, full state resync", session, (unsigned long long)epoch_); log(l);
        }
        remote_.valid = true; remote_.id = from; remote_.seq = seq;
        for (int i = 0; i < 3; ++i) remote_.pos[i] = getF(d + 12 + 4 * i);
        for (int i = 0; i < 2; ++i) remote_.ang[i] = getF(d + 24 + 4 * i);
        remoteAt_ = now;
        return;
    }
}

// ------------------------------------------------------------------------------------------------ detector side
bool SyncCore::scanWorld() {
    std::vector<unsigned> p, d; uint64_t tid = 0;
    if (!w_->scan(p, d, &tid)) return false;
    bool changed = tid != tableId_ || p.size() != panels_.size() || d.size() != doors_.size();
    tableId_ = tid; panels_.swap(p); doors_.swap(d);
    return changed;
}

uint64_t SyncCore::signature() {   // FNV-1a over which panels are solved and which doors are open
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
    for (unsigned id : panels_) if (w_->entityExists(id) && w_->panelSolved(id)) mix(id);
    mix(0xFFFF);
    for (unsigned id : doors_) if (w_->entityExists(id) && w_->doorOpen(id)) mix(id);
    mix(0xFFFE);
    for (unsigned id : panels_) if (w_->entityExists(id) && w_->panelPowered(id)) mix(id);
    return h;
}

void SyncCore::baseline(bool full) {
    for (unsigned id : panels_) {
        bool s = w_->entityExists(id) && w_->panelSolved(id);
        if (s) knownPanel_.insert(id); else knownPanel_.erase(id);
        if (full && s) emit(EV_PANEL_SOLVED, id, true);
        bool powered = w_->entityExists(id) && w_->panelPowered(id);
        if (powered) knownPower_.insert(id); else knownPower_.erase(id);
        if (full && powered && !f_.noPanelPower) emit(EV_PANEL_POWERED, id, true);
    }
    for (unsigned id : doors_) {
        bool o = w_->entityExists(id) && w_->doorOpen(id);
        if (o) knownDoor_.insert(id); else knownDoor_.erase(id);
        if (full && o) emit(EV_DOOR_OPENED, id, true);
    }
}

void SyncCore::pollState(uint64_t now) {
    Guard g(h_);
    if (!gateStarted_) { gateStarted_ = true; nextGate_ = now + 3000; }
    if (!ready_) {
        // Wait until the world is stable: camera valid, and the same panel/door counts AND the same set of solved panels and open
        // doors on 3 scans 3 s apart. (A baseline taken while the world was loading reported 39 transient "open" doors, and a
        // counts-only gate still let a default-open door power up late; see also the settling window below.)
        bool go = false;
        if (f_.noGate) { scanWorld(); go = w_->cameraValid() && !panels_.empty(); }          // twin: no gate
        else if (now >= nextGate_) {
            nextGate_ = now + 3000; scanWorld();
            uint64_t sig = signature();
            if (w_->cameraValid() && !panels_.empty() && panels_.size() == gateLastPanels_ && doors_.size() == gateLastDoors_ && sig == gateLastSig_) { if (++gateSame_ >= 3) go = true; }
            else gateSame_ = 0;
            gateLastPanels_ = panels_.size(); gateLastDoors_ = doors_.size(); gateLastSig_ = sig;
        }
        if (!go) return;
        char m[160]; std::snprintf(m, sizeof m, "state: world stable, %zu panels, %zu doors", panels_.size(), doors_.size()); log(m);
        baseline(false);
        ready_ = true; settleUntil_ = f_.noSettle ? now : now + kSettleMs; nextScan_ = now + 3000; nextStats_ = now + 10000;
        return;
    }
    if (now >= nextScan_) { nextScan_ = now + 3000; if (scanWorld()) { log("state: entity table changed, new baseline"); baseline(false); } }
    if (resync_) { resync_ = false; baseline(true); log("state: full state queued for the peer"); }
    for (unsigned id : panels_) {
        if (!w_->entityExists(id)) continue;
        bool s = w_->panelSolved(id); bool known = knownPanel_.count(id) != 0;
        auto sp = suppressPanel_.find(id);
        if (sp != suppressPanel_.end() && now < sp->second) { if (s) knownPanel_.insert(id); else knownPanel_.erase(id); continue; }
        if (s && !known) { knownPanel_.insert(id); emit(EV_PANEL_SOLVED, id, false); st_.emittedPanels++; lastLocalSolveAt_ = now; anyLocalSolve_ = true; char l[100]; std::snprintf(l, sizeof l, "state: local panel %#x solved -> event", id); log(l); }
        else if (!s) knownPanel_.erase(id);
    }
    for (unsigned id : panels_) {
        if (!w_->entityExists(id)) continue;
        bool powered = w_->panelPowered(id), known = knownPower_.count(id) != 0;
        auto sp = suppressPower_.find(id);
        bool suppressed = sp != suppressPower_.end() && now < sp->second;
        bool cascade = anyLocalSolve_ && now < lastLocalSolveAt_ + kCascadeMs;
        if (powered && !known && !suppressed && !(now < settleUntil_ && !cascade) && !f_.noPanelPower) {
            emit(EV_PANEL_POWERED, id, false); st_.emittedPowers++;
            char l[100]; std::snprintf(l, sizeof l, "state: local panel %#x powered -> event", id); log(l);
        }
        if (powered) knownPower_.insert(id); else knownPower_.erase(id);
    }
    for (unsigned id : doors_) {
        if (!w_->entityExists(id)) continue;
        bool o = w_->doorOpen(id); bool known = knownDoor_.count(id) != 0;
        auto sp = suppressDoor_.find(id);
        if (sp != suppressDoor_.end() && now < sp->second) { if (o) knownDoor_.insert(id); else knownDoor_.erase(id); continue; }
        bool cascade = anyLocalSolve_ && now < lastLocalSolveAt_ + kCascadeMs;
        if (now < settleUntil_ && o != known && !cascade) { if (o) knownDoor_.insert(id); else knownDoor_.erase(id); char l[100]; std::snprintf(l, sizeof l, "state: door %#x changed during start-up, folded into the baseline", id); log(l); continue; }
        if (o && !known) { knownDoor_.insert(id); emit(EV_DOOR_OPENED, id, false); st_.emittedDoors++; char l[100]; std::snprintf(l, sizeof l, "state: local door %#x opened -> event", id); log(l); }
        else if (!o) knownDoor_.erase(id);
    }
    if (now >= nextStats_) {
        nextStats_ = now + 10000;
        char l[320]; std::snprintf(l, sizeof l, "state: emitted panels %u doors %u, applied panels %u doors %u | net sent %u resent %u in-order %u dup %u ooo %u acked %u | power emitted %u applied %u",
            st_.emittedPanels, st_.emittedDoors, st_.appliedPanels, st_.appliedDoors, st_.sent, st_.resent, st_.inOrder, st_.dup, st_.ooo, st_.acked, st_.emittedPowers, st_.appliedPowers); log(l);
    }
}

void SyncCore::applyRemote(uint64_t now) {
    Guard g(h_);
    if (!ready_) return;   // events stay queued until this world is stable
    // One attempt per queued event per frame. Keep temporarily unavailable entities and refused calls: the transport already
    // ACKed these events, so the sender will not send them again. Other independent, monotonic changes can still make progress.
    size_t attempts = recvQ_.size();
    while (attempts--) {
        Recv r = recvQ_.front(); recvQ_.erase(recvQ_.begin());
        char l[140];
        if (r.entity >= kMaxEntityId || !w_->entityExists(r.entity)) {
            if (!f_.noApplyRetry) recvQ_.push_back(r);
            continue;
        }
        if (r.kind == EV_PANEL_SOLVED && w_->isPanel(r.entity)) {
            if (w_->panelSolved(r.entity) && !f_.noDedupe) {
                knownPanel_.insert(r.entity);
                if (h_.onRejected) h_.onRejected(r.kind, r.entity, r.epoch, r.seq, "already solved");
                continue;
            }
            if (!f_.noEchoGuard) { knownPanel_.insert(r.entity); suppressPanel_[r.entity] = now + kSuppressMs; }    // echo guard first
            w_->applyPanelSolved(r.entity);
            st_.appliedPanels++;
            if (h_.onApplied) h_.onApplied(r.kind, r.entity, r.epoch, r.seq);
            std::snprintf(l, sizeof l, "state: remote panel %#x applied as solved", r.entity); log(l);
        } else if (r.kind == EV_PANEL_POWERED && w_->isPanel(r.entity)) {
            if (w_->panelPowered(r.entity) && !f_.noDedupe) { knownPower_.insert(r.entity); continue; }
            if (!w_->applyPanelPower(r.entity)) {
                if (!f_.noApplyRetry) recvQ_.push_back(r);
                continue;
            }
            if (!f_.noEchoGuard) { knownPower_.insert(r.entity); suppressPower_[r.entity] = now + kSuppressMs; }
            st_.appliedPowers++;
            if (h_.onApplied) h_.onApplied(r.kind, r.entity, r.epoch, r.seq);
            std::snprintf(l, sizeof l, "state: remote panel %#x powered", r.entity); log(l);
        } else if (r.kind == EV_DOOR_OPENED && w_->isDoor(r.entity)) {
            if (w_->doorOpen(r.entity) && !f_.noDedupe) {
                knownDoor_.insert(r.entity);
                if (h_.onRejected) h_.onRejected(r.kind, r.entity, r.epoch, r.seq, "already open");
                continue;
            }
            bool ok = w_->applyDoorOpen(r.entity);
            if (!ok) {
                if (!f_.noApplyRetry) recvQ_.push_back(r);
                continue;
            }
            // The core lock keeps the detector out until the successful call's echo guard is installed.
            if (!f_.noEchoGuard) { knownDoor_.insert(r.entity); suppressDoor_[r.entity] = now + kSuppressMs; }
            st_.appliedDoors++;
            if (h_.onApplied) h_.onApplied(r.kind, r.entity, r.epoch, r.seq);
            std::snprintf(l, sizeof l, "state: remote door %#x opened (call %s)", r.entity, ok ? "ok" : "REFUSED"); log(l);
        } else {
            std::snprintf(l, sizeof l, "state: remote event kind %u entity %#x: wrong entity type, ignored", r.kind, r.entity); log(l);
            if (h_.onRejected) h_.onRejected(r.kind, r.entity, r.epoch, r.seq, "wrong entity type");
        }
    }
}

bool SyncCore::remotePose(RemotePose* out, uint64_t now) {
    Guard g(h_);
    *out = remote_; out->ageMs = remote_.valid ? now - remoteAt_ : 0;
    return out->valid;
}
Stats SyncCore::stats() const { Guard g(h_); return st_; }
size_t SyncCore::unacked() const { Guard g(h_); size_t n = 0; for (auto& s : sent_) if (!s.acked) n++; return n; }
size_t SyncCore::queuedRemote() const { Guard g(h_); return recvQ_.size(); }

}  // namespace witness
