#include "../model.h"
#include "../gestures.h"
#include <algorithm>
#include "../../voice/gesture.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <cstdlib>
#define REQUIRE(x) do { if(!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); return 1; } } while(0)
static std::array<float,3> joint(const wm::Model& m,const std::vector<float>& mats,const char* name) {
    for(size_t j=0;j<m.joints.size();++j) if(m.nodes[m.joints[j]].name==name) {
        const float* ib=m.ibm.data()+j*16; float p[3];
        for(int r=0;r<3;++r) p[r]=-(ib[r*4]*ib[12]+ib[r*4+1]*ib[13]+ib[r*4+2]*ib[14]);
        const float* skin=mats.data()+j*16; std::array<float,3> out;
        for(int r=0;r<3;++r) out[r]=skin[r]*p[0]+skin[4+r]*p[1]+skin[8+r]*p[2]+skin[12+r];
        return out;
    }
    std::abort();
}
int main(int argc,char** argv) {
    using namespace wm;
    REQUIRE(argc>=2);
    std::ifstream f(argv[1],std::ios::binary); std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),{});
    Model m; std::string err; REQUIRE(LoadGlb(data.data(),data.size(),&m,&err));
    for(int b=0;b<GestureBones;++b) { bool found=false; for(auto& n:m.nodes) found|=n.name==kGestureBones[b]; REQUIRE(found); }
    for(int page=0;page<3;++page) for(int slot=0;slot<8;++slot) {
        float a=-1.5707963268f+slot*.7853981634f;
        REQUIRE(witness::EmoteSlot(100*cosf(a),100*sinf(a),page)==page*8+slot+1);
    }
    REQUIRE(witness::EmoteSlot(0,0,0)==0);
    REQUIRE(witness::EmoteSlot(std::numeric_limits<float>::quiet_NaN(),0,0)==0);
    for(int id=1;id<=witness::kEmoteCount;++id) {
        float duration=witness::kEmotes[id].duration; REQUIRE(duration>=2.6f && duration<=6);
        REQUIRE(witness::EmoteEnvelope(id,0)==0);
        REQUIRE(witness::kEmotes[id].loop || witness::EmoteEnvelope(id,duration)==0);
        if(witness::kEmotes[id].loop) REQUIRE(witness::EmoteEnvelope(id,duration+1)>.99f);
        GestureState st; std::vector<float> mats(kMaxJoints*16),base(kMaxJoints*16),prev; bool moved=false;
        int idle=FindClip(m,"idle");
        // 120 Hz sample: total duration, limits, finiteness and rendered continuity.
        for(int frame=0;frame<int((duration+1.5f)*120);++frame) {
            float t=frame/120.f; st.Step(id,t,0,false,1/120.f);
            for(int b=0;b<GestureBones;++b) {
                auto& j=st.pose[b]; REQUIRE(j.weight>=0 && j.weight<=1.00001f);
                for(float a:j.a) REQUIRE(std::isfinite(a) && fabsf(a)<=180);      // per-bone anatomical ranges: the quality gate
                if(b==ElbowL || b==ElbowR) REQUIRE(j.a[0]>=0 && j.a[0]<=140);
                if(b==ShinL || b==ShinR) REQUIRE(j.a[0]<=0 && j.a[0]>=-140);
            }
            JointMatricesBlend(m,-1,0,idle,t,1,mats.data(),&st);
            JointMatrices(m,idle,t,base.data());
            // Feet: planted soles, ground contact and balance are the quality gate's (tests/emote_quality.cpp).
            if(!prev.empty()) {
                for(const char* bone:{"hand.L","hand.R","head"}) {
                    auto a=joint(m,mats,bone),b=joint(m,prev,bone);
                    float d=0; for(int k=0;k<3;++k) d+=(a[k]-b[k])*(a[k]-b[k]); if(d>=.075f*.075f) std::fprintf(stderr,"joint jump id %d frame %d bone %s distance %.4f\n",id,frame,bone,sqrtf(d)); REQUIRE(d<.075f*.075f);      // 9 m/s, as the quality gate
                }
            }
            for(size_t j=0;j<m.joints.size()*16;++j) {
                REQUIRE(std::isfinite(mats[j]));
                if(!prev.empty()) {
                    if(j%16<12) REQUIRE(fabsf(mats[j]-prev[j])<.15f);
                    moved|=fabsf(mats[j]-prev[j])>.002f;
                }
            }
            prev=mats;
        }
        REQUIRE(moved);
        if(!witness::kEmotes[id].loop) for(auto& j:st.pose) REQUIRE(j.weight<.001f);
        std::printf("pose %d %s: duration, limits, continuity passed\n",id,witness::kEmotes[id].name);
    }
    // Actual idle/walk/run cross-fades with an upper-body gesture entering and
    // leaving each gait. The lower-body locomotion clips keep advancing.
    GaitCfg gaitCfg; gaitCfg.refSpeed=1.2f; gaitCfg.runRef=2.8f;
    Avatar avatar(gaitCfg); GestureState layer; bool walk=false,run=false;
    std::vector<float> gait(kMaxJoints*16),last; float pos[3]={};
    for(unsigned frame=0;frame<720;++frame) {
        float speed=frame<120?0:frame<300?1.6f:frame<480?4.5f:0;
        pos[0]+=speed/60.f; avatar.Update(1000+frame*1000/60,true,frame,pos,0);
        auto pick=avatar.Pick(m); walk|=pick.clip==FindClip(m,"walk"); run|=pick.clip==FindClip(m,"run");
        if(frame>=300 && frame<480 && run && avatar.speed()>4.2f) REQUIRE(pick.clip==FindClip(m,"run"));
        float local=float(frame%180)/60.f;
        layer.Step(local<2.6f?1:0,local,avatar.speed(),false,1/60.f);
        JointMatricesBlend(m,pick.from,pick.fromTime,pick.clip,pick.time,pick.blend,gait.data(),&layer);
        if(!last.empty()) for(const char* bone:{"hand.L","hand.R","head"}) {
            auto a=joint(m,gait,bone),b=joint(m,last,bone); float d=0;
            for(int k=0;k<3;++k) d+=(a[k]-b[k])*(a[k]-b[k]); if(d>=.12f*.12f) std::fprintf(stderr,"gait frame %u bone %s distance %.3f clip %d from %d w %.3f speed %.3f\n",frame,bone,sqrtf(d),pick.clip,pick.from,pick.blend,avatar.speed()); REQUIRE(d<.12f*.12f);
        }
        last=gait;
    }
    REQUIRE(walk && run);
    GestureState st;
    for(int n=0;n<120;++n) st.Step(22,n/60.f,0,false,1/60.f);
    float held=st.pose[Chest].weight; st.Step(22,2,2,false,1/60.f); REQUIRE(st.emote==0);
    REQUIRE(held>.9f && st.pose[Chest].weight>.5f*held);      // walking off fades the emote out, it does not snap back to the gait
    st.Step(22,2.1f,0,false,1/60.f); REQUIRE(st.emote==0); // never resumes after movement
    st.Step(22,0,0,false,1/60.f); REQUIRE(st.emote==22);
    for(int n=0;n<120;++n) st.Step(0,0,0,true,1/60.f);
    REQUIRE(st.talk>.99f); REQUIRE(st.pose[ElbowR].a[0]>110);
    std::vector<float> speech(kMaxJoints*16); JointMatricesBlend(m,-1,0,FindClip(m,"idle"),0,1,speech.data(),&st);
    auto wrist=joint(m,speech,"hand.R");
    std::printf("speech wrist %.3f %.3f %.3f\n",wrist[0],wrist[1],wrist[2]);
    REQUIRE(wrist[1]>1.4f && wrist[1]<1.75f && wrist[2]>.08f);

    for(int n=0;n<120;++n) st.Step(20,n/60.f,0,true,1/60.f);
    REQUIRE(st.talk<.001f); REQUIRE(st.emote==20);
    float prev=st.pose[ArmR].a[0]; st.Step(1,1,3,false,1/60.f);
    REQUIRE(st.emote==1); REQUIRE(fabsf(st.pose[ArmR].a[0]-prev)<30);
    for(int n=0;n<120;++n) st.Step(0,0,0,false,1/60.f);
    for(auto& j:st.pose) REQUIRE(j.weight<.001f);
    // ---- the clip format: left bones take anatomical numbers (twist and side mirrored for the rig), .LR sets both
    {
        EmoteLibrary lib; std::string e;
        REQUIRE(lib.Parse("emote 1 T\nduration 2\nkey 0 rest\nkey 1 arm.L=10,20,30 fore.LR=40,0,5\nkey 2 rest\n",&e));
        const auto& k=lib.clips[0].keys[1];
        REQUIRE(k.v[ArmL*3]==10 && k.v[ArmL*3+1]==-20 && k.v[ArmL*3+2]==-30);
        REQUIRE(k.v[ElbowR*3]==40 && k.v[ElbowR*3+2]==5 && k.v[ElbowL*3]==40 && k.v[ElbowL*3+2]==-5);
        REQUIRE(lib.clips[0].Drives(ArmL) && !lib.clips[0].Drives(ArmR) && !lib.clips[0].Drives(kHipsBit));
        // a key inherits the one before; rest and @pose start over
        REQUIRE(lib.Parse("emote 1 T\nduration 2\npose p head=5\nkey 0 rest\nkey 1 chest=3\nkey 1.5 head=2\nkey 2 @p\n",&e));
        REQUIRE(lib.clips[0].keys[2].v[Chest*3]==3 && lib.clips[0].keys[2].v[Head*3]==2 && lib.clips[0].keys[3].v[Chest*3]==0 && lib.clips[0].keys[3].v[Head*3]==5);
        // step.R's x is outward, so the right foot's model x is negated; plant defaults from 'feet'
        REQUIRE(lib.Parse("emote 1 T\nduration 2\nfeet free\nkey 0 rest\nkey 1 step.R=3,1,2\nkey 2 rest\n",&e));
        REQUIRE(lib.clips[0].keys[1].v[kStepChannel+3]==-3 && lib.clips[0].keys[1].v[kStepChannel+5]==2 && lib.clips[0].keys[1].v[kPlantChannel]==0);
        // every malformed clip is refused with its line, and a failed reload keeps the library
        for(const char* bad:{"key 0 rest\n","emote 1 T\nkey 0 rest\nkey 1 rest\n","emote 1 T\nduration 2\nkey 0 rest\nkey 0 rest\n",
                             "emote 1 T\nduration 2\nkey 0 rest\nkey 1 elbow.R=5\n","emote 1 T\nduration 2\nkey 0 rest\nkey 3 rest\n","emote 1 T\nduration 2\nloop 1 3\nkey 0 rest\nkey 2 rest\n",
                             "emote 1 T\nduration 2\nkey 0 rest\nkey 1 arm.R=1,2,3,4\n","emote 1 T\nduration 2\nkey 0 rest\nkey 1 plant=2\n","emote 1 T\nduration 2\nkey 0 @nope\nkey 1 rest\n"}) {
            EmoteLibrary l2; std::string why; REQUIRE(!l2.Parse(bad,&why)); REQUIRE(why.find(':')!=std::string::npos);
        }
        size_t before=EmoteClips().clips.size(); std::string why;
        REQUIRE(!ReloadEmoteClips("emote 1 Wave\nduration 2.6\nkey 0 rest\nkey 1 rest\n",&why));      // ids 2..24 missing
        REQUIRE(EmoteClips().clips.size()==before && EmoteClips().ById(24));
    }
    // ---- overlap: a hand samples its curve drag * 4.5 seconds after the shoulder does
    {
        EmoteLibrary lib; std::string e;
        REQUIRE(lib.Parse("emote 1 T\nduration 3\ndrag 0.04\nkey 0 rest\nkey 1 arm.R=60 hand.R=60\nkey 3 rest\n",&e));
        float v[kClipChannels], w[kClipChannels]; SampleClip(lib.clips[0],.7f,v); SampleClip(lib.clips[0],.7f-.04f*(4.5f-2),w);
        REQUIRE(fabsf(v[HandR*3]-w[ArmR*3])<1e-3f && v[HandR*3]<v[ArmR*3]-1);
    }
    // ---- follow-through: after a fast move the hand overshoots a little and settles (a spring, not a filter)
    {
        float x=0,v=0,peak=0; for(int n=0;n<240;++n) { GestureState::SpringTo(x,v,90,kSpringHz[ElbowR],kSpringZeta[ElbowR],1/60.f); peak=std::max(peak,x); }
        std::printf("elbow spring: a 90 deg step peaks at %.1f and settles at %.2f\n",peak,x);
        REQUIRE(peak>90*1.02f && peak<90*1.15f && fabsf(x-90)<.5f);      // carries on 2-15 % past the pose, then comes back
        float y=0,w=0; for(int n=0;n<6;++n) GestureState::SpringTo(y,w,90,kSpringHz[ElbowR],kSpringZeta[ElbowR],.1f);
        float z=0,u=0; for(int n=0;n<36;++n) GestureState::SpringTo(z,u,90,kSpringHz[ElbowR],kSpringZeta[ElbowR],1/60.f);
        REQUIRE(fabsf(y-z)<1.f);      // the same motion at 10 and 60 frames a second
    }
    wvoice::GestureActivity vad;
    REQUIRE(!vad.Feed(1000,true,0,.02f)); // no single-frame flash
    for(int n=0;n<10;++n) REQUIRE(!vad.Feed(n%2?0:1000,true,0,.02f));
    for(int n=0;n<1000;++n) vad.Feed(1000,true,0,.02f);
    REQUIRE(vad.Feed(500,true,0,.02f));
    for(int n=0;n<10;++n) REQUIRE(vad.Feed(0,true,0,.02f));
    for(int n=0;n<20;++n) vad.Feed(0,true,0,.02f);
    REQUIRE(!vad.Feed(0,true,0,.02f));
    for(unsigned channel=1;channel<=3;++channel) {
        for(int n=0;n<10;++n) vad.Feed(1000,true,0,.02f);
        REQUIRE(!vad.Feed(1000,true,channel,.02f));
    }
    REQUIRE(!vad.Feed(1000,false,0,.02f));
    std::puts("gesture: wheel, interruption, speech channels/hysteresis/conflicts passed");
}
