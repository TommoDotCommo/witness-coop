// psolve: finds a line that solves a Witness panel, for the sweep bot. Game-free (T0): reads a description of the panel, prints a node list.
//
// Input (stdin, whitespace separated):
//   N M GX GY NDEC            nodes, edges, grid nodes across and up (0 when the panel is not a grid), decoration cells
//   N lines: flags            node flags (0x1 end, 0x2 start, 0x20 dot, 0x100 blue, 0x200 yellow, ...)
//   M lines: a b              edges (node indices)
//   NDEC lines: value         decoration of each cell, row 0 at the bottom: shape<<16 | type | colour (type 0x100 square, 0x200 star,
//                             0x400 poly (+0x1000 any rotation, +0x2000 negative), 0x500 eraser, 0x600 triangle (count in shape))
//   R then R ints             mirror node of each node (symmetry panels), or R = 0
// Output: one line, the nodes of the line from a start to an end, or "none". Exit code 0 when a line was found.
//
// Grid nodes are numbered row * GX + col (row 0 at the bottom); the cell at (row, col) lies between nodes (row, col) .. (row+1, col+1) and has
// index row * (GX - 1) + col. Nodes beyond GX * GY (the start and end stubs) belong to no cell.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#include <functional>
#include <cstdint>
#include <unordered_map>
using namespace std;

static int N, M, GX, GY, NDEC, CW, CH;
static vector<unsigned> flags, cellv;
static vector<vector<int>> adj;
static vector<int> eu, ev;
static vector<int> mirror;
static vector<int> eid;      // edge id lookup: eid[a * N + b]
static vector<int> midNode;  // when an edge of the grid is split by a node (a dot or gap marker on it): the node, midNode[a * N + b], else -1
static const char* MUTANT = getenv("PSOLVE_MUTANT");      // test mutants: a rule that is switched off, which the T0 tests must notice
static bool mut(const char* n) { return MUTANT && !strcmp(MUTANT, n); }
static const unsigned T_SQUARE = 0x100, T_STAR = 0x200, T_POLY = 0x400, T_ERASER = 0x500, T_TRI = 0x600;

static int ctype(unsigned v) { return (v & 0xF00); }
static int ccol(unsigned v) { return v & 0xFF; }
static bool isPoly(unsigned v) { return ctype(v) == T_POLY; }

// ---- polyomino tiling -------------------------------------------------------------------------------------------------------------------
typedef vector<pair<int, int>> Shape;
static Shape shapeOf(unsigned mask) { Shape s; for (int k = 0; k < 16; k++) if (mask >> k & 1) s.push_back({k / 4, k % 4}); return s; }
static Shape norm(Shape s) { int mr = 99, mc = 99; for (auto& p : s) { mr = min(mr, p.first); mc = min(mc, p.second); } for (auto& p : s) { p.first -= mr; p.second -= mc; } sort(s.begin(), s.end()); return s; }
static Shape rot(const Shape& s) { Shape r; for (auto& p : s) r.push_back({p.second, -p.first}); return norm(r); }
static vector<Shape> orientations(unsigned mask, bool rotate) {
    vector<Shape> out; Shape s = norm(shapeOf(mask));
    for (int k = 0; k < (rotate ? 4 : 1); k++) { bool dup = false; for (auto& o : out) if (o == s) dup = true; if (!dup) out.push_back(s); s = rot(s); }
    return out;
}

