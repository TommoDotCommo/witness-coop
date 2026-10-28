#include "../model.h"
#include "../gestures.h"
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
                for(float a:j.a) REQUIRE(std::isfinite(a) && fabsf(a)<=165);
                if(b==ElbowL || b==ElbowR) REQUIRE(j.a[0]>=0 && j.a[0]<=140);
                if(b==ShinL || b==ShinR) REQUIRE(j.a[0]<=0 && j.a[0]>=-140);
            }
            JointMatricesBlend(m,-1,0,idle,t,1,mats.data(),&st);
            JointMatrices(m,idle,t,base.data());
            for(const char* foot:{"foot.L","foot.R"}) {
                auto p=joint(m,mats,foot),q=joint(m,base,foot);
                REQUIRE(fabsf(p[1]-q[1])<.025f);
                if(id!=14 && id!=7) for(int axis=0;axis<3;++axis) REQUIRE(fabsf(p[axis]-q[axis])<.001f);
            }
            if(!prev.empty()) {
                for(const char* bone:{"hand.L","hand.R","head"}) {
                    auto a=joint(m,mats,bone),b=joint(m,prev,bone);
                    float d=0; for(int k=0;k<3;++k) d+=(a[k]-b[k])*(a[k]-b[k]); if(d>=.05f*.05f) std::fprintf(stderr,"joint jump id %d frame %d bone %s distance %.4f\n",id,frame,bone,sqrtf(d)); REQUIRE(d<.05f*.05f);
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
    st.Step(22,2,2,false,1/60.f); REQUIRE(st.emote==0);
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
    auto cheer=GestureSample(3,1); REQUIRE(cheer[ArmL].a[1]==-cheer[ArmR].a[1]); REQUIRE(cheer[ArmL].a[2]==-cheer[ArmR].a[2]);
    for(int a=1;a<=witness::kEmoteCount;++a) for(int b=a+1;b<=witness::kEmoteCount;++b) {
        auto pa=GestureSample(a,1.f),pb=GestureSample(b,1.f); float diff=0;
        for(int j=0;j<GestureBones;++j) for(int k=0;k<3;++k) diff+=fabsf(pa[j].a[k]-pb[j].a[k]);
        REQUIRE(diff>5.f);
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
