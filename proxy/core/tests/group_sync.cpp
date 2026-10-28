// Exercise the production core with eight independent worlds and addressed lossy links.
#include "../sync_core.h"
#include <array>
#include <memory>
#include <cstdio>
#include <cstdlib>
struct World : witness::World {
    std::array<bool, 32> solved{}, doors{}, power{}, cables{};
    unsigned player = 0;
    bool animate = false;
    std::array<bool, 32> requested{};
    std::array<uint64_t, 32> fadeAt{};
    uint64_t clock = 0;
    void step(uint64_t now) {
        clock = now;
        if (animate) for (unsigned id : {17u,18u}) if (fadeAt[id] && now >= fadeAt[id]) { cables[id] = requested[id]; fadeAt[id] = 0; }
    }
    void switchCable(unsigned id, bool on) {
        if (animate) { requested[id] = on; fadeAt[id] = clock + 3000; }
        else cables[id] = on;
    }
    bool cameraValid() override { return true; }
    bool scan(std::vector<unsigned>& p, std::vector<unsigned>& d, uint64_t* t) override {
        for (unsigned i=1;i<=8;++i) { p.push_back(i); d.push_back(i+8); } *t=1; return true;
    }
    bool scanCables(std::vector<unsigned>& c) override { c={17,18}; return true; }
    bool entityExists(unsigned id) override { return id>0 && id<=18; }
    bool isPanel(unsigned id) override { return id>0 && id<=8; }
    bool isDoor(unsigned id) override { return id>8 && id<=16; }
    bool isCable(unsigned id) override { return id==17 || id==18; }
    bool panelSolved(unsigned id) override { return solved[id]; }
    bool doorOpen(unsigned id) override { return doors[id]; }
    bool panelPowered(unsigned id) override { return power[id]; }
    bool cableLit(unsigned id) override { return cables[id] && (!animate || requested[id]); }
    bool cableRequested(unsigned id) override { return animate ? requested[id] : cables[id]; }
    void applyPanelSolved(unsigned id) override { solved[id]=true; }
    bool applyDoorOpen(unsigned id) override { doors[id]=true; return true; }
    bool applyPanelPower(unsigned id) override { power[id]=true; return true; }
    bool applyCableLit(unsigned id) override { switchCable(id,true); return true; }
    bool applyCableOff(unsigned id) override { switchCable(id,false); return true; }
    bool pose(float p[3],float a[2]) override { p[0]=float(player);p[1]=p[2]=a[0]=a[1]=0;return true; }
};
struct Packet { unsigned from,to; witness::Bytes bytes; };
struct Group {
    World worlds[8]; std::unique_ptr<witness::SyncCore> cores[8];
    std::vector<Packet> packets; uint64_t now=0; unsigned serial=0; unsigned liveCableEmits=0; bool isolate=false, mutant=false, versionMutant=false, budgetMutant=false, backlogMutant=false, intentMutant=false;
    Group(bool broken, bool oldVersions=false, bool oldBudget=false, bool oldBacklog=false, bool oldIntent=false):mutant(broken),versionMutant(oldVersions),budgetMutant(oldBudget),backlogMutant(oldBacklog),intentMutant(oldIntent) { for(unsigned i=0;i<8;++i) start(i,1); }
    void start(unsigned i,unsigned session) {
        worlds[i]=World();worlds[i].player=i+1;
        witness::Hooks h;
        h.onEmit = [this](uint8_t kind, unsigned, bool snapshot) { if (!snapshot && (kind==witness::EV_CABLE_LIT || kind==witness::EV_CABLE_OFF)) ++liveCableEmits; };
        h.sendTo=[this,i](unsigned to,const witness::Bytes& b) {
            // The mutant models the old broadcast ACK bug: every peer's ACK is attributed to all peers.
            if (mutant && b.size()==24) {
                for(unsigned j=0;j<8;++j) if(j!=to-1) { auto d=b;d[4]=uint8_t(j+1);
                    uint32_t sum=2166136261u;for(size_t k=0;k<d.size()-4;++k){sum^=d[k];sum*=16777619u;}
                    for(int k=0;k<4;++k)d[d.size()-4+k]=uint8_t(sum>>(8*k));
                    packets.push_back({i,to-1,d}); }
            }
            packets.push_back({i,to-1,b});
        };
        witness::Flags flags; flags.noGroupVersions=versionMutant; flags.noIndependentBudget=budgetMutant; flags.noBacklogReset=backlogMutant; flags.noGroupCableIntent=intentMutant;
        cores[i]=std::make_unique<witness::SyncCore>(i+1,session,&worlds[i],h,flags);
        std::vector<unsigned> ids;for(unsigned j=1;j<=8;++j)if(j!=i+1)ids.push_back(j);
        if(!cores[i]->setPeers(ids)) std::abort();
    }
    void advance(unsigned ms) {
        auto end=now+ms;
        while(now<end) { now+=50;
            for(auto& w:worlds)w.step(now);
            for(auto& c:cores)c->pollState(now);
            for(auto& c:cores)c->tick(now);
            auto q=std::move(packets);packets.clear();
            // Reverse delivery, deterministic loss and duplicates; isolate player 8's incoming events.
            for(auto it=q.rbegin();it!=q.rend();++it) {
                auto& p=*it;if(isolate && p.to==7 && p.bytes.size()==44)continue;
                if(++serial%7==0)continue;
                cores[p.to]->onPacket(p.bytes.data(),p.bytes.size(),now);
                if(serial%11==0)cores[p.to]->onPacket(p.bytes.data(),p.bytes.size(),now);
            }
            for(auto& c:cores)c->applyRemote(now);
        }
    }
    bool run() {
        advance(32000);isolate=true;
        for(unsigned i=0;i<8;++i){worlds[i].solved[i+1]=true;worlds[i].doors[i+9]=true;worlds[i].power[i+1]=true;}
        worlds[3].cables[17]=true;advance(4000);
        if(cores[0]->unacked()==0 || worlds[7].solved[1]) return false;
        // One absent peer must not block the remaining peers behind its resend window.
        for(unsigned i=0;i<4200;++i) { worlds[0].cables[18]=(i%2==0); advance(200); }
        advance(4000);
        for(unsigned i=1;i<7;++i)if(worlds[i].cables[18])return false;
        isolate=false;advance(45000); // reverse ordering admits one event per resend round; drain the compacted snapshot and tail
        for(auto& w:worlds)for(unsigned i=1;i<=8;++i)if(!w.solved[i]||!w.doors[i+8]||!w.power[i]||!w.cables[17])return false;
        for(auto& c:cores)if(c->unacked() || c->remotePoses(now).size()!=7)return false;
        worlds[5].cables[17]=false;advance(7000);
        for(auto& w:worlds)if(w.cables[17])return false;
        start(7,2);advance(32000);
        for(unsigned i=1;i<=8;++i)if(!worlds[7].solved[i]||!worlds[7].doors[i+8]||!worlds[7].power[i])return false;
        for(auto& c:cores)if(c->unacked())return false;
        // Opposing toggles occur before either actor receives the other's packets.
        worlds[4].cables[18]=true; cores[4]->pollState(now+1); cores[4]->tick(now+1);
        worlds[5].cables[18]=true; cores[5]->pollState(now+1);
        worlds[5].cables[18]=false; cores[5]->pollState(now+2);
        advance(10000);
        for(auto& w:worlds)if(w.cables[18])return false;
        // A same-process save reload must also discard cable baselines from the discarded world.
        worlds[6]=World();worlds[6].player=7;
        advance(32000);
        for(auto& w:worlds) {
            for(unsigned i=1;i<=8;++i)if(!w.solved[i]||!w.doors[i+8]||!w.power[i])return false;
            if(w.cables[17]||w.cables[18])return false;
        }
        return true;
    }
};
bool recentCableChange(bool animated, bool broken=false, bool restart=false) {
    Group g(false,false,false,false,broken);
    for(auto& w:g.worlds)w.animate=animated;
    g.advance(32000);
    g.worlds[0].switchCable(18,true);g.advance(1000);
    for(auto& w:g.worlds)if(!w.cableRequested(18))return false;
    if(animated && g.worlds[0].cableLit(18))return false; // intent must arrive before the glow finishes
    if(restart) { g.start(7,2); g.advance(50); } // every survivor has a resync pending at the next detector poll
    g.worlds[6].switchCable(18,false); // a real local switch during the remote echo-guard window
    g.advance(restart ? 32000 : 8000);
    for(auto& w:g.worlds)if(w.cableRequested(18)||w.cableLit(18))return false;
    return g.liveCableEmits==2; // exactly the two local commands, with no received-change echoes
}
bool rosterAndProtocol() {
    World w; std::vector<witness::Bytes> wire;
    witness::Hooks h; h.sendTo = [&](unsigned, const witness::Bytes& b) { wire.push_back(b); };
    witness::SyncCore receiver(1, 1, &w, h), sender(2, 1, &w, h);
    std::vector<unsigned> peers; for (unsigned i=2; i<=16; ++i) peers.push_back(i);
    if (!receiver.setPeers(peers) || !sender.setPeers({1})) return false;
    auto invalid=peers; invalid.push_back(17);
    witness::SyncCore bad(1,1,&w,h);
    if (bad.setPeers(invalid) || bad.setPeers({2,2}) || bad.setPeers({1}) || bad.setPeers({})) return false;
    sender.tick(33); if (wire.empty()) return false;
    auto pose=wire.front();
    receiver.onPacket(pose.data(),pose.size(),33);
    receiver.onPacket(pose.data(),pose.size(),100); // duplicate cannot refresh last-seen time
    auto seen=receiver.remotePoses(100);
    if (seen.size()!=1 || seen[0].id!=2 || seen[0].ageMs!=67) return false;
    auto patch = [&](unsigned from,unsigned magic) {
        auto p=pose;
        for (int i=0;i<4;++i) { p[i]=uint8_t(magic>>(8*i)); p[i+4]=uint8_t(from>>(8*i)); }
        uint32_t sum=2166136261u;for(size_t i=0;i<p.size()-4;++i){sum^=p[i];sum*=16777619u;}
        for(int i=0;i<4;++i)p[p.size()-4+i]=uint8_t(sum>>(8*i));
        receiver.onPacket(p.data(),p.size(),110);
    };
    patch(17,0x37505457); // valid group checksum and magic, foreign sender
    patch(3,0x34505457);  // configured sender, incompatible legacy single-link protocol
    patch(3,0x36505457);  // configured sender, incompatible legacy group protocol
    return receiver.remotePoses(120).size()==1;
}
int main(){bool rapid=recentCableChange(false), fade=recentCableChange(true), resync=recentCableChange(false,false,true);
    bool intentTwin=!recentCableChange(false,true) && !recentCableChange(true,true) && !recentCableChange(false,true,true);
    std::printf("%s local cable reversal during remote echo guard\n",rapid?"PASS":"FAIL");
    std::printf("%s requested cable state precedes glow, with no echo\n",fade?"PASS":"FAIL");
    std::printf("%s cable command detected before concurrent restart snapshot\n",resync?"PASS":"FAIL");
    std::printf("%s old cable-intent/echo bookkeeping mutant caught\n",intentTwin?"PASS":"FAIL");
    bool roster=rosterAndProtocol();std::printf("%s 16-entry roster capacity, invalid rosters, foreign/legacy packets and stale poses\n",roster?"PASS":"FAIL");bool ok=Group(false).run(), caught=!Group(true).run(), versions=!Group(false,true).run(), budgets=!Group(false,false,true).run(), backlog=!Group(false,false,false,true).run();
    std::printf("%s eight-player loss, per-peer ACK, progress, poses, cable off and restart\n",ok?"PASS":"FAIL");
    std::printf("%s broadcast-ACK mutant caught\n",caught?"PASS":"FAIL");std::printf("%s two-player cable arbiter mutant caught in group conflict\n",versions?"PASS":"FAIL");std::printf("%s shared resend budget mutant caught\n",budgets?"PASS":"FAIL");std::printf("%s silent full-backlog drop mutant caught\n",backlog?"PASS":"FAIL");return rapid&&fade&&resync&&intentTwin&&roster&&ok&&caught&&versions&&budgets&&backlog?0:1;}