// cells of a region, with the polyominoes to place (positive) and subtract (negative): the region must be covered exactly once by the positives
// minus... the Witness rule is a sum: positive shapes cover cells (+1), negative shapes (-1), and every cell must end at exactly 1. The cells may
// be covered by shapes sticking outside the region only when a negative shape cancels them, which this search does allow (offsets range over the
// bounding box plus margin).
static bool tile(const vector<pair<int, int>>& cells, vector<unsigned> pos, vector<unsigned> neg, vector<bool> posRot, vector<bool> negRot) {
    // a negative shape cancels a positive one of the same shape (rotations count when either may turn): cancel such pairs first; whatever is left must tile
    for (size_t j = 0; j < neg.size();) {
        bool hit = false;
        for (size_t i = 0; i < pos.size() && !hit; i++) {
            bool same = false; for (auto& o : orientations(pos[i] & 0xFFFF, true)) if (o == norm(shapeOf(neg[j] & 0xFFFF))) same = true;
            bool exact = false; for (auto& o : orientations(pos[i] & 0xFFFF, posRot[i] || negRot[j])) if (o == norm(shapeOf(neg[j] & 0xFFFF))) exact = true;
            if (same && exact) { pos.erase(pos.begin() + i); posRot.erase(posRot.begin() + i); hit = true; }
        }
        if (hit) { neg.erase(neg.begin() + j); negRot.erase(negRot.begin() + j); } else j++;
    }
    if (pos.empty() && neg.empty()) return true;
    int area = 0; for (auto m : pos) area += __builtin_popcount(m & 0xFFFF); int narea = 0; for (auto m : neg) narea += __builtin_popcount(m & 0xFFFF);
    if (area - narea != (int)cells.size()) return false;
    if (pos.empty() && neg.empty()) return true;
    if (pos.empty()) return false;
    int minr = 99, maxr = -99, minc = 99, maxc = -99; for (auto& c : cells) { minr = min(minr, c.first); maxr = max(maxr, c.first); minc = min(minc, c.second); maxc = max(maxc, c.second); }
    int margin = neg.empty() ? 0 : 4;      // shapes may stick out of the region only where a negative shape cancels them
    int R = maxr - minr + 1 + 2 * margin, C = maxc - minc + 1 + 2 * margin, off = margin;
    vector<int> target(R * C, 0), cover(R * C, 0);
    for (auto& c : cells) target[(c.first - minr + off) * C + (c.second - minc + off)] = 1;
    vector<vector<Shape>> po, no; for (size_t i = 0; i < pos.size(); i++) po.push_back(orientations(pos[i] & 0xFFFF, posRot[i]));
    for (size_t i = 0; i < neg.size(); i++) no.push_back(orientations(neg[i] & 0xFFFF, negRot[i]));
    vector<char> used(pos.size(), 0);
    // positives: exact cover of `target` (the region plus whatever the negatives cancel), always covering the first cell that is still short
    function<bool(int)> placePos = [&](int left) -> bool {
        if (left == 0) { for (int i = 0; i < R * C; i++) if (cover[i] != target[i]) return false; return true; }
        int first = -1; for (int i = 0; i < R * C; i++) if (cover[i] < target[i]) { first = i; break; }
        if (first < 0) return false;
        int fr = first / C, fc = first % C;
        vector<pair<unsigned, bool>> tried;
        for (size_t k = 0; k < pos.size(); k++) {
            if (used[k]) continue; bool dup = false; for (auto& t : tried) if (t.first == (pos[k] & 0xFFFF) && t.second == posRot[k]) dup = true; if (dup) continue; tried.push_back({pos[k] & 0xFFFF, (bool)posRot[k]});
            for (auto& sh : po[k]) {
                int r0 = fr - sh[0].first, c0 = fc - sh[0].second;      // the shape's first cell (smallest row, then column) sits on `first`
                bool ok = true; for (auto& p : sh) { int r = r0 + p.first, c = c0 + p.second; if (r < 0 || c < 0 || r >= R || c >= C || cover[r * C + c] + 1 > target[r * C + c]) { ok = false; break; } }
                if (!ok) continue;
                for (auto& p : sh) cover[(r0 + p.first) * C + c0 + p.second]++;
                used[k] = 1; bool done = placePos(left - 1); used[k] = 0;
                for (auto& p : sh) cover[(r0 + p.first) * C + c0 + p.second]--;
                if (done) return true;
            }
        }
        return false;
    };
    // negatives first: every placement of every negative shape (they are few); each raises the target of the cells it covers
    function<bool(size_t)> placeNeg = [&](size_t k) -> bool {
        if (k == neg.size()) return placePos((int)pos.size());
        for (auto& sh : no[k]) for (int r0 = 0; r0 < R; r0++) for (int c0 = 0; c0 < C; c0++) {
            bool ok = true; for (auto& p : sh) { int r = r0 + p.first, c = c0 + p.second; if (r >= R || c >= C) { ok = false; break; } }
            if (!ok) continue;
            for (auto& p : sh) target[(r0 + p.first) * C + c0 + p.second]++;
            bool done = placeNeg(k + 1);
            for (auto& p : sh) target[(r0 + p.first) * C + c0 + p.second]--;
            if (done) return true;
        }
        return false;
    };
    return placeNeg(0);
}

