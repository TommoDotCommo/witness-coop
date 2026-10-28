// Inspect sole markers through the same glTF animation and skinning path as the renderer.
// usage: gait_probe model.glb walk_speed run_speed
// This is a diagnostic, not a guarantee of contact quality: the sole markers
// follow the foot joint rigidly and do not inspect every skinned boot vertex.
#include "../proxy/model/model.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 4) { std::fprintf(stderr, "usage: gait_probe model.glb walk_speed run_speed\n"); return 2; }
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
    wm::Model m; std::string err;
    if (!wm::LoadGlb(bytes.data(), bytes.size(), &m, &err)) { std::fprintf(stderr, "load: %s\n", err.c_str()); return 1; }
    int foot[2] = {-1, -1};
    for (size_t i = 0; i < m.joints.size(); ++i) {
        const auto& n = m.nodes[m.joints[i]].name;
        if (n == "foot.L") foot[0] = int(i);
        if (n == "foot.R") foot[1] = int(i);
    }
    if (foot[0] < 0 || foot[1] < 0) { std::fprintf(stderr, "missing foot joints\n"); return 1; }
    for (int c = 0; c < 2; ++c) {
        const char* name = c == 0 ? "walk" : "run"; int clip = wm::FindClip(m, name);
        if (clip < 0) { std::fprintf(stderr, "missing %s clip\n", name); return 1; }
        float speed = std::strtof(argv[c + 2], nullptr), duration = m.clips[clip].duration;
        if (!(speed > 0 && duration > 0)) return 2;
        constexpr int n = 240; std::vector<float> mats(wm::kMaxJoints * 16), slip;
        int prevSide = -1, prevPoint = -1; float prevForward = 0, lowest = 1e9f; int underground = 0;
        for (int i = 0; i <= n; ++i) {
            float t = duration * i / n;
            wm::JointMatrices(m, clip, t, mats.data());
            float minY = 1e9f, forward = 0; int side = -1, marker = -1;
            for (int s = 0; s < 2; ++s) for (int k = 0; k < 2; ++k) {
                wm::Vertex v = {}; v.pos[0] = s ? -0.10f : 0.10f; v.pos[1] = 0.012f;
                v.pos[2] = k ? 0.195f : -0.045f; v.joints[0] = uint8_t(foot[s]); v.weights[0] = 1;
                float p[3]; wm::SkinPosition(m, mats.data(), v, p);
                if (p[1] < minY) { minY = p[1]; forward = p[2] + speed * t; side = s; marker = k; }
            }
            lowest = std::min(lowest, minY); if (minY < -0.01f) ++underground;
            if (minY < 0.006f && prevSide == side && prevPoint == marker)
                slip.push_back(std::fabs(forward - prevForward) * n / duration);
            if (minY < 0.006f) { prevSide = side; prevPoint = marker; prevForward = forward; }
            else prevSide = prevPoint = -1;
        }
        if (slip.empty()) { std::fprintf(stderr, "%s: no contact samples\n", name); return 1; }
        std::sort(slip.begin(), slip.end());
        std::printf("%s: ground marker slip median %.3f p90 %.3f m/s (%zu samples)\n", name, slip[slip.size()/2], slip[slip.size()*9/10], slip.size());
        std::printf("%s: lowest marker %.3f m, below -0.01 m in %d/%d samples\n", name, lowest, underground, n + 1);
    }
}
