#pragma once
#include "panelframe.h"
namespace wframe {
// Panel-local +X is the surface normal. Keep the entire strip in that surface,
// rather than rotating its width toward the observer and lifting an edge away.
inline bool Ribbon(const float a[3], const float b[3], const Q& rotation,
                   float halfWidth, float out[4][3]) {
    const float axis[3] = {1, 0, 0}; float normal[3];
    Rotate(Normalize(rotation), axis, normal);
#ifdef WR_MUT_VIEW_FACING
    normal[0]=0; normal[1]=1; normal[2]=1;
#endif
    float d[3] = {b[0]-a[0], b[1]-a[1], b[2]-a[2]};
    float side[3] = {normal[1]*d[2]-normal[2]*d[1], normal[2]*d[0]-normal[0]*d[2], normal[0]*d[1]-normal[1]*d[0]};
    float n = std::sqrt(side[0]*side[0]+side[1]*side[1]+side[2]*side[2]);
    if (!(n > 1e-7f) || !std::isfinite(n)) return false;
    for (int k=0;k<3;++k) {
        float offset = side[k]*halfWidth/n;
        out[0][k]=a[k]-offset; out[1][k]=a[k]+offset;
        out[2][k]=b[k]-offset; out[3][k]=b[k]+offset;
#ifdef WR_MUT_EXTEND
        out[0][k]-=d[k]*.03f;out[1][k]-=d[k]*.03f;
        out[2][k]+=d[k]*.03f;out[3][k]+=d[k]*.03f;
#endif
    }
    return true;
}
}