// ---- region checking --------------------------------------------------------------------------------------------------------------------
struct Elem { int cell; unsigned v; };
static bool regionOk(const vector<int>& cellsIdx, const vector<unsigned char>& erasedMask);

static bool checkNoErasers(const vector<Elem>& els, const vector<int>& cellsIdx) {
    // squares: one colour; stars: exactly one partner of the colour; polys: tile
    int sqCol = -1;
    if (!mut("squares")) for (auto& e : els) if (ctype(e.v) == T_SQUARE) { if (sqCol < 0) sqCol = ccol(e.v); else if (sqCol != ccol(e.v)) return false; }
    if (!mut("stars")) for (auto& e : els) if (ctype(e.v) == T_STAR) {
        int c = ccol(e.v), n = 0; for (auto& o : els) if (&o != &e && ccol(o.v) == c && ctype(o.v) != T_ERASER) n++;
        if (n != 1) return false;
    }
    vector<unsigned> pos, neg; vector<bool> pr, nr;
    for (auto& e : els) if (isPoly(e.v) || (e.v & 0x2000 && (e.v & 0xF00) == T_POLY)) {
        unsigned mask = e.v >> 16; if (e.v & 0x2000) { neg.push_back(mask); nr.push_back(e.v & 0x1000); } else { pos.push_back(mask); pr.push_back(e.v & 0x1000); }
    }
    if (!mut("poly") && (!pos.empty() || !neg.empty())) {
        vector<pair<int, int>> cs; for (int ci : cellsIdx) cs.push_back({ci / CW, ci % CW});
        if (!tile(cs, pos, neg, pr, nr)) return false;
    }
    return true;
}

static bool regionValidRaw(const vector<int>& cellsIdx, const vector<int>& triOk, int spentOnDots);
static unordered_map<unsigned long long, char> g_cacheLo;      // key: the region's cells (<= 64 of them), the cells whose triangle is wrong, the erasers spent on dots
struct Key { unsigned long long a, b; int c; bool operator==(const Key& o) const { return a == o.a && b == o.b && c == o.c; } };
struct KeyHash { size_t operator()(const Key& k) const { return (size_t)(k.a * 0x9E3779B97F4A7C15ULL ^ (k.b + 0x7F4A7C15ULL) * 0xC2B2AE3D27D4EB4FULL ^ (unsigned long long)k.c); } };
static unordered_map<Key, char, KeyHash> g_cache;
static bool regionValid(const vector<int>& cellsIdx, const vector<int>& triOk, int spentOnDots = 0) {
    if (NDEC > 64) return regionValidRaw(cellsIdx, triOk, spentOnDots);
    Key k = {0, 0, spentOnDots}; for (int ci : cellsIdx) { k.a |= 1ULL << ci; if (ctype(cellv[ci]) == T_TRI && !triOk[ci]) k.b |= 1ULL << ci; }
    auto it = g_cache.find(k); if (it != g_cache.end()) return it->second;
    bool r = regionValidRaw(cellsIdx, triOk, spentOnDots); g_cache[k] = r; return r;
}
static bool regionValidRaw(const vector<int>& cellsIdx, const vector<int>& triOk, int spentOnDots) {
    vector<Elem> els; vector<int> erasers;
    for (int ci : cellsIdx) { unsigned v = cellv[ci]; if (!v) continue; if (ctype(v) == T_ERASER) { if (spentOnDots > 0) { spentOnDots--; continue; } erasers.push_back((int)els.size()); } if (ctype(v) == T_TRI) continue; els.push_back({ci, v}); }
    // triangles are checked on the path; an erased triangle's failure is forgiven, so they take part as elements with a precomputed verdict
    vector<Elem> triEls; vector<int> triBad;
    for (int ci : cellsIdx) if (ctype(cellv[ci]) == T_TRI) { triEls.push_back({ci, cellv[ci]}); triBad.push_back(!triOk[ci]); }
    int ne = (int)erasers.size();
    if (ne == 0) { for (int b : triBad) if (b) return false; return checkNoErasers(els, cellsIdx); }
    // erasers: choose for each eraser one other element to erase (an eraser may erase another eraser); try all assignments
    vector<int> all; for (size_t i = 0; i < els.size(); i++) all.push_back((int)i);
    int T = (int)triEls.size(); int total = (int)els.size() + T;
    vector<char> erased(total, 0);
    function<bool(int)> go = [&](int k) -> bool {
        if (k == ne) {
            vector<Elem> rest; for (size_t i = 0; i < els.size(); i++) if (!erased[i]) { if (ctype(els[i].v) == T_ERASER) continue; rest.push_back(els[i]); }
            // erasers that erased nothing remain: they are valid only if every eraser erased something (a spare eraser is wrong)
            for (int t = 0; t < T; t++) if (!erased[els.size() + t] && triBad[t]) return false;
            return checkNoErasers(rest, cellsIdx);
        }
        for (int i = 0; i < total; i++) {
            if (erased[i]) continue; if (i < (int)els.size() && i == erasers[k]) continue;
            // an eraser cannot be erased by itself; mark
            erased[i] = 1; if (go(k + 1)) { erased[i] = 0; return true; } erased[i] = 0;
        }
        return false;
    };
    return go(0);
}

