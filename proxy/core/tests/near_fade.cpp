#include "../../near_fade.h"
#include <cassert>
#include <cstdio>
int main() {
    float cam[3]={0,0,0}, eye[3]={3,0,0}; wnear::State s;
    auto alpha=[&](bool enabled=true) { return s.step(cam,0,0,54,16.f/9,eye,1.4f,.6f,enabled); };
    assert(alpha()==1); eye[0]=1; assert(std::fabs(alpha()-.5f)<.001);
    eye[0]=1.02f; assert(std::fabs(alpha()-.5f)<.001);
    eye[0]=.6f; assert(alpha()==0); eye[0]=0; assert(alpha()==0);
    assert(alpha(false)==1); // the production off-switch is the mutant
    eye[0]=-.8f; assert(alpha()==1); eye[0]=0; eye[1]=.8f; assert(alpha()==1);
    eye[0]=.1f; eye[1]=0; eye[2]=-.1f; assert(alpha()==0); // inside the head
    eye[2]=1; assert(alpha()==0); // camera inside the torso
    eye[0]=3; eye[2]=0; assert(alpha()==1);
    eye[0]=1; assert(s.step(cam,0,0,54,16.f/9,eye,.2f,1.4f)==.5f); // invalid keys use defaults
    wnear::State custom; eye[0]=2;
    assert(std::fabs(custom.step(cam,0,0,54,16.f/9,eye,3,1)-.5f)<.001);
    // A camera that turns away restores the player immediately, independent of
    // where the local body's feet are; turning back re-evaluates the eye pose.
    eye[0]=1; eye[1]=0;
    assert(custom.step(cam,3.14159265f,0,54,16.f/9,eye)==1);
    assert(custom.step(cam,0,0,54,16.f/9,eye)<1);
    // one fade, not two: a ghosted body (0.22) half faded by distance stays at 0.22, not 0.11; either alone is kept
    assert(wnear::BodyOpacity(.22f,.5f)==.22f && wnear::BodyOpacity(1,.5f)==.5f && wnear::BodyOpacity(.22f,1)==.22f && wnear::BodyOpacity(.6f,.6f)==.6f);
    std::puts("near fade: distance, deadband, overlap, view gate, restore and disable switch passed");
}
