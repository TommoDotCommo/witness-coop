// The variant third_party/witness-randomizer (NewSoupVi's Witness Random Puzzle Generator for Archipelago): Sigma's Normal and Expert, and Variety.
// Its files are compiled in namespace v_nsv (variant.h); this is its glue.
#include "variant_prelude.h"
#include "../../common/wpuzzle.h"
#include "../../common/wpuzzle_panels.h"
namespace v_nsv {
#include "../../../third_party/witness-randomizer/src/Randomizer.h"
#include "../../../third_party/witness-randomizer/src/Panel.h"
#include "../../../third_party/witness-randomizer/src/Generate.h"
#include "../../../third_party/witness-randomizer/src/Special.h"
#include "../../../third_party/witness-randomizer/src/Watchdog.h"
#include "../../../third_party/witness-randomizer/src/Random.h"

// Upstream keeps state in statics between runs (one run per process there). A second run in the same game (after a save is loaded) must start
// from the same state as the first, or it would make other puzzles than a partner's first run. Panel and Generate name these two classes as
// friends (upstream's panel test and Archipelago's watchdog, neither of which is built here), which lets the reset reach their private statics
// without changing upstream's files.
class PanelExtractionTests { public: static void Reset() { Panel::generatedPanels.clear(); Panel::arrowPuzzles.clear(); }
    static void WriteCustom(int id, const wpz::Puzzle& p);      // (below: the player's own puzzles reach the panel's private grid the same way)
    static wpz::Puzzle ReadCustom(int id);
};
class APWatchdog { public: static void Reset() { Generate::_SHAPEDIRECTIONS.clear(); } };

std::mt19937 Random::gen = std::mt19937(0);        // upstream's Random.cpp seeds it from the clock; every run here seeds it from the Spec
static std::vector<Watchdog*> g_started;
#ifndef WR_MUT_WATCHDOG_THREAD
void Watchdog::start() { g_started.push_back(this); }      // upstream: a thread of its own (Watchdog.cpp, left out by WITNESS_COOP)
#else
void Watchdog::start() {}      // mutant: the watchdogs are lost
#endif
void Watchdog::run() {}

static std::vector<::wrand::Dog> Run(const ::wrand::Spec& spec) {
    g_started.clear();
#ifndef WR_MUT_NO_RESET
    PanelExtractionTests::Reset(); APWatchdog::Reset(); Point::pillarWidth = 0; Special::correctShapesById.clear();
#endif
    Random::seed((int)(spec.seed & 0x7FFFFFFFu));
    Randomizer rz;
    rz.seed = (int)(spec.seed & 0x7FFFFFFFu); rz.seedIsRNG = false; rz.colorblind = spec.colorblind; rz.doubleMode = spec.shuffle;
    if (spec.mode == ::wrand::kNormal) rz.GenerateNormal();
    else if (spec.mode == ::wrand::kExpert) rz.GenerateHard();
    else if (spec.mode == ::wrand::kVariety) rz.GenerateVariety();
    else throw std::logic_error("v_nsv: not one of its modes");
    if (spec.fast) rz.AdjustSpeed();
    std::vector<::wrand::Dog> dogs;
    for (Watchdog* w : g_started) dogs.push_back(WRAND_DOG(w));
    return dogs;
}

// The player's own puzzles: each written into its panel as the generator writes its own (Panel::Write: the grid's geometry, symmetry data, gaps,
// dots, decorations and their colours, the style flags the game checks the line with). The game then checks a drawn line by its own rules.
static int Flags(const wpz::Puzzle& p, int x, int y) {
    const wpz::Cell& c = p.at(x, y); int v = 0;
    if (wpz::Puzzle::isCell(x, y)) {
        if (!c.symbol()) return 0;
        int colour = c.colour & 0xF;
        switch (c.kind) {
        case wpz::kSquare: return Decoration::Stone | colour;
        case wpz::kStar: return Decoration::Star | colour;
        case wpz::kNega: return Decoration::Eraser | colour;
        case wpz::kTriangle: return (Decoration::Triangle | (c.count << 16)) | colour;
        default: break;
        }
        // a polyomino: the game's bits are column + 4 * (rows up from the bottom of the shape); his are column * 4 + row down from the top
        int minc = 4, maxr = -1, minr = 4;
        for (int cx = 0; cx < 4; cx++) for (int cy = 0; cy < 4; cy++) if (wpz::PolySet(c.shape, cx, cy)) { minc = std::min(minc, cx); maxr = std::max(maxr, cy); minr = std::min(minr, cy); }
        int bits = 0;
#ifndef WR_MUT_SHAPE_FLIPPED
        for (int cx = 0; cx < 4; cx++) for (int cy = 0; cy < 4; cy++) if (wpz::PolySet(c.shape, cx, cy)) bits |= 1 << ((cx - minc) + 4 * (maxr - cy));
#else
        for (int cx = 0; cx < 4; cx++) for (int cy = 0; cy < 4; cy++) if (wpz::PolySet(c.shape, cx, cy)) bits |= 1 << ((cx - minc) + 4 * (cy - minr));      // mutant: upside down
#endif
        v = Decoration::Poly | (bits << 16) | colour;
        if (c.shape & wpz::kRotBit) v |= Decoration::Can_Rotate;
        if (c.kind == wpz::kYlop) v |= Decoration::Negative;
        return v;
    }
    if (c.gap == wpz::kGapFull) return OPEN;
    bool corner = x % 2 == 0 && y % 2 == 0;
    int where = corner ? IntersectionFlags::INTERSECTION : (x % 2 ? IntersectionFlags::ROW : IntersectionFlags::COLUMN);
    if (c.gap == wpz::kGapBreak) v |= Decoration::Gap | where;
    if (c.dot) {
        v |= Decoration::Dot | where;
        if (c.dot == wpz::kDotBlue) v |= IntersectionFlags::DOT_IS_BLUE;
        if (c.dot == wpz::kDotYellow) v |= IntersectionFlags::DOT_IS_ORANGE;
        if (c.dot == wpz::kDotInvisible) v |= IntersectionFlags::DOT_IS_INVISIBLE;
    }
    return v;
}
// The panel reader (Panel::Read) needs a grid: on a panel that is not one (the first two Tutorial panels, a straight line and a bend) it divides by
// zero and indexes far outside its arrays (found in the game: both games crashed). Such a panel takes the frame of a grid panel of the Tutorial
// (0x00293); its own id and style flags are kept, and everything else is written from the puzzle.
static bool IsGridPanel(int id) {
    Memory* m = Memory::get(); int gx = m->ReadPanelData<int>(id, GRID_SIZE_X), gy = m->ReadPanelData<int>(id, GRID_SIZE_Y), n = m->ReadPanelData<int>(id, NUM_DOTS);
    return gx >= 2 && gy >= 2 && gx <= 16 && gy <= 16 && n >= gx * gy && !m->ReadPanelData<int>(id, IS_CYLINDER);
}
void PanelExtractionTests::WriteCustom(int id, const wpz::Puzzle& p) {
#ifndef WR_MUT_READ_ANY_PANEL
    bool grid = IsGridPanel(id);
#else
    bool grid = true;      // mutant: every panel is read as it is
#endif
    Panel panel(grid ? id : 0x00293);
    if (!grid) { panel.id = id; panel._style = Memory::get()->ReadPanelData<int>(id, STYLE_FLAGS); panel.symmetry = Panel::Symmetry::None; }
    Point::pillarWidth = 0;
    panel.Resize(p.width(), p.height());
    for (int x = 0; x < p.width(); x++) for (int y = 0; y < p.height(); y++) panel._grid[x][y] = Flags(p, x, y);
    panel._startpoints.clear(); panel._endpoints.clear();
    for (int x = 0; x < p.width(); x++) for (int y = 0; y < p.height(); y++) {
        const wpz::Cell& c = p.at(x, y);
        if (c.start) panel._startpoints.push_back(Point(x, y));
        if (c.end) {
            Endpoint::Direction d = c.end == wpz::kEndLeft ? Endpoint::Direction::LEFT : c.end == wpz::kEndRight ? Endpoint::Direction::RIGHT
                                  : c.end == wpz::kEndTop ? Endpoint::Direction::UP : Endpoint::Direction::DOWN;
            panel._endpoints.emplace_back(Endpoint(x, y, d, IntersectionFlags::ENDPOINT | (d == Endpoint::Direction::UP || d == Endpoint::Direction::DOWN ? IntersectionFlags::COLUMN : IntersectionFlags::ROW)));
        }
    }
    panel.symmetry = p.sym == wpz::kSymX ? Panel::Symmetry::Vertical : p.sym == wpz::kSymY ? Panel::Symmetry::Horizontal : p.sym == wpz::kSymXY ? Panel::Symmetry::Rotational : Panel::Symmetry::None;
    panel.colorMode = Panel::ColorMode::WriteColors;
    if (p.sym != wpz::kSymNone) {      // two lines of the symmetry panels' colours (as the generator's Write2Color, from Symmetry Island's 0x0007C)
        Memory* memory = Memory::get();
        memory->WritePanelData(id, PATTERN_POINT_COLOR_A, memory->ReadPanelData<Color>(0x0007C, PATTERN_POINT_COLOR_A));
        memory->WritePanelData(id, PATTERN_POINT_COLOR_B, memory->ReadPanelData<Color>(0x0007C, PATTERN_POINT_COLOR_B));
        memory->WritePanelData(id, REFLECTION_PATH_COLOR, memory->ReadPanelData<Color>(0x0007C, PATTERN_POINT_COLOR_B));
        memory->WritePanelData(id, ACTIVE_COLOR, memory->ReadPanelData<Color>(0x0007C, PATTERN_POINT_COLOR_A));
        // and its colours of the two solved lines: a panel that is not a symmetry panel kept its own (on the Tutorial a pale yellow and a green,
        // seen in the game over a blue and a yellow dot)
        memory->WritePanelData(id, SUCCESS_COLOR_A, memory->ReadPanelData<Color>(0x0007C, SUCCESS_COLOR_A));
        memory->WritePanelData(id, SUCCESS_COLOR_B, memory->ReadPanelData<Color>(0x0007C, SUCCESS_COLOR_B));
    }
    panel.Write(id);
}
// A panel read back into the editor's model, by the generator's own reader (Panel::Read): what the game holds, for the tests.
wpz::Puzzle PanelExtractionTests::ReadCustom(int id) {
    Panel panel(id);
    wpz::Puzzle p = wpz::NewPuzzle((panel._width - 1) / 2, (panel._height - 1) / 2);
    for (auto& c : p.g) { c.start = false; c.end = wpz::kEndNone; }
    for (int x = 0; x < p.width() && x < panel._width; x++) for (int y = 0; y < p.height() && y < panel._height; y++) {
        int v = panel._grid[x][y]; wpz::Cell& c = p.at(x, y);
        if (wpz::Puzzle::isCell(x, y)) {
            int t = v & 0x700; c.colour = (uint8_t)(v & 0xF);
            if (t == Decoration::Stone) c.kind = wpz::kSquare; else if (t == Decoration::Star) c.kind = wpz::kStar; else if (t == Decoration::Eraser) c.kind = wpz::kNega;
            else if (t == Decoration::Triangle) { c.kind = wpz::kTriangle; c.count = (uint8_t)((v >> 16) & 0xF); }
            else if (t == Decoration::Poly) {
                c.kind = (v & Decoration::Negative) ? wpz::kYlop : wpz::kPoly;
                int bits = (v >> 16) & 0xFFFF, maxUp = 0;
                for (int b = 0; b < 16; b++) if (bits >> b & 1) maxUp = std::max(maxUp, b / 4);
                for (int b = 0; b < 16; b++) if (bits >> b & 1) c.shape |= wpz::PolyMask(b % 4, maxUp - b / 4);
                if (v & Decoration::Can_Rotate) c.shape |= wpz::kRotBit;
            } else c.colour = 0;
            continue;
        }
        if (v == OPEN) { c.gap = wpz::kGapFull; continue; }
        if (v & Decoration::Gap) c.gap = wpz::kGapBreak;
        if (v & IntersectionFlags::DOT) c.dot = (v & IntersectionFlags::DOT_IS_BLUE) ? wpz::kDotBlue : (v & IntersectionFlags::DOT_IS_ORANGE) ? wpz::kDotYellow
                                              : (v & IntersectionFlags::DOT_IS_INVISIBLE) ? wpz::kDotInvisible : wpz::kDotBlack;
    }
    for (Point s : panel._startpoints) if (p.in(s.first, s.second)) p.at(s.first, s.second).start = true;
    for (Endpoint e : panel._endpoints) {
        if (!p.in(e.GetX(), e.GetY())) continue;
        Endpoint::Direction d = e.GetDir();
        p.at(e.GetX(), e.GetY()).end = (d & Endpoint::Direction::LEFT) ? wpz::kEndLeft : (d & Endpoint::Direction::RIGHT) ? wpz::kEndRight : (d & Endpoint::Direction::UP) ? wpz::kEndTop : wpz::kEndBottom;
    }
    p.sym = panel.symmetry == Panel::Symmetry::Vertical ? wpz::kSymX : panel.symmetry == Panel::Symmetry::Horizontal ? wpz::kSymY : panel.symmetry == Panel::Symmetry::Rotational ? wpz::kSymXY : wpz::kSymNone;
    return p;
}

static std::vector<::wrand::Dog> RunCustom(const ::wrand::Spec& spec) {
#ifndef WR_MUT_NO_RESET
    PanelExtractionTests::Reset(); APWatchdog::Reset(); Point::pillarWidth = 0; Special::correctShapesById.clear();
#endif
    std::string text = ::wrand::LoadPuzzleSet ? ::wrand::LoadPuzzleSet(spec.set) : std::string();
    if (text.empty()) throw std::runtime_error("the puzzle set " + spec.set + " is not on this PC");
    if (wpz::Hash(text) != spec.set) throw std::runtime_error("the puzzle set " + spec.set + " on this PC is not the one chosen");
    wpz::Set set; std::string err;
    if (!wpz::Parse(text, &set, &err)) throw std::runtime_error("the puzzle set cannot be read: " + err);
    std::vector<unsigned> where = wpz::Placement(set);
    for (size_t k = 0; k < set.puzzles.size(); k++) {
        if (!where[k] || !wpz::Problems(set.puzzles[k]).empty()) continue;      // (the launcher never sends such a puzzle)
        ::wrand::StatusW(L"Your puzzles");
        PanelExtractionTests::WriteCustom((int)where[k], set.puzzles[k]);
    }
    return {};
}
}  // namespace v_nsv

std::vector<wrand::Dog> wrand::RunNsv(const Spec& spec) { return v_nsv::Run(spec); }
std::vector<wrand::Dog> wrand::RunCustom(const Spec& spec) { return v_nsv::RunCustom(spec); }
wpz::Puzzle wrand::ReadCustomPuzzle(int id) { return v_nsv::PanelExtractionTests::ReadCustom(id); }
std::string (*wrand::LoadPuzzleSet)(const std::string& hash) = nullptr;
