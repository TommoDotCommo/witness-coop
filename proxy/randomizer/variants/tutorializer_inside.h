// Included inside namespace v_tutorializer before each of its files (variants.mk: tutorializer_INSIDE): the Tutorializer's own Memory, as its files
// use it (third_party/tutorializer is hatkirby's, with its own memory layer, not the randomizers'), on top of the calling thread's backend
// (backend.h), and the panel offsets its Memory.cpp sets for this build (the globals at 0x62D0A0).
// Not carried over: the two sigscanned code patches its Randomize() asks for (they stop the endings from ending the game); AddSigScan here
// keeps nothing, so in the co-op mod the endings work as in the game.
#pragma once
using byte = unsigned char;
inline int GLOBALS = 0x62D0A0, POSITION = 0x24, ORIENTATION = 0x34, PATH_COLOR = 0xC0, REFLECTION_PATH_COLOR = 0xD0, DOT_COLOR = 0xF0, ACTIVE_COLOR = 0x100,
    BACKGROUND_REGION_COLOR = 0x110, SUCCESS_COLOR_A = 0x120, SUCCESS_COLOR_B = 0x130, STROBE_COLOR_A = 0x140, STROBE_COLOR_B = 0x150, ERROR_COLOR = 0x160,
    PATTERN_POINT_COLOR = 0x180, PATTERN_POINT_COLOR_A = 0x190, PATTERN_POINT_COLOR_B = 0x1A0, SYMBOL_A = 0x1B0, SYMBOL_B = 0x1C0, SYMBOL_C = 0x1D0,
    SYMBOL_D = 0x1E0, SYMBOL_E = 0x1F0, PUSH_SYMBOL_COLORS = 0x200, OUTER_BACKGROUND = 0x204, OUTER_BACKGROUND_MODE = 0x214, TRACED_EDGES = 0x228,
    AUDIO_PREFIX = 0x270, POWER = 0x2A0, TARGET = 0x2B4, POWER_OFF_ON_FAIL = 0x2B8, IS_CYLINDER = 0x2F4, CYLINDER_Z0 = 0x2F8, CYLINDER_Z1 = 0x2FC,
    CYLINDER_RADIUS = 0x300, CURSOR_SPEED_SCALE = 0x350, NEEDS_REDRAW = 0x37C, SPECULAR_ADD = 0x38C, SPECULAR_POWER = 0x390, PATH_WIDTH_SCALE = 0x39C,
    STARTPOINT_SCALE = 0x3A0, NUM_DOTS = 0x3B4, NUM_CONNECTIONS = 0x3B8, MAX_BROADCAST_DISTANCE = 0x3BC, DOT_POSITIONS = 0x3C0, DOT_FLAGS = 0x3C8,
    DOT_CONNECTION_A = 0x3D0, DOT_CONNECTION_B = 0x3D8, RANDOMISE_ON_POWER_ON = 0x3E0, DECORATIONS = 0x418, DECORATION_FLAGS = 0x420,
    DECORATION_COLORS = 0x428, NUM_DECORATIONS = 0x430, REFLECTION_DATA = 0x438, GRID_SIZE_X = 0x440, GRID_SIZE_Y = 0x444, STYLE_FLAGS = 0x448,
    SEQUENCE_LEN = 0x454, SEQUENCE = 0x458, DOT_SEQUENCE_LEN = 0x460, DOT_SEQUENCE = 0x468, DOT_SEQUENCE_LEN_REFLECTION = 0x470,
    DOT_SEQUENCE_REFLECTION = 0x478, NUM_COLORED_REGIONS = 0x498, COLORED_REGIONS = 0x4A0, PANEL_TARGET = 0x4A8, SPECULAR_TEXTURE = 0x4D0,
    CABLE_TARGET_2 = 0xD0, AUDIO_LOG_NAME = 0x0, OPEN_RATE = 0xE0, METADATA = 0x13A, HOTEL_EP_NAME = 0x51E340;

class Memory {
public:
    explicit Memory(const std::wstring&) : b_(::wrand::CurrentBackend()) { if (!b_) throw std::runtime_error("randomizer: no backend on this thread"); }
    template <class T> std::vector<T> ReadArray(int id, int offset, int size) {
        std::vector<T> v((size_t)size); if (size && !b_->readArray(id, offset, v.data(), v.size() * sizeof(T))) throw std::runtime_error("tutorializer: array read failed"); return v;
    }
    template <class T> void WriteArray(int id, int offset, const std::vector<T>& data) {      // upstream: into the array the field points to
        if (!data.empty() && !b_->writeArrayInPlace(id, offset, data.data(), data.size() * sizeof(T), data.size() * sizeof(T))) throw std::runtime_error("tutorializer: array write failed");
    }
    template <class T> void WriteNewArray(int id, int offset, const std::vector<T>& data) {   // upstream: a new array (VirtualAllocEx), the field set to it
        if (!b_->writeArrayNew(id, offset, data.data(), data.size() * sizeof(T))) throw std::runtime_error("tutorializer: array write failed");
    }
    template <class T> std::vector<T> ReadEntityData(int id, int offset, size_t size) {
        std::vector<T> v(size); if (size && !b_->readField(id, offset, v.data(), size * sizeof(T))) throw std::runtime_error("tutorializer: field read failed"); return v;
    }
    template <class T> void WriteEntityData(int id, int offset, const std::vector<T>& data) {
        if (!data.empty() && !b_->writeField(id, offset, data.data(), data.size() * sizeof(T))) throw std::runtime_error("tutorializer: field write failed");
    }
    void AddSigScan(const std::vector<byte>&, const std::function<void(int)>&) {}      // (not carried over: see above)
    int ExecuteSigScans() { return 0; }
    template <typename T> void CopyEntityData(int fromId, int toId, int offset, int size) { WriteEntityData<T>(toId, offset, ReadEntityData<T>(fromId, offset, size)); }
    template <typename T> void CopyArray(int fromId, int toId, int offset, int size) {
        if (size == 0) WriteEntityData<uintptr_t>(toId, offset, {static_cast<uintptr_t>(0)});
        else WriteNewArray<T>(toId, offset, ReadArray<T>(fromId, offset, size));
    }
    template <typename T> void CopyArrayDynamicSize(int fromId, int toId, int arrayOffset, int sizeOffset) { CopyArray<T>(fromId, toId, arrayOffset, ReadEntityData<int>(fromId, sizeOffset, sizeof(int))[0]); }
    template <class T> void WritePanelData(int panel, int offset, const std::vector<T>& data) { WriteEntityData<T>(panel, offset, data); }
    template <class T> T ReadPanelData(int panel, int offset) { return ReadEntityData<T>(panel, offset, 1)[0]; }
    template <class T> std::vector<T> ReadPanelData(int panel, int offset, size_t size) { if (size == 0) return std::vector<T>(); return ReadEntityData<T>(panel, offset, size); }
    template <class T> void WriteData(const std::vector<int>&, const std::vector<T>&) { throw std::logic_error("tutorializer: code patches are not carried over"); }
    template <class T> std::vector<T> ReadData(const std::vector<int>&, size_t) { throw std::logic_error("tutorializer: code patches are not carried over"); }
private:
    ::wrand::Backend* b_;
};
