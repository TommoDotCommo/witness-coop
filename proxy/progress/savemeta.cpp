// See savemeta.h.
#include "savemeta.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace wprog {
void FillProgress(const Data& d, const std::set<unsigned>& solved, unsigned doorsOpen, unsigned doorsTotal, SaveMeta* m) {
    m->areas.assign(d.areas.size(), MetaArea()); for (size_t i = 0; i < d.areas.size(); ++i) m->areas[i].name = d.areas[i];
    m->solved = m->total = 0;
    for (const auto& p : d.panels) {
        if (p.optional) continue;
        m->areas[p.area].total++; m->total++;
#ifndef WP_MUT_COUNT_ALL
        if (solved.count(p.id))
#endif
        { m->areas[p.area].solved++; m->solved++; }
    }
    std::vector<MetaArea> keep; for (auto& a : m->areas) if (a.total) keep.push_back(a); m->areas = keep;
    m->lasersTotal = (unsigned)d.lasers.size(); m->lasers = 0; for (unsigned l : d.lasers) if (solved.count(l)) ++m->lasers;
    m->doorsOpen = doorsOpen; m->doorsTotal = doorsTotal;
}

void MergePlayers(std::vector<MetaPlayer>* into, const std::vector<MetaPlayer>& earlier) {
#ifdef WP_MUT_NO_MERGE
    (void)into; (void)earlier; return;
#endif
    for (const auto& e : earlier) {
        bool found = false;
        for (auto& p : *into) if (p.name == e.name) { p.seconds += e.seconds; p.sessions += e.sessions; found = true; break; }
        if (!found) into->push_back(e);
    }
}

static std::string Clean(std::string s) { for (auto& c : s) if (c == '\t' || c == '\n' || c == '\r') c = ' '; return s; }
std::string FormatMeta(const SaveMeta& m) {
    std::ostringstream o; o << "coop-save 1\nsaved " << m.savedAt << "\nsession_seconds " << m.sessionSeconds << "\ntotal_seconds " << m.totalSeconds
        << "\nprogress " << m.solved << " " << m.total << "\nlasers " << m.lasers << " " << m.lasersTotal << "\ndoors " << m.doorsOpen << " " << m.doorsTotal << "\n";
    for (const auto& a : m.areas) { std::string n = a.name; for (auto& c : n) if (c == ' ') c = '_'; o << "area " << n << " " << a.solved << " " << a.total << "\n"; }
    for (const auto& p : m.players) o << "player\t" << p.id << "\t" << Clean(p.name) << "\t" << Clean(p.character) << "\t" << p.seconds << "\t" << p.sessions << "\n";
    return o.str();
}

bool ParseMeta(const std::string& text, SaveMeta* out) {
    SaveMeta m; std::istringstream in(text); std::string line; bool head = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line.rfind("player\t", 0) == 0) {
            std::vector<std::string> f; size_t pos = 0; for (;;) { size_t t = line.find('\t', pos); f.push_back(line.substr(pos, t == std::string::npos ? t : t - pos)); if (t == std::string::npos) break; pos = t + 1; }
            if (f.size() != 6) return false; MetaPlayer p; p.id = (unsigned)atoi(f[1].c_str()); p.name = f[2]; p.character = f[3]; p.seconds = (unsigned)atoi(f[4].c_str()); p.sessions = (unsigned)atoi(f[5].c_str()); m.players.push_back(p); continue;
        }
        std::istringstream ls(line); std::string k; ls >> k;
        if (k == "coop-save") { int v = 0; ls >> v; if (v != 1) return false; head = true; }
        else if (k == "saved") { std::getline(ls, m.savedAt); if (!m.savedAt.empty() && m.savedAt[0] == ' ') m.savedAt.erase(0, 1); }
        else if (k == "session_seconds") ls >> m.sessionSeconds; else if (k == "total_seconds") ls >> m.totalSeconds;
        else if (k == "progress") ls >> m.solved >> m.total; else if (k == "lasers") ls >> m.lasers >> m.lasersTotal; else if (k == "doors") ls >> m.doorsOpen >> m.doorsTotal;
        else if (k == "area") { MetaArea a; ls >> a.name >> a.solved >> a.total; for (auto& c : a.name) if (c == '_') c = ' '; m.areas.push_back(a); }
        else return false;
    }
    if (!head) return false; *out = m; return true;
}

std::string OneLine(const SaveMeta& m) {
    char b[200]; unsigned pct = m.total ? (m.solved * 100 + m.total / 2) / m.total : 0;
    std::string who; for (size_t i = 0; i < m.players.size(); ++i) who += (i ? " + " : "") + m.players[i].name;
    unsigned h = m.totalSeconds / 3600, mi = (m.totalSeconds % 3600) / 60;
    snprintf(b, sizeof b, "%u%% done, %u lasers, ", pct, m.lasers); std::string s = b; s += who.empty() ? "no co-op players recorded" : who;
    snprintf(b, sizeof b, " (%u h %u min)", h, mi); return s + b;
}
}  // namespace wprog