// ---- path search ------------------------------------------------------------------------------------------------------------------------
static vector<char> usedEdge, visited, vis1, vis2; static vector<int> path;
static bool sym = false;
static vector<int> dots;                 // nodes that must be visited
static int nodeCellRow(int n) { return n / GX; }
static bool gridPanel;

static bool evaluate() {
    // dots that the line does not satisfy: they can only be erased by an eraser whose region touches them
    vector<int> unmet;
    for (int d : dots) {
        bool ok; if (!sym) ok = visited[d]; else { unsigned f = flags[d]; ok = (f & 0x100) ? vis1[d] : (f & 0x200) ? vis2[d] : (vis1[d] || vis2[d]); }
        if (mut("dots")) ok = true;
        if (!ok) unmet.push_back(d);
    }
    bool anyEraser = false; if (gridPanel) for (unsigned v : cellv) if (ctype(v) == T_ERASER) { anyEraser = true; break; }
    if (!unmet.empty() && !anyEraser) return false;
    if (!gridPanel || NDEC == 0) return unmet.empty();
    // cells separated by used edges: cell (r,c) and (r,c+1) are separated by the edge between node (r,c+1) and (r+1,c+1); (r,c) and (r+1,c) by (r+1,c)-(r+1,c+1)
    auto edgeUsed = [&](int a, int b) { if (a < 0 || b < 0) return false; int e = eid[a * N + b]; if (e >= 0) return (bool)usedEdge[e]; int m = midNode[a * N + b]; return m >= 0 && visited[m]; };
    vector<int> triOk(NDEC, 1);
    for (int r = 0; r < CH; r++) for (int c = 0; c < CW; c++) {
        int ci = r * CW + c; unsigned v = cellv[ci]; if (ctype(v) != T_TRI) continue;
        int n00 = r * GX + c, n01 = r * GX + c + 1, n10 = (r + 1) * GX + c, n11 = (r + 1) * GX + c + 1, cnt = 0;
        cnt += edgeUsed(n00, n01) + edgeUsed(n10, n11) + edgeUsed(n00, n10) + edgeUsed(n01, n11);
        triOk[ci] = mut("triangles") || cnt == (int)(v >> 16);
    }
    vector<int> comp(NDEC, -1); int nc = 0; vector<vector<int>> regs;
    for (int s = 0; s < NDEC; s++) {
        if (comp[s] >= 0) continue; vector<int> st = {s}, cells; comp[s] = nc;
        while (!st.empty()) {
            int ci = st.back(); st.pop_back(); cells.push_back(ci); int r = ci / CW, c = ci % CW;
            if (c + 1 < CW && comp[ci + 1] < 0 && !edgeUsed((r) * GX + c + 1, (r + 1) * GX + c + 1)) { comp[ci + 1] = nc; st.push_back(ci + 1); }
            if (c > 0 && comp[ci - 1] < 0 && !edgeUsed(r * GX + c, (r + 1) * GX + c)) { comp[ci - 1] = nc; st.push_back(ci - 1); }
            if (r + 1 < CH && comp[ci + CW] < 0 && !edgeUsed((r + 1) * GX + c, (r + 1) * GX + c + 1)) { comp[ci + CW] = nc; st.push_back(ci + CW); }
            if (r > 0 && comp[ci - CW] < 0 && !edgeUsed(r * GX + c, r * GX + c + 1)) { comp[ci - CW] = nc; st.push_back(ci - CW); }
        }
        regs.push_back(cells); nc++;
    }
    // erasers available per region, and the regions each unmet dot touches
    vector<int> cap(nc, 0); for (int ci = 0; ci < NDEC; ci++) if (ctype(cellv[ci]) == T_ERASER) cap[comp[ci]]++;
    vector<vector<int>> touch;
    for (int d : unmet) {
        vector<int> rs; auto addCell = [&](int r, int c) { if (r >= 0 && c >= 0 && r < CH && c < CW) { int g = comp[r * CW + c]; if (find(rs.begin(), rs.end(), g) == rs.end()) rs.push_back(g); } };
        if (d < GX * GY) { int r = d / GX, c = d % GX; addCell(r - 1, c - 1); addCell(r - 1, c); addCell(r, c - 1); addCell(r, c); }
        else if (adj[d].size() == 2) {
            int a = adj[d][0], b = adj[d][1]; if (a < GX * GY && b < GX * GY) {
                int ar = a / GX, ac = a % GX, br = b / GX, bc = b % GX;
                if (ar == br) { int c = min(ac, bc); addCell(ar - 1, c); addCell(ar, c); } else { int r = min(ar, br); addCell(r, ac - 1); addCell(r, ac); }
            }
        }
        touch.push_back(rs);
    }
    vector<int> spent(nc, 0);
    function<bool(size_t)> assign = [&](size_t k) -> bool {
        if (k == unmet.size()) { for (int g = 0; g < nc; g++) if (!regionValid(regs[g], triOk, spent[g])) return false; return true; }
        for (int g : touch[k]) if (spent[g] < cap[g]) { spent[g]++; if (assign(k + 1)) return true; spent[g]--; }
        return false;
    };
    return assign(0);
}

