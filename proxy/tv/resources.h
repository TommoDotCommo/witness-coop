#pragma once
namespace wtv {
// Render-thread-owned COM references. Map erasure releases both references once;
// copying or moving a live stream would duplicate ownership and is prohibited.
template<class Texture,class View> struct StreamResources {
    Texture* texture=nullptr; View* srv=nullptr;
    StreamResources()=default;
    StreamResources(const StreamResources&)=delete;
    StreamResources& operator=(const StreamResources&)=delete;
    ~StreamResources(){ if(srv) srv->Release(); if(texture) texture->Release(); }
};
// Keep diagnostic records bounded by the actual active deployments.
template<class Stats,class Screens> void pruneStats(Stats& stats,const Screens& screens) {
    for(auto it=stats.begin();it!=stats.end();) {
        auto d=screens.find(it->first);
        if(d==screens.end() || !d->second.active) it=stats.erase(it); else ++it;
    }
}
}
