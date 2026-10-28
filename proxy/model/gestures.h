#pragma once
#include "../core/emotes.h"
#include <array>
#include <algorithm>
#include <cmath>
namespace wm {
// Anatomical forward, twist and side angles in degrees, resolved against the
// authored rig's bind axes in model.cpp. Legs remain under locomotion control.
enum GestureBone { Spine, Chest, Neck, Head, ArmL, ElbowL, HandL, ArmR, ElbowR, HandR, ThighL, ShinL, FootL, ThighR, ShinR, FootR, GestureBones };
inline constexpr const char* kGestureBones[] = {"spine","chest","neck","head","upper_arm.L","forearm.L","hand.L","upper_arm.R","forearm.R","hand.R","thigh.L","shin.L","foot.L","thigh.R","shin.R","foot.R"};
struct GestureJoint { float a[3] = {}; float weight = 0; };
using GesturePose = std::array<GestureJoint,GestureBones>;
inline GesturePose GestureSample(int id, float t) {
    GesturePose p{}; float e=witness::EmoteEnvelope(id,t); if (!e) return p;
    auto set=[&](int b,float f,float twist=0,float side=0) { p[b]={{f,twist,side},e}; };
    float beat=std::sin(6.2831853f*t), slow=std::sin(3.14159265f*t);
    // Small opposing torso motion transfers weight without moving the feet.
    set(Spine,1.5f*slow); set(Chest,-1.f*slow,1.5f*slow); set(Neck,0); set(Head,0);
    set(ArmL,0); set(ElbowL,8); set(HandL,0); set(ArmR,0); set(ElbowR,8); set(HandR,0);
    switch(id) {
    case 1: set(ArmR,95,0,-28); set(ElbowR,60); set(HandR,0,20*beat,12*beat); break;
    case 2: set(Chest,3,10*slow,6*beat); set(ArmL,25+18*beat,0,25); set(ArmR,25-18*beat,0,-25); set(ElbowL,75); set(ElbowR,75); break;
    case 3: set(ArmL,145+8*beat,0,18); set(ArmR,145-8*beat,0,-18); set(ElbowL,20); set(ElbowR,20); set(Head,-8); break;
    case 4: set(Head,14*std::sin(9*t)); break;
    case 5: set(Head,0,22*std::sin(8*t)); break;
    case 6: set(Spine,22); set(Chest,18); set(Neck,-10); set(ArmL,-12); set(ArmR,-12); break;
    case 7: set(Chest,0,30*slow); set(ArmL,40,0,55); set(ArmR,40,0,-55); set(Head,0,20*slow); break;
    case 8: set(ArmL,10,-35,30); set(ArmR,10,35,-30); set(ElbowL,95); set(ElbowR,95); set(Head,-7,0,8); break;
    case 9: set(ArmR,88,0,-12); set(ElbowR,8); set(Head,0,-12); break;
    case 10: set(ArmL,36,0,65-7*beat); set(ArmR,26,0,-62+7*beat); set(ElbowL,88); set(ElbowR,90); break;
    case 11: set(ArmR,80,0,41); set(ElbowR,110); set(HandR,-20); set(Head,12,0,-6); break;
    case 12: set(ArmR,38,0,-12); set(ElbowR,100); set(HandR,0,-65); break;
    case 13: set(ArmR,58,0,65); set(ElbowR,125); set(HandR,-12); set(Head,8,0,-8); break;
    case 14: set(ThighL,95); set(ShinL,-95); set(FootL,0); set(ThighR,95); set(ShinR,-95); set(FootR,0); set(Spine,8); set(Chest,-4); set(ArmL,35); set(ArmR,35); set(ElbowL,20); set(ElbowR,20); break;
    case 15: set(ArmL,155,0,15); set(ArmR,155,0,-15); set(ElbowL,15); set(ElbowR,15); set(Chest,-8); set(Head,-8); break;
    case 16: set(ArmR,81,0,12); set(ElbowR,108); set(HandR,-8); break;
    case 17: set(ArmR,65,0,-20); set(ElbowR,70+24*beat); set(HandR,-15*beat); break;
    case 18: set(Chest,-6,8*slow,5*beat); set(ArmL,125+12*beat,0,32); set(ArmR,125-12*beat,0,-32); set(ElbowL,55-15*beat); set(ElbowR,55+15*beat); set(Head,-12,10*slow); break;
    case 19: set(Spine,10); set(Chest,8); set(Head,18); set(ArmL,-8); set(ArmR,-8); break;
    case 20: set(Spine,7+3*beat); set(Chest,4*beat); set(Head,-10+3*beat); set(ArmL,30); set(ElbowL,85); break;
    case 21: set(ArmL,115,0,40); set(ArmR,115,0,-40); set(ElbowL,45+20*beat); set(ElbowR,45-20*beat); set(Head,0,12*beat); break;
    case 22: set(Chest,0,12*slow,8*beat); set(ArmL,30+20*slow,0,35); set(ArmR,30-20*slow,0,-35); set(ElbowL,50); set(ElbowR,50); break;
    case 23: set(Head,12*(1-witness::Ease((t-1)/.5f))); set(ArmR,35+65*witness::Ease((t-1)/.5f)); set(ElbowR,100-75*witness::Ease((t-1)/.5f)); break;
    case 24: set(ArmR,81,0,12); set(ElbowR,108); set(HandR,0,-35); set(Head,0,18*std::sin(1.5f*t)); break;
    }
#ifdef WE_MUT_LIMIT
    p[ElbowR].a[0]=190;
#endif
    return p;
}
// Persistent per avatar: input loss, cancellation, switching and speech conflicts
// all use the same bounded slew. No frame-time allocations or clock reads.
struct GestureState {
    GesturePose pose{}; float talk = 0, turn = 0; int emote = 0, cancelled = 0; float lastT=-1;
    void Step(int id,float t,float speed,bool proximity,float dt) {
        dt = std::isfinite(dt) ? std::max(0.f,std::min(.1f,dt)) : 0;
        if (!witness::EmoteActive(id,t)) id=0;
        if (!id || id!=cancelled || t<lastT) cancelled=0;
        if(id && speed>.35f && !witness::kEmotes[id].mobile) cancelled=id;
#ifndef WE_MUT_MOVE
        if(id==cancelled) id=0;
#endif
        lastT=t;
        emote=id; auto target=GestureSample(id,t);
#ifndef WE_MUT_CONFLICT
        proximity = proximity && !id;
#endif
        float k=1-std::exp(-dt/.09f);
#ifdef WE_MUT_BLEND
        k=1;
#endif
        float targetTurn=id==7 ? 6.2831853f*witness::Ease((t-.2f)/1.9f) : 0;
        // Follow the shortest arc on interruption; a completed turn is identity.
        turn += std::remainder(targetTurn-turn,6.2831853f)*k;
        talk += ((proximity ? 1.f : 0.f)-talk)*k;
        if (talk > .0001f && !id) {
            target[ArmR]={{50,0,65},talk}; target[ElbowR]={{123,0,0},talk}; target[HandR]={{-15,-20,0},talk};
        }
        for(int b=0;b<GestureBones;++b) {
            pose[b].weight += (target[b].weight-pose[b].weight)*k;
            // Keep the outgoing pose while its influence fades out.
            if (target[b].weight > 0) for(int a=0;a<3;++a) pose[b].a[a]+=(target[b].a[a]-pose[b].a[a])*k;
        }
    }
};
}
