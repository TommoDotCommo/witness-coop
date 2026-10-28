#include "tv.h"
#include "tv/tv.h"
#include "coop.h"
#include "config.h"
#include <cstdio>
namespace {
SRWLOCK lock=SRWLOCK_INIT;
struct Guard { Guard(){ AcquireSRWLockExclusive(&lock); } ~Guard(){ ReleaseSRWLockExclusive(&lock); } };
struct State {
    bool enabled=GetCfg().povTv, capture=false; wtv::Sender sender; wtv::Receiver receiver;
    uint64_t requestAt=0; unsigned displayTarget=0; uint64_t disabledAt=0; unsigned sequence=0,sent=0,uploaded=0,draws=0,vertices=0; bool drawnLive=false;
    float sway=0,power=0; unsigned maxCaptureUs=0,visiblePixels=0,uploadHash=0,uploadedSequence=0;
    std::vector<std::pair<unsigned,unsigned>> hashes;
    State(){ sender.allow(GetCfg().povTvConsent); receiver.ticket=GetTickCount()|1u; }
};
State& state(){ static State s; return s; }
void send(wtv::Control c){ auto b=wtv::control(c); CoopSendRawTo(c.to,b.data(),b.size()); }
ID3D11Texture2D* texture; ID3D11ShaderResourceView* srv;
}
bool TvEnabled(){ Guard g; return state().enabled; }
bool TvConsent(){ Guard g; return state().sender.consent; }
void TvEnable(bool on){ Guard g; auto& s=state(); s.enabled=on; s.disabledAt=GetTickCount64(); if(!on) { if(s.receiver.target) send({CoopMyId(),s.receiver.target,s.receiver.ticket,0}); s.receiver.select(0); } }
void TvAllow(bool on){ Guard g; auto& s=state(); if(!on) for(auto p:s.sender.subs) send({CoopMyId(),p.first,p.second.ticket,2}); s.sender.allow(on); }
unsigned TvTarget(){ Guard g; auto& s=state(); return s.enabled?s.receiver.target:(GetTickCount64()-s.disabledAt<350?s.displayTarget:0); }
void TvTick(const float camera[3],bool captureAllowed){
    unsigned nearest=0; float distance=12*12;
    for(auto r:CoopGetRemotes()) if(r.valid && r.ageMs<1000){
        float d=0; for(int i=0;i<3;++i) d+=(r.pos[i]-camera[i])*(r.pos[i]-camera[i]);
        if(d<distance || (d==distance && r.id<nearest)){ distance=d; nearest=r.id; }
    }
    Guard g; auto& s=state(); s.capture=captureAllowed; if(!s.enabled || !captureAllowed) nearest=0;
    if(nearest!=s.receiver.target){
        if(s.receiver.target) send({CoopMyId(),s.receiver.target,s.receiver.ticket,0});
        s.receiver.select(nearest); s.requestAt=0; if(s.enabled) s.displayTarget=nearest;
    }
    uint64_t now=GetTickCount64();
    if(nearest && (!s.requestAt || now-s.requestAt>=500)){ send({CoopMyId(),nearest,s.receiver.ticket,1}); s.requestAt=now; }
}
bool TvWanted(){ Guard g; auto& s=state(); s.sender.expire(GetTickCount64()); return s.capture && s.sender.consent && !s.sender.subs.empty(); }
void TvCapture(const uint8_t* rgba,unsigned w,unsigned h){
    Guard g; auto& s=state(); if(!s.capture || !s.sender.due(GetTickCount64()) || !w || !h) return;
    LARGE_INTEGER start,end,freq; QueryPerformanceCounter(&start); QueryPerformanceFrequency(&freq);
    wtv::Bytes px(wtv::Pixels,0); // Letterbox to 4:3 rather than stretch the sender's view.
    unsigned oh=wtv::H,ow=wtv::W;
    if(uint64_t(w)*wtv::H>uint64_t(h)*wtv::W) oh=std::max(1u,unsigned(uint64_t(h)*wtv::W/w)); else ow=std::max(1u,unsigned(uint64_t(w)*wtv::H/h));
    for(unsigned y=0;y<oh;++y) for(unsigned x=0;x<ow;++x){
        const uint8_t* p=rgba+(size_t(y*h/oh)*w+x*w/ow)*4;
        px[(y+(wtv::H-oh)/2)*wtv::W+x+(wtv::W-ow)/2]=(p[0]&224)|((p[1]>>3)&28)|(p[2]>>6);
    }
    if(!++s.sequence) ++s.sequence;
    for(auto sub:s.sender.subs) for(auto& b:wtv::frame(CoopMyId(),sub.second.ticket,s.sequence,px)) CoopSendRawTo(sub.first,b.data(),b.size());
    s.hashes.push_back({s.sequence,wtv::hash(px.data(),px.size())}); if(s.hashes.size()>16) s.hashes.erase(s.hashes.begin());
    ++s.sent; QueryPerformanceCounter(&end); s.maxCaptureUs=std::max(s.maxCaptureUs,unsigned((end.QuadPart-start.QuadPart)*1000000/freq.QuadPart));
}
void TvOnPacket(const uint8_t* p,size_t n){
    Guard g; auto& s=state(); uint64_t now=GetTickCount64(); wtv::Control c;
    if(wtv::parseControl(p,n,c)){
        if(c.to!=CoopMyId()) return;
        if(c.kind==2){ s.receiver.deny(c); return; }
        if(!s.sender.request(c,now) && c.kind==1) send({CoopMyId(),c.from,c.ticket,2});
    } else if(s.enabled) s.receiver.packet(p,n,now);
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
        std::array<uint8_t,wtv::Pixels*4> px; uint32_t rng=unsigned(now/200);
        for(unsigned i=0;i<wtv::Pixels;++i){ auto* p=px.data()+i*4;
            if(*live){ uint8_t c=s.receiver.pixels[i]; p[0]=uint8_t(((c>>5)*255)/7); p[1]=uint8_t((((c>>2)&7)*255)/7); p[2]=uint8_t((c&3)*85); }
            else { rng=rng*1664525+1013904223; p[0]=p[1]=p[2]=uint8_t(24+(rng>>27)*3); }
            p[3]=255;
        }
        ctx->UpdateSubresource(texture,0,nullptr,px.data(),wtv::W*4,0); s.uploaded=s.receiver.decoded; s.uploadedSequence=*live?s.receiver.sequence():0; s.uploadHash=*live?wtv::hash(s.receiver.pixels.data(),s.receiver.pixels.size()):0; noiseAt=now;
    }
    wasLive=*live; return srv;
}
void TvDrawn(unsigned vertices,bool live,float sway,float power){ Guard g; auto& s=state(); ++s.draws; s.vertices=vertices; s.drawnLive=live; s.sway=sway; s.power=power; }
void TvVisible(unsigned pixels){ Guard g; state().visiblePixels=pixels; }
extern HMODULE g_selfModule();
void TvDump(){
    Guard g; auto& s=state(); wchar_t path[MAX_PATH]; GetModuleFileNameW(g_selfModule(),path,MAX_PATH); auto* slash=wcsrchr(path,L'\\'); if(slash) slash[1]=0;
    wcscat_s(path,L"witness_tv.json"); wchar_t tmp[MAX_PATH]; wcscpy_s(tmp,path); wcscat_s(tmp,L".tmp");
    FILE* f=_wfopen(tmp,L"w"); if(!f) return; s.sender.expire(GetTickCount64());
    fprintf(f,"{\"enabled\":%d,\"consent\":%d,\"target\":%u,\"subscribers\":%zu,\"sent\":%u,\"decoded\":%u,\"fresh\":%d,\"draws\":%u,\"vertices\":%u,\"drawn_live\":%d,\"sway\":%.6f,\"power\":%.5f,\"capture_cpu_max_us\":%u,",s.enabled,s.sender.consent,s.receiver.target,s.sender.subs.size(),s.sent,s.receiver.decoded,s.receiver.fresh(GetTickCount64()),s.draws,s.vertices,s.drawnLive,s.sway,s.power,s.maxCaptureUs);
    fprintf(f,"\"visible_pixels\":%u,\"received_sequence\":%u,\"upload_hash\":%u,\"uploaded_sequence\":%u,\"sent_hashes\":[",s.visiblePixels,s.receiver.sequence(),s.uploadHash,s.uploadedSequence);
    for(size_t i=0;i<s.hashes.size();++i) fprintf(f,"%s[%u,%u]",i?",":"",s.hashes[i].first,s.hashes[i].second);
    fprintf(f,"]}\n");
    fclose(f); MoveFileExW(tmp,path,MOVEFILE_REPLACE_EXISTING);
}
