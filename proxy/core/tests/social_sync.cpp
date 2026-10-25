// The social layer: reliable, ordered chat / location pings / drawing strokes, and presence (character, emote, typing, speaking), between two players on a lossy,
// delaying, reordering link and in a group of four. Every protection has a twin (a Flags switch) that this test must fail.
#include "../sync_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

using namespace witness;

struct W : World {
    bool cameraValid() override { return true; }
    bool scan(std::vector<unsigned>& p, std::vector<unsigned>&, uint64_t* t) override { p = {5}; *t = 1; return true; }
    bool entityExists(unsigned id) override { return id == 5; }
    bool isPanel(unsigned id) override { return id == 5; }
    bool isDoor(unsigned) override { return false; }
    bool panelSolved(unsigned) override { return false; }
    bool doorOpen(unsigned) override { return false; }
    void applyPanelSolved(unsigned) override {}
    bool applyDoorOpen(unsigned) override { return false; }
    bool pose(float p[3], float a[2]) override { p[0] = p[1] = p[2] = 1; a[0] = a[1] = 0; return true; }
};
struct Msg { uint64_t at; unsigned to; Bytes d; };
// n players in a full mesh (addressed transport, a fixed roster) or two on the legacy single link; loss and jitter per packet
struct Net {
    unsigned n; std::vector<W> worlds; std::vector<std::unique_ptr<SyncCore>> cores; std::vector<Msg> q; uint64_t now = 0, delay = 60, jitter = 0; unsigned lossPct = 0, seed = 11; bool alive[8];
    bool lose() { seed = seed * 1103515245u + 12345u; return ((seed >> 16) % 100) < lossPct; }
    unsigned rnd(unsigned m) { seed = seed * 1103515245u + 12345u; return m ? (seed >> 16) % m : 0; }
    void start(unsigned i, uint32_t session, Flags f) {
        Hooks h;
        if (n > 2) h.sendTo = [this, i](unsigned to, const Bytes& b) { if (!lose()) q.push_back({now + delay + rnd(jitter + 1), to - 1, b}); };
        else h.send = [this, i](const Bytes& b) { if (!lose()) q.push_back({now + delay + rnd(jitter + 1), 1 - i, b}); };
        cores[i] = std::make_unique<SyncCore>(int(i + 1), session, &worlds[i], h, f);
        if (n > 2) { std::vector<unsigned> ids; for (unsigned j = 1; j <= n; ++j) if (j != i + 1) ids.push_back(j); if (!cores[i]->setPeers(ids)) std::abort(); }
        cores[i]->setName("P" + std::to_string(i + 1)); alive[i] = true;
    }
    Net(unsigned players, Flags f = {}) : n(players), worlds(players), cores(players) { for (unsigned i = 0; i < n; ++i) start(i, 1, f); }
    void advance(uint64_t ms) {
        uint64_t end = now + ms;
        while (now < end) {
            now += 10;
            for (unsigned i = 0; i < n; ++i) if (alive[i]) { cores[i]->pollState(now); cores[i]->tick(now); }
            auto cur = std::move(q); q.clear();
            for (auto& m : cur) { if (m.at <= now) { if (alive[m.to]) cores[m.to]->onPacket(m.d.data(), m.d.size(), now); } else q.push_back(std::move(m)); }
        }
    }
};

