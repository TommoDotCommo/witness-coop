// Windows game-free integration fixture: the actual coop.cpp UDP/config layer over a fake World.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include "../../coop.h"
struct FakeWorld : witness::World {
    std::atomic<bool> panels[9]{}, doors[9]{}, powers[9]{}, cables[9]{};
    unsigned player=0;
    bool cameraValid() override { return true; }
    bool scan(std::vector<unsigned>& p,std::vector<unsigned>& d,uint64_t* table) override {
        for(unsigned i=1;i<=8;++i){p.push_back(i);d.push_back(i+8);}*table=1;return true;
    }
    bool scanCables(std::vector<unsigned>& c) override {for(unsigned i=17;i<=24;++i)c.push_back(i);return true;}
    bool entityExists(unsigned id) override {return id>0&&id<=24;}
    bool isPanel(unsigned id) override {return id>0&&id<=8;}
    bool isDoor(unsigned id) override {return id>8&&id<=16;}
    bool isCable(unsigned id) override {return id>16&&id<=24;}
    bool panelSolved(unsigned id) override {return panels[id];}
    bool doorOpen(unsigned id) override {return doors[id-8];}
    bool panelPowered(unsigned id) override {return powers[id];}
    bool cableLit(unsigned id) override {return cables[id-16];}
    void applyPanelSolved(unsigned id) override {panels[id]=true;}
    bool applyDoorOpen(unsigned id) override {doors[id-8]=true;return true;}
    bool applyPanelPower(unsigned id) override {powers[id]=true;return true;}
    bool applyCableLit(unsigned id) override {cables[id-16]=true;return true;}
    bool applyCableOff(unsigned id) override {cables[id-16]=false;return true;}
    bool pose(float p[3],float a[2]) override {p[0]=float(player);p[1]=p[2]=a[0]=a[1]=0;return true;}
} world;
HMODULE g_selfModule(){return GetModuleHandleW(nullptr);}
witness::World* GameWorldInstance(){return &world;}
void VideoOnPacket(const uint8_t*,size_t){}
void Log(const char* text){std::puts(text);std::fflush(stdout);}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    world.player=(unsigned)std::strtoul(argv[1],nullptr,10);
    if(world.player<1||world.player>8)return 2;
    CoopStart(); uint64_t begin=GetTickCount64(),report=0;bool lit=false,off=false;
    for(;;){
        uint64_t now=GetTickCount64(),age=now-begin;
        auto* core=CoopCore();
        if(core){
            if(age>=30000&&!lit){world.panels[world.player]=true;world.powers[world.player]=true;world.doors[world.player]=true;world.cables[world.player]=true;lit=true;}
            if(age>=37000&&!off){world.cables[world.player]=false;off=true;}
            core->pollState(now);core->applyRemote(now);
            if(now>=report){report=now+500;
                unsigned solved=0,doors=0,power=0,cables=0;
                for(unsigned i=1;i<=8;++i){solved+=world.panels[i];doors+=world.doors[i];power+=world.powers[i];cables+=world.cables[i];}
                FILE* f=std::fopen("transport.json.tmp","w");if(!f)return 3;
                std::fprintf(f,"{\"id\":%u,\"ready\":%s,\"panels\":%u,\"doors\":%u,\"power\":%u,\"lit\":%u,\"poses\":%zu,\"unacked\":%zu,\"queued\":%zu}\n",world.player,core->ready()?"true":"false",solved,doors,power,cables,CoopGetRemotes().size(),core->unacked(),core->queuedRemote());
                std::fclose(f);MoveFileExA("transport.json.tmp","transport.json",MOVEFILE_REPLACE_EXISTING);
                CoopDumpStatus();
            }
        }
        if(age>150000)return 4;
        Sleep(20);
    }
}
