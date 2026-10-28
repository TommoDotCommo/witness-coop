#include "radio_game.h"
#include "../coop.h"
#include "../social.h"
#include "../voice/voice.h"
#include "../config.h"
#include <windows.h>
#include <cstdio>
#include <map>
#include <cstring>
extern HMODULE g_selfModule();
std::vector<wvoice::SpeakerStat> VoiceSnapshot();
namespace {
struct Draw { float raise; unsigned vertices; float wrist[3]; uint64_t at; };
std::map<unsigned, Draw> draws; volatile LONG request=0;
}
void RadioRecord(unsigned id, float raise, unsigned vertices, const float* wrist) {
    Draw d={}; d.raise=raise; d.vertices=vertices; d.at=GetTickCount64(); if(wrist) memcpy(d.wrist,wrist,12); draws[id]=d;
}
void RadioRequestDump(unsigned token) { if(token && token<=0x7fffffff) InterlockedExchange(&request,(LONG)token); }
void RadioDumpFrame() {
    unsigned token=(unsigned)InterlockedExchange(&request,0); if(!token) return;
    wchar_t path[MAX_PATH]; GetModuleFileNameW(g_selfModule(),path,MAX_PATH); wchar_t* s=wcsrchr(path,L'\\'); if(!s) return; *(s+1)=0;
    wcscat_s(path,L"witness_radio.json"); wchar_t tmp[MAX_PATH]; wcscpy_s(tmp,path); wcscat_s(tmp,L".tmp"); FILE* f=_wfopen(tmp,L"w"); if(!f) return;
    fprintf(f,"{\"token\":%u,\"enabled\":%s,\"tx\":%s,\"channel\":%u,\"draws\":[",token,GetCfg().radioEnabled?"true":"false",VoiceRadioTx()?"true":"false",VoiceChannel());
    bool comma=false; for(const auto& kv:draws) if(GetTickCount64()-kv.second.at<250) { const auto& d=kv.second;
        fprintf(f,"%s{\"id\":%u,\"raise\":%.5f,\"vertices\":%u,\"wrist\":[%.5f,%.5f,%.5f]}",comma?",":"",kv.first,d.raise,d.vertices,d.wrist[0],d.wrist[1],d.wrist[2]); comma=true; }
    fprintf(f,"],\"remotes\":["); comma=false; for(const auto& r:CoopGetRemotes()) {
        fprintf(f,"%s{\"id\":%u,\"tx\":%s,\"channel\":%u,\"equipped\":%s,\"age_ms\":%u}",comma?",":"",r.id,r.radioTx?"true":"false",unsigned(r.voiceChannel),r.radioEquipped?"true":"false",r.ageMs); comma=true; }
    fprintf(f,"],\"speakers\":["); comma=false; for(const auto& s:VoiceSnapshot()) {
        fprintf(f,"%s{\"id\":%u,\"talking\":%s,\"channel\":%u,\"radio\":%.6f,\"prox\":%.6f,\"rms\":%.6f,\"tone\":[%.6f,%.6f,%.6f]}",comma?",":"",s.id,s.talking?"true":"false",unsigned(s.channel),s.radio,s.prox,s.rms,s.tone[0],s.tone[1],s.tone[2]); comma=true; }
    fprintf(f,"]}\n"); fclose(f); MoveFileExW(tmp,path,MOVEFILE_REPLACE_EXISTING);
}
