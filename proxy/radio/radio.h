#pragma once
// Portable radio state and original, untextured geometry. Metres, Y up, +Z front.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
namespace wradio {
inline bool Valid(uint8_t flags, uint8_t channel) {
#ifdef WR_MUT_VALIDATE
    (void)flags; (void)channel;
#else
    if (flags & ~15u || channel > 3) return false;
    if ((flags & 4) && (!(flags & 8) || !(flags & 2) || channel != 1)) return false;
#endif
    return true;
}
struct Hold {
    bool down = false;
    void Key(bool pressed, bool enabled, bool blocked) {
#ifdef WR_MUT_HOLD
        (void)enabled; (void)blocked;
        down = pressed;
#else
        down = pressed && enabled && !blocked;
#endif
    }
    bool Want(bool enabled, bool blocked, bool voiceOn) const { return enabled && !blocked && voiceOn && down; }
};
struct Raise {
    float value = 0;
    void Step(bool tx, float dt) {
#ifdef WR_MUT_SNAP
        (void)dt;
        value = tx ? 1 : 0;
#else
        value += ((tx ? 1.0f : 0.0f) - value) * (1 - std::exp(-std::max(0.0f, std::min(dt, .1f)) / .085f));
#endif
    }
};
struct GuideLayout {
    float scale, x, y, width, height;
    float Row(unsigned i) const { return y+(40+24*i)*scale; }
};
inline GuideLayout Guide(float width, float height, unsigned rows) {
    float k=std::min(width/1280.0f,height/720.0f);
#ifdef WR_MUT_GUIDE
    rows=0;
#endif
    return {k,20*k,80*k,350*k,(50+24*rows)*k};
}
inline constexpr const char* kRosterHint="Left/Right: voice volume   M: mute/unmute";
struct RosterLayout { float scale,x,y,width,height,rowStep; };
inline RosterLayout Roster(float width,float height,unsigned rows,float cellHeight) {
    float k=std::min(width/1280.0f,height/720.0f),step=cellHeight+6;
    k=std::min(k,height/(120+step*(rows+3)));
#ifdef WR_MUT_ROSTER
    k=1;
#endif
    return {k,24*k,90*k,520*k,(step*(rows+3)+14)*k,step*k};
}
struct Vertex { float p[3], c[4]; };
inline void Box(std::vector<Vertex>& out, float x, float y, float z, float sx, float sy, float sz, float r, float g, float b) {
    static constexpr int faces[6][4] = {{0,2,3,1},{4,5,7,6},{0,1,5,4},{2,6,7,3},{0,4,6,2},{1,3,7,5}};
    for (int f=0; f<6; ++f) for (int i : {0,1,2,0,2,3}) {
        int k=faces[f][i]; float shade=.72f+.055f*f;
        out.push_back({{x+((k&1)?sx:-sx)*.5f,y+((k&2)?sy:-sy)*.5f,z+((k&4)?sz:-sz)*.5f},{r*shade,g*shade,b*shade,1}});
    }
}
inline void Shell(std::vector<Vertex>& out) {
    constexpr float ring[8][2]={{-.0315f,-.0215f},{.0315f,-.0215f},{.0375f,-.0155f},{.0375f,.0155f},{.0315f,.0215f},{-.0315f,.0215f},{-.0375f,.0155f},{-.0375f,-.0155f}};
    constexpr float heights[4]={-.08f,-.074f,.074f,.08f};
    auto vertex=[&](int corner,int band) { float k=(band==0 || band==3)?.87f:1; float shade=.75f+.04f*(corner%5);
        return Vertex{{ring[corner][0]*k,heights[band],ring[corner][1]*k},{.25f*shade,.29f*shade,.23f*shade,1}}; };
    for(int band=0;band<3;++band) for(int corner=0;corner<8;++corner) {
        int next=(corner+1)%8; Vertex quad[4]={vertex(corner,band),vertex(next,band),vertex(next,band+1),vertex(corner,band+1)};
        for(int i:{0,1,2,0,2,3}) out.push_back(quad[i]);
    }
    for(int band:{0,3}) for(int corner=0;corner<8;++corner) { out.push_back(vertex(corner,band)); out.push_back(vertex((corner+1)%8,band)); out.push_back({{0,heights[band],0},{.22f,.26f,.20f,1}}); }
}
inline std::vector<Vertex> Mesh(bool light) {
    std::vector<Vertex> v; v.reserve(864);
    Shell(v); // bevelled olive enamel shell
    Box(v,0,-.078f,0,.079f,.012f,.047f,.13f,.10f,.075f); // leather bumper
    Box(v,0,.078f,0,.079f,.012f,.047f,.39f,.30f,.17f); // brass rim
    Box(v,-.025f,.143f,0,.009f,.13f,.009f,.09f,.10f,.085f); // antenna
    Box(v,.02f,.091f,0,.019f,.02f,.019f,.13f,.14f,.12f); // knob
    Box(v,0,.011f,.024f,.061f,.071f,.006f,.07f,.085f,.07f); // grille recess
    for (int i=0;i<7;++i) Box(v,0,-.017f+i*.009f,.028f,.054f,.003f,.003f,.41f,.39f,.29f);
    Box(v,.023f,.06f,.025f,.01f,.009f,.006f,light?.9f:.2f,light?.18f:.25f,.09f);
    Box(v,-.039f,.025f,0,.008f,.037f,.026f,.35f,.23f,.13f); // PTT paddle
    Box(v,0,-.055f,.024f,.04f,.012f,.004f,.66f,.57f,.35f); // label plate
    return v;
}
inline void CameraToWorld(const float cam[3], float yaw, float pitch, const float p[3], float out[3]) {
    float st=std::sin(yaw),ct=std::cos(yaw),sp=std::sin(pitch),cp=std::cos(pitch);
#ifdef WR_MUT_CAMERA
    st=-st;
#endif
    out[0]=cam[0]+st*p[0]-ct*sp*p[1]+ct*cp*p[2];
    out[1]=cam[1]-ct*p[0]-st*sp*p[1]+st*cp*p[2];
    out[2]=cam[2]+cp*p[1]+sp*p[2];
}
// Camera-space position. The complete mesh remains right of the puzzle's central
// 60% in the raised pose, including narrow aspect ratios. The idle pose
// intentionally lowers the body partly below the bottom edge.
inline void FirstPerson(float aspect, float tanHalfFov, float raise, const float p[3], float out[3]) {
    float depth=.65f;
    float scale=.75f*std::min(1.0f,aspect); float x=.77f*depth*tanHalfFov*aspect;
#ifdef WR_MUT_CENTER
    x=0;
#endif
    out[0]=x+p[0]*scale; out[1]=(-.92f+.34f*raise)*depth*tanHalfFov+p[1]*.60f;
    out[2]=depth+p[2]*.6f;
}
} // namespace wradio
