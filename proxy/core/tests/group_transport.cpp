// Windows game-free integration fixture: the actual coop.cpp UDP/config layer over a fake World.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cmath>
#include "../../coop.h"
struct FakeWorld : witness::World {
    std::atomic<bool> panels[9]{}, doors[9]{}, powers[9]{}, cables[9]{};
    unsigned player=0, panelIds[9]={0,1,2,3,4,5,6,7,8};
    bool hybrid=false; float origin[3]={};
    unsigned panelIndex(unsigned id) const { for(unsigned i=1;i<=8;++i)if(panelIds[i]==id)return i;return 0; }
    bool cameraValid() override { return true; }
    bool scan(std::vector<unsigned>& p,std::vector<unsigned>& d,uint64_t* table) override {
        for(unsigned i=1;i<=8;++i){p.push_back(panelIds[i]);if(!hybrid)d.push_back(i+8);}*table=1;return true;
    }
    bool scanCables(std::vector<unsigned>& c) override {if(!hybrid)for(unsigned i=17;i<=24;++i)c.push_back(i);return true;}
    bool entityExists(unsigned id) override {return id>0&&id<(hybrid?witness::kMaxEntityId:25);}
    bool isPanel(unsigned id) override {return panelIndex(id)!=0;}
    bool isDoor(unsigned id) override {return !hybrid&&id>8&&id<=16;}
    bool isCable(unsigned id) override {return !hybrid&&id>16&&id<=24;}
    bool panelSolved(unsigned id) override {return panels[panelIndex(id)];}
    bool doorOpen(unsigned id) override {return isDoor(id)&&doors[id-8];}
    bool panelPowered(unsigned id) override {return powers[panelIndex(id)];}
    bool cableLit(unsigned id) override {return isCable(id)&&cables[id-16];}
    void applyPanelSolved(unsigned id) override {panels[panelIndex(id)]=true;}
    bool applyDoorOpen(unsigned id) override {if(!isDoor(id))return false;doors[id-8]=true;return true;}
    bool applyPanelPower(unsigned id) override {powers[panelIndex(id)]=true;return true;}
    bool applyCableLit(unsigned id) override {if(!isCable(id))return false;cables[id-16]=true;return true;}
    bool applyCableOff(unsigned id) override {if(!isCable(id))return false;cables[id-16]=false;return true;}
    bool pose(float p[3],float a[2]) override {
        if(hybrid){
            p[0]=origin[0]+5.0f*float(player-2);p[1]=origin[1];p[2]=origin[2];
            if(player==2)p[0]+=1.5f*std::sin(float(GetTickCount64()%100000)*0.0007f);
            a[0]=a[1]=0;
        } else {p[0]=float(player);p[1]=p[2]=a[0]=a[1]=0;}
        return true;
    }
} world;
HMODULE g_selfModule(){return GetModuleHandleW(nullptr);}
witness::World* GameWorldInstance(){return &world;}
void VideoOnPacket(const uint8_t*,size_t){}
void Log(const char* text){std::puts(text);std::fflush(stdout);}
int main(int argc,char** argv){
    if(argc!=2&&argc!=3)return 2;
    world.player=(unsigned)std::strtoul(argv[1],nullptr,10);
    if(world.player<1||world.player>8)return 2;
    if(argc==3){
        FILE* spec=std::fopen(argv[2],"r");if(!spec)return 2;
        for(unsigned i=1;i<=8;++i)if(std::fscanf(spec,"%x",&world.panelIds[i])!=1)return 2;
        if(std::fscanf(spec,"%f %f %f",&world.origin[0],&world.origin[1],&world.origin[2])!=3)return 2;
        std::fclose(spec);world.hybrid=true;
    }
    CoopStart(); uint64_t begin=GetTickCount64(),report=0;bool lit=false,off=false,rapidOff=false;
    for(;;){
        uint64_t now=GetTickCount64(),age=now-begin;
        auto* core=CoopCore();
        if(core){
            if(age>=30000&&!lit){world.panels[world.player]=true;world.powers[world.player]=true;world.doors[world.player]=true;world.cables[world.player]=true;lit=true;}
            if(!world.hybrid && world.player==2 && age>=31000 && !rapidOff && world.cables[1]) { world.cables[1]=false; rapidOff=true; }
            if(age>=37000&&!off){if(world.hybrid||world.player!=1)world.cables[world.player]=false;off=true;} // client 1 must not rescue a lost remote reversal
            core->pollState(now);core->applyRemote(now);
            if(now>=report){report=now+500;
                unsigned solved=0,doors=0,power=0,cables=0;
                for(unsigned i=1;i<=8;++i){solved+=world.panels[i];doors+=world.doors[i];power+=world.powers[i];cables+=world.cables[i];}
                FILE* f=std::fopen("transport.json.tmp","w");if(!f)return 3;
                std::fprintf(f,"{\"id\":%u,\"ready\":%s,\"panels\":%u,\"doors\":%u,\"power\":%u,\"lit\":%u,\"shared\":%u,\"rapid_off\":%s,\"poses\":%zu,\"unacked\":%zu,\"queued\":%zu}\n",world.player,core->ready()?"true":"false",solved,doors,power,cables,unsigned(world.cables[1]),rapidOff?"true":"false",CoopGetRemotes().size(),core->unacked(),core->queuedRemote());
                std::fclose(f);MoveFileExA("transport.json.tmp","transport.json",MOVEFILE_REPLACE_EXISTING);
                CoopDumpStatus();
            }
        }
        if(age>150000)return 4;
        Sleep(20);
    }
}
