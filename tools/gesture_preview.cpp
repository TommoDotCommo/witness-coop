// Software preview of the production skin palette. No graphics API or game.
#include "../proxy/model/model.h"
#include <fstream>
#include <iterator>
#include <algorithm>
#include <cstdio>
#include <cmath>
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    std::ifstream f(argv[1],std::ios::binary); std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
    wm::Model m; std::string err; if(!wm::LoadGlb(bytes.data(),bytes.size(),&m,&err)) return 3;
    const int W=1000,H=1250,CW=200,CH=250;
    std::vector<uint8_t> rgb(W*H*3,235); std::vector<float> depth(W*H,-1e20f);
    struct P { float x,y,z; }; std::vector<P> pts(m.verts.size());
    for(int id=1;id<=25;++id) {
        wm::GestureState state; for(int n=0;n<90;++n) state.Step(id==25?0:id,n/60.f,0,id==25,1/60.f);
        std::vector<float> bones(wm::kMaxJoints*16); wm::JointMatricesBlend(m,-1,0,wm::FindClip(m,"idle"),1.5f,1,bones.data(),&state);
        int cx=(id-1)%5*CW,cy=(id-1)/5*CH;
        for(size_t v=0;v<m.verts.size();++v) { float p[3]; wm::SkinPosition(m,bones.data(),m.verts[v],p); pts[v]={cx+100+90*(.94f*p[0]-.342f*p[2]),cy+225-105*p[1],.342f*p[0]+.94f*p[2]}; }
        for(size_t i=0;i<m.idx.size();i+=3) {
            P a=pts[m.idx[i]],b=pts[m.idx[i+1]],c=pts[m.idx[i+2]];
            float area=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x); if(fabsf(area)<.001f) continue;
            int x0=std::max(cx,int(std::floor(std::min({a.x,b.x,c.x})))),x1=std::min(cx+CW-1,int(std::ceil(std::max({a.x,b.x,c.x}))));
            int y0=std::max(cy,int(std::floor(std::min({a.y,b.y,c.y})))),y1=std::min(cy+CH-1,int(std::ceil(std::max({a.y,b.y,c.y}))));
            const auto& v=m.verts[m.idx[i]]; float light=.65f+.35f*fabsf(v.nrm[2]); float colour[3]={float(v.col[0]),float(v.col[1]),float(v.col[2])};
            if(v.col[3] && m.texW>0 && !m.rgba.empty()) {
                int tx=std::max(0,std::min(m.texW-1,int(v.uv[0]*m.texW))),ty=std::max(0,std::min(m.texH-1,int(v.uv[1]*m.texH)));
                for(int k=0;k<3;++k) colour[k]=m.rgba[(ty*m.texW+tx)*4+k];
            }
            for(int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x) {
                float u=((b.x-x)*(c.y-y)-(b.y-y)*(c.x-x))/area;
                float v1=((c.x-x)*(a.y-y)-(c.y-y)*(a.x-x))/area,w=1-u-v1;
                if(u<0 || v1<0 || w<0) continue;
                float z=u*a.z+v1*b.z+w*c.z; int pixel=y*W+x; if(z<=depth[pixel]) continue; depth[pixel]=z;
                for(int k=0;k<3;++k) rgb[pixel*3+k]=uint8_t(std::min(255.f,colour[k]*light));
            }
        }
    }
    FILE* out=fopen(argv[2],"wb"); if(!out) return 4; fprintf(out,"P6\n%d %d\n255\n",W,H); fwrite(rgb.data(),1,rgb.size(),out); fclose(out);
}