constexpr size_t kSocPayloadAt = 28;      // the payload of a social packet starts after magic, from, epoch, seq, type, len
static std::string text(int i) { return "message number " + std::to_string(i); }
// every message of every sender arrives at the other, once and in order
static bool chatArrives(unsigned loss, unsigned jitter, Flags f = {}, int count = 40) {
    Net net(2, f); net.lossPct = loss; net.jitter = jitter; net.advance(1500);
    for (int i = 0; i < count; ++i) { net.cores[0]->sendChat(text(i)); net.cores[1]->sendChat("B" + text(i)); net.advance(40); }
    net.advance(30000);
    auto a = net.cores[1]->takeChat(), b = net.cores[0]->takeChat();
    if (int(a.size()) != count || int(b.size()) != count) { std::printf("    got %zu and %zu of %d\n", a.size(), b.size(), count); return false; }
    for (int i = 0; i < count; ++i) if (a[i].text != text(i) || a[i].from != 1 || b[i].text != "B" + text(i) || b[i].from != 2) return false;
    return net.cores[1]->takeChat().empty();
}
static bool marksAndStrokes(Flags f = {}) {
    Net net(2, f); net.lossPct = 30; net.jitter = 80; net.advance(1500);
    float where[3] = {-148.25f, -155.5f, 7.75f}; net.cores[0]->sendMark(3, where);
    float pts[30]; for (int i = 0; i < 30; ++i) pts[i] = float(i) * 0.5f;
    net.cores[0]->sendStroke(77, true, false, 2, pts, 10); net.cores[0]->sendStroke(77, false, false, 2, pts + 15, 5); net.cores[0]->sendStroke(77, false, true, 2, pts, 3); net.cores[0]->sendStrokeClear(0);
    net.advance(20000);
    auto m = net.cores[1]->takeMarks(); auto s = net.cores[1]->takeStrokes();
    if (m.size() != 1 || m[0].kind != 3 || m[0].pos[0] != where[0] || m[0].pos[1] != where[1] || m[0].pos[2] != where[2] || m[0].from != 1) return false;
    if (s.size() != 4 || !s[0].begin || s[0].end || s[0].pts.size() != 30 || s[0].id != 77 || s[0].color != 2 || s[1].pts.size() != 15 || !s[2].end || s[2].pts.size() != 9 || !s[3].clear || s[3].id != 0) return false;
    for (int i = 0; i < 30; ++i) if (s[0].pts[i] != pts[i]) return false;
    return true;
}
// the sender restarts (a later session, its numbers start again): its new messages are taken, not mistaken for old ones
static bool restart(Flags f = {}) {
    Net net(2, f); net.advance(1500);
    for (int i = 0; i < 5; ++i) net.cores[0]->sendChat(text(i));
    net.advance(5000); net.cores[1]->takeChat();
    net.start(0, 2, f); net.advance(3000);
    net.cores[0]->sendChat("after the restart"); net.advance(5000);
    auto c = net.cores[1]->takeChat(); return c.size() == 1 && c[0].text == "after the restart";
}
static bool presence(Flags f = {}) {
    Net net(2, f); net.advance(1000);
    Presence p; p.character = "Magic Frog"; p.emote = 4; p.emoteSeq = 9; p.typing = true; p.speaking = true; net.cores[0]->setPresence(p); net.advance(1500);
    auto v = net.cores[1]->remotePoses(net.now); if (v.empty()) return false;
    return v[0].character == "Magic Frog" && v[0].emote == 4 && v[0].emoteSeq == 9 && v[0].typing && v[0].speaking;
}
static bool presenceExpires(Flags f = {}) {      // typing and speaking end when presence stops arriving; the character stays
    Net net(2, f); net.advance(1000);
    Presence p; p.character = "Fox"; p.typing = true; net.cores[0]->setPresence(p); net.advance(2000);
    if (!net.cores[1]->remotePoses(net.now)[0].typing) return false;
    net.alive[0] = false; net.advance(8000);
    auto v = net.cores[1]->remotePoses(net.now); return !v.empty() && !v[0].typing && v[0].character == "Fox";
}
static bool presenceChangesAreFast(Flags f = {}) {      // a change goes out at once, not at the next heartbeat
    Net net(2, f); net.advance(3000);
    Presence p; p.typing = true; net.cores[0]->setPresence(p); net.advance(250);
    auto v = net.cores[1]->remotePoses(net.now); return !v.empty() && v[0].typing;
}
static bool olderPresenceLoses(Flags f = {}) {      // a delayed older presence packet does not overwrite a newer one
    Net net(2, f); net.advance(1000);
    Presence p; p.character = "New"; net.cores[0]->setPresence(p); net.advance(2000);
    Bytes b; auto put = [&](uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); };
    put(0x34535457); put(1); put(1); put(1); b.push_back(0); b.push_back(0); b.push_back(0); b.push_back(0); const char* nm = "Old"; for (int i = 0; i < 24; ++i) b.push_back(i < 3 ? uint8_t(nm[i]) : 0);
    uint32_t h = 2166136261u; for (uint8_t c : b) { h ^= c; h *= 16777619u; } put(h);
    net.cores[1]->onPacket(b.data(), b.size(), net.now);
    auto v = net.cores[1]->remotePoses(net.now); return !v.empty() && v[0].character == "New";
}
// A packet from the real peer (player 1, a later session so its numbers are accepted) with one flipped bit is dropped; the same packet unchanged is taken (the control: the
// test can fail). Without the checksum check (Flags::noChecksum) the corrupted one gets through.
static bool corruptIgnored(Flags f = {}) {
    Net net(2, f); net.advance(1500);
    Bytes captured; Hooks h; h.send = [&](const Bytes& b) { if (b.size() > 30 && b[0] == 'W' && b[1] == 'T' && b[2] == 'X') captured = b; };
    W w; SyncCore c(1, 5, &w, h); c.sendChat("evil"); c.tick(1000); c.tick(2000);
    if (captured.empty()) return false;
    Bytes bad = captured; bad[kSocPayloadAt + 1] ^= 0x01;      // "evil" -> "ewil"
    net.cores[1]->onPacket(bad.data(), bad.size(), net.now);
    if (!net.cores[1]->takeChat().empty()) return false;
    net.cores[1]->onPacket(captured.data(), captured.size(), net.now);
    auto got = net.cores[1]->takeChat(); return got.size() == 1 && got[0].text == "evil";
}
// four players, 25% loss and jitter: players 1 and 3 each say twelve things; every other player hears each of them once and in order
static bool group(Flags f = {}) {
    Net net(4, f); net.lossPct = 25; net.jitter = 50; net.advance(2000);
    for (int i = 0; i < 12; ++i) { net.cores[0]->sendChat("A" + std::to_string(i)); net.cores[2]->sendChat("C" + std::to_string(i)); net.advance(60); }
    net.advance(30000);
    for (unsigned r = 0; r < 4; ++r) {
        auto v = net.cores[r]->takeChat(); std::vector<std::string> a, c;
        for (auto& l : v) (l.from == 1 ? a : l.from == 3 ? c : a).push_back(l.text);
        size_t wantA = r == 0 ? 0 : 12, wantC = r == 2 ? 0 : 12;
        if (a.size() != wantA || c.size() != wantC) { std::printf("    player %u heard %zu from 1 and %zu from 3\n", r + 1, a.size(), c.size()); return false; }
        for (size_t i = 0; i < a.size(); ++i) if (a[i] != "A" + std::to_string(i)) return false;
        for (size_t i = 0; i < c.size(); ++i) if (c[i] != "C" + std::to_string(i)) return false;
    }
    return true;
}