static vector<int> ends, starts; static vector<char> isEnd;
static vector<int> dseq; static int seqPos = 0;      // sound dots (flags 0x2000 / 0x4000 / 0x8000 = pitch 1 / 2 / 3): the line must meet them in the order of the panel's own sequence
static int pitchOf(int n) { unsigned f = flags[n]; return (f & 0x2000) ? 1 : (f & 0x4000) ? 2 : (f & 0x8000) ? 3 : 0; }
static long long nodesSeen = 0, LIMIT = 400000000LL;
static bool found = false; static vector<int> solution;

static void dfs(int v) {
    if (found || nodesSeen > LIMIT) return; nodesSeen++;
    if (isEnd[v] && (!sym || (mirror[v] >= 0 && mirror[v] != v && isEnd[mirror[v]]))) { if ((dseq.empty() || seqPos == (int)dseq.size()) && evaluate()) { found = true; solution = path; } return; }
    for (int w : adj[v]) {
        if (visited[w]) continue; int e = eid[v * N + w];
        int mv = sym ? mirror[v] : -1, mw = sym ? mirror[w] : -1, e2 = -1;
        if (sym) {      // the mirror line makes the mirrored move: its node and edge must exist, be free, and not be the node we step on
            if (mw < 0 || mv < 0 || mw == w || visited[mw]) continue; e2 = eid[mv * N + mw]; if (e2 < 0 || e2 == e || usedEdge[e2]) continue;
            visited[mw] = 1; vis2[mw] = 1; usedEdge[e2] = 1;
        }
        if (sym) vis1[w] = 1;
        // triangle pruning: no cell may get more used edges than its count
        visited[w] = 1; usedEdge[e] = 1; path.push_back(w);
        bool ok = true; int pw = dseq.empty() ? 0 : pitchOf(w), seqStep = 0;
        if (pw && !mut("pitch")) { if (seqPos < (int)dseq.size() && dseq[seqPos] == pw) { seqPos++; seqStep = 1; } else ok = false; }
        if (gridPanel && NDEC && v < GX * GY && w < GX * GY) {
            // the two cells beside this edge
            int vr = v / GX, vc = v % GX, wr = w / GX, wc = w % GX;
            int cells2[2][2] = {{-1, -1}, {-1, -1}};
            if (vr == wr) { int c = min(vc, wc); cells2[0][0] = vr - 1; cells2[0][1] = c; cells2[1][0] = vr; cells2[1][1] = c; }
            else { int r = min(vr, wr); cells2[0][0] = r; cells2[0][1] = vc - 1; cells2[1][0] = r; cells2[1][1] = vc; }
            for (auto& cc : cells2) {
                int r = cc[0], c = cc[1]; if (r < 0 || c < 0 || r >= CH || c >= CW) continue; unsigned cv = cellv[r * CW + c]; if (ctype(cv) != T_TRI) continue;
                int n00 = r * GX + c, n01 = r * GX + c + 1, n10 = (r + 1) * GX + c, n11 = (r + 1) * GX + c + 1, cnt = 0;
                auto eu_ = [&](int a, int b) { int e2 = eid[a * N + b]; if (e2 >= 0) return (bool)usedEdge[e2]; int m = midNode[a * N + b]; return m >= 0 && visited[m]; };
                cnt = eu_(n00, n01) + eu_(n10, n11) + eu_(n00, n10) + eu_(n01, n11);
                if (cnt > (int)(cv >> 16)) { ok = false; break; }
            }
        }
        if (ok) dfs(w);
        path.pop_back(); usedEdge[e] = 0; visited[w] = 0; seqPos -= seqStep;
        if (sym) { visited[mw] = 0; vis2[mw] = 0; usedEdge[e2] = 0; vis1[w] = 0; }
        if (found) return;
    }
}

