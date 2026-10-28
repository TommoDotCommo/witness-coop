#include "tv/tv.h"
#include "tv/mesh.h"
#include <cstdio>
#include <limits>
#include <cstdlib>
#include <new>
static size_t largestAllocation;
void* operator new(size_t n) { largestAllocation=std::max(largestAllocation,n); if(void* p=std::malloc(n)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,size_t) noexcept { std::free(p); }
#include <random>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while(0)
using namespace wtv;
bool deliver(Receiver& r,const std::vector<Bytes>& f,uint64_t now){ bool complete=false; for(auto& p:f) complete=r.packet(p.data(),p.size(),now)||complete; return complete; }
int main(){
    Sender s; Control c{2,1,7,1}; CHECK(!s.consent); CHECK(!s.request(c,100)); CHECK(s.subs.empty());
    s.allow(true); CHECK(s.request(c,100)); CHECK(s.due(100)); CHECK(!s.due(101)); CHECK(s.due(300));
    CHECK(s.request({3,1,8,1},100)); CHECK(!s.request({4,1,9,1},100)); CHECK(s.subs.size()==2);
    s.expire(1601); CHECK(s.subs.empty()); CHECK(!s.due(1700));
    CHECK(s.request(c,1800)); s.allow(false); CHECK(s.subs.empty()); CHECK(!s.due(2000));
    auto ctl=control(c); Control parsed; CHECK(parseControl(ctl.data(),ctl.size(),parsed)); CHECK(parsed.ticket==7);
    ctl[16]=3; seal(ctl); CHECK(!parseControl(ctl.data(),ctl.size(),parsed));
    Receiver r; r.select(1); Bytes solid(Pixels,0xab), random(Pixels);
    std::mt19937 rng(723); for(auto& p:random) p=uint8_t(rng());
    auto pack=[&](unsigned seq,const Bytes& px){ return frame(1,r.ticket,seq,px); };
    auto f=pack(1,solid); CHECK(f.size()==1); CHECK(f[0][18]==1);
    CHECK(deliver(r,f,200)); CHECK(r.pixels==solid); CHECK(r.fresh(1000)); CHECK(!r.fresh(1001));
    CHECK(!deliver(r,f,1100)); // replay
    auto g=pack(2,random); CHECK(g.size()==MaxChunks); CHECK(g[0][18]==0);
    CHECK(!deliver(r,g,201)); // completed frame rate
    std::reverse(g.begin(),g.end()); CHECK(deliver(r,g,1200)); CHECK(r.pixels==random);
    CHECK(!deliver(r,g,1400));
    auto reject=[](const std::vector<Bytes>& fs,uint64_t now){ Receiver isolated; isolated.select(1); return !deliver(isolated,fs,now); };
    auto wrong=frame(2,r.ticket,3,solid); CHECK(reject(wrong,1600));
    wrong=frame(1,r.ticket+1,3,solid); CHECK(reject(wrong,1800));
    wrong=pack(3,solid); wrong[0][25]^=1; CHECK(reject(wrong,2000)); // checksum
    wrong=pack(3,solid); wrong[0][19]=1; seal(wrong[0]); CHECK(reject(wrong,2200));
    wrong=pack(3,solid); wrong[0][18]=2; seal(wrong[0]); CHECK(reject(wrong,2400));
    wrong=pack(3,solid); wrong[0][16]=12; seal(wrong[0]); CHECK(reject(wrong,2600));
    wrong=pack(3,solid); put(wrong[0],20,999); seal(wrong[0]); CHECK(reject(wrong,2800));
    wrong=pack(3,solid); wrong[0][24]=0; seal(wrong[0]); CHECK(reject(wrong,3000)); // zero RLE run
    wrong=pack(3,solid); wrong[0][24]=254; seal(wrong[0]); CHECK(reject(wrong,3200)); // short decode
    wrong=pack(3,solid); wrong[0][wrong[0].size()-6]=255; seal(wrong[0]); CHECK(reject(wrong,3400)); // overrun
    auto bomb=pack(3,solid); bomb[0].resize(28+1024); put(bomb[0],20,1024);
    for(unsigned i=24;i<24+1024;i+=2){ bomb[0][i]=255; bomb[0][i+1]=7; } seal(bomb[0]);
    largestAllocation=0; CHECK(reject(bomb,3600)); CHECK(largestAllocation<=2*Pixels);
    // Incomplete frames do not survive a long loss gap, mix modes or chunks, or beat the ingress budget.
    Receiver loss; loss.select(1); auto lossFrames=frame(1,loss.ticket,1,random);
    CHECK(!loss.packet(lossFrames[0].data(),lossFrames[0].size(),200));
    std::vector<Bytes> tail(lossFrames.begin()+1,lossFrames.end()); CHECK(!deliver(loss,tail,1001));
    auto recovery=frame(1,loss.ticket,2,solid); CHECK(deliver(loss,recovery,1400));
    Receiver limited; limited.select(1); auto burst=frame(1,limited.ticket,1,solid);
    uint8_t junk=0; for(unsigned i=0;i<2*MaxChunks;++i) limited.packet(&junk,1,200);
    CHECK(!deliver(limited,burst,200)); CHECK(deliver(limited,burst,400));
    auto before=limited.ticket; limited.deny({1,2,before,2}); CHECK(limited.pixels.empty()); CHECK(limited.ticket!=before);
    CHECK(!deliver(limited,burst,600));
    limited.select(0); CHECK(!deliver(limited,frame(1,limited.ticket,2,solid),800));
    // Wrap-aware ordering and byte-exact round trips under reordering, duplicates and loss.
    Receiver wrap; wrap.select(1); CHECK(deliver(wrap,frame(1,wrap.ticket,0xffffffffu,solid),200));
    CHECK(deliver(wrap,frame(1,wrap.ticket,1,random),400));
    for(unsigned cycle=0;cycle<500;++cycle){ Receiver a; a.select(1); Bytes px(Pixels); for(auto& p:px) p=uint8_t(rng());
        auto fs=frame(1,a.ticket,1,px); std::shuffle(fs.begin(),fs.end(),rng); CHECK(deliver(a,fs,200)); CHECK(a.pixels==px);
        CHECK(!deliver(a,fs,400)); CHECK(a.pixels.size()==Pixels);
        for(auto& b:fs){ b[rng()%b.size()]^=uint8_t(1u<<(rng()%8)); a.packet(b.data(),b.size(),600); } CHECK(a.pixels==px);
    }
    // Random malformed packets, including valid-checksum mutations, stay bounded.
    Receiver fuzz; fuzz.select(1);
    for(unsigned i=0;i<100000;++i){ Bytes b(rng()%1501); for(auto& p:b) p=uint8_t(rng());
        if(i%2==0){ auto valid=frame(1,fuzz.ticket,i+1,solid); b=valid[0]; b[rng()%b.size()]=uint8_t(rng()); if(i%4==0) seal(b); }
        fuzz.packet(b.data(),b.size(),uint64_t(i)*200); CHECK(fuzz.pixels.size()<=Pixels);
    }
    Motion m; m.sway=10; m.velocity=100; m.step(.1f,8,true); CHECK(fabsf(m.sway)<=.16f && fabsf(m.velocity)<=1);
    for(unsigned i=0;i<100000;++i){ float dt=i%89==0?10.f:1.f/(10+rng()%240); m.step(dt,float(rng()%100),i%100<50);
        CHECK(std::isfinite(m.sway) && fabsf(m.sway)<=.16f); CHECK(fabsf(m.antenna)<=.41f); CHECK(m.power>=0 && m.power<=1);
    }
    for(int i=0;i<1000;++i) { m.step(.01f,0,false); }
    CHECK(fabsf(m.sway)<.0001f && m.power==0);
    m.step(std::numeric_limits<float>::quiet_NaN(),1,true); CHECK(std::isfinite(m.sway));
    // Layout is outside the authored backpack, and the screen has full UV coverage,
    // opaque emissive input, proper chest weights, finite geometry and a hard draw cap.
    m={}; m.power=1; auto model=mesh(3,m); CHECK(model.body.size()+model.screen.size()<=1024); CHECK(model.screen.size()==6);
    float minx=1,maxx=-1,miny=2,maxy=0;
    for(auto v:model.screen){ CHECK(v.pos[2]<-.47f); CHECK(v.col[3]==255 && v.appearanceGroup==0); CHECK(v.joints[0]==3 && v.weights[0]==1);
        minx=std::min(minx,v.uv[0]); maxx=std::max(maxx,v.uv[0]); miny=std::min(miny,v.uv[1]); maxy=std::max(maxy,v.uv[1]); }
    CHECK(minx==0 && maxx==1 && miny==0 && maxy==1);
    for(auto v:model.body) for(float p:v.pos) CHECK(std::isfinite(p));
    auto off=mesh(3,Motion{}); float open=fabsf(model.screen[2].pos[1]-model.screen[0].pos[1]),closed=fabsf(off.screen[2].pos[1]-off.screen[0].pos[1]); CHECK(closed<open*.02f);
    puts("TV: consent, wire, caps, expiry, loss/reorder/replay, 500 sessions, 100000 malformed packets, spring and mesh passed");
}
