#pragma once
#include "config.h"
#include "../common/guide.h"

template<char C> struct SingleChar { static constexpr char str[2] = {C, 0}; };
template<char C> constexpr char SingleChar<C>::str[2];

// One vocabulary for configuration, launcher controls and the F1 quick guide.
struct KeyBinding { const char* key; const char* label; const char* fallback; unsigned Cfg::*member; };
inline constexpr KeyBinding kBindings[] = {
#define BIND(name, member, key, text, label) {name, label, SingleChar<key>::str, &Cfg::member},
#include "../common/bindings.def"
#undef BIND
};

inline unsigned BindingKey(std::string text) {
    return guide::ParseKey(text);
}

inline bool SetBindingLine(Cfg* cfg, std::string line) {
    while(!line.empty() && (line.back()=='\r' || line.back()=='\n')) line.pop_back();
    for(const auto& b:guide::bindings) {
        std::string prefix=std::string(b.config)+"=";
        if(line.rfind(prefix,0)) continue;
        unsigned key=guide::ParseKey(line.substr(prefix.size()));
#ifdef WR_MUT_BINDING
        (void)key; (void)cfg;
#else
        if(key) cfg->*(b.member)=key;
#endif
        return true;
    }
    return false;
}