int main(int argc, char** argv) {
    if (scanf("%d %d %d %d %d", &N, &M, &GX, &GY, &NDEC) != 5) return 2;
    flags.resize(N); for (auto& f : flags) scanf("%u", &f);
    adj.assign(N, {}); eu.resize(M); ev.resize(M); eid.assign((size_t)N * N, -1);
    for (int i = 0; i < M; i++) { scanf("%d %d", &eu[i], &ev[i]); adj[eu[i]].push_back(ev[i]); adj[ev[i]].push_back(eu[i]); eid[eu[i] * N + ev[i]] = eid[ev[i] * N + eu[i]] = i; }
    midNode.assign((size_t)N * N, -1);
    for (int m = 0; m < N; m++) if (adj[m].size() == 2) { int a = adj[m][0], b = adj[m][1]; if (eid[a * N + b] < 0) midNode[a * N + b] = midNode[b * N + a] = m; }
    cellv.resize(NDEC); for (auto& v : cellv) scanf("%u", &v);
    int R = 0; scanf("%d", &R); mirror.resize(R); for (auto& m : mirror) scanf("%d", &m);
    { int K = 0; if (scanf("%d", &K) == 1) { dseq.resize(K); for (auto& d : dseq) scanf("%d", &d); } }
    gridPanel = GX > 1 && GY > 1; CW = GX - 1; CH = GY - 1;
    if (gridPanel && NDEC != CW * CH) { gridPanel = NDEC == 0 ? gridPanel : false; }
    usedEdge.assign(M, 0); visited.assign(N, 0); vis1.assign(N, 0); vis2.assign(N, 0); isEnd.assign(N, 0);
    sym = R == N;
    for (int i = 0; i < N; i++) { if ((flags[i] & 1)) { ends.push_back(i); isEnd[i] = 1; } if ((flags[i] & 2) && !(flags[i] & 1)) starts.push_back(i); if (flags[i] & 0x20) dots.push_back(i); }
    if (argc > 1) LIMIT = atoll(argv[1]);
    for (int s : starts) {
        if (sym && (mirror[s] < 0 || mirror[s] == s)) continue;
        visited[s] = 1; path = {s}; if (sym) { vis1[s] = 1; visited[mirror[s]] = 1; vis2[mirror[s]] = 1; }
        dfs(s); visited[s] = 0; if (sym) { vis1[s] = 0; visited[mirror[s]] = 0; vis2[mirror[s]] = 0; }
        if (found) break;
    }
    if (!found) { printf("none\n"); fprintf(stderr, "searched %lld nodes\n", nodesSeen); return 1; }
    for (size_t i = 0; i < solution.size(); i++) printf("%d%c", solution[i], i + 1 < solution.size() ? ',' : '\n');
    fprintf(stderr, "searched %lld nodes\n", nodesSeen);
    return 0;
}
