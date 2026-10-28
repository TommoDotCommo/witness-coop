#include "ribbon.h"
#include <cassert>
#include <initializer_list>
#include <cstdio>
int main(){
    for(auto q : {wframe::Q{0,0,0,1},wframe::Normalize({.2f,.3f,.4f,.5f})}) {
        const float localA[3]={.002f,-.23f,-.18f},localB[3]={.002f,.27f,.12f};
        float a[3],b[3],v[4][3],n[3];const float x[3]={1,0,0};
        wframe::Rotate(q,localA,a);wframe::Rotate(q,localB,b);wframe::Rotate(q,x,n);
        assert(wframe::Ribbon(a,b,q,.006f,v));
        for(int end=0;end<2;++end)for(int k=0;k<3;++k)
            assert(std::fabs((v[end*2][k]+v[end*2+1][k])*.5f-(end?b[k]:a[k]))<1e-6f);
        for(int i=0;i<4;++i){float plane=0;for(int k=0;k<3;++k)plane+=(v[i][k]-a[k])*n[k];assert(std::fabs(plane)<1e-6f);}
    }
    float zero[3]={0,0,0},v[4][3];assert(!wframe::Ribbon(zero,zero,{0,0,0,1},.006f,v));
    puts("PASS ribbon surface, exact centers/endpoints, tilted panel and degenerate edge");
}
