// See progress.h.
#include "progress.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

namespace wprog {
static std::string Norm(const std::string& s) { std::string r; for (char c : s) if (isalnum((unsigned char)c)) r.push_back((char)tolower((unsigned char)c)); return r; }
int Data::AreaByName(const std::string& name) const { std::string n = Norm(name); for (size_t i = 0; i < areas.size(); ++i) if (Norm(areas[i]) == n) return (int)i; return -1; }
size_t Data::AreaCount(int area) const { size_t n = 0; for (const auto& p : panels) if (p.area == area && !p.optional) ++n; return n; }

bool Parse(const std::string& text, Data* out, std::string* err) {
    Data d; std::istringstream in(text); std::string line; int lineNo = 0;
    auto fail = [&](const std::string& m) { if (err) *err = "line " + std::to_string(lineNo) + ": " + m; return false; };
    while (std::getline(in, line)) {
        ++lineNo; if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line); std::string kind; ls >> kind;
        if (kind == "area") { int n; std::string name; if (!(ls >> n >> name) || n != (int)d.areas.size()) return fail("bad area"); for (auto& c : name) if (c == '_') c = ' '; d.areas.push_back(name); }
        else if (kind == "panel") {
            Panel p; std::string name; std::string idtxt; if (!(ls >> idtxt >> p.area >> name)) return fail("bad panel"); p.id = (unsigned)strtoul(idtxt.c_str(), nullptr, 16);
            if (p.area < 0 || p.area >= (int)d.areas.size()) return fail("panel in an unknown area");
            for (auto& c : name) if (c == '_') c = ' '; p.name = name;
            std::string t; while (ls >> t) p.needs.push_back((unsigned)strtoul(t.c_str(), nullptr, 16));
            if (d.index.count(p.id)) return fail("panel twice"); d.index[p.id] = d.panels.size(); d.panels.push_back(p);
        } else if (kind == "optional") { std::string idtxt; if (!(ls >> idtxt)) return fail("bad optional"); auto it = d.index.find((unsigned)strtoul(idtxt.c_str(), nullptr, 16)); if (it == d.index.end()) return fail("optional: unknown panel"); d.panels[it->second].optional = true;
        } else if (kind == "door") {
            std::string idtxt, alts; if (!(ls >> idtxt >> alts)) return fail("bad door"); Data::Door dr; dr.id = (unsigned)strtoul(idtxt.c_str(), nullptr, 16);
            std::vector<unsigned> cur; std::string tok;
            auto endTok = [&] { if (!tok.empty()) cur.push_back((unsigned)strtoul(tok.c_str(), nullptr, 16)); tok.clear(); };
            for (char ch : alts) { if (ch == '&') endTok(); else if (ch == '|') { endTok(); dr.opensWith.push_back(cur); cur.clear(); } else tok.push_back(ch); }
            endTok(); dr.opensWith.push_back(cur); d.doors.push_back(dr);
        } else if (kind == "laser") { std::string idtxt; int a; if (!(ls >> idtxt >> a)) return fail("bad laser"); d.lasers.push_back((unsigned)strtoul(idtxt.c_str(), nullptr, 16)); }
        else return fail("unknown line");
    }
    for (const auto& p : d.panels) for (unsigned n : p.needs) if (!d.index.count(n)) { lineNo = 0; return fail("a prerequisite is not a panel"); }
    *out = std::move(d); return true;
}
const Data& Builtin() { static Data d = [] { Data x; std::string e; if (!Parse(BuiltinText(), &x, &e)) x = Data(); return x; }(); return d; }

std::vector<unsigned> Complete(const Data& d, const std::vector<int>& areas, const std::vector<unsigned>& panels, bool withPrerequisites) {
    std::set<unsigned> want;
    for (int a : areas) for (const auto& p : d.panels) if (p.area == a && !p.optional) want.insert(p.id);
    for (unsigned id : panels) if (d.index.count(id)) want.insert(id);
#ifndef WP_MUT_NOPREREQ
    std::set<unsigned> all = want;
    if (withPrerequisites) for (unsigned id : want) for (unsigned n : d.panels[d.index.at(id)].needs) all.insert(n);
    want = all;
#endif
    std::vector<unsigned> out(want.begin(), want.end());
#ifndef WP_MUT_UNORDERED
    std::stable_sort(out.begin(), out.end(), [&](unsigned a, unsigned b) { return d.panels[d.index.at(a)].needs.size() < d.panels[d.index.at(b)].needs.size(); });      // a panel's prerequisites are a strict subset of its own: fewer first
#endif
    return out;
}

