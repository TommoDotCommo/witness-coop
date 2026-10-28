#define main protocol_contract_main
#include "../core/tests/protocol.cpp"
#undef main
#include "radio.h"
#include "../voice/voice.h"
#include "../model/model.h"
#include "../bindings.h"
#include <fstream>
#include <iterator>
#include <random>
#include <cstring>
#define CHECK(x) do { if(!(x)) { std::printf("radio FAIL %d: %s\n",__LINE__,#x); return 1; } } while(0)
int main() {
    CHECK(protocol_contract_main()==0);
    // Exhaustive presence vocabulary: reject malformed combinations atomically.
    for(unsigned f=0;f<256;++f) for(unsigned c=0;c<256;++c) {
        bool expected=!(f&~15u) && c<=3 && (!(f&4) || ((f&8) && (f&2) && c==1));
        CHECK(wradio::Valid(f,c)==expected);
    }
    wradio::Hold hold; hold.Key(true,true,false); CHECK(hold.Want(true,false,true));
    CHECK(!hold.Want(true,true,true) && !hold.Want(false,false,true) && !hold.Want(true,false,false));
    hold.Key(true,true,true); CHECK(!hold.down); hold.Key(true,false,false); CHECK(!hold.down); hold.Key(false,true,false); CHECK(!hold.down);
    wradio::Raise raise; raise.Step(true,.016f); CHECK(raise.value>0 && raise.value<.3f);
    for(int i=0;i<60;++i) raise.Step(true,.016f); CHECK(raise.value>.999f);
    raise.Step(false,.016f); CHECK(raise.value>.7f && raise.value<1);
    for(int i=0;i<60;++i) raise.Step(false,.016f); CHECK(raise.value<.001f);
    // Budget and projection: all raised vertices on screen, away from central puzzle.
    auto mesh=wradio::Mesh(true); CHECK(mesh.size()<=900 && mesh.size()%3==0);
    for(float aspect:{.5625f,1.0f,1.333f,1.778f,2.333f,3.556f}) for(float fov:{40.f,58.72f,90.f,110.f}) {
        float tv=std::tan(fov*.00872664626f);
        for(const auto& v:mesh) { float p[3]; wradio::FirstPerson(aspect,tv,1,v.p,p); float x=p[0]/(p[2]*tv*aspect),y=p[1]/(p[2]*tv);
            CHECK(p[2]>.1f && x>.60f && x<.98f && y>-.99f && y<.99f);
            for(float yaw:{0.f,1.f,3.f}) for(float pitch:{-.9f,0.f,.9f}) {
                wm::Cam cam={{2,3,4},yaw,pitch,fov,aspect,.1f,2000};float world[3],nx,ny;
                wradio::CameraToWorld(cam.pos,yaw,pitch,p,world);
                CHECK(wm::ProjectPoint(cam,world,&nx,&ny) && std::fabs(nx-x)<.0001f && std::fabs(ny-y)<.0001f);
            }
        }
    }
    // Same binding records drive configuration, launcher and guide.
    Cfg cfg; for(const auto& b:kBindings) CHECK(cfg.*(b.member)==unsigned(b.fallback[0]));
    CHECK(SetBindingLine(&cfg,"key_radio=F12\r\n") && cfg.keyRadio==0x7b);
    CHECK(SetBindingLine(&cfg,"key_radio=k") && cfg.keyRadio=='K');
    for(const char* bad:{"", "F0", "F13", "F01", "F1junk", " K", "K junk"}) { CHECK(SetBindingLine(&cfg,std::string("key_radio=")+bad)); CHECK(cfg.keyRadio=='K'); }
    CHECK(!SetBindingLine(&cfg,"unknown=Q"));
    for(auto size:{std::pair<float,float>{640,480},{1280,720},{2560,1080},{1080,1920},{3840,2160}}) {
        auto g=wradio::Guide(size.first,size.second,9);
        CHECK(g.x>=0 && g.y>=0 && g.x+g.width<size.first && g.y+g.height<size.second);
        CHECK(g.Row(8)+20*g.scale<g.y+g.height);
        for(const auto& b:kBindings) CHECK((5+std::strlen(b.label))*10*g.scale<g.width-24*g.scale);
        for(unsigned n=0;n<=16;++n) { auto r=wradio::Roster(size.first,size.second,n,20);
            CHECK(r.x+r.width<size.first && r.y+r.height<size.second);
            CHECK(std::strlen(wradio::kRosterHint)*10*.8f*r.scale<r.width-24*r.scale);
        }
    }
    // DSP: deterministic, finite and bounded for overloaded synthetic signals;
    // no lookahead, impulse onset within a sample, passband exceeds both skirts.
    wvoice::RadioDsp da,db; double sums[3]={}; float hz[3]={120,1000,5500};
    for(int i=0;i<32000;++i) { float x=4*std::sin(i*.137f); auto a=wvoice::ProcessRadio(&da,x),b=wvoice::ProcessRadio(&db,x);
        CHECK(a.voice==b.voice && a.noise==b.noise); CHECK(std::isfinite(a.voice) && std::fabs(a.voice)<=.613f);
    }
    wvoice::RadioDsp silence; auto click=wvoice::ProcessRadio(&silence,0,1280); CHECK(std::fabs(click.squelch)>.001f); CHECK(wvoice::ProcessRadio(&silence,0,0).squelch==0);
    wvoice::RadioDsp impulse, reference;
    CHECK(std::fabs(wvoice::ProcessRadio(&impulse,1).voice-wvoice::ProcessRadio(&reference,0).voice)>.00001f);
    wvoice::RadioDsp quiet; CHECK(std::fabs(wvoice::ProcessRadio(&quiet,0).voice)>.001f);
    for(int k=0;k<3;++k) { wvoice::RadioDsp d; for(int i=0;i<16000;++i) { auto a=wvoice::ProcessRadio(&d,.1f*std::sin(6.2831853f*hz[k]*i/16000)); if(i>1000) sums[k]+=a.voice*a.voice; } }
    CHECK(sums[1]>sums[0]*16 && sums[1]>sums[2]*16);
    // Real codec and mixer at opposite distances. Per-player mute/gain takes effect
    // after the existing one-frame gain ramp, leaving the other speaker untouched.
    wvoice::Mixer mix; wvoice::Settings settings; mix.Configure(settings); wvoice::Adpcm enc; uint64_t sample=0;
    int16_t pcm[wvoice::kFrame]; float out[wvoice::kFrame*2];
    for(unsigned i=0;i<50;++i) { wvoice::Tone(hz,3,.5f,&sample,pcm); wvoice::Packet p; p.from=1;p.seq=i;p.channel=1;wvoice::Encode(pcm,&enc,p.payload);mix.Push(p);mix.Render({{1,5000,1,0,true}},out); }
    CHECK(mix.Stats()[0].rms<1e-6f);
    for(unsigned i=50;i<80;++i) { wvoice::Tone(hz,3,.5f,&sample,pcm); wvoice::Packet p; p.from=1;p.seq=i;p.channel=1;wvoice::Encode(pcm,&enc,p.payload);mix.Push(p);mix.Render({{1,5000,1,1,false}},out); }
    CHECK(mix.Stats()[0].radio>.8f && mix.Stats()[0].prox==0 && mix.Stats()[0].rms>.01f);
    // Actual sync encoder/decoder, stale updates, heartbeat/loss, schema validation.
    W world; std::vector<Bytes> wire; Hooks hooks; hooks.send=[&](const Bytes& d){wire.push_back(d);};
    SyncCore sender(1,1,&world,hooks), receiver(2,1,&world,{});
    Presence presence; presence.speaking=presence.radioEquipped=presence.radioTx=true; presence.voiceChannel=1;
    sender.setPresence(presence); sender.tick(1000); Bytes packet;
    for(const auto& d:wire) { receiver.onPacket(d.data(),d.size(),1000); if(d[2]=='S') packet=d; }
    auto remote=receiver.remotePoses(1000); CHECK(remote.size()==1 && remote[0].radioTx && remote[0].radioEquipped && remote[0].voiceChannel==1);
    for(auto badPair: {std::pair<int,int>{16,1},{4,1},{6,1},{14,2},{14,255}}) { auto bad=packet; bad[12]=2; bad[18]=badPair.first;bad[19]=badPair.second;checksum(bad); receiver.onPacket(bad.data(),bad.size(),1100); CHECK(receiver.remotePoses(1100)[0].radioTx); }
    auto newer=packet;newer[12]=2;newer[18]=8;newer[19]=0;checksum(newer);receiver.onPacket(newer.data(),newer.size(),1200);CHECK(!receiver.remotePoses(1200)[0].radioTx);
    receiver.onPacket(packet.data(),packet.size(),1300);CHECK(!receiver.remotePoses(1300)[0].radioTx);
    newer[12]=3;newer[18]=14;newer[19]=1;checksum(newer);receiver.onPacket(newer.data(),newer.size(),1400);CHECK(receiver.remotePoses(1400)[0].radioTx);
    CHECK(!receiver.remotePoses(10000)[0].radioTx);
    SyncCore late(2,1,&world,{}); wire.clear();sender.tick(11000);for(const auto& d:wire) late.onPacket(d.data(),d.size(),11000);CHECK(late.remotePoses(11000)[0].radioTx);
    // Seeded lossy-link simulator: delayed, duplicated and reordered real packets.
    // A final heartbeat must converge after arbitrary loss during channel changes.
    for(unsigned seed=0;seed<200;++seed) {
        std::mt19937 random(seed); std::vector<Bytes> outbound; Hooks send;
        send.send=[&](const Bytes& d){outbound.push_back(d);};
        SyncCore a(1,1,&world,send), b(2,1,&world,{});
        std::vector<std::pair<uint64_t,Bytes>> pending;
        for(uint64_t now=1000;now<5000;now+=20) {
            Presence p; p.radioEquipped=true; p.speaking=p.radioTx=(random()%2)==0; p.voiceChannel=p.radioTx?1:0;
            a.setPresence(p); a.tick(now);
            for(const auto& d:outbound) if(random()%10>=4) { pending.push_back({now+random()%300,d}); if(random()%5==0) pending.push_back({now+random()%500,d}); }
            outbound.clear(); std::shuffle(pending.begin(),pending.end(),random);
            for(auto it=pending.begin();it!=pending.end();) { if(it->first<=now) { b.onPacket(it->second.data(),it->second.size(),now);it=pending.erase(it); } else ++it; }
        }
        Presence final; final.radioEquipped=final.speaking=final.radioTx=true; final.voiceChannel=1;
        a.setPresence(final); a.tick(6000); for(const auto& d:outbound) b.onPacket(d.data(),d.size(),6000);
        for(const auto& delayed:pending) b.onPacket(delayed.second.data(),delayed.second.size(),6100);
        auto v=b.remotePoses(6100); CHECK(v.size()==1 && v[0].radioTx && v[0].voiceChannel==1);
    }
    // Actual adventurer wrist overlay, independent from left arm and gait.
    std::ifstream file("../../assets/models/adventurer.glb",std::ios::binary); std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
    wm::Model model; std::string error; CHECK(wm::LoadGlb(bytes.data(),bytes.size(),&model,&error));
    float bones[wm::kMaxJoints*16], posed[wm::kMaxJoints*16], hand0[16],hand1[16];
    wm::JointMatricesBlend(model,-1,0,-1,0,1,bones,0,hand0);wm::JointMatricesBlend(model,-1,0,-1,0,1,posed,1,hand1);
    std::printf("wrist idle %.3f %.3f %.3f raised %.3f %.3f %.3f\n",hand0[12],hand0[13],hand0[14],hand1[12],hand1[13],hand1[14]);
    CHECK(hand1[13]>1.48f && hand1[13]<1.70f && hand1[14]>.02f && hand1[14]<.25f);
    for(size_t j=0;j<model.joints.size();++j) if(model.nodes[model.joints[j]].name=="hand.L") for(int k=0;k<16;++k) CHECK(bones[j*16+k]==posed[j*16+k]);
    std::puts("radio: state, exhaustive validation, layout, DSP, mixer, actual sync and model pose passed");
}
