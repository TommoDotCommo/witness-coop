#pragma once
#include "tv.h"
#include "../model/model.h"
namespace wtv {
// Original procedural CRT and harness, authored for the bundled adventurer in
// glTF bind space (+Y up, +Z front). Chest skinning carries all clip animation.
struct Mesh { std::vector<wm::Vertex> body,screen; };
inline Mesh mesh(unsigned chest,const Motion& motion) {
    Mesh out;
    auto vertex=[&](float x,float y,float z,float nx,float ny,float nz,const std::array<uint8_t,3>& col,bool texture,float u=0,float v=0){
        wm::Vertex p={}; p.pos[0]=x; p.pos[1]=y; p.pos[2]=z; p.nrm[0]=nx; p.nrm[1]=ny; p.nrm[2]=nz;
        p.joints[0]=uint8_t(chest); p.weights[0]=1; for(int i=0;i<3;++i) p.col[i]=col[i]; p.col[3]=texture?255:0; p.uv[0]=u; p.uv[1]=v; return p;
    };
    auto transform=[&](float x,float y,float z){ // gentle roll about the mounting bracket
        float c=cosf(motion.sway),s=sinf(motion.sway); return std::array<float,3>{c*x-s*(y-1.22f),1.22f+s*x+c*(y-1.22f),z};
    };
    auto quad=[&](std::array<std::array<float,3>,4> pts,std::array<uint8_t,3> color,bool screen,bool moving){
        float a[3],b[3],n[3]; for(int i=0;i<3;++i){a[i]=pts[1][i]-pts[0][i];b[i]=pts[2][i]-pts[0][i];}
        n[0]=a[1]*b[2]-a[2]*b[1]; n[1]=a[2]*b[0]-a[0]*b[2]; n[2]=a[0]*b[1]-a[1]*b[0];
        float len=sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]); for(float& k:n) k/=std::max(1e-6f,len);
        auto& verts=screen?out.screen:out.body; const unsigned idx[]={0,1,2,0,2,3}; const float uv[4][2]={{0,1},{1,1},{1,0},{0,0}};
        for(unsigned i:idx){ auto p=pts[i]; if(moving) p=transform(p[0],p[1],p[2]); verts.push_back(vertex(p[0],p[1],p[2],n[0],n[1],n[2],color,screen,uv[i][0],uv[i][1])); }
    };
    auto box=[&](float x,float y,float z,float w,float h,float d,std::array<uint8_t,3> color){
        std::array<std::array<float,3>,8> p; for(int i=0;i<8;++i) p[i]={x+(i&1?w:-w)/2,y+(i&2?h:-h)/2,z+(i&4?d:-d)/2};
        const unsigned face[6][4]={{0,2,3,1},{4,5,7,6},{0,4,6,2},{1,3,7,5},{0,1,5,4},{2,6,7,3}};
        for(auto& f:face) quad({p[f[0]],p[f[1]],p[f[2]],p[f[3]]},color,false,true);
    };
    const std::array<uint8_t,3> shell={72,81,77}, trim={27,30,29}, strap={80,51,29}, brass={168,139,76};
    // Rear of the existing pack reaches z=-0.29. The mounting plate and TV sit outside it.
    box(0,1.18f,-.31f,.25f,.28f,.025f,strap);
    box(0,1.20f,-.39f,.38f,.30f,.14f,shell);
    box(0,1.20f,-.468f,.34f,.265f,.018f,trim);
    box(.153f,1.12f,-.485f,.025f,.025f,.012f,brass); // dial
    box(.153f,1.18f,-.485f,.025f,.025f,.012f,brass);
    for(int i=0;i<5;++i) box(.153f,1.24f+i*.009f,-.483f,.022f,.003f,.004f,trim); // speaker slots
    box(-.145f,1.075f,-.483f,.012f,.008f,.008f,motion.power>.5f?std::array<uint8_t,3>{70,220,100}:trim);
    // Screen opens/closes like a CRT. The texture can be replaced without changing the prop.
    float sh=.103f*(.015f+.985f*motion.power);
    quad({{{.124f,1.21f-sh,-.481f},{-.139f,1.21f-sh,-.481f},{-.139f,1.21f+sh,-.481f},{.124f,1.21f+sh,-.481f}}},{255,255,255},true,true);
    // Two straps continue over the shoulders to the front. Moving attachment
    // points stretch each ribbon, keeping tension as the bracket sways.
    for(float x:{-.14f,.14f}) {
        std::array<float,3> a=transform(x,1.32f,-.36f), b=transform(x,1.08f,-.36f);
        auto ribbon=[&](std::array<float,3> p,std::array<float,3> q){ float hw=.014f; quad({{{p[0]-hw,p[1],p[2]},{p[0]+hw,p[1],p[2]},{q[0]+hw,q[1],q[2]},{q[0]-hw,q[1],q[2]}}},strap,false,false); };
        ribbon(a,{x,1.46f,-.07f}); ribbon({x,1.46f,-.07f},{x,1.43f,.12f}); ribbon({x,1.43f,.12f},{x,1.07f,.16f}); ribbon({x,1.07f,.16f},b);
        box(x,1.32f,-.47f,.038f,.032f,.012f,brass);
    }
    box(.10f+motion.antenna*.10f,1.49f,-.38f,.009f,.28f,.009f,brass);
    box(.10f+motion.antenna*.10f,1.635f,-.38f,.018f,.018f,.018f,trim);
    return out;
}
} // namespace wtv