std::vector<unsigned> Lasers(const Data& d, int n) {
    std::set<unsigned> chosen; std::vector<unsigned> pool = d.lasers;
    auto cost = [&](unsigned l) { std::set<unsigned> s = chosen; s.insert(l); for (unsigned x : d.panels[d.index.at(l)].needs) s.insert(x); return s.size(); };
#ifdef WP_MUT_LASERS_FEW
    --n;
#endif
    for (int k = 0; k < n && !pool.empty(); ++k) {
        size_t best = 0;
#ifndef WP_MUT_LASERS_FIRST
        for (size_t i = 1; i < pool.size(); ++i) if (cost(pool[i]) < cost(pool[best]) || (cost(pool[i]) == cost(pool[best]) && pool[i] < pool[best])) best = i;
#endif
        unsigned l = pool[best]; pool.erase(pool.begin() + best); chosen.insert(l); for (unsigned x : d.panels[d.index.at(l)].needs) chosen.insert(x);
    }
    return Complete(d, {}, std::vector<unsigned>(chosen.begin(), chosen.end()));
}

std::vector<std::string> PresetNames() { return {"tutorial", "three-lasers", "seven-lasers", "all-lasers", "everything"}; }
std::string PresetSpec(const std::string& n) {
    if (n == "tutorial") return "area:Tutorial (Inside);area:Tutorial (Outside)";
   
    if (n == "three-lasers") return "lasers:3";
    if (n == "seven-lasers") return "lasers:7";
    if (n == "all-lasers") return "lasers:11";
    if (n == "everything") return "area:all";
    return "";
}

std::vector<unsigned> Resolve(const Data& d, const std::string& spec, std::string* err, bool withPrerequisites) {
    std::vector<int> areas; std::vector<unsigned> panels; std::set<unsigned> extra; size_t pos = 0;
    auto bad = [&](const std::string& m) { if (err) *err = m; return std::vector<unsigned>(); };
    while (pos <= spec.size()) {
        size_t e = spec.find(';', pos); if (e == std::string::npos) e = spec.size(); std::string tok = spec.substr(pos, e - pos); pos = e + 1;
        while (!tok.empty() && isspace((unsigned char)tok.front())) tok.erase(tok.begin());
        while (!tok.empty() && isspace((unsigned char)tok.back())) tok.pop_back();
        if (tok.empty()) continue;
        size_t c = tok.find(':'); std::string key = c == std::string::npos ? tok : tok.substr(0, c), val = c == std::string::npos ? "" : tok.substr(c + 1);
        if (key == "area") { if (Norm(val) == "all") { for (size_t i = 0; i < d.areas.size(); ++i) areas.push_back((int)i); continue; } int a = d.AreaByName(val); if (a < 0) return bad("unknown area '" + val + "'"); areas.push_back(a); }
        else if (key == "panel") { unsigned id = (unsigned)strtoul(val.c_str(), nullptr, 16); if (!d.index.count(id)) return bad("unknown panel '" + val + "'"); panels.push_back(id); }
        else if (key == "lasers") { int n = atoi(val.c_str()); if (n < 0 || n > (int)d.lasers.size()) return bad("lasers: 0 to " + std::to_string(d.lasers.size())); for (unsigned id : Lasers(d, n)) extra.insert(id); }
        else if (key == "preset") { std::string s = PresetSpec(val); if (s.empty()) return bad("unknown preset '" + val + "'"); std::string e2; auto r = Resolve(d, s, &e2); for (unsigned id : r) extra.insert(id); }
        else return bad("unknown choice '" + key + "'");
    }
    for (unsigned id : extra) panels.push_back(id);
    return Complete(d, areas, panels, withPrerequisites);
}
}  // namespace wprog
