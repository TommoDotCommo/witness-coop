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
constexpr unsigned MaxPlayers = 16, MaxSubscriptions = 2 * (MaxPlayers - 1);
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
struct Sub { unsigned viewer,ticket; uint64_t at; };
inline uint64_t subKey(unsigned viewer,unsigned ticket) { return (uint64_t(viewer)<<32)|ticket; }
class Sender {
public:
    bool consent=false;
    std::map<uint64_t,Sub> subs;
    void expire(uint64_t now) {
        for(auto i=subs.begin();i!=subs.end();) if(now-i->second.at>Lease) i=subs.erase(i); else ++i;
    }
    void allow(bool v) { consent=v; if(!v) subs.clear(); }
    bool request(Control c,uint64_t now) {
        expire(now); uint64_t key=subKey(c.from,c.ticket);
        if(c.kind==0) { subs.erase(key); return false; }
#ifndef TV_MUT_CONSENT
        if(!consent) return false;
#endif
#ifndef TV_MUT_SUB_CAP
        if(!subs.count(key) && subs.size()>=MaxSubscriptions) return false;
#endif
        if(c.kind!=1) return false;
        subs[key]={c.from,c.ticket,now}; return true;
    }
    bool due(uint64_t now,bool localAudience=false) {
        expire(now);
        if(!consent || (subs.empty() && !localAudience) || (sent_ && now-last_<Period)) return false;
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
    void inject(unsigned seq,Bytes px,uint64_t now) { if(px.size()==Pixels) { pixels=std::move(px); at=now; done_=seq; completed_=true; lastComplete_=now; ++decoded; } }
private:
    std::array<Bytes,MaxChunks> chunks_; unsigned cur_=0,done_=0,have_=0,count_=0,mode_=0,budget_=0;
    uint64_t start_=0,window_=0,lastComplete_=0; bool completed_=false;
};
struct Deployment {
    unsigned from=0,target=0,sequence=0; float eye[3]={},yaw=0; bool active=false; uint64_t at=0;
};
inline bool validDeployment(const Deployment& d) {
    if(!d.from || !d.sequence) return false;
    if(!d.active) return d.target==0;
    if(!d.target || d.target==d.from || !std::isfinite(d.yaw)) return false;
    for(float v:d.eye) if(!std::isfinite(v) || fabsf(v)>100000) return false;
    return true;
}
inline Bytes deploymentPacket(const Deployment& d) {
    Bytes b(40); memcpy(b.data(),"WTD1",4); put(b,4,d.from); put(b,8,d.target); put(b,12,d.sequence);
    for(unsigned i=0;i<3;++i) { uint32_t v; memcpy(&v,&d.eye[i],4); put(b,16+4*i,v); }
    uint32_t yaw; memcpy(&yaw,&d.yaw,4); put(b,28,yaw); put(b,32,d.active); seal(b); return b;
}
inline bool parseDeployment(const uint8_t* p,size_t n,Deployment& d) {
    if(n!=40 || memcmp(p,"WTD1",4) || !checked(p,n)) return false;
    d={}; d.from=get(p+4); d.target=get(p+8); d.sequence=get(p+12);
    for(unsigned i=0;i<3;++i) { uint32_t v=get(p+16+4*i); memcpy(&d.eye[i],&v,4); }
    uint32_t yaw=get(p+28); memcpy(&d.yaw,&yaw,4); d.active=get(p+32)!=0;
    return get(p+32)<=1 && validDeployment(d);
}
class Deployments {
public:
    unsigned me=0;
    std::map<unsigned,Deployment> screens;
    bool deploy(unsigned target,const float eye[3],float yaw,uint64_t now) {
        Deployment d; d.from=me; d.target=target; d.sequence=local_.sequence+1; if(!d.sequence) ++d.sequence;
        for(unsigned i=0;i<3;++i) d.eye[i]=eye[i];
        d.yaw=yaw; d.active=true; d.at=now;
        if(!me || !validDeployment(d)) return false;
        local_=d; screens[me]=d; missing_[me]=0; return true;
    }
    bool retract(uint64_t now) {
        if(!local_.active) return false;
        local_.active=false; local_.target=0; ++local_.sequence; if(!local_.sequence) ++local_.sequence; local_.at=now; screens[me]=local_; return true;
    }
    bool packet(const uint8_t* p,size_t n,uint64_t now) {
        Deployment d; if(!parseDeployment(p,n,d)) return false;
#ifndef TV_MUT_AUTHORITY
        if(d.from==me) return false;
#endif
        auto it=screens.find(d.from);
#ifndef TV_MUT_SEQUENCE
        if(it!=screens.end()) {
            int32_t order=int32_t(d.sequence-it->second.sequence);
            if(order<0) return false;
            if(order==0) {
                // A heartbeat is byte-identical state, including inactive tombstones.
                // Comparing the wire form avoids padding and float equality ambiguities.
                if(deploymentPacket(d)!=deploymentPacket(it->second)) return false;
                it->second.at=now; return true;
            }
        }
#endif
#ifndef TV_MUT_DEPLOY_CAP
        if(it==screens.end() && screens.size()>=MaxPlayers) return false;
#endif
        d.at=now; screens[d.from]=d; missing_[d.from]=0; return true;
    }
    void tick(uint64_t now,const std::vector<unsigned>& alive) {
        for(auto it=screens.begin();it!=screens.end();) {
            Deployment d=it->second; unsigned owner=it->first;
            if(owner!=me && now-d.at>2*Lease) { screens.erase(it++); missing_.erase(owner); continue; }
            bool gone=d.active && d.target!=me && std::find(alive.begin(),alive.end(),d.target)==alive.end();
            uint64_t& since=missing_[owner]; if(gone) { if(!since) since=now; } else since=0;
#ifndef TV_MUT_DISCONNECT
            if(gone && now-since>2*Lease) {
                if(owner==me) { retract(now); ++it; } else { screens.erase(it++); missing_.erase(owner); }
                continue;
            }
#endif
            ++it;
        }
    }
    const Deployment& local() const { return local_; }
private:
    Deployment local_; std::map<unsigned,uint64_t> missing_;
};
constexpr float ScreenWidth=4.0f,ScreenHeight=2.5f,ScreenBottom=2.0f,ScreenBehind=3.0f,CameraEye=1.71f;
struct Placement { float center[3],right[3],forward[3],ground,bottom; };
inline Placement placement(const Deployment& d) {
    Placement p={}; p.forward[0]=cosf(d.yaw); p.forward[1]=sinf(d.yaw); p.right[0]=p.forward[1]; p.right[1]=-p.forward[0];
    p.ground=d.eye[2]-CameraEye;
#ifdef TV_MUT_PLACEMENT
    p.bottom=p.ground;
#else
    p.bottom=p.ground+ScreenBottom;
#endif
    for(unsigned i=0;i<2;++i) p.center[i]=d.eye[i]-p.forward[i]*ScreenBehind;
    p.center[2]=p.bottom+ScreenHeight*.5f; return p;
}
// Three low stools on the viewing side. The Sit gesture bends the legs;
// its eye drops 0.40 m. The existing gesture already lowers the pelvis, so
// reconstruct the standing ground from the seated eye rather than dropping twice.
constexpr unsigned SeatCount=3;
constexpr float SeatHeight=.50f,SeatEye=CameraEye-.40f;
template<class Controller> inline void seatedGesture(Controller& c,float t,bool speaking,float dt) {
    // A prior approach/teleport may have canceled this non-mobile emote.
    // An actively held stool is authoritative until its explicit exit.
    c.cancelled=0; c.Step(14,std::max(t,1.f),0,speaking,dt);
}
inline float seatModelGround(float eyeZ) { return eyeZ-SeatEye; }
inline std::array<float,3> seatPoint(const Deployment& d,unsigned index) {
    auto p=placement(d); float side=(float(index)-1.f)*1.35f;
    return {p.center[0]+p.forward[0]*4+p.right[0]*side,
            p.center[1]+p.forward[1]*4+p.right[1]*side,p.ground};
}
inline int aimSeat(const Deployment& d,const float eye[3],float yaw,float pitch) {
    if(!d.active) return -1;
    float dir[3]={cosf(pitch)*cosf(yaw),cosf(pitch)*sinf(yaw),sinf(pitch)};
    for(unsigned i=0;i<SeatCount;++i) {
        auto p=seatPoint(d,i); p[2]+=SeatHeight;
        float v[3]={p[0]-eye[0],p[1]-eye[1],p[2]-eye[2]},along=0,dist=0;
        for(unsigned j=0;j<3;++j) { along+=v[j]*dir[j]; dist+=v[j]*v[j]; }
        if(along>0 && dist<2.5f*2.5f && dist-along*along<.32f*.32f && fabsf(eye[2]-p[2]-(CameraEye-SeatHeight))<.5f) return int(i);
    }
    return -1;
}
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
