// Exercise the actual packet sender/receiver and the production surface geometry.
#define main line_sync_main
#include "line_sync.cpp"
#undef main
#include "../../frame/ribbon.h"
#include <cassert>
#include <algorithm>

static void checksum(Bytes& b) {
    uint32_t h = 2166136261u;
    for (size_t i=0;i<b.size()-4;++i) { h ^= b[i]; h *= 16777619u; }
    for(int k=0;k<4;++k) b[b.size()-4+k]=uint8_t(h>>(8*k));
}
static void put(Bytes& b, size_t at, unsigned v) { for(int k=0;k<4;++k)b[at+k]=uint8_t(v>>(8*k)); checksum(b); }
static std::vector<Bytes> packets(LineOut l) {
    LineWorld w; w.lines[5]=l; std::vector<Bytes> out; Hooks h;
    h.send=[&](const Bytes& b){if(Pair::isLine(b))out.push_back(b);};
    SyncCore c(1,1,&w,h); for(unsigned t=50;t<=500;t+=50){c.pollState(t);c.tick(t);}
    return out;
}
static void protocol() {
    auto l=makeLine(5,140,true); l.fine=true;
    auto ps=packets(l); Bytes legacy,fine;
    for(const auto& p:ps) { assert(p.size()==1152 && p.size()<=1200); (p[3]=='7'?fine:legacy)=p; }
    assert(!fine.empty() && !legacy.empty());
    // Golden legacy representation: 140 points, same nodes, units rounded to mm.
    for(unsigned i=0;i<140;++i)for(int k=0;k<3;++k) {
        auto at=30+8*i+2*k; int16_t v=uint16_t(legacy[at]|legacy[at+1]<<8);
        assert(v==std::lround(double(l.pts[i].d[k])/20));
    }
    Pair p; p.advance(500);
    auto feed=[&](Bytes b){p.cb->onPacket(b.data(),b.size(),p.now);};
    feed(legacy); assert(p.get(5) && !p.get(5)->fine);
    feed(fine); assert(p.get(5)->fine && same(p.get(5),l));
    feed(legacy); assert(p.get(5)->fine && same(p.get(5),l)); // no downgrade
    p.startB(2); p.advance(100); feed(fine); feed(legacy); assert(p.get(5)->fine);
    // A newer coarse line must replace an old fine one (large panel fallback).
    auto newer=legacy; put(newer,16,100); feed(newer); assert(!p.get(5)->fine);
    feed(fine); assert(!p.get(5)->fine);
    // Invalid packets must not consume a sequence, even with a valid checksum.
    for (int mode=0;mode<6;++mode) {
        auto bad=fine; put(bad,16,101);
        if(mode==0)put(bad,20,2); // unknown flag
        if(mode==1)put(bad,24,141);
        if(mode==2){bad[28]=0xff;bad[29]=0xff;checksum(bad);} // invalid node
        if(mode==3){bad[30]=0xff;bad[31]=0x7f;checksum(bad);} // outside bounded range
        if(mode==4){bad.pop_back();}
        if(mode==5){bad[40]^=1;}
        feed(bad); assert(!p.get(5)->fine && p.get(5)->seq==100);
    }
    auto valid=fine; put(valid,16,101); feed(valid); assert(p.get(5)->fine);
    p.a.lines[5]=l; p.advance(2000); assert(p.get(5)->fine);
    p.startB(3); p.advance(3000); assert(same(p.get(5),l) && p.get(5)->fine);
    p.a.lines.erase(5); p.advance(2000); assert(!p.get(5));
    Pair units; units.advance(500); units.a.lines[5]=l; units.advance(500);
    unsigned seq=units.get(5)->seq;
    units.a.lines[5].fine=false; units.advance(500);
    assert(!units.get(5)->fine && units.get(5)->seq>seq);
    seq=units.get(5)->seq; units.a.lines[5].fine=true; units.advance(500);
    assert(units.get(5)->fine && units.get(5)->seq>seq);
    // Both representations lost/reordered: use existing line tests in fine mode below.
}
static void curvedFallback() {
    for (bool large : {false,true}) {
        const float points[4][3]={{.012473f,-.31f,.17f},{.061237f,-.1f,.23f},{.071371f,.16f,.11f},{.009473f,large?2.1f:.31f,-.17f}};
        LineOut l; l.panel=5; l.fine=true;
        for (const auto& p : points) if(!LineFitsFine(p)) l.fine=false;
        assert(l.fine!=large);
        for (unsigned i=0;i<4;++i) { LinePoint p; p.node=i; for(int k=0;k<3;++k)p.d[k]=LineQuantize(points[i][k],l.fine); l.pts.push_back(p); }
        auto ps=packets(l); Pair pair; pair.advance(500);
        for (const auto& b : ps) pair.cb->onPacket(b.data(),b.size(),pair.now);
        auto r=pair.get(5); assert(r && same(r,l));
        for (unsigned i=0;i<4;++i) for(int k=0;k<3;++k)
            assert(std::fabs(r->pts[i].d[k]*LineUnit(r->fine)-points[i][k]) <= (large?.000501f:.000026f));
    }
    puts("PASS curved 3D coordinates and large-panel millimetre fallback round trip");
}
static void geometry() {
    double coarseMax=0, fineMax=0, centerMax=0;
    const float sender[3]={-80.123f,17.5f,4.2f}, receiver[3]={80.43f,-23.1f,9.7f};
    auto qs=wframe::Normalize({.2f,.3f,.4f,.5f});
    for(auto qr:{qs,wframe::Normalize({-.4f,.1f,.7f,.2f})})for(int i=0;i<1000;++i){
        // Native planar path: one corner, then an arbitrary partial edge tip.
        float local[3]={.012473f,-.27f+i*.000713f,.19327f};
        float native[3],back[3];wframe::ToWorld(sender,qs,local,native);wframe::ToLocal(sender,qs,native,back);
        assert(LineFitsFine(back));
        for(bool fine:{false,true}){
            float decoded[3],expected[3],world[3],end[3],v[4][3];
            for(int k=0;k<3;++k)decoded[k]=LineQuantize(back[k],fine)*LineUnit(fine);
            wframe::ToWorld(receiver,qr,back,expected);wframe::ToWorld(receiver,qr,decoded,world);
            float corner[3]={decoded[0],decoded[1],decoded[2]+.17f};wframe::ToWorld(receiver,qr,corner,end);
            assert(wframe::Ribbon(world,end,qr,.006f,v));
            double err=0,ce=0;
            for(int k=0;k<3;++k){err+=std::pow(world[k]-expected[k],2);ce+=std::pow((v[0][k]+v[1][k])*.5f-expected[k],2);}
            (fine?fineMax:coarseMax)=std::max(fine?fineMax:coarseMax,std::sqrt(err)*1000);
            if(fine)centerMax=std::max(centerMax,std::sqrt(ce)*1000);
        }
    }
    float large[3]={.01f,2,0};assert(!LineFitsFine(large));
    large[1]=32;assert(LineQuantize(large[1],false)==32000);
    large[1]=NAN;assert(!LineFitsFine(large));
    assert(fineMax<.1 && centerMax<.1 && coarseMax>.2);
    printf("PASS 2000 angled/moving-frame endpoints: coarse max %.6f mm, fine max %.6f mm, ribbon center max %.6f mm\n",coarseMax,fineMax,centerMax);
}
int main(){protocol();curvedFallback();geometry();fineLines=true;assert(line_sync_main()==0);puts("PASS fine/legacy units, 140 points/1152 bytes, upgrade/reorder, invalid packets, restart/reset");}
