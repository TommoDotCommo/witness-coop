#include "sync_core.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace witness {
namespace {

// Protocol v6 adds a versioned appearance record to presence. Distinct magics prevent an older peer from ACKing an incomplete vocabulary.
constexpr uint32_t kMagicPose = 0x36505457, kMagicEv = 0x36455457, kMagicAck = 0x36415457;   // "WTP6" "WTE6" "WTA6"
constexpr uint32_t kMagicPing = 0x36515457;   // "WTQ6": ping and pong, carry the sender's name
constexpr size_t kPingLen = 48, kNameLen = 24;
constexpr uint32_t kMagicSoc = 0x36585457, kMagicSocAck = 0x364B5457, kMagicPres = 0x36535457;   // "WTX6" a reliable social message, "WTK6" its acknowledgement, "WTS6" presence
constexpr size_t kPresLen = 36, kSocHeader = 28, kSocMax = 900, kSocAckLen = 24;
constexpr uint64_t kPresExpiryMs = 4000;
constexpr uint32_t kMagicLine = 0x364C5457;   // "WTL6": a drawn line, best effort, the whole line every time
constexpr uint32_t kGroupPose = 0x37505457, kGroupEv = 0x37455457, kGroupAck = 0x37415457;
constexpr size_t kGroupEvLen = 44;
constexpr size_t kPoseLen = 40, kEvLen = 32, kAckLen = 24;                                    // with the 4-byte checksum
constexpr uint32_t kSnapBit = 0x100;      // set in an event's kind word when it is part of a full-state snapshot
constexpr uint64_t kConflictMs = 1000, kSuppressMs = 5000, kSettleMs = 15000, kCascadeMs = 5000;

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
void SyncCore::sendTo(unsigned peer, const Bytes& b) {
    if (h_.sendTo) h_.sendTo(peer, b); else if (h_.send) h_.send(b);
}
void SyncCore::send(Bytes b) {
    if (h_.sendTo) { for (auto& p : peers_) sendTo(p.first, b); }
    else if (h_.send) h_.send(b);
}
bool SyncCore::setPeers(const std::vector<unsigned>& ids) {
    if (gateStarted_ || poseSeq_ || ids.empty() || ids.size() >= kMaxPlayers || !h_.sendTo) return false;
    std::map<unsigned, Peer> roster;
    for (unsigned id : ids) {
        if (!id || id == unsigned(id_) || !roster.emplace(id, Peer{}).second) return false;
    }
    peers_ = std::move(roster); fixedRoster_ = true; return true;
}

void SyncCore::emit(uint8_t kind, unsigned id, bool resync) {
    if (f_.raceMode && !f_.raceEmits) return;      // a race: this game's progress stays in this game
    // A complete snapshot must itself fit, even if a future entity table is larger than today's build.
    size_t capacity = fixedRoster_ && !f_.noBacklogReset ? std::max<size_t>(4096, panels_.size() * 2 + doors_.size() + cables_.size() + 64) : 4096;
    if (fixedRoster_ && sent_.size() >= capacity && !f_.noBacklogReset) {
        // One absent member cannot make the detector silently forget new progress. Start a new stream and
        // replace the bounded backlog with a snapshot of current state (including versioned cable OFFs).
        epoch_ = (uint64_t(session_) << 16) | ++restartsSeen_; nextSeq_ = 1; sent_.clear(); resync_ = true;
        log("coop: group backlog full, replacing it with a current-state snapshot");
    }
    Version version = {};
    if (fixedRoster_ && (kind == EV_CABLE_LIT || kind == EV_CABLE_OFF)) {
        if (!resync) cableVersion_[id] = {++clock_, unsigned(id_)};
        version = cableVersion_[id];
    }
    if (sent_.size() < capacity) { sent_.push_back({nextSeq_++, uint32_t(kind) | (resync ? kSnapBit : 0u), id, false, false, {}, {}, version}); st_.sent++; }
    if (h_.onEmit) h_.onEmit(kind, id, resync);
}

// ------------------------------------------------------------------------------------------------ network side
void SyncCore::tick(uint64_t now) {
    Guard g(h_);
    if (now >= nextPose_) {
        nextPose_ = now + 33;
        float pos[3], ang[2];
        if (w_->pose(pos, ang)) {
            Bytes b; put32(b, fixedRoster_ ? kGroupPose : kMagicPose); put32(b, uint32_t(id_)); put32(b, ++poseSeq_);
            for (float f : pos) putF(b, f);
            for (float f : ang) putF(b, f);
            put32(b, session_); send(finish(std::move(b)));
        }
    }
    if (now >= nextPing_) {        // a ping to every peer once a second; it carries this player's name, the pong brings the round trip back
        nextPing_ = now + 1000; ++pingNonce_;
        Bytes b; put32(b, kMagicPing); put32(b, uint32_t(id_)); put32(b, 1); put32(b, pingNonce_); put32(b, uint32_t(now));
        for (size_t i = 0; i < kNameLen; ++i) b.push_back(!f_.noNameSend && i < name_.size() ? uint8_t(name_[i]) : 0);
        send(finish(std::move(b)));
    }
    if (!f_.noPresenceSend && (presDirty_ || now >= nextPres_)) { presDirty_ = false; nextPres_ = now + 1000; sendPresence(); }
    if (!socOut_.empty()) socialTick(now);
    for (auto& kv : lines_) {      // a changed line goes out at once while it is being drawn at most every 100 ms, a finished or cleared one immediately
        LineState& s = kv.second;
        if (s.dirty && (s.final || s.pts.empty() || now >= s.sentAt + 100)) { sendLine(kv.first, s); s.dirty = false; s.sentAt = now; }
    }
    if (!f_.noLineResend && now >= nextLineResend_ && !lines_.empty()) {      // anti-entropy: one stored line per second, round robin
        nextLineResend_ = now + 1000;
        auto it = lines_.begin(); std::advance(it, lineCursor_ % lines_.size()); ++lineCursor_;
        if (!it->second.pts.empty() || it->second.seq) sendLine(it->first, it->second);
    }
    if (now >= nextResend_) {   // resend unacknowledged events, oldest first, at most 32 per recipient per round
        nextResend_ = now + 150; std::map<unsigned, unsigned> budget;
        for (auto& s : sent_) {
            if (s.acked) continue;
            Bytes b; bool built = false;      // the datagram is only built when some peer is actually sent to this round (a stalled peer's backlog is long and its budget is spent after 32)
            auto attempt = [&](unsigned peer) {
                unsigned budgetKey = f_.noIndependentBudget ? 0 : peer;
                if (budget[budgetKey] >= 32 || (f_.noResend && s.attempted.count(peer))) return;
                bool first = s.attempted.insert(peer).second;
                ++budget[budgetKey];
                if (!built) {
                    b.reserve(48); put32(b, fixedRoster_ ? kGroupEv : kMagicEv); put32(b, uint32_t(id_)); put64(b, epoch_); put32(b, s.seq); put32(b, s.kind); put32(b, s.entity);
                    if (fixedRoster_) { put64(b, s.version.first); put32(b, s.version.second); }
                    b = finish(std::move(b)); built = true;
                }
                if (!(f_.dropFirstEvent && first)) sendTo(peer, b);
            };
            if (peers_.empty()) attempt(0);  // legacy transport before the first pose
            else for (const auto& peer : peers_) if (!s.acknowledgers.count(peer.first)) attempt(peer.first);
            if (s.sentOnce) st_.resent++;
            s.sentOnce = true;
        }
    }
}

void SyncCore::onPacket(const uint8_t* d, size_t n, uint64_t now) {
    if (n < 8 || (!f_.noChecksum && sum32(d, n - 4) != get32(d + n - 4))) return;   // corrupted or foreign: drop, the sender resends
    uint32_t magic = get32(d);
    if (magic == kMagicLine) { onLine(d, n, now); return; }
    if (magic == kMagicPing) { onPing(d, n, now); return; }
    if (magic == kMagicSoc) { onSocial(d, n, now); return; }
    if (magic == kMagicSocAck) { onSocialAck(d, n); return; }
    if (magic == kMagicPres) { onPresence(d, n, now); return; }
    Version version = {};
    if (fixedRoster_) {
        if (magic == kGroupEv && n == kGroupEvLen) { version = {get64(d + 28), get32(d + 36)}; magic = kMagicEv; }
        else if (magic == kGroupAck && n == kAckLen) magic = kMagicAck;
        else if (magic == kGroupPose && n == kPoseLen) magic = kMagicPose;
        else return;
    }
    if (!((magic == kMagicEv && n == (fixedRoster_ ? kGroupEvLen : kEvLen)) || (magic == kMagicAck && n == kAckLen) || (magic == kMagicPose && n == kPoseLen))) return;
    Guard g(h_);
    unsigned sender = get32(d + 4);
    if (sender == unsigned(id_)) return;
    auto pi = peers_.find(sender);
    if (pi == peers_.end()) {
        if (fixedRoster_ || !peers_.empty()) return;
        pi = peers_.emplace(sender, Peer{}).first;
    }
    Peer& peer = pi->second;
    auto& peerEpoch_ = peer.epoch;
    auto& peerApplied_ = peer.applied;
    if (magic == kMagicEv && n == (fixedRoster_ ? kGroupEvLen : kEvLen)) {
        uint32_t from = get32(d + 4), seq = get32(d + 16), kind = get32(d + 20), entity = get32(d + 24); bool snap = (kind & kSnapBit) != 0; kind &= 0xFF; uint64_t epoch = get64(d + 8);
        if (int(from) == id_ || (kind != EV_PANEL_SOLVED && kind != EV_DOOR_OPENED && kind != EV_PANEL_POWERED && kind != EV_CABLE_LIT && kind != EV_CABLE_OFF) || entity >= kMaxEntityId) return;
        if (epoch < peerEpoch_) { st_.ooo++; if (h_.onRejected) h_.onRejected(uint8_t(kind), entity, epoch, seq, "stale epoch"); return; }   // a delayed packet of an earlier run
        if (epoch > peerEpoch_) { peerEpoch_ = epoch; if (!f_.noEpochReset) peerApplied_ = 0; }
        bool ordered = false, dup = false;
        if (seq == peerApplied_ + 1 && recvQ_.size() < 1024) { recvQ_.push_back({uint8_t(kind), entity, epoch, seq, snap, version}); peerApplied_ = seq; ordered = true; }
        else if (f_.applyOutOfOrder && seq > peerApplied_ + 1 && recvQ_.size() < 1024) { recvQ_.push_back({uint8_t(kind), entity, epoch, seq, snap, version}); peerApplied_ = seq; ordered = true; }
        else if (seq <= peerApplied_) {
            dup = true;
            if (f_.noDedupe && recvQ_.size() < 1024) recvQ_.push_back({uint8_t(kind), entity, epoch, seq, snap, version});    // twin: duplicates reach the apply step
        }
        if (ordered) { clock_ = std::max(clock_, version.first); st_.inOrder++; if (h_.onAccepted) h_.onAccepted(epoch, seq); } else if (dup) st_.dup++; else st_.ooo++;
        if (!ordered && h_.onRejected) h_.onRejected(uint8_t(kind), entity, epoch, seq, dup ? "duplicate" : "out of order");
        Bytes a; put32(a, fixedRoster_ ? kGroupAck : kMagicAck); put32(a, uint32_t(id_)); put64(a, peerEpoch_); put32(a, peerApplied_);
        sendTo(sender, finish(std::move(a)));
        return;
    }
    if (magic == kMagicAck && n == kAckLen) {
        uint32_t from = get32(d + 4), ack = get32(d + 16); uint64_t epoch = get64(d + 8);
        if (int(from) == id_ || (epoch != epoch_ && !f_.wrongAckEpoch)) return;
        for (auto& s : sent_) if (s.seq <= ack && !s.acked) {
            s.acknowledgers.insert(sender);
            if (s.acknowledgers.size() == peers_.size()) { s.acked = true; st_.acked++; }
        }
        while (!sent_.empty() && sent_.front().acked) sent_.erase(sent_.begin());
        return;
    }
    if (magic == kMagicPose && n == kPoseLen) {
        uint32_t from = get32(d + 4), seq = get32(d + 8), session = get32(d + 32);   // pose: magic id seq pos[3] ang[2] session checksum
        if (int(from) == id_) return;
        if (fixedRoster_ && session == peer.session && peer.pose.valid && seq <= peer.pose.seq) return;
        if (session < peer.session) return;     // a delayed packet from an earlier run of the peer: ignore it completely
        if (session > peer.session) {           // the peer (re)started: begin a new stream epoch and resend the full state
            peer.session = session; peerSession_ = session;
            epoch_ = (uint64_t(session_) << 16) | ++restartsSeen_; nextSeq_ = 1; sent_.clear();
            if (!f_.noResyncOnRestart) resync_ = true;
            st_.resyncs++;
            if (h_.onRestartSeen) h_.onRestartSeen();
            char l[120]; std::snprintf(l, sizeof l, "coop: peer session %08x seen, new event epoch %llx, full state resync", session, (unsigned long long)epoch_); log(l);
        }
        remote_.valid = true; remote_.id = from; remote_.seq = seq;
        for (int i = 0; i < 3; ++i) remote_.pos[i] = getF(d + 12 + 4 * i);
        for (int i = 0; i < 2; ++i) remote_.ang[i] = getF(d + 24 + 4 * i);
        remoteAt_ = now; peer.pose = remote_; peer.poseAt = now;
        return;
    }
}

// ------------------------------------------------------------------------------------------------ names and ping
void SyncCore::setName(const std::string& name) { Guard g(h_); name_ = name.substr(0, kNameLen); }

void SyncCore::onPing(const uint8_t* d, size_t n, uint64_t now) {
    if (n != kPingLen || sum32(d, n - 4) != get32(d + n - 4)) return;
    uint32_t from = get32(d + 4), kind = get32(d + 8), nonce = get32(d + 12), t0 = get32(d + 16);
    if (int(from) == id_ || (kind != 1 && kind != 2)) return;
    Guard g(h_);
    auto pi = peers_.find(from);
    if (pi == peers_.end()) { if (fixedRoster_ || !peers_.empty()) return; pi = peers_.emplace(from, Peer{}).first; }
    Peer& peer = pi->second;
    if (kind == 1) {                // a ping: remember the name, answer with the same nonce and time
        char nm[kNameLen + 1] = {}; memcpy(nm, d + 20, kNameLen); peer.name = nm;
        if (f_.noPingReply) return;
        Bytes b; put32(b, kMagicPing); put32(b, uint32_t(id_)); put32(b, 2); put32(b, nonce); put32(b, t0); for (size_t i = 0; i < kNameLen; ++i) b.push_back(0);
        sendTo(from, finish(std::move(b)));
        return;
    }
    if (nonce != pingNonce_) return;       // a pong for an older ping, or none of ours: the round trip would be wrong
    if (nonce == peer.lastNonce) return;   // one pong per ping (and per peer)
    peer.lastNonce = nonce; float rtt = float(uint32_t(uint32_t(now) - t0));
    peer.ping = peer.ping < 0 ? rtt : 0.7f * peer.ping + 0.3f * rtt; peer.pongAt = now;
}

// ------------------------------------------------------------------------------------------------ drawn lines
// Line packet: magic, from, session, panel, seq, flags (bit 0: finished), n, then n x (node u16, dx dy dz i16), checksum. The whole line travels every
// time and the highest seq wins, so a lost or reordered packet costs nothing once any later one arrives; every stored line is repeated about once a
// second (round robin) so a restarted peer, or one that missed everything, catches up.
void SyncCore::sendLine(unsigned panel, const LineState& s) {
    if (f_.noLineSend) return;
    Bytes b; put32(b, kMagicLine); put32(b, uint32_t(id_)); put32(b, session_); put32(b, panel); put32(b, s.seq); put32(b, s.final ? 1u : 0u);
    size_t n = std::min<size_t>(s.pts.size(), kMaxLinePoints); put32(b, uint32_t(n));
    for (size_t i = 0; i < n; ++i) {
        const LinePoint& p = s.pts[i];
        b.push_back(uint8_t(p.node)); b.push_back(uint8_t(p.node >> 8));
        for (int k = 0; k < 3; ++k) { b.push_back(uint8_t(uint16_t(p.d[k]))); b.push_back(uint8_t(uint16_t(p.d[k]) >> 8)); }
    }
    send(finish(std::move(b)));
}

void SyncCore::scanLines(uint64_t now) {
    if (f_.raceMode) return;      // (a drawn line is progress on a puzzle: it is not shared in a race)
    std::vector<LineOut> out;
    if (!w_->scanLines(out)) return;
    std::set<unsigned> present;
    for (auto& l : out) {
        if (l.panel >= kMaxEntityId || l.pts.empty()) continue;
        if (l.pts.size() > kMaxLinePoints) {       // keep the ends and an even spread of the points between them
            std::vector<LinePoint> thin; size_t keep = kMaxLinePoints;
            for (size_t i = 0; i < keep; ++i) thin.push_back(l.pts[i * (l.pts.size() - 1) / (keep - 1)]);
            l.pts = std::move(thin);
        }
        present.insert(l.panel);
        uint64_t h = 1469598103934665603ull; auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
        mix(l.final ? 1 : 0); for (auto& p : l.pts) { mix(p.node); for (int k = 0; k < 3; ++k) mix(uint16_t(p.d[k])); }
        LineState& s = lines_[l.panel];
        if (s.hash != h || s.seq == 0) { s.hash = h; s.final = l.final; s.pts = std::move(l.pts); ++s.seq; s.dirty = true; }
    }
    for (auto& kv : lines_) if (!present.count(kv.first) && !kv.second.pts.empty()) { kv.second.pts.clear(); kv.second.final = false; kv.second.hash = 0; ++kv.second.seq; kv.second.dirty = true; }   // the panel was reset: tell the peers
    (void)now;
}

void SyncCore::onLine(const uint8_t* d, size_t n, uint64_t now) {
    if (n < 32 || sum32(d, n - 4) != get32(d + n - 4)) return;
    uint32_t from = get32(d + 4), session = get32(d + 8), panel = get32(d + 12), seq = get32(d + 16), flags = get32(d + 20), cnt = get32(d + 24);
    if (int(from) == id_ || panel >= kMaxEntityId || cnt > kMaxLinePoints || n != size_t(32 + 8 * cnt)) return;
    Guard g(h_);
    if (!w_->isPanel(panel)) return;
    auto pi = peers_.find(from);
    if (pi == peers_.end()) { if (fixedRoster_ || !peers_.empty()) return; pi = peers_.emplace(from, Peer{}).first; }
    LineIn& l = pi->second.lines[panel];
    if (!f_.noLineSeqCheck && l.at && (session < l.session || (session == l.session && seq <= l.seq))) return;     // older or the same: already have it
    if (pi->second.lines.size() > 256) return;
    l.seq = seq; l.session = session; l.final = (flags & 1) != 0; l.at = now ? now : 1; l.pts.clear();
    for (uint32_t i = 0; i < cnt; ++i) {
        const uint8_t* p = d + 28 + 8 * i; LinePoint pt; pt.node = uint16_t(p[0] | p[1] << 8);
        for (int k = 0; k < 3; ++k) pt.d[k] = int16_t(uint16_t(p[2 + 2 * k] | p[3 + 2 * k] << 8));
        l.pts.push_back(pt);
    }
}

std::vector<RemoteLine> SyncCore::remoteLines(uint64_t now) {
    Guard g(h_); std::vector<RemoteLine> out; if (f_.raceMode) return out;
    for (const auto& p : peers_) for (const auto& kv : p.second.lines) if (!kv.second.pts.empty()) {
        RemoteLine r; r.from = p.first; r.panel = kv.first; r.seq = kv.second.seq; r.final = kv.second.final; r.pts = kv.second.pts; r.ageMs = now - kv.second.at; out.push_back(std::move(r));
    }
    return out;
}

// ------------------------------------------------------------------------------------------------ detector side
bool SyncCore::scanWorld() {
    std::vector<unsigned> p, d; uint64_t tid = 0;
    if (!w_->scan(p, d, &tid)) return false;
    bool changed = tid != tableId_ || p.size() != panels_.size() || d.size() != doors_.size();
    std::vector<unsigned> c; if (!w_->scanCables(c)) return false;
    changed = changed || c.size() != cables_.size();
    tableId_ = tid; panels_.swap(p); doors_.swap(d); cables_.swap(c);
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
    mix(0xFFFD);
    for (unsigned id : cables_) if (w_->entityExists(id) && w_->cableLit(id)) mix(id);
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
    for (unsigned id : cables_) {
        bool lit = w_->entityExists(id) && (fixedRoster_ && !f_.noGroupCableIntent ? w_->cableRequested(id) : w_->cableLit(id));
        if (lit) knownCable_.insert(id); else knownCable_.erase(id);
        if (full && lit && !f_.noCableSync) emit(EV_CABLE_LIT, id, true);
        if (full && !lit && !f_.noCableSync && !f_.noCableOff && !f_.noOffSnapshot && everOff_.count(id)) emit(EV_CABLE_OFF, id, true);   // a default-lit cable that was switched off
    }
}

bool SyncCore::reloadSeen(uint64_t now) {   // doors do not close and one panel can be redrawn, so those alone are not a load
    unsigned closed = 0, unsolved = 0;
    for (unsigned id : doors_) {
        auto sp = suppressDoor_.find(id);
        if (knownDoor_.count(id) && w_->entityExists(id) && !w_->doorOpen(id) && !(sp != suppressDoor_.end() && now < sp->second)) closed++;
    }
    for (unsigned id : panels_) {
        auto sp = suppressPanel_.find(id);
        if (knownPanel_.count(id) && w_->entityExists(id) && !w_->panelSolved(id) && !(sp != suppressPanel_.end() && now < sp->second)) unsolved++;
    }
    return closed >= 1 || unsolved >= 2;
}

void SyncCore::pollState(uint64_t now) {
    Guard g(h_);
    const bool groupIntent = fixedRoster_ && !f_.noGroupCableIntent;
    scanLines(now);
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
    if (!settledLogged_ && now >= settleUntil_) { settledLogged_ = true; log("state: settled"); }      // the start-up window is over: from here local changes are shared as they happen (tests wait for this line)
    if (!f_.noReloadSync && reloadSeen(now)) {
        // The process and the link are up, but the world lost progress it had: a save was loaded. Nothing told the peer, so
        // announce a new session (the peer then resends its full state, as for a restart), wait for the loading world to
        // settle again, and publish this world's full state once it has.
        log("state: save reload suspected (known progress vanished), waiting for a stable world");
        ready_ = false; gateSame_ = 0; gateLastPanels_ = gateLastDoors_ = 0; gateLastSig_ = 0; nextGate_ = now + 3000;
        cableVersion_.clear();
        suppressPanel_.clear(); suppressDoor_.clear(); suppressPower_.clear(); suppressCable_.clear(); anyLocalSolve_ = false;
        session_++; epoch_ = (uint64_t(session_) << 16) | ++restartsSeen_; nextSeq_ = 1; sent_.clear(); resync_ = true;
        st_.reloads++;
        return;
    }
    if (now >= nextScan_) { nextScan_ = now + 3000; if (scanWorld()) { log("state: entity table changed, new baseline"); baseline(false); } }
    if (resync_ && !groupIntent) { resync_ = false; baseline(true); log("state: full state queued for the peer"); }
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
    for (unsigned id : cables_) {      // same rules as panel power: a cable that lights during start-up is folded into the baseline
        if (!w_->entityExists(id)) continue;
        bool lit = groupIntent ? w_->cableRequested(id) : w_->cableLit(id);
        bool known = knownCable_.count(id) != 0;
        auto sp = suppressCable_.find(id);
        bool suppressed = sp != suppressCable_.end() && now < sp->second;
        // The render-thread applier already updated knownCable_ to the remote requested level. A different
        // requested level is a new local command, even during the echo guard; brightness fading is not.
        if (groupIntent && lit != known) suppressed = false;
        bool cascade = anyLocalSolve_ && now < lastLocalSolveAt_ + kCascadeMs;
        if (lit && !known && !suppressed && !(now < settleUntil_ && !cascade) && !f_.noCableSync) {
            emit(EV_CABLE_LIT, id, false); st_.emittedCables++;
            char l[100]; std::snprintf(l, sizeof l, "state: local cable %#x lit -> event", id); log(l);
        }
        if (!lit && known && !suppressed && !(now < settleUntil_ && !cascade) && !f_.noCableSync && !f_.noCableOff) {     // lit before, dark now: the game's power-off call
            emit(EV_CABLE_OFF, id, false); st_.emittedCables++; everOff_.insert(id); cableToggledAt_[id] = now;
            char l[100]; std::snprintf(l, sizeof l, "state: local cable %#x dark -> event", id); log(l);
        }
        if (lit && !known && !suppressed) cableToggledAt_[id] = now;
        if (lit) { knownCable_.insert(id); everOff_.erase(id); } else knownCable_.erase(id);
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
    // Detect new commands before a snapshot can fold them into the baseline with an older version.
    if (resync_ && groupIntent) { resync_ = false; baseline(true); log("state: full state queued for the peer"); }
    if (now >= nextStats_) {
        nextStats_ = now + 10000;
        char l[320]; std::snprintf(l, sizeof l, "state: emitted panels %u doors %u, applied panels %u doors %u | net sent %u resent %u in-order %u dup %u ooo %u acked %u | power emitted %u applied %u | cable emitted %u applied %u",
            st_.emittedPanels, st_.emittedDoors, st_.appliedPanels, st_.appliedDoors, st_.sent, st_.resent, st_.inOrder, st_.dup, st_.ooo, st_.acked, st_.emittedPowers, st_.appliedPowers, st_.emittedCables, st_.appliedCables); log(l);
    }
}

void SyncCore::applyRemote(uint64_t now) {
    Guard g(h_);
    if (f_.raceMode && !f_.raceApplies) { recvQ_.clear(); return; }      // a race: what another player did is not done here (the events were acknowledged, so the sender does not keep sending them)
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
        if (fixedRoster_ && !f_.noGroupVersions && (r.kind == EV_CABLE_LIT || r.kind == EV_CABLE_OFF) && w_->isCable(r.entity)) {
            // Snapshots carry the original mutation's version. They cannot resurrect an older on/off state.
            // A logical clock plus player id gives all receivers the same order without synchronized wall clocks.
            auto known = cableVersion_.find(r.entity);
            if (known != cableVersion_.end() && r.version <= known->second) continue;
            bool on = r.kind == EV_CABLE_LIT;
            bool needsApply = f_.noGroupCableIntent ? (on ? !w_->cableLit(r.entity) : w_->cableRequested(r.entity)) : w_->cableRequested(r.entity) != on;
            if (needsApply && !(on ? w_->applyCableLit(r.entity) : w_->applyCableOff(r.entity))) {
                if (!f_.noApplyRetry) recvQ_.push_back(r);
                continue;
            }
            cableVersion_[r.entity] = r.version;
            if (on) { knownCable_.insert(r.entity); everOff_.erase(r.entity); }
            else { knownCable_.erase(r.entity); everOff_.insert(r.entity); }
            if (!f_.noEchoGuard) suppressCable_[r.entity] = now + kSuppressMs;
            st_.appliedCables++;
            if (h_.onApplied) h_.onApplied(r.kind, r.entity, r.epoch, r.seq);
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
        } else if (r.kind == EV_CABLE_LIT && w_->isCable(r.entity)) {
            if (w_->cableLit(r.entity) && !f_.noDedupe) { knownCable_.insert(r.entity); everOff_.erase(r.entity); continue; }
            auto ct = cableToggledAt_.find(r.entity);
            // A peer's snapshot only says what its world looks like (a fresh process has its default-lit cables on): it never
            // overrides a cable this player switched off; a live event can.
            if (!f_.noCableArbiter && everOff_.count(r.entity) && (r.snap || (id_ == 1 && ct != cableToggledAt_.end() && now < ct->second + kConflictMs))) {     // this player switched it off: that wins, and the peer is told
                if (!f_.noSnapReassert) emit(EV_CABLE_OFF, r.entity, true);
                continue;
            }
            if (!w_->applyCableLit(r.entity)) {
                if (!f_.noApplyRetry) recvQ_.push_back(r);
                continue;
            }
            everOff_.erase(r.entity);
            if (!f_.noEchoGuard) { knownCable_.insert(r.entity); suppressCable_[r.entity] = now + kSuppressMs; }
            st_.appliedCables++;
            if (h_.onApplied) h_.onApplied(r.kind, r.entity, r.epoch, r.seq);
            std::snprintf(l, sizeof l, "state: remote cable %#x lit", r.entity); log(l);
        } else if (r.kind == EV_CABLE_OFF && w_->isCable(r.entity)) {
            if (f_.noCableOff) continue;
            bool lit = w_->cableRequested(r.entity);
            if (!lit && !f_.noDedupe) { knownCable_.erase(r.entity); everOff_.insert(r.entity); continue; }
            // Both players may switch the same cable at the same moment: the lower player id wins, so both worlds end the same.
            auto ct = cableToggledAt_.find(r.entity);
            if (!f_.noCableArbiter && id_ == 1 && ct != cableToggledAt_.end() && now < ct->second + kConflictMs) {
                emit(EV_CABLE_LIT, r.entity, true);      // keep this world's state and tell the peer, who just switched the same cable the other way
                continue;
            }
            if (!w_->applyCableOff(r.entity)) {
                if (!f_.noApplyRetry) recvQ_.push_back(r);
                continue;
            }
            knownCable_.erase(r.entity); everOff_.insert(r.entity);
            if (!f_.noEchoGuard) suppressCable_[r.entity] = now + kSuppressMs;
            st_.appliedCables++;
            if (h_.onApplied) h_.onApplied(r.kind, r.entity, r.epoch, r.seq);
            std::snprintf(l, sizeof l, "state: remote cable %#x dark", r.entity); log(l);
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
    auto peer = peers_.find(out->id); if (peer != peers_.end()) out->appearance = peer->second.pres.appearance;
    return out->valid;
}
std::vector<RemotePose> SyncCore::remotePoses(uint64_t now) {
    Guard g(h_); std::vector<RemotePose> poses;
    for (const auto& p : peers_) if (p.second.pose.valid) {
        auto pose = p.second.pose; pose.ageMs = now - p.second.poseAt;
        bool fresh = f_.noPingExpiry || (p.second.pongAt && now - p.second.pongAt < 10000);
        pose.pingMs = (p.second.ping >= 0 && fresh) ? int(p.second.ping + 0.5f) : -1; pose.name = p.second.name;
        bool presFresh = f_.noPresenceExpiry || (p.second.presAt && now - p.second.presAt < kPresExpiryMs);
        pose.appearance = p.second.pres.appearance; pose.emote = (!presFresh && ValidEmote(p.second.pres.emote) && kEmotes[p.second.pres.emote].loop) ? 0 : p.second.pres.emote; pose.emoteSeq = p.second.pres.emoteSeq; pose.typing = presFresh && p.second.pres.typing; pose.speaking = presFresh && p.second.pres.speaking; pose.proximitySpeaking = presFresh && p.second.pres.proximitySpeaking; pose.radioTx = presFresh && p.second.pres.radioTx;
#ifdef WR_MUT_RADIO_EXPIRE
        pose.radioTx = p.second.pres.radioTx;
#endif
        pose.radioEquipped = p.second.pres.radioEquipped; pose.voiceChannel = p.second.pres.voiceChannel;
        poses.push_back(pose);
    }
    return poses;
}
bool SyncCore::ready() const { Guard g(h_); return ready_; }
std::set<unsigned> SyncCore::solvedPanels() const { Guard g(h_); return knownPanel_; }
Stats SyncCore::stats() const { Guard g(h_); return st_; }
size_t SyncCore::unacked() const { Guard g(h_); size_t n = 0; for (auto& s : sent_) if (!s.acked) n++; return n; }
size_t SyncCore::queuedRemote() const { Guard g(h_); return recvQ_.size(); }


// ------------------------------------------------------------------------------------------------ social layer
// A reliable message: magic, from, epoch, seq, type, len, payload, checksum. Each sender numbers its messages (a new epoch starts the numbers again); a receiver takes them in
// order, once, and answers with the highest number it has taken (WTK6: magic, from, epoch, applied, checksum); the sender resends what a peer has not acknowledged every 250 ms.
// Presence (WTS6: magic, from, session, seq, emote, emoteSeq, flags, voiceChannel, appearance[12], checksum) is state: the newest wins, and typing / speaking end when it stops arriving.
void SyncCore::setPresence(const Presence& p) {
    Guard g(h_);
    Presence clean = p;
    clean.appearance = ClampAppearance(p.appearance);
    if (!ValidEmote(clean.emote)) clean.emote = 0;
    clean.voiceChannel = std::min<uint8_t>(3, p.voiceChannel);
    clean.radioTx = p.radioTx && p.radioEquipped && p.speaking && clean.voiceChannel == 1;
    if (!(clean == pres_)) { pres_ = clean; presDirty_ = true; }
}
void SyncCore::sendPresence() {
    Bytes b; put32(b, kMagicPres); put32(b, uint32_t(id_)); put32(b, session_); put32(b, ++presSeq_);
#ifdef WR_MUT_RADIO_TX
    pres_.radioTx=false;
#endif
#ifdef WR_MUT_RADIO_CHANNEL
    pres_.voiceChannel=0;
#endif
    b.push_back(pres_.emote); b.push_back(pres_.emoteSeq);
    b.push_back(uint8_t((pres_.typing ? 1 : 0) | (pres_.speaking ? 2 : 0) | (pres_.radioTx ? 4 : 0) | (pres_.radioEquipped ? 8 : 0) | (pres_.proximitySpeaking ? 4 : 0)));
    b.push_back(pres_.voiceChannel);
    auto appearance = EncodeAppearance(pres_.appearance); b.insert(b.end(), appearance.begin(), appearance.end());
    send(finish(std::move(b)));
}
void SyncCore::onPresence(const uint8_t* d, size_t n, uint64_t now) {
    if (n != kPresLen) return;
#ifndef WE_MUT_VALIDATION
    if (!ValidEmote(d[16]) || (d[18] & ~15) || d[19] > 3) return;
    if (d[19] != 0 && (d[18] & 4) && (!(d[18] & 8) || !(d[18] & 2) || d[19] != 1)) return;
    if (d[19] > 1 && (d[18] & 4)) return;
#endif
    Appearance appearance; if (!DecodeAppearance(d + 20, kAppearanceBytes, &appearance)) return;
    uint32_t from = get32(d + 4), session = get32(d + 8), seq = get32(d + 12);
    if (int(from) == id_) return;
    Guard g(h_);
    auto pi = peers_.find(from);
    if (pi == peers_.end()) { if (fixedRoster_ || !peers_.empty()) return; pi = peers_.emplace(from, Peer{}).first; }
    Peer& peer = pi->second;
    bool newer = session > peer.presSession || (session == peer.presSession && seq > peer.presSeq);
    if (!newer && !f_.noPresenceSeq) return;
    bool sessionChanged = (session != peer.presSession);
    if (sessionChanged) peer.emoteGate = {};
    peer.presSession = session; peer.presSeq = seq; peer.presAt = now;
    peer.pres.appearance = appearance;
    if (peer.emoteGate.Accept(now, d[16], sessionChanged || d[17] != peer.pres.emoteSeq || d[16] != peer.pres.emote)) {
        peer.pres.emote = d[16]; peer.pres.emoteSeq = d[17];
    }
    peer.pres.typing = (d[18] & 1) != 0;
    peer.pres.speaking = (d[18] & 2) != 0;
    peer.pres.radioTx = (d[19] == 1) && ((d[18] & 4) != 0);
    peer.pres.radioEquipped = (d[18] & 8) != 0;
    peer.pres.proximitySpeaking = (d[19] == 0) && ((d[18] & 4) != 0);
    peer.pres.voiceChannel = d[19];
}
bool SyncCore::socialQueue(uint8_t type, Bytes payload) {
    if (payload.size() > kSocMax) return false;
    Guard g(h_);
    if (socOut_.size() >= 1024) {      // a peer that never acknowledges must not make this grow for ever: a new stream (the receivers start counting again)
        socEpoch_ = (uint64_t(session_) << 16) | ++socRestarts_; socNext_ = 1; socOut_.clear();
    }
    if (!socEpoch_) socEpoch_ = uint64_t(session_) << 16;
    SocOut o; o.seq = socNext_++; o.type = type; o.payload = std::move(payload); socOut_.push_back(std::move(o)); return true;
}
bool SyncCore::sendChat(const std::string& text) {
    std::string t = text.substr(0, kMaxChatBytes); if (t.empty()) return false;
    return socialQueue(SOC_CHAT, Bytes(t.begin(), t.end()));
}
bool SyncCore::sendMark(uint8_t kind, const float pos[3]) { Bytes b; b.push_back(kind); for (int i = 0; i < 3; ++i) putF(b, pos[i]); return socialQueue(SOC_MARK, std::move(b)); }
bool SyncCore::sendStroke(uint32_t id, bool begin, bool end, uint8_t color, const float* pts, size_t count) {
    if (count > kMaxStrokePoints) return false;
    Bytes b; put32(b, id); b.push_back(uint8_t((begin ? 1 : 0) | (end ? 2 : 0))); b.push_back(color); b.push_back(uint8_t(count));
    for (size_t i = 0; i < count * 3; ++i) putF(b, pts[i]);
    return socialQueue(SOC_STROKE, std::move(b));
}
bool SyncCore::sendStrokeClear(uint32_t id) { Bytes b; put32(b, id); return socialQueue(SOC_STROKE_CLEAR, std::move(b)); }
std::vector<ChatLine> SyncCore::takeChat() { Guard g(h_); auto v = std::move(chatIn_); chatIn_.clear(); return v; }
std::vector<MarkMsg> SyncCore::takeMarks() { Guard g(h_); auto v = std::move(markIn_); markIn_.clear(); return v; }
std::vector<StrokeMsg> SyncCore::takeStrokes() { Guard g(h_); auto v = std::move(strokeIn_); strokeIn_.clear(); return v; }

void SyncCore::socialTick(uint64_t now) {
    std::map<unsigned, unsigned> budget;
    for (auto& it : socOut_) {
        if (it.sent && (f_.noSocialResend || now < it.lastSent + 250)) continue;
        Bytes b; bool built = false, any = false;
        auto attempt = [&](unsigned peer) {
            if (budget[peer] >= 24) return;
            ++budget[peer];
            if (!built) { put32(b, kMagicSoc); put32(b, uint32_t(id_)); put64(b, socEpoch_); put32(b, it.seq); put32(b, it.type); put32(b, uint32_t(it.payload.size())); b.insert(b.end(), it.payload.begin(), it.payload.end()); b = finish(std::move(b)); built = true; }
            sendTo(peer, b); any = true;
        };
        if (peers_.empty()) attempt(0); else for (const auto& peer : peers_) if (!it.acked.count(peer.first)) attempt(peer.first);
        if (any) { it.sent = true; it.lastSent = now; }
    }
    auto done = [&](const SocOut& o) { if (peers_.empty()) return false; for (const auto& peer : peers_) if (!o.acked.count(peer.first)) return false; return true; };
    while (!socOut_.empty() && done(socOut_.front())) socOut_.erase(socOut_.begin());
}
void SyncCore::onSocialAck(const uint8_t* d, size_t n) {
    if (n != kSocAckLen) return;
    uint32_t from = get32(d + 4), applied = get32(d + 16); uint64_t epoch = get64(d + 8);
    if (int(from) == id_) return;
    Guard g(h_);
    if (epoch != socEpoch_) return;
    for (auto& o : socOut_) if (o.seq <= applied) o.acked.insert(from);
}
void SyncCore::onSocial(const uint8_t* d, size_t n, uint64_t) {
    if (n < kSocHeader + 4) return;
    uint32_t len = get32(d + 24);
    if (len > kSocMax || n != kSocHeader + len + 4) return;
    uint32_t from = get32(d + 4), seq = get32(d + 16), type = get32(d + 20); uint64_t epoch = get64(d + 8);
    if (int(from) == id_) return;
    Guard g(h_);
    auto pi = peers_.find(from);
    if (pi == peers_.end()) { if (fixedRoster_ || !peers_.empty()) return; pi = peers_.emplace(from, Peer{}).first; }
    Peer& peer = pi->second;
    if (epoch < peer.socEpoch) return;                                                // a delayed packet of an earlier run
    if (epoch > peer.socEpoch) { peer.socEpoch = epoch; if (!f_.noSocialEpochReset) peer.socApplied = 0; }
    bool take = false;
    if (seq == peer.socApplied + 1) take = true;
    else if (seq <= peer.socApplied) take = f_.noSocialDedupe;                       // twin: the resend is delivered again
    else if (f_.noSocialOrder) take = true;                                          // twin: ahead of the one before it
    if (take) {
        const uint8_t* p = d + kSocHeader;
        if (type == SOC_CHAT && len >= 1 && len <= kMaxChatBytes) { ChatLine c; c.from = from; c.seq = seq; for (uint32_t i = 0; i < len; ++i) c.text.push_back(char(p[i])); if (chatIn_.size() >= 256) chatIn_.erase(chatIn_.begin()); chatIn_.push_back(std::move(c)); }
        else if (type == SOC_MARK && len == 13) { MarkMsg m; m.from = from; m.seq = seq; m.kind = p[0]; for (int i = 0; i < 3; ++i) m.pos[i] = getF(p + 1 + 4 * i); if (markIn_.size() >= 256) markIn_.erase(markIn_.begin()); markIn_.push_back(m); }
        else if (type == SOC_STROKE && len >= 7) {
            StrokeMsg s; s.from = from; s.seq = seq; s.id = get32(p); s.begin = (p[4] & 1) != 0; s.end = (p[4] & 2) != 0; s.color = p[5]; size_t count = p[6];
            if (count <= kMaxStrokePoints && len == 7 + count * 12) { for (size_t i = 0; i < count * 3; ++i) s.pts.push_back(getF(p + 7 + 4 * i)); if (strokeIn_.size() >= 1024) strokeIn_.erase(strokeIn_.begin()); strokeIn_.push_back(std::move(s)); }
        }
        else if (type == SOC_STROKE_CLEAR && len == 4) { StrokeMsg s; s.from = from; s.seq = seq; s.id = get32(p); s.clear = true; if (strokeIn_.size() >= 1024) strokeIn_.erase(strokeIn_.begin()); strokeIn_.push_back(std::move(s)); }
        if (seq > peer.socApplied) peer.socApplied = seq;
    }
    Bytes a; put32(a, kMagicSocAck); put32(a, uint32_t(id_)); put64(a, peer.socEpoch); put32(a, peer.socApplied);
    sendTo(from, finish(std::move(a)));
}

}  // namespace witness