int main() {
    Flags noResend, noDedupe, noOrder, noEpoch, noPres, noPresSeq, noPresExp;
    noResend.noSocialResend = true; noDedupe.noSocialDedupe = true; noOrder.noSocialOrder = true; noEpoch.noSocialEpochReset = true; noPres.noPresenceSend = true; noPresSeq.noPresenceSeq = true; noPresExp.noPresenceExpiry = true;
    bool ok = true; auto show = [&](bool r, const char* what) { std::printf("%s  %s\n", r ? "PASS" : "FAIL", what); ok = ok && r; };
    show(chatArrives(0, 0), "social: chat both ways arrives once, in order (no loss)");
    show(chatArrives(35, 0), "social: 35% loss, every message arrives once and in order");
    show(chatArrives(30, 150), "social: loss and a 150 ms jitter (packets overtake each other)");
    show(marksAndStrokes(), "social: a location ping and a stroke in three chunks, then a clear, arrive intact over a lossy link");
    show(restart(), "social: a restarted sender's new messages are taken");
    show(presence(), "presence: character, emote, typing and speaking arrive");
    show(presenceChangesAreFast(), "presence: a change arrives within 250 ms");
    show(presenceExpires(), "presence: typing and speaking end when the peer goes quiet, the character stays");
    show(olderPresenceLoses(), "presence: a delayed older packet does not overwrite a newer one");
    show(corruptIgnored(), "social: a corrupted packet is dropped");
    show(group(), "social: four players, 25% loss: both senders reach the others once each, in order");
    Flags noSum; noSum.noChecksum = true;
    show(!corruptIgnored(noSum), "twin no-checksum caught (the corrupted packet gets through)");
    show(!chatArrives(35, 0, noResend), "twin no-resend caught");
    show(!chatArrives(0, 0, noDedupe, 5) || !chatArrives(35, 0, noDedupe), "twin no-dedupe caught");
    show(!chatArrives(30, 150, noOrder), "twin no-order caught");
    show(!restart(noEpoch), "twin no-epoch-reset caught");
    show(!presence(noPres), "twin no-presence caught");
    show(!olderPresenceLoses(noPresSeq), "twin no-presence-seq caught");
    show(!presenceExpires(noPresExp), "twin no-presence-expiry caught");
    std::printf("%s\n", ok ? "ALL OK" : "FAILED"); return ok ? 0 : 1;
}
