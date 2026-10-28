#define main protocol_main
#include "protocol.cpp"
#undef main
#include <memory>
#define REQUIRE(x) do { if(!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while(0)
int main() {
    REQUIRE(protocol_main()==0);
    W w; std::vector<Bytes> wire; Hooks h; h.send=[&](const Bytes& b){wire.push_back(b);};
    SyncCore a(1,1,&w,h), b(2,1,&w,{}); Presence p; p.emote=24; p.emoteSeq=1; p.proximitySpeaking=true;
    a.setPresence(p); a.tick(1000); REQUIRE(!wire.empty());
    Bytes pres; for(auto& packet:wire) { b.onPacket(packet.data(),packet.size(),1000); if(packet[2]=='S') pres=packet; }
    REQUIRE(!pres.empty());
    uint64_t current=1000; auto get=[&](){return b.remotePoses(current)[0];}; REQUIRE(get().emote==24 && get().proximitySpeaking);
    auto packet=pres; auto send=[&](unsigned seq,int emote,int counter,uint64_t now) {current=now; packet[12]=seq; packet[16]=emote; packet[17]=counter; checksum(packet); b.onPacket(packet.data(),packet.size(),now);};
    packet[21]=1;
    for(int value=25;value<256;++value) { send(2,value,2,1100); REQUIRE(get().emote==24 && get().appearance==p.appearance); }
    packet[19]=1; send(2,1,2,1100); REQUIRE(get().emote==24 && get().appearance==p.appearance); packet[19]=0;
    packet[18]=128; send(2,1,2,1100); REQUIRE(get().emote==24 && get().proximitySpeaking); packet[18]=4; packet[21]=0;
    send(2,1,2,1100); REQUIRE(get().emote==24); // flood cannot restart
    send(3,1,2,1400); REQUIRE(get().emote==1); // stable retry converges
    send(4,0,3,1401); REQUIRE(get().emote==0); // cancel never throttled
    send(3,24,2,1600); REQUIRE(get().emote==0); // reordering cannot resurrect
    send(5,22,4,2000); REQUIRE(get().emote==22);
    auto expired=b.remotePoses(6000)[0]; REQUIRE(!expired.proximitySpeaking && expired.emote==0);
    send(6,24,255,7000); REQUIRE(get().emote==24);
    send(7,1,0,7500); REQUIRE(get().emote==1); // eight-bit counter wrap
    packet[8]=2; send(1,2,1,7600); REQUIRE(get().emote==2); // new session resets rate limit gate
    // Loss/reordering simulator: each catalog ID eventually converges over state
    // heartbeats without retriggering when duplicate packets are delivered.
    for(int id=1;id<=kEmoteCount;++id) {
        send(8+id,id,id,8000+id*500); REQUIRE(get().emote==id);
        auto duplicate=packet; send(7+id,1,1,8010+id*500); REQUIRE(get().emote==id); packet=duplicate;
    }
    // Eight-player simulator: deterministic loss, reverse delivery and duplicates
    // exercise the production group fan-out and per-peer presence state.
    for(unsigned seed=1;seed<=32;++seed) {
    std::vector<std::pair<unsigned,Bytes>> group;
    Hooks gh; gh.sendTo=[&](unsigned to,const Bytes& bytes){group.push_back({to,bytes});};
    SyncCore leader(1,1,&w,gh); REQUIRE(leader.setPeers({2,3,4,5,6,7,8}));
    std::vector<std::unique_ptr<SyncCore>> followers;
    for(int id=2;id<=8;++id) {
        Hooks fh; fh.sendTo=[](unsigned,const Bytes&){}; auto peer=std::make_unique<SyncCore>(id,1,&w,fh); std::vector<unsigned> roster;
        for(int other=1;other<=8;++other) if(other!=id) roster.push_back(other);
        REQUIRE(peer->setPeers(roster)); followers.push_back(std::move(peer));
    }
    unsigned random=seed;
    for(int id=1;id<=kEmoteCount;++id) {
        p.emote=id; p.emoteSeq=id; leader.setPresence(p);
        uint64_t end=10000+id*2200;
        for(uint64_t t=end-2000;t<=end;t+=100) {
            group.clear(); leader.tick(t);
            for(auto it=group.rbegin();it!=group.rend();++it) {
                random^=random<<13; random^=random>>17; random^=random<<5;
                if(random%4==0 && t<end-500) continue; // connectivity recovers before final heartbeat
                auto& peer=*followers[it->first-2]; peer.onPacket(it->second.data(),it->second.size(),t);
                peer.onPacket(it->second.data(),it->second.size(),t); // duplicate
            }
        }
        for(auto& peer:followers) { auto poses=peer->remotePoses(end); if(poses.size()!=1 || poses[0].emote!=id || !poses[0].proximitySpeaking) std::fprintf(stderr,"group id %d size %zu got %d prox %d\n",id,poses.size(),poses.empty()?-1:poses[0].emote,poses.empty()?0:poses[0].proximitySpeaking); REQUIRE(poses.size()==1 && poses[0].emote==id && poses[0].proximitySpeaking); }
    }
    }
    wire.clear(); p.emote=255; a.setPresence(p); a.tick(100000);
    bool sanitized=false; for(auto& bytes:wire) if(bytes[2]=='S') sanitized=bytes[16]==0;
    REQUIRE(sanitized);
    std::puts("emotes: all IDs, hostile bytes, rate limit, cancel, reorder, wrap, expiry passed");
}
