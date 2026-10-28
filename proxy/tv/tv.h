#pragma once
// Portable, bounded snapshot transport. All wire integers are little endian.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>
namespace wtv {
using Bytes = std::vector<uint8_t>;
constexpr unsigned W = 128, H = 96, Pixels = W * H, Chunk = 1024, MaxChunks = 12;
constexpr uint64_t Period = 200, Lease = 1500, Fresh = 800;
inline uint32_t get(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24; }
inline void put(Bytes& b, size_t at, uint32_t v) { for (unsigned i=0;i<4;++i) b[at+i]=uint8_t(v>>(i*8)); }
inline uint32_t hash(const uint8_t* p, size_t n) { uint32_t h=2166136261u; while(n--) { h^=*p++; h*=16777619u; } return h; }
inline void seal(Bytes& b) { put(b,b.size()-4,hash(b.data(),b.size()-4)); }
inline bool checked(const uint8_t* p,size_t n) { return n>=4 && get(p+n-4)==hash(p,n-4); }
struct Control { unsigned from=0,to=0,ticket=0,kind=0; }; // 0 cancel, 1 request, 2 denied/revoked
inline Bytes control(Control c) { Bytes b(24); memcpy(b.data(),"WTU1",4); put(b,4,c.from); put(b,8,c.to); put(b,12,c.ticket); put(b,16,c.kind); seal(b); return b; }
inline bool parseControl(const uint8_t* p,size_t n,Control& c) {
    if(n!=24 || memcmp(p,"WTU1",4) || !checked(p,n)) return false;
    c={get(p+4),get(p+8),get(p+12),get(p+16)}; return c.from && c.to && c.from!=c.to && c.ticket && c.kind<=2;
}
struct Sub { unsigned ticket; uint64_t at; };
class Sender {
public:
    bool consent=false;
    std::map<unsigned,Sub> subs;
    void expire(uint64_t now) {
        for(auto i=subs.begin();i!=subs.end();) if(now-i->second.at>Lease) i=subs.erase(i); else ++i;
    }
    void allow(bool v) { consent=v; if(!v) subs.clear(); }
    bool request(Control c,uint64_t now) {
        expire(now);
        if(c.kind==0) { subs.erase(c.from); return false; }
#ifndef TV_MUT_CONSENT
        if(!consent) return false;
#endif
#ifndef TV_MUT_SUB_CAP
        if(!subs.count(c.from) && subs.size()>=2) return false;
#endif
        if(c.kind!=1) return false;
        subs[c.from]={c.ticket,now}; return true;
    }
    bool due(uint64_t now) {
        expire(now);
        if(!consent || subs.empty() || (sent_ && now-last_<Period)) return false;
        last_=now; sent_=true; return true;
    }
private: uint64_t last_=0; bool sent_=false;
};
// RGB332, compressed with (run length,value) pairs only if smaller than raw.
inline Bytes compress(const Bytes& px,bool& rle) {
    Bytes b; for(size_t i=0;i<px.size();) { size_t j=i+1; while(j<px.size() && px[j]==px[i] && j-i<255) ++j; b.push_back(uint8_t(j-i)); b.push_back(px[i]); i=j; }
    rle=b.size()<px.size(); return rle?b:px;
}
inline std::vector<Bytes> frame(unsigned from,unsigned ticket,unsigned seq,const Bytes& px) {
    if(px.size()!=Pixels || !from || !ticket || !seq) return {};
    bool rle; Bytes data=compress(px,rle); unsigned count=unsigned((data.size()+Chunk-1)/Chunk); std::vector<Bytes> out;
    for(unsigned i=0;i<count;++i) {
        unsigned len=std::min(size_t(Chunk),data.size()-i*Chunk); Bytes b(28+len);
        memcpy(b.data(),"WTT1",4); put(b,4,from); put(b,8,ticket); put(b,12,seq);
        b[16]=uint8_t(i); b[17]=uint8_t(count); b[18]=uint8_t(rle); b[19]=0; put(b,20,len);
        memcpy(b.data()+24,data.data()+i*Chunk,len); seal(b); out.push_back(std::move(b));
    } return out;
}
class Receiver {
public:
    unsigned target=0,ticket=1,decoded=0;
    unsigned sequence() const { return done_; } Bytes pixels; uint64_t at=0;
    void select(unsigned id) { if(id!=target) { target=id; reset(); if(!++ticket) ++ticket; } }
    void deny(Control c) { if(c.kind==2 && c.from==target && c.ticket==ticket) { reset(); if(!++ticket) ++ticket; } }
    bool fresh(uint64_t now) const {
#ifdef TV_MUT_STALE
        (void)now; return !pixels.empty();
#else
        return !pixels.empty() && now-at<=Fresh;
#endif
    }
    bool packet(const uint8_t* p,size_t n,uint64_t now) {
        // Limit ingress before checksum and allocations: at most two frames' worth per 200ms window.
        if(now-window_>=Period) { window_=now; budget_=0; }
#ifndef TV_MUT_RATE
        if(++budget_>2*MaxChunks) return false;
#endif
        if(n<29 || n>Chunk+28 || memcmp(p,"WTT1",4) || !checked(p,n)) return false;
#ifndef TV_MUT_TARGET
        if(!target || get(p+4)!=target || get(p+8)!=ticket) return false;
#endif
        unsigned seq=get(p+12),idx=p[16],count=p[17],mode=p[18],len=get(p+20);
        if(!seq || p[19] || mode>1 || !count || count>MaxChunks || idx>=count || !len || len>Chunk || n!=28+len || (idx+1<count && len!=Chunk)) return false;
        if(done_ && int32_t(seq-done_)<=0) return false;
        if(cur_ && now-start_>Fresh) { cur_=0; have_=0; }
        if(seq!=cur_) {
            if(cur_ && int32_t(seq-cur_)<=0) return false;
#ifndef TV_MUT_FRAME_RATE
            if(completed_ && now-lastComplete_<Period) return false;
#endif
            cur_=seq; count_=count; mode_=mode; have_=0; start_=now;
        }
        if(count!=count_ || mode!=mode_ || (have_ & (1u<<idx))) return false;
        chunks_[idx].assign(p+24,p+24+len); have_|=1u<<idx;
        if(have_!=((1u<<count)-1)) return false;
        Bytes data; for(unsigned i=0;i<count;++i) data.insert(data.end(),chunks_[i].begin(),chunks_[i].end());
        Bytes out;
        if(!mode) out=std::move(data);
        else { if(data.size()%2) return false; for(size_t i=0;i<data.size();i+=2) {
            if(!data[i] || out.size()+data[i]>Pixels) return false;
            out.insert(out.end(),data[i],data[i+1]);
        } }
        if(out.size()!=Pixels) return false;
        pixels.swap(out); at=now; done_=seq; completed_=true; lastComplete_=now; ++decoded; return true;
    }
    void reset() { pixels.clear(); at=0; cur_=done_=have_=0; completed_=false; for(auto& c:chunks_) c.clear(); }
private:
    std::array<Bytes,MaxChunks> chunks_; unsigned cur_=0,done_=0,have_=0,count_=0,mode_=0,budget_=0;
    uint64_t start_=0,window_=0,lastComplete_=0; bool completed_=false;
};
struct Motion {
    float sway=0,velocity=0,antenna=0,power=0;
    void step(float dt,float speed,bool on) {
        if(!std::isfinite(dt) || dt<=0) return;
        dt=std::min(dt,0.1f); if(!std::isfinite(speed)) speed=0; speed=std::clamp(speed,0.f,8.f);
        // Fixed substeps keep the damped spring stable even after stalls/teleports.
        for(float t=0;t<dt;) { float h=std::min(1.f/240,dt-t); t+=h;
            phase_+=h*(3+speed*2); float goal=0.045f*std::min(speed,3.f)*sinf(phase_);
            velocity+=(90*(goal-sway)-18*velocity)*h; sway+=velocity*h;
#ifndef TV_MUT_SPRING
            sway=std::clamp(sway,-0.16f,0.16f); velocity=std::clamp(velocity,-1.f,1.f);
#endif
            antenna=0.08f*sinf(phase_*2.7f)*std::min(speed,3.f)+sway;
            power=std::clamp(power+(on?1.f:-1.f)*h*3,0.f,1.f);
        }
    }
private: float phase_=0;
};
} // namespace wtv
