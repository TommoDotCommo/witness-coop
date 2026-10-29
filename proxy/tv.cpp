#include "tv.h"
#include "tv/tv.h"
#include "tv/resources.h"
#include "coop.h"
#include "config.h"
#include <cstdio>
#include <set>
#include <atomic>
#include "game.h"
#include "social.h"
void Log(const char* msg);
namespace {
std::atomic<bool> seatRequest{false},seatExit{false},seated{false},seatAvailable{false};
struct SeatState { unsigned owner=0,sequence=0,index=0; float entry[3]={},yaw=0,pitch=0; } seat;
SRWLOCK lock=SRWLOCK_INIT;
struct Guard { Guard(){ AcquireSRWLockExclusive(&lock); } ~Guard(){ ReleaseSRWLockExclusive(&lock); } };
struct AudienceStream : wtv::StreamResources<ID3D11Texture2D,ID3D11ShaderResourceView> {
    wtv::Receiver receiver; uint64_t requestAt=0,noiseAt=0;
    unsigned uploaded=0,uploadedSequence=0,uploadHash=0; bool wasLive=false;
};
struct AudienceStats { unsigned draws=0,vertices=0,visiblePixels=0; bool drawnLive=false; };
struct State {
    bool enabled=GetCfg().povTv, capture=false; wtv::Sender sender; wtv::Receiver receiver;
    uint64_t requestAt=0; unsigned displayTarget=0; uint64_t disabledAt=0; unsigned sequence=0,sent=0,uploaded=0,draws=0,vertices=0; bool drawnLive=false;
    float sway=0,power=0; unsigned maxCaptureUs=0,visiblePixels=0,uploadHash=0,uploadedSequence=0;
    std::vector<std::pair<unsigned,unsigned>> hashes;
    wtv::Deployments deployments; std::map<unsigned,AudienceStream> audience; std::map<unsigned,AudienceStats> audienceStats;
    unsigned nextTicket=1; uint64_t deploymentBroadcastAt=0;
    bool restore=false,wantProbe=false;
    wtv::GroundProber prober; unsigned probeTarget=0; float probeEye[3]={},probeYaw=0,probeHome[5]={}; unsigned lastMeasured=0; bool probing=false;
    State(){ sender.allow(GetCfg().povTvConsent); receiver.ticket=GetTickCount()|1u; nextTicket=receiver.ticket+1; }
};
State& state(){ static State s; return s; }
void send(wtv::Control c){ auto b=wtv::control(c); CoopSendRawTo(c.to,b.data(),b.size()); }
void sendDeployment(const wtv::Deployment& d){ if(!d.sequence) return; auto b=wtv::deploymentPacket(d); CoopSendRaw(b.data(),b.size()); }
ID3D11Texture2D* texture; ID3D11ShaderResourceView* srv;
}
bool TvEnabled(){ Guard g; return state().enabled; }
bool TvConsent(){ Guard g; return state().sender.consent; }
void TvEnable(bool on){ Guard g; auto& s=state(); s.enabled=on; s.disabledAt=GetTickCount64(); if(!on) { if(s.receiver.target) send({CoopMyId(),s.receiver.target,s.receiver.ticket,0}); s.receiver.select(0); } }
void TvAllow(bool on){ Guard g; auto& s=state(); if(!on) for(auto p:s.sender.subs) send({CoopMyId(),p.second.viewer,p.second.ticket,2}); s.sender.allow(on); }
unsigned TvTarget(){ Guard g; auto& s=state(); return s.enabled?s.receiver.target:(GetTickCount64()-s.disabledAt<350?s.displayTarget:0); }
bool TvDeployAction(unsigned target){
    Guard g; auto& s=state(); uint64_t now=GetTickCount64(); s.deployments.me=CoopMyId();
    if(s.probing){ s.prober.cancel(); s.probing=false; s.probeTarget=0; s.restore=true; return true; }      // a second press while measuring cancels
    if(s.deployments.local().active){ bool ok=s.deployments.retract(now); if(ok) sendDeployment(s.deployments.local()); return ok; }
    if(!target || target==CoopMyId() || seated) return false;
    for(const auto& r:CoopGetRemotes()) if(r.id==target && r.valid && r.ageMs<1000){
        s.probeTarget=target; s.wantProbe=true; return true;     // TvTick anchors it at this player, measures the ground, then deploys
    }
    return false;
}
bool TvProbing(){ Guard g; return state().probing; }
bool TvProbeHome(float pos[3],float ang[2]){
    Guard g; auto& s=state();
    if(!s.probing || GetCfg().tvProbeShow) return false;
    for(unsigned i=0;i<3;++i) pos[i]=s.probeHome[i]; ang[0]=s.probeHome[3]; ang[1]=s.probeHome[4]; return true;
}
unsigned TvDeploymentTarget(){ Guard g; return state().deployments.local().active?state().deployments.local().target:0; }
std::vector<wtv::Deployment> TvAudienceScreens(){
    Guard g; std::vector<wtv::Deployment> out; for(const auto& screen:state().deployments.screens) if(screen.second.active) out.push_back(screen.second); return out;
}
bool TvSeatedPose(const float eye[3]) {
    Guard g;
    for(const auto& entry:state().deployments.screens) if(entry.second.active) for(unsigned i=0;i<wtv::SeatCount;++i) {
        auto p=wtv::seatPoint(entry.second,i); p[2]+=wtv::SeatEye;
        float d=0; for(unsigned j=0;j<3;++j) d+=(eye[j]-p[j])*(eye[j]-p[j]);
        if(d<.20f*.20f) return true;
    }
    return false;
}
void TvSeatRequest(){ seatRequest=true; }
void TvSeatExit(){ seatExit=true; }
bool TvSeated(){ return seated; }
bool TvSeatAvailable(){ return seatAvailable; }
void TvTick(const float camera[3],bool captureAllowed){
    auto remotes=CoopGetRemotes(); std::vector<unsigned> alive; unsigned nearest=0; float distance=12*12;
    for(const auto& r:remotes) if(r.valid && r.ageMs<2000){
        alive.push_back(r.id); float d=0; for(int i=0;i<3;++i) d+=(r.pos[i]-camera[i])*(r.pos[i]-camera[i]);
        if(r.ageMs<1000 && (d<distance || (d==distance && r.id<nearest))){ distance=d; nearest=r.id; }
    }
    Guard g; auto& s=state(); uint64_t now=GetTickCount64(); s.capture=captureAllowed; s.deployments.me=CoopMyId();
    if(!s.enabled || !captureAllowed) nearest=0;
    if(nearest!=s.receiver.target){
        if(s.receiver.target) send({CoopMyId(),s.receiver.target,s.receiver.ticket,0});
        s.receiver.select(nearest); s.requestAt=0; if(s.enabled) s.displayTarget=nearest;
    }
    if(nearest && (!s.requestAt || now-s.requestAt>=500)){ send({CoopMyId(),nearest,s.receiver.ticket,1}); s.requestAt=now; }
    s.deployments.tick(now,alive);
    wtv::pruneStats(s.audienceStats,s.deployments.screens);
    const float* angles=(const float*)((uintptr_t)GetModuleHandleA(nullptr)+0x6303EC);
    bool anglesOk=!IsBadReadPtr(angles,8), exit=seatExit.exchange(false), action=seatRequest.exchange(false);
    // Deploy: first measure the ground under the stools and the screen (the player drops onto each point and is put back), then announce.
    if(s.wantProbe && anglesOk) wtv::anchorAtDeployer(camera,angles[0],s.probeEye,&s.probeYaw);
    if(s.wantProbe){
        s.wantProbe=false; wtv::Deployment base; base.from=CoopMyId(); base.target=s.probeTarget; base.sequence=1; base.active=true; base.yaw=s.probeYaw;
        for(unsigned i=0;i<3;++i) base.eye[i]=s.probeEye[i];
        if(GetCfg().noTerrainProbe || !anglesOk) { if(s.deployments.deploy(s.probeTarget,s.probeEye,s.probeYaw,now)) sendDeployment(s.deployments.local()); s.probeTarget=0; }
        else {
            for(unsigned i=0;i<3;++i) s.probeHome[i]=camera[i];
            s.probeHome[3]=angles[0]; s.probeHome[4]=angles[1]; GameHoverOff(); s.prober.begin(base,camera,now); s.probing=true;
        }
    }
    if(s.probing){
        action=false; exit=false;
        auto c=s.prober.update(camera,now);
        if(c.teleport) GameTeleport(c.to[0],c.to[1],c.to[2],s.probeHome[3],s.probeHome[4]);
        if(s.prober.done()){
            s.probing=false; s.lastMeasured=s.prober.result().measured;
            for(const auto& h:s.prober.history){ char m[160]; snprintf(m,sizeof m,"tv: ground probe pass %u point %u outcome %u camera %.2f",h.pass,h.point,h.outcome,h.camera); Log(m); }
            { char m[160]; snprintf(m,sizeof m,"tv: ground measured %#x relief %d %d %d %d %d %d cm",s.lastMeasured,s.prober.result().relief[0],s.prober.result().relief[1],s.prober.result().relief[2],s.prober.result().relief[3],s.prober.result().relief[4],s.prober.result().relief[5]); Log(m); }
            if(s.deployments.deploy(s.probeTarget,s.probeEye,s.probeYaw,now,&s.prober.result())) sendDeployment(s.deployments.local());
            s.probeTarget=0;
        }
    }
    if(s.restore){ s.restore=false; GameTeleport(s.probeHome[0],s.probeHome[1],s.probeHome[2],s.probeHome[3],s.probeHome[4]); }
    if(seated) {
        auto d=s.deployments.screens.find(seat.owner);
        // Two players can press at the same instant: neither sees the other's pose yet, so both sit. The one with the higher id gets up.
        bool yield=false;
        if(d!=s.deployments.screens.end() && d->second.active && !GetCfg().noSeatTiebreak) {
            auto mine=wtv::seatPoint(d->second,seat.index);
            for(const auto& r:remotes) if(r.valid && r.ageMs<2000 && r.emote==14 && r.id<CoopMyId()) {
                float dx=r.pos[0]-mine[0],dy=r.pos[1]-mine[1]; if(dx*dx+dy*dy<.5f*.5f) yield=true;
            }
        }
        if(exit || action || yield || !captureAllowed || d==s.deployments.screens.end() || !d->second.active || d->second.sequence!=seat.sequence) {
            GameSeatPose(seat.entry,seat.yaw,seat.pitch); seated=false; SocialCommand("emote 0");
        } else {
            auto p=wtv::seatPoint(d->second,seat.index); p[2]+=wtv::SeatEye;
            // A teleport/load away from the held pose cancels rather than pulling the player back.
            float delta=0; for(unsigned i=0;i<3;++i) delta+=(camera[i]-p[i])*(camera[i]-p[i]);
            if(delta>4) { seated=false; SocialCommand("emote 0"); }
            else GameSeatPose(p.data(),anglesOk?angles[0]:seat.yaw,anglesOk?angles[1]:seat.pitch);
        }
        action=false; // one press cannot stand and immediately sit again
    }
    seatAvailable=false;
    if(!seated && captureAllowed && anglesOk) for(const auto& entry:s.deployments.screens) {
        int index=wtv::aimSeat(entry.second,camera,angles[0],angles[1]); if(index<0) continue;
        seatAvailable=true;
        if(action && !exit) {
            auto p=wtv::seatPoint(entry.second,unsigned(index));
            bool occupied=false;
            for(const auto& r:remotes) if(r.valid && r.ageMs<2000 && r.emote==14) {
                float dx=r.pos[0]-p[0],dy=r.pos[1]-p[1]; if(dx*dx+dy*dy<.5f*.5f) occupied=true;
            }
            if(occupied) continue;
            seat={entry.first,entry.second.sequence,unsigned(index),{camera[0],camera[1],camera[2]},angles[0],angles[1]};
            p[2]+=wtv::SeatEye;
            if(GameSeatPose(p.data(),entry.second.yaw+3.14159265f,.45f)) { seated=true; SocialCommand("emote 14"); }
        }
        break;
    }
    if(s.deployments.local().sequence && now-s.deploymentBroadcastAt>=500){ sendDeployment(s.deployments.local()); s.deploymentBroadcastAt=now; }
    std::set<unsigned> wanted; for(const auto& screen:s.deployments.screens) if(screen.second.active) wanted.insert(screen.second.target);
    for(auto it=s.audience.begin();it!=s.audience.end();){
        if(wanted.count(it->first)) ++it;
        else { send({CoopMyId(),it->first,it->second.receiver.ticket,0}); it=s.audience.erase(it); }
    }
    if(captureAllowed) for(unsigned target:wanted){
        auto& stream=s.audience[target];
        if(stream.receiver.target!=target){ stream.receiver.select(target); stream.receiver.ticket=s.nextTicket++; if(!stream.receiver.ticket) stream.receiver.ticket=s.nextTicket++; stream.requestAt=0; }
        if(!stream.requestAt || now-stream.requestAt>=500){ if(target!=CoopMyId()) send({CoopMyId(),target,stream.receiver.ticket,1}); stream.requestAt=now; }
    }
}
bool TvWanted(){ Guard g; auto& s=state(); s.sender.expire(GetTickCount64()); return s.capture && s.sender.consent && (!s.sender.subs.empty() || s.audience.count(CoopMyId())); }
void TvCapture(const uint8_t* rgba,unsigned w,unsigned h){
    Guard g; auto& s=state(); if(!s.capture || !s.sender.due(GetTickCount64(),s.audience.count(CoopMyId())!=0) || !w || !h) return;
    LARGE_INTEGER start,end,freq; QueryPerformanceCounter(&start); QueryPerformanceFrequency(&freq);
    wtv::Bytes px(wtv::Pixels,0); // Letterbox to the stream's 16:9 rather than stretch the sender's view.
    unsigned oh=wtv::H,ow=wtv::W;
    if(uint64_t(w)*wtv::H>uint64_t(h)*wtv::W) oh=std::max(1u,unsigned(uint64_t(h)*wtv::W/w)); else ow=std::max(1u,unsigned(uint64_t(w)*wtv::H/h));
    for(unsigned y=0;y<oh;++y) for(unsigned x=0;x<ow;++x){
        const uint8_t* p=rgba+(size_t(y*h/oh)*w+x*w/ow)*4;
        // RGB332 bands smooth skies and walls into stripes; an ordered (4x4 Bayer) dither turns the steps into fine grain.
        static const uint8_t bayer[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5}; int t=bayer[(y&3)*4+(x&3)];
        auto q=[&](int v,int step){ return std::min(255,std::max(0,v+(2*t-15)*step/32)); };
        px[(y+(wtv::H-oh)/2)*wtv::W+x+(wtv::W-ow)/2]=uint8_t((q(p[0],32)&224)|((q(p[1],32)>>3)&28)|(q(p[2],64)>>6));
    }
    if(!++s.sequence) ++s.sequence;
    auto itSelf=s.audience.find(CoopMyId());
    if(itSelf!=s.audience.end()) itSelf->second.receiver.inject(s.sequence,px,GetTickCount64());
    for(auto sub:s.sender.subs) for(auto& b:wtv::frame(CoopMyId(),sub.second.ticket,s.sequence,px)) CoopSendRawTo(sub.second.viewer,b.data(),b.size());
    s.hashes.push_back({s.sequence,wtv::hash(px.data(),px.size())}); if(s.hashes.size()>16) s.hashes.erase(s.hashes.begin());
    ++s.sent; QueryPerformanceCounter(&end); s.maxCaptureUs=std::max(s.maxCaptureUs,unsigned((end.QuadPart-start.QuadPart)*1000000/freq.QuadPart));
}
void TvOnPacket(const uint8_t* p,size_t n){
    Guard g; auto& s=state(); uint64_t now=GetTickCount64(); s.deployments.me=CoopMyId();
    if(n>=4 && !memcmp(p,"WTD2",4)){ s.deployments.packet(p,n,now); return; }
    wtv::Control c;
    if(wtv::parseControl(p,n,c)){
        if(c.to!=CoopMyId()) return;
        if(c.kind==2){ s.receiver.deny(c); for(auto& stream:s.audience) stream.second.receiver.deny(c); return; }
        if(!s.sender.request(c,now) && c.kind==1) send({CoopMyId(),c.from,c.ticket,2});
        return;
    }
    if(n<8) return;
    if(s.enabled && s.receiver.target==wtv::get(p+4)) s.receiver.packet(p,n,now);
    for(auto& stream:s.audience) if(stream.first==wtv::get(p+4)) stream.second.receiver.packet(p,n,now);
}
ID3D11ShaderResourceView* TvTexture(ID3D11Device* dev,ID3D11DeviceContext* ctx,bool* live){
    Guard g; auto& s=state(); *live=s.enabled && s.receiver.fresh(GetTickCount64());
    if(!texture){
        D3D11_TEXTURE2D_DESC d={}; d.Width=wtv::W; d.Height=wtv::H; d.MipLevels=d.ArraySize=1; d.Format=DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count=1; d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        if(FAILED(dev->CreateTexture2D(&d,nullptr,&texture))) return nullptr;
        if(FAILED(dev->CreateShaderResourceView(texture,nullptr,&srv))){ texture->Release(); texture=nullptr; return nullptr; }
    }
    // One small persistent texture; static refreshes at 5 Hz, no per-frame allocation.
    static uint64_t noiseAt=0; static bool wasLive=false; uint64_t now=GetTickCount64();
    bool update=*live ? (s.uploaded!=s.receiver.decoded || !wasLive) : (wasLive || now-noiseAt>=200);
    if(update){
        static std::array<uint8_t,wtv::Pixels*4> px; uint32_t rng=unsigned(now/200);
        for(unsigned i=0;i<wtv::Pixels;++i){ auto* p=px.data()+i*4;
            if(*live){ uint8_t c=s.receiver.pixels[i]; p[0]=uint8_t(((c>>5)*255)/7); p[1]=uint8_t((((c>>2)&7)*255)/7); p[2]=uint8_t((c&3)*85); }
            else { rng=rng*1664525+1013904223; p[0]=p[1]=p[2]=uint8_t(24+(rng>>27)*3); }
            p[3]=255;
        }
        ctx->UpdateSubresource(texture,0,nullptr,px.data(),wtv::W*4,0); s.uploaded=s.receiver.decoded; s.uploadedSequence=*live?s.receiver.sequence():0; s.uploadHash=*live?wtv::hash(s.receiver.pixels.data(),s.receiver.pixels.size()):0; noiseAt=now;
    }
    wasLive=*live; return srv;
}
ID3D11ShaderResourceView* TvAudienceTexture(ID3D11Device* dev,ID3D11DeviceContext* ctx,unsigned source,bool* live){
    Guard g; auto& s=state(); auto it=s.audience.find(source); if(it==s.audience.end()) return nullptr; auto& stream=it->second; uint64_t now=GetTickCount64();
    *live=stream.receiver.fresh(now) && (source!=CoopMyId() || s.sender.consent);
    if(!stream.texture){
        D3D11_TEXTURE2D_DESC d={}; d.Width=wtv::W; d.Height=wtv::H; d.MipLevels=d.ArraySize=1; d.Format=DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count=1; d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        if(FAILED(dev->CreateTexture2D(&d,nullptr,&stream.texture))) return nullptr;
        if(FAILED(dev->CreateShaderResourceView(stream.texture,nullptr,&stream.srv))){ stream.texture->Release(); stream.texture=nullptr; return nullptr; }
    }
    bool update=*live?(stream.uploaded!=stream.receiver.decoded || !stream.wasLive):(stream.wasLive || now-stream.noiseAt>=wtv::Period);
    if(update){
        static std::array<uint8_t,wtv::Pixels*4> px; uint32_t rng=unsigned(now/wtv::Period)^source;
        for(unsigned i=0;i<wtv::Pixels;++i){ auto* p=px.data()+i*4;
            if(*live){ uint8_t c=stream.receiver.pixels[i]; p[0]=uint8_t(((c>>5)*255)/7); p[1]=uint8_t((((c>>2)&7)*255)/7); p[2]=uint8_t((c&3)*85); }
            else { rng=rng*1664525+1013904223; p[0]=p[1]=p[2]=uint8_t(24+(rng>>27)*3); }
            p[3]=255;
        }
        ctx->UpdateSubresource(stream.texture,0,nullptr,px.data(),wtv::W*4,0); stream.uploaded=stream.receiver.decoded;
        stream.uploadedSequence=*live?stream.receiver.sequence():0; stream.uploadHash=*live?wtv::hash(stream.receiver.pixels.data(),stream.receiver.pixels.size()):0; stream.noiseAt=now;
    }
    stream.wasLive=*live; return stream.srv;
}
void TvDrawn(unsigned vertices,bool live,float sway,float power){ Guard g; auto& s=state(); ++s.draws; s.vertices=vertices; s.drawnLive=live; s.sway=sway; s.power=power; }
void TvVisible(unsigned pixels){ Guard g; state().visiblePixels=pixels; }
void TvAudienceDrawn(unsigned owner,unsigned vertices,bool live){ Guard g; auto& stats=state().audienceStats[owner]; ++stats.draws; stats.vertices=vertices; stats.drawnLive=live; }
void TvAudienceVisible(unsigned owner,unsigned pixels){ Guard g; state().audienceStats[owner].visiblePixels=pixels; }
extern HMODULE g_selfModule();
void TvDump(){
    Guard g; auto& s=state(); uint64_t now=GetTickCount64(); wchar_t path[MAX_PATH]; GetModuleFileNameW(g_selfModule(),path,MAX_PATH); auto* slash=wcsrchr(path,L'\\'); if(slash) slash[1]=0;
    wcscat_s(path,L"witness_tv.json"); wchar_t tmp[MAX_PATH]; wcscpy_s(tmp,path); wcscat_s(tmp,L".tmp");
    FILE* f=_wfopen(tmp,L"w"); if(!f) return; s.sender.expire(now);
    fprintf(f,"{\"enabled\":%d,\"consent\":%d,\"target\":%u,\"deploy_target\":%u,\"subscribers\":%zu,\"sent\":%u,\"decoded\":%u,\"fresh\":%d,\"draws\":%u,\"vertices\":%u,\"drawn_live\":%d,\"sway\":%.6f,\"power\":%.5f,\"capture_cpu_max_us\":%u,",s.enabled,s.sender.consent,s.receiver.target,s.deployments.local().active?s.deployments.local().target:0,s.sender.subs.size(),s.sent,s.receiver.decoded,s.receiver.fresh(now),s.draws,s.vertices,s.drawnLive,s.sway,s.power,s.maxCaptureUs);
    fprintf(f,"\"visible_pixels\":%u,\"received_sequence\":%u,\"upload_hash\":%u,\"uploaded_sequence\":%u,\"sent_hashes\":[",s.visiblePixels,s.receiver.sequence(),s.uploadHash,s.uploadedSequence);
    for(size_t i=0;i<s.hashes.size();++i) fprintf(f,"%s[%u,%u]",i?",":"",s.hashes[i].first,s.hashes[i].second);
    fprintf(f,"],\"audience\":[");
    bool comma=false; for(const auto& entry:s.deployments.screens){ const auto& d=entry.second; if(!d.active) continue; auto p=wtv::placement(d); const auto& stats=s.audienceStats[d.from];
        auto stream=s.audience.find(d.target); bool fresh=stream!=s.audience.end() && stream->second.receiver.fresh(now);
        fprintf(f,"%s{\"owner\":%u,\"source\":%u,\"sequence\":%u,\"yaw\":%.6f,\"center\":[%.5f,%.5f,%.5f],\"ground\":%.5f,\"stools\":[%.5f,%.5f,%.5f],\"measured\":%u,\"bottom\":%.5f,\"width\":%.2f,\"height\":%.2f,\"draws\":%u,\"vertices\":%u,\"drawn_live\":%d,\"fresh\":%d,\"visible_pixels\":%u",comma?",":"",d.from,d.target,d.sequence,d.yaw,p.center[0],p.center[1],p.center[2],p.ground,p.stool[0],p.stool[1],p.stool[2],d.measured,p.bottom,wtv::ScreenWidth,wtv::ScreenHeight,stats.draws,stats.vertices,stats.drawnLive,fresh,stats.visiblePixels);
        if(stream!=s.audience.end()) fprintf(f,",\"decoded\":%u,\"upload_hash\":%u,\"uploaded_sequence\":%u",stream->second.receiver.decoded,stream->second.uploadHash,stream->second.uploadedSequence);
        else fprintf(f,",\"decoded\":0,\"upload_hash\":0,\"uploaded_sequence\":0"); fprintf(f,"}"); comma=true;
    }
    fprintf(f,"],\"probing\":%d,\"seated\":%d,\"seat_owner\":%u,\"audience_streams\":%zu,\"audience_stats\":%zu,\"audience_resources\":%zu}\n",int(s.probing),int(seated.load()),seat.owner,s.audience.size(),s.audienceStats.size(),size_t(std::count_if(s.audience.begin(),s.audience.end(),[](const auto& e){return e.second.texture!=nullptr;}))); fclose(f); MoveFileExW(tmp,path,MOVEFILE_REPLACE_EXISTING);
}
