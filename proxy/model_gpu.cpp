#include "radio/radio_game.h"
#include "radio/radio.h"
#include <map>
#include <set>
#include <memory>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <vector>
#include <cstddef>
#include "model_gpu.h"
#include "model/gestures.h"
#include "model/life.h"
#include "voice/footsteps.h"
void VoiceFootstep(unsigned id, const float pos[3], float speed, bool run);      // voice_audio.cpp: a footfall heard where the walker is
#include "tv.h"
#include "tv/mesh.h"
static_assert(offsetof(wm::Vertex, appearanceGroup) == 56, "appearance input layout must match Vertex");
#include "config.h"
#include "depth.h"
HMODULE g_selfModule();
#include "devmenu.h"
#include "ghost/ghost.h"

void Log(const char* msg);
extern HMODULE g_selfModule();

// One loaded character model with everything that belongs to it: the mesh on the GPU, how it is fitted and timed, and the walk state of each player drawn with it.
struct LifeTrack { wm::LifeState state; wvoice::FootstepTracker steps; float lastPos[3] = {}, vel[3] = {}, acc[3] = {}; bool have = false, wasClosed = false, wasFidgeting = false; unsigned blinks = 0, fidgets = 0; explicit LifeTrack(unsigned seed) : state(seed) {} };
struct Loaded {
    wm::Model model; ID3D11Buffer *vb = nullptr, *ib = nullptr; ID3D11ShaderResourceView *tex = nullptr, *texN = nullptr, *texM = nullptr; wm::Fit fit = {}; wm::GaitCfg gait; float yaw = 0, height = 0; char eyeJoint[80] = "";
    wm::LifeRig rig;
    std::map<unsigned, std::unique_ptr<wm::Avatar>> avatars;
    std::map<unsigned, std::unique_ptr<LifeTrack>> life;
    ~Loaded() { if (vb) vb->Release(); if (ib) ib->Release(); if (tex) tex->Release(); if (texN) texN->Release(); if (texM) texM->Release(); }
};
static Loaded g_adventurer;
static Loaded* g_L = &g_adventurer;
static volatile LONG g_modelRenderThread;
static bool g_ready, g_tried;
static ID3D11Device* g_dev; static ID3D11DeviceContext* g_ctx;
static ID3D11VertexShader *g_vs, *g_vsFull; static ID3D11PixelShader *g_psDepth, *g_psProbe, *g_psDump; static volatile LONG g_probeReq, g_gridReq, g_dumpReq, g_depthViewCmd; static float g_gridBox[4] = {0.2f, 0.2f, 0.8f, 0.8f};
static ID3D11VertexShader* g_vsUnused; static ID3D11PixelShader* g_ps; static ID3D11InputLayout* g_layout;
static ID3D11Buffer *g_cbFrame, *g_cbBones;
static ID3D11Buffer* g_vbTv;
static ID3D11Buffer* g_vbAudience;
struct AudienceQuery { ID3D11Query* query=nullptr; bool pending=false; };
static std::map<unsigned,AudienceQuery> g_audienceQueries;
static ID3D11Query* g_tvQuery; static bool g_tvQueryPending;
static wtv::Motion g_tvMotion; static uint64_t g_tvTime; static unsigned g_tvPlayer;
static ID3D11SamplerState* g_samp;
static ID3D11RasterizerState* g_rs; static ID3D11DepthStencilState* g_dss; static ID3D11BlendState *g_blend, *g_blendAlpha;
static ID3D11Texture2D* g_depthTex; static ID3D11DepthStencilView* g_dsv; static ID3D11ShaderResourceView* g_depthSrv; static UINT g_depthW, g_depthH;
static ID3D11Texture2D* g_maskTex; static ID3D11RenderTargetView* g_maskRtv; static ID3D11ShaderResourceView* g_maskSrv;      // the sun shadow: the flattened model is drawn into this mask, then laid on the ground
static ID3D11PixelShader *g_psMask, *g_psShadow; static ID3D11Query* g_qShadow; static bool g_qShadowPending;
struct PlayerDraw { float ground=0; int emote=0; float gesture=0, talk=0; float hand[3]={}; witness::Appearance appearance; unsigned id, seq; float pos[3], speed; int clip; float yaw; };
static std::vector<PlayerDraw> g_playerDraws;
static ID3D11VertexShader* g_vsLine; static ID3D11PixelShader* g_psLine; static ID3D11InputLayout* g_layoutLine; static ID3D11Buffer* g_vbLine; static ID3D11Query* g_qLine; static bool g_qLinePending; static LineDrawStats g_lineStats;
static const size_t kLineVerts = 65536;
struct GhostState { float alpha = 1.0f; float target = 1.0f; ULONGLONG t = 0; };
static std::map<unsigned, GhostState> g_ghost; static std::vector<float> g_panelPts; static ULONGLONG g_panelPtsT; static size_t g_panelCount;
static ModelDrawStats g_stats; static ID3D11Query* g_query; static bool g_queryPending;

struct FrameCB { float viewProj[16], model[16]; float light[4]; float tint[4]; float depthp[4]; float screen[4]; float appearance[9][4]; float camPos[4], camRight[4], camUp[4], camFwd[4], look[4], shadow[4]; };       // 432 bytes

static const char* kHlsl = R"(
cbuffer Frame : register(b0) { float4x4 viewProj; float4x4 model; float4 light; float4 tint; float4 depthp; float4 screen; float4 appearance[9]; float4 camPos; float4 camRight; float4 camUp; float4 camFwd; float4 look; float4 shadow; };
// screen: width, height. depthp: x mode (0 none, 1 plain, 2 multisampled), y samples, z relative bias, w the game's near plane (its depth is near / distance, reversed)
// camRight.w, camUp.w: tan of half the field of view (horizontal, vertical). look: x the sun, y the sky and ground (linear light), z shading (0 the old flat shading, 1 lit), w how dark the shadow is.
// shadow: x the ground's height under the player, y and z our depth constants (depth = y + z / distance)
cbuffer Bones : register(b1) { float4x4 bones[256]; };
Texture2D tex : register(t0); SamplerState smp : register(s0);
Texture2DMS<float> sceneMS : register(t1); Texture2D<float> scene1 : register(t2);
Texture2D texN : register(t3); Texture2D texM : register(t4); Texture2D<float> mask : register(t5); Texture2D<float> ownDepth : register(t6);
struct VIn { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; uint4 j : BLENDINDICES; float4 w : BLENDWEIGHT; float4 c : COLOR0; float group : TEXCOORD2; };
struct VOut { float4 pos : SV_POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; float dist : TEXCOORD1; float4 c : COLOR0; float3 appearance : TEXCOORD2; float3 wp : TEXCOORD3; float group : TEXCOORD4; };
VOut VS(VIn v) {
  float4x4 skin = bones[v.j.x] * v.w.x + bones[v.j.y] * v.w.y + bones[v.j.z] * v.w.z + bones[v.j.w] * v.w.w;
  float4 lp = mul(skin, float4(v.p, 1)); float3 ln = mul((float3x3)skin, v.n);
  VOut o; float4 wp = mul(model, lp); o.pos = mul(viewProj, wp); o.dist = o.pos.w; o.n = mul((float3x3)model, ln); o.uv = v.uv; o.c = v.c; o.wp = wp.xyz; o.group = v.group;
  o.appearance = appearance[min(8u, (uint)(max(0.0, v.group) + 0.5))].rgb; return o; }
float3 ToLin(float3 c) { return lerp(c / 12.92, pow(max(c, 0.0) + 0.055, 2.4) / pow(1.055, 2.4), step(0.04045, c)); }
float3 ToSrgb(float3 l) { return lerp(l * 12.92, 1.055 * pow(max(l, 0.0), 1.0 / 2.4) - 0.055, step(0.0031308, l)); }
// how much of this pixel the game's own geometry leaves free: the fraction of the scene's depth samples behind the given view depth (the game stores near / distance)
float SceneVis(float2 pos, float dist, float extra) {
  int2 px = int2(pos); float zm = depthp.w / dist * (1 + depthp.z + extra);
  if (depthp.x < 1.5) return (scene1.Load(int3(px, 0)) <= zm) ? 1 : 0;
  int ns = (int)depthp.y; float s = 0; for (int k = 0; k < ns; k++) s += (sceneMS.Load(px, k) <= zm) ? 1 : 0; return s / ns; }
float4 PS(VOut i) : SV_TARGET {
  float3 n = normalize(i.n);
  float3 base = lerp(float3(1, 1, 1), tex.Sample(smp, i.uv).rgb, i.c.a) * i.c.rgb;      // c.a: 1 where the vertex samples the model's texture, 0 for a flat-colour material
  float3 c;
  if (look.z < 0.5) {      // the old flat shading, in display space (the identity palette keeps the exact old path)
    float3 b = base;
    if (any(i.appearance != float3(1, 1, 1))) b = ToSrgb(ToLin(base) * i.appearance);
    float d = saturate(dot(n, normalize(light.xyz))); c = b * tint.rgb * (light.w + (1 - light.w) * d);
  } else {
    float3 lin = ToLin(base) * i.appearance;      // the palette multiplies in linear light
    float3 V = normalize(camPos.xyz - i.wp); float3 L = normalize(light.xyz);
    if (dot(n, V) < 0) n = -n;      // drawn without culling: a face seen from behind (inside an open coat) lights as it faces us
    float4 mm = i.c.a > 0.5 && shadow.w < 0.5 ? texM.Sample(smp, i.uv) : float4(1, 0.6, 0, 1);      // shadow.w: 1 ignores the material map (diagnostic)
    float ao = mm.r, rough = clamp(mm.g, 0.08, 1.0), metal = mm.b;
    if (i.c.a > 0.5 && light.w < 0.9) {      // the tangent-space normal map through the derivatives' cotangent frame (the vertices carry no tangents); light.w: 1 ignores it (diagnostic)
      float3 tn = texN.Sample(smp, i.uv).xyz * 2 - 1; tn.y = -tn.y;      // glTF: +Y points up the image; our bitangent follows v, which runs down it
      float3 dp1 = ddx(i.wp), dp2 = ddy(i.wp); float2 duv1 = ddx(i.uv), duv2 = ddy(i.uv);
      float3 dp2perp = cross(dp2, n), dp1perp = cross(n, dp1);
      float3 T = dp2perp * duv1.x + dp1perp * duv2.x, B = dp2perp * duv1.y + dp1perp * duv2.y;
      float mx = max(dot(T, T), dot(B, B)), mn = min(dot(T, T), dot(B, B));
      if (mx > 1e-20 && mn > 0.05 * mx) { float invmax = rsqrt(mx); float k = 0.75 * saturate(dot(normalize(i.n), V) * 3); n = normalize(T * invmax * tn.x * k + B * invmax * tn.y * k + n * max(tn.z, 0.2)); }
    }
    uint g = (uint)(max(0.0, i.group) + 0.5);      // the group is one value per vertex; interpolation lands a hair under it, and truncating flipped pixels between two materials' shading (a speckle)
    float NdotL = dot(n, L), NdotV = max(dot(n, V), 1e-4), nl = saturate(NdotL);
    float wrap = g == 1 ? 0.45 : 0.12;      // skin is lit a little round the terminator (light that goes through it)
    float diff = saturate((NdotL + wrap) / (1 + wrap));
    float3 H = normalize(L + V); float NdotH = saturate(dot(n, H)); float VdotH = saturate(dot(V, H));
    float a = rough * rough, a2 = a * a; float dd = NdotH * NdotH * (a2 - 1) + 1; float D = a2 / (3.14159 * dd * dd);
    float k = (rough + 1) * (rough + 1) / 8; float G = (NdotV / (NdotV * (1 - k) + k)) * (nl / (nl * (1 - k) + k));
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), lin, metal); float3 F = F0 + (1 - F0) * pow(1 - VdotH, 5);
    float3 spec = D * G * F / (4 * NdotV * max(nl, 1e-3)) * nl;
    float3 kd = (1 - F) * (1 - metal);
    float3 sunCol = float3(1.0, 0.96, 0.88) * look.x;
    float3 sky = float3(0.50, 0.58, 0.70), ground = float3(0.34, 0.30, 0.24);
    float3 hemi = lerp(ground, sky, n.z * 0.5 + 0.5) * look.y * ao;
    float sheen = (g == 4 || g == 5 || g == 6) ? pow(1 - NdotV, 3) * 0.22 * rough : 0;      // wool and knit catch the light along their edges
    float3 scatter = g == 1 ? lin * float3(1.0, 0.45, 0.35) * 0.10 * (1 - abs(NdotL)) * look.x : 0;      // skin: a warm band at the terminator
    c = lin * kd * (diff * sunCol + hemi) + spec * sunCol + sheen * (sunCol * 0.5 + hemi) + scatter + metal * lin * hemi * 0.6;
    c = ToSrgb(c) * tint.rgb;
  }
  float vis = 1;
  if (depthp.x > 0.5) { vis = SceneVis(i.pos.xy, i.dist, 0); if (vis <= 0) discard; }
  return float4(c, vis * tint.w); }      // tint.w: how solid this player is (see-through players, ghost/ghost.h)
float4 PSMask(VOut i) : SV_TARGET { return float4(1, 1, 1, 1); }      // the flattened model, for the shadow
struct LIn { float3 p : POSITION; float4 c : COLOR0; };
struct LOut { float4 pos : SV_POSITION; float4 c : COLOR0; float dist : TEXCOORD1; };
LOut VSLine(LIn v) { LOut o; o.pos = mul(viewProj, float4(v.p, 1)); o.dist = o.pos.w; o.c = v.c; return o; }
float4 PSLine(LOut i) : SV_TARGET {      // a line lies in the panel's own surface: it counts as 1.5% nearer than the scene so it is not hidden by the panel it is drawn on
  if (depthp.x > 0.5) {
    int2 px = int2(i.pos.xy); float zm = depthp.w / i.dist * (1 + depthp.z + 0.015); float vis = 1;
    if (depthp.x < 1.5) vis = (scene1.Load(int3(px, 0)) <= zm) ? 1 : 0;
    else { int ns = (int)depthp.y; float s = 0; for (int k = 0; k < ns; k++) s += (sceneMS.Load(px, k) <= zm) ? 1 : 0; vis = s / ns; }
    if (vis <= 0) discard;
  }
  return float4(i.c.rgb, 1); }
struct FS { float4 pos : SV_POSITION; };
// depth probe: renders into an 8x1 float target; pixel i holds the scene depth at the i-th probe position (centre, 4 samples of it, then four other points)
float PSProbe(FS i) : SV_TARGET {
  int k = (int)i.pos.x; float w = screen.x, h = screen.y; int2 p = int2(w * tint.x, h * tint.y);      // tint.xy: where to look, as fractions of the screen (the middle for pings)
  int smp = 0;
  if (k < 4) smp = k; else if (k == 4) p = int2(w * 0.5, h * 0.8); else if (k == 5) p = int2(w * 0.5, h * 0.2); else if (k == 6) p = int2(w * 0.2, h * 0.5); else p = int2(w * 0.8, h * 0.5);
  return depthp.x < 1.5 ? scene1.Load(int3(p, 0)) : sceneMS.Load(p, smp); }
// depth dump: a small picture of the scene depth (tint.xy: screen pixels per dump pixel), the first sample of each pixel
float PSDump(FS i) : SV_TARGET { int2 p = int2(i.pos.xy * tint.xy); return depthp.x < 1.5 ? scene1.Load(int3(p, 0)) : sceneMS.Load(p, 0); }
// debugging view of the scene depth: red = 1 - z (x10), green = z (x10), blue = untouched (z at 1)
FS VSFull(uint id : SV_VertexID) { FS o; float2 q = float2((id << 1) & 2, id & 2); o.pos = float4(q * float2(2, -2) + float2(-1, 1), 0, 1); return o; }
// the sun shadow: the blurred mask of the flattened model, laid only on the game's geometry near the player's ground height, and not over our own figures
float4 PSShadow(FS i) : SV_TARGET {
  int2 px = int2(i.pos.xy); float m = 0;
  [unroll] for (int y = -2; y <= 2; y++) [unroll] for (int x = -2; x <= 2; x++) m += mask.Load(int3(px + int2(x, y) * 2, 0));
  m /= 25; if (m <= 0.002) discard;
  float z = depthp.x < 1.5 ? scene1.Load(int3(px, 0)) : sceneMS.Load(px, 0); if (z <= 1e-7) discard;      // the sky
  float dist = depthp.w / z;
  float nx = (i.pos.x / screen.x) * 2 - 1, ny = 1 - (i.pos.y / screen.y) * 2;
  float3 wp = camPos.xyz + (camFwd.xyz + camRight.xyz * nx * camRight.w + camUp.xyz * ny * camUp.w) * dist;
  float fade = 1 - smoothstep(0.25, 0.6, abs(wp.z - shadow.x)); if (fade <= 0) discard;
  float own = ownDepth.Load(int3(px, 0)), groundDepth = shadow.y + shadow.z / dist;
  if (own < groundDepth - 2e-5) discard;      // our own figure stands in front of that bit of ground
  return float4(0, 0, 0, look.w * m * fade * tint.w); }
float4 PSDepth(FS i) : SV_TARGET {
  int2 px = int2(i.pos.xy); float z = depthp.x < 1.5 ? scene1.Load(int3(px, 0)) : sceneMS.Load(px, 0);
  return float4(saturate((1 - z) * 10), saturate(z * 10), z >= 0.99999 ? 1 : 0, 1); }
)";

static bool Fail(const char* m) { Log(m); return false; }

bool ModelGpuReady() { return g_ready; }
void ModelGpuDepthView(bool on) { InterlockedExchange(&g_depthViewCmd, on); }
static volatile LONG g_forceBlink;
void ModelGpuForceBlink() { InterlockedExchange(&g_forceBlink, 1); }
void ModelGpuRequestProbe() { InterlockedExchange(&g_probeReq, 1); }
void ModelGpuRequestProbeGrid(float x0, float y0, float x1, float y1) { g_gridBox[0] = x0; g_gridBox[1] = y0; g_gridBox[2] = x1; g_gridBox[3] = y1; InterlockedExchange(&g_gridReq, 1); }
ModelDrawStats ModelGpuStats() { return g_stats; }


// Load the sole bundled character once. Legacy config paths never reach the loader.
static bool LoadFile(const std::wstring& path, wm::Model* out, std::string* err);
static bool LoadAdventurer() {
    const Cfg& cfg = GetCfg(); ID3D11Device* dev = g_dev; ID3D11DeviceContext* ctx = g_ctx;
    wchar_t d[MAX_PATH]; GetModuleFileNameW(g_selfModule(), d, MAX_PATH); wchar_t* sl = wcsrchr(d, L'\\'); if (sl) *(sl + 1) = 0;
    const std::wstring path = std::wstring(d) + L"witness_models\\adventurer.glb";
    wm::Model nm; std::string err;
    if (!LoadFile(path, &nm, &err)) { char m[240]; snprintf(m, sizeof m, "model: cannot load %s: %s", "adventurer", err.c_str()); return Fail(m); }
    g_L->model = std::move(nm);
    float height = 1.8f, speed = 1.6f, yaw = cfg.modelYaw, runSpeed = 0, eye = wm::kCameraEye, lidClose = 1.0f; char eyeJoint[80] = "";
    // the model's own <file>.glb.txt sets its size, walking speed and facing; witness_coop.cfg overrides it. eye_joint=<part of a joint name> sizes the model so that
    // those joints (its eyes) are at the camera's height above the feet (eye=<metres> asks for another height); run_speed=<m/s> is what the run clip was made for.
    if (FILE* sc = _wfopen((path + L".txt").c_str(), L"r")) {
        char l[200]; float v; while (fgets(l, sizeof l, sc)) { if (sscanf(l, "height=%f", &v) == 1) height = v; else if (sscanf(l, "speed=%f", &v) == 1) speed = v; else if (sscanf(l, "yaw=%f", &v) == 1) yaw += v;
            else if (sscanf(l, "run_speed=%f", &v) == 1) runSpeed = v; else if (sscanf(l, "eye=%f", &v) == 1) eye = v; else if (sscanf(l, "lid_close=%f", &v) == 1) lidClose = v; else if (sscanf(l, "eye_joint=%79s", eyeJoint) == 1) {} }
        fclose(sc);
    }
    if (cfg.modelHeight > 0) height = cfg.modelHeight; if (cfg.modelStride > 0) speed = cfg.modelStride;
    g_L->yaw = yaw * 3.14159265f / 180.0f;
    memcpy(g_L->eyeJoint, eyeJoint, sizeof g_L->eyeJoint);
    float eyeAt = wm::EyeHeight(g_L->model, eyeJoint);
    g_L->fit = (cfg.modelHeight <= 0 && eyeJoint[0] && eyeAt > wm::kNoEye * 0.5f) ? wm::FitToEye(g_L->model, eye, eyeJoint) : wm::FitToHeight(g_L->model, height);
    g_L->height = (g_L->model.bmax[1] - g_L->model.bmin[1]) * g_L->fit.scale;
    if (eyeJoint[0]) { char m[200]; snprintf(m, sizeof m, "model: eyes (%s) %s: %.3f m above the feet, camera %.3f m, model %.3f m high", eyeJoint, eyeAt > wm::kNoEye * 0.5f ? "found" : "NOT found", eyeAt > wm::kNoEye * 0.5f ? (eyeAt - g_L->model.bmin[1]) * g_L->fit.scale : -1.0f, wm::kCameraEye, g_L->height); Log(m); }
    wm::GaitCfg gc; gc.refSpeed = speed; gc.runRef = runSpeed; if (cfg.noModelGait) gc.idleBelow = 1e9f; g_L->gait = gc;
    g_L->rig.Bind(g_L->model, lidClose);
    { char m[240]; snprintf(m, sizeof m, "model: life rig %s: lids %s (close %.2f), eyes %s, scarf chain %d+%d, gear %d+%d, maps normal %dx%d material %dx%d", g_L->rig.ok ? "bound" : "NOT bound (no head)", g_L->rig.lid[0] >= 0 ? "found" : "none", g_L->rig.lidClose, g_L->rig.eye[0] >= 0 ? "found" : "none", g_L->rig.scarfA.count, g_L->rig.scarfB.count, g_L->rig.canteen.count, g_L->rig.satchel.count, g_L->model.nrmW, g_L->model.nrmH, g_L->model.mrW, g_L->model.mrH); Log(m); }
    if (std::none_of(g_L->model.verts.begin(), g_L->model.verts.end(), [](const wm::Vertex& v) { return v.appearanceGroup > 0; }))
        Log("model: appearance regions absent; regenerate the bundled adventurer to enable palettes");
    D3D11_BUFFER_DESC bd = {}; D3D11_SUBRESOURCE_DATA sd = {};
    bd.ByteWidth = (UINT)(g_L->model.verts.size() * sizeof(wm::Vertex)); bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; sd.pSysMem = g_L->model.verts.data();
    if (FAILED(dev->CreateBuffer(&bd, &sd, &g_L->vb))) return Fail("model: vertex buffer failed");
    bd.ByteWidth = (UINT)(g_L->model.idx.size() * 4); bd.BindFlags = D3D11_BIND_INDEX_BUFFER; sd.pSysMem = g_L->model.idx.data();
    if (FAILED(dev->CreateBuffer(&bd, &sd, &g_L->ib))) return Fail("model: index buffer failed");
    // textures (a 1x1 one when the model has none), with mip maps: the colour atlas, the normal map (flat when absent) and the material map (rough, not metal, when absent)
    auto upload = [&](int w, int h, const std::vector<uint8_t>& rgba, uint32_t fallback, ID3D11ShaderResourceView** out) {
        int tw = w ? w : 1, th = w ? h : 1; const void* px = w ? (const void*)rgba.data() : &fallback;
        D3D11_TEXTURE2D_DESC td = {}; td.Width = tw; td.Height = th; td.MipLevels = 0; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET; td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        ID3D11Texture2D* tx = nullptr; if (FAILED(dev->CreateTexture2D(&td, nullptr, &tx))) return false;
        ctx->UpdateSubresource(tx, 0, nullptr, px, tw * 4, 0);
        bool ok = SUCCEEDED(dev->CreateShaderResourceView(tx, nullptr, out)); if (ok) ctx->GenerateMips(*out); tx->Release(); return ok;
    };
    if (!upload(g_L->model.texW, g_L->model.texH, g_L->model.rgba, 0xFFFFFFFF, &g_L->tex)) return Fail("model: texture failed");
    if (!upload(g_L->model.nrmW, g_L->model.nrmH, g_L->model.nrm, 0xFFFF8080, &g_L->texN)) return Fail("model: normal map failed");       // (0x80, 0x80, 0xFF): straight up
    if (!upload(g_L->model.mrW, g_L->model.mrH, g_L->model.mr, 0xFF00D9FF, &g_L->texM)) return Fail("model: material map failed");     // occlusion 1, roughness 0.85, metal 0
    if (!g_L->vb || !g_L->ib || !g_L->tex) return Fail("model: GPU upload failed");
    char m[240]; snprintf(m, sizeof m, "model: loaded %zu vertices, %zu triangles, %zu joints, %zu clips, texture %dx%d, bind height %.2f scaled to %.2f", g_L->model.verts.size(), g_L->model.idx.size() / 3, g_L->model.joints.size(), g_L->model.clips.size(), g_L->model.texW, g_L->model.texH, g_L->model.bmax[1] - g_L->model.bmin[1], g_L->height);
    Log(m);
    return true;
}
static bool LoadFile(const std::wstring& path, wm::Model* out, std::string* err) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { *err = "file not found"; return false; }
    std::vector<uint8_t> bytes(GetFileSize(f, nullptr)); DWORD got = 0; ReadFile(f, bytes.data(), (DWORD)bytes.size(), &got, nullptr); CloseHandle(f);
    return got == bytes.size() && wm::LoadGlb(bytes.data(), bytes.size(), out, err);
}


bool ModelGpuInit(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    if (g_tried) return g_ready;
    g_tried = true; g_dev = dev; g_ctx = ctx;

    typedef HRESULT(WINAPI* CompileFn)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
    HMODULE c = LoadLibraryA("d3dcompiler_47.dll"); CompileFn compile = c ? (CompileFn)GetProcAddress(c, "D3DCompile") : nullptr;
    if (!compile) return Fail("model: no d3dcompiler_47");
    ID3DBlob *vsb = nullptr, *psb = nullptr, *eb = nullptr;
    if (FAILED(compile(kHlsl, strlen(kHlsl), "model", nullptr, nullptr, "VS", "vs_5_0", 0, 0, &vsb, &eb))) return Fail(eb ? (const char*)eb->GetBufferPointer() : "model: VS compile failed");
    if (FAILED(compile(kHlsl, strlen(kHlsl), "model", nullptr, nullptr, "PS", "ps_5_0", 0, 0, &psb, &eb))) return Fail(eb ? (const char*)eb->GetBufferPointer() : "model: PS compile failed");
    dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &g_vs);
    ID3DBlob *fvb = nullptr, *fpb = nullptr;
    if (FAILED(compile(kHlsl, strlen(kHlsl), "model", nullptr, nullptr, "VSFull", "vs_5_0", 0, 0, &fvb, &eb)) || FAILED(compile(kHlsl, strlen(kHlsl), "model", nullptr, nullptr, "PSDepth", "ps_5_0", 0, 0, &fpb, &eb))) return Fail("model: depth view shaders failed");
    ID3DBlob* ppb = nullptr; if (FAILED(compile(kHlsl, strlen(kHlsl), "model", nullptr, nullptr, "PSProbe", "ps_5_0", 0, 0, &ppb, &eb))) return Fail("model: probe shader failed");
    dev->CreatePixelShader(ppb->GetBufferPointer(), ppb->GetBufferSize(), nullptr, &g_psProbe);
    {   // the sun shadow: the flattened model into a mask, then the mask onto the ground
        ID3DBlob *mb = nullptr, *sb = nullptr;
        if (FAILED(compile(kHlsl, strlen(kHlsl), "model", nullptr, nullptr, "PSMask", "ps_5_0", 0, 0, &mb, &eb)) || FAILED(compile(kHlsl, strlen(kHlsl), "model", nullptr, nullptr, "PSShadow", "ps_5_0", 0, 0, &sb, &eb))) return Fail(eb ? (const char*)eb->GetBufferPointer() : "model: shadow shaders failed");
        dev->CreatePixelShader(mb->GetBufferPointer(), mb->GetBufferSize(), nullptr, &g_psMask); dev->CreatePixelShader(sb->GetBufferPointer(), sb->GetBufferSize(), nullptr, &g_psShadow);
    }
    ID3DBlob* pdb = nullptr; if (SUCCEEDED(compile(kHlsl, strlen(kHlsl), "model", nullptr, nullptr, "PSDump", "ps_5_0", 0, 0, &pdb, &eb))) dev->CreatePixelShader(pdb->GetBufferPointer(), pdb->GetBufferSize(), nullptr, &g_psDump);
    {   // the other players' lines
        ID3DBlob *lvb = nullptr, *lpb = nullptr;
        if (SUCCEEDED(compile(kHlsl, strlen(kHlsl), "model", nullptr, nullptr, "VSLine", "vs_5_0", 0, 0, &lvb, &eb)) && SUCCEEDED(compile(kHlsl, strlen(kHlsl), "model", nullptr, nullptr, "PSLine", "ps_5_0", 0, 0, &lpb, &eb))) {
            dev->CreateVertexShader(lvb->GetBufferPointer(), lvb->GetBufferSize(), nullptr, &g_vsLine); dev->CreatePixelShader(lpb->GetBufferPointer(), lpb->GetBufferSize(), nullptr, &g_psLine);
            D3D11_INPUT_ELEMENT_DESC li[] = {{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}, {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}};
            dev->CreateInputLayout(li, 2, lvb->GetBufferPointer(), lvb->GetBufferSize(), &g_layoutLine);
            D3D11_BUFFER_DESC vb = {}; vb.ByteWidth = UINT(kLineVerts * sizeof(LineVert)); vb.Usage = D3D11_USAGE_DYNAMIC; vb.BindFlags = D3D11_BIND_VERTEX_BUFFER; vb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            dev->CreateBuffer(&vb, nullptr, &g_vbLine);
        } else Log(eb ? (const char*)eb->GetBufferPointer() : "model: line shaders failed");
    }
    dev->CreateVertexShader(fvb->GetBufferPointer(), fvb->GetBufferSize(), nullptr, &g_vsFull); dev->CreatePixelShader(fpb->GetBufferPointer(), fpb->GetBufferSize(), nullptr, &g_psDepth);
    dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g_ps);
    static_assert(sizeof(wm::Vertex) == 60, "vertex layout");
    D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}, {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0}, {"BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0}, {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 52, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT, 0, 56, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    if (FAILED(dev->CreateInputLayout(il, 7, vsb->GetBufferPointer(), vsb->GetBufferSize(), &g_layout))) return Fail("model: input layout failed");
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(FrameCB); bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; if (FAILED(dev->CreateBuffer(&bd, nullptr, &g_cbFrame))) return Fail("model: cb failed");
    bd.ByteWidth = wm::kMaxJoints * 64; if (FAILED(dev->CreateBuffer(&bd, nullptr, &g_cbBones))) return Fail("model: bone cb failed");
    D3D11_SAMPLER_DESC ss = {}; ss.Filter = D3D11_FILTER_ANISOTROPIC; ss.AddressU = ss.AddressV = ss.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; ss.MaxLOD = D3D11_FLOAT32_MAX; ss.MaxAnisotropy = 8;
    dev->CreateSamplerState(&ss, &g_samp);
    D3D11_RASTERIZER_DESC rd = {}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE; dev->CreateRasterizerState(&rd, &g_rs);
    D3D11_DEPTH_STENCIL_DESC dd = {}; dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dd.DepthFunc = D3D11_COMPARISON_LESS; dev->CreateDepthStencilState(&dd, &g_dss);
    D3D11_BLEND_DESC bl = {}; bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; dev->CreateBlendState(&bl, &g_blend);
    bl.RenderTarget[0].BlendEnable = TRUE; bl.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA; bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO; bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE; bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD; dev->CreateBlendState(&bl, &g_blendAlpha);
    g_ready = g_vs && g_ps && g_layout && g_cbFrame && g_cbBones && g_samp && g_rs && g_dss && g_blend && g_blendAlpha && g_vsFull && g_psDepth && g_psProbe && g_psMask && g_psShadow;
    if (g_ready && !LoadAdventurer()) g_ready = false;
    return g_ready;
}

static bool EnsureDepth(UINT w, UINT h) {
    if (g_dsv && g_depthW == w && g_depthH == h) return true;
    if (g_dsv) { g_dsv->Release(); g_dsv = nullptr; } if (g_depthSrv) { g_depthSrv->Release(); g_depthSrv = nullptr; } if (g_depthTex) { g_depthTex->Release(); g_depthTex = nullptr; }
    if (g_maskRtv) { g_maskRtv->Release(); g_maskRtv = nullptr; } if (g_maskSrv) { g_maskSrv->Release(); g_maskSrv = nullptr; } if (g_maskTex) { g_maskTex->Release(); g_maskTex = nullptr; }
    D3D11_TEXTURE2D_DESC td = {}; td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R32_TYPELESS; td.SampleDesc.Count = 1; td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    D3D11_DEPTH_STENCIL_VIEW_DESC dv = {}; dv.Format = DXGI_FORMAT_D32_FLOAT; dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv = {}; sv.Format = DXGI_FORMAT_R32_FLOAT; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
    if (FAILED(g_dev->CreateTexture2D(&td, nullptr, &g_depthTex)) || FAILED(g_dev->CreateDepthStencilView(g_depthTex, &dv, &g_dsv)) || FAILED(g_dev->CreateShaderResourceView(g_depthTex, &sv, &g_depthSrv))) return false;
    DepthIgnore(g_depthTex);      // the depth module watches the game's depth-stencil binds for a full-size typeless buffer: ours now looks like one
    D3D11_TEXTURE2D_DESC md = {}; md.Width = w; md.Height = h; md.MipLevels = 1; md.ArraySize = 1; md.Format = DXGI_FORMAT_R8_UNORM; md.SampleDesc.Count = 1; md.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(g_dev->CreateTexture2D(&md, nullptr, &g_maskTex)) || FAILED(g_dev->CreateRenderTargetView(g_maskTex, nullptr, &g_maskRtv)) || FAILED(g_dev->CreateShaderResourceView(g_maskTex, nullptr, &g_maskSrv))) return false;
    g_depthW = w; g_depthH = h; return true;
}
static void Mul16(const float* a, const float* b, float* out) {      // out = a * b, column-major
    float r[16]; for (int c = 0; c < 4; c++) for (int row = 0; row < 4; row++) { float v = 0; for (int k = 0; k < 4; k++) v += a[k * 4 + row] * b[c * 4 + k]; r[c * 4 + row] = v; } memcpy(out, r, 64);
}

// Reads the scene depth at 8 places into out (centre: out[0..3] are the four samples of the middle pixel, then low, high, left, right). false: could not.
static bool ProbeDepth(UINT w, UINT h, ID3D11ShaderResourceView* sceneSrv, bool ms, float out[8], float fx = 0.5f, float fy = 0.5f) {
    D3D11_TEXTURE2D_DESC td = {}; td.Width = 8; td.Height = 1; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R32_FLOAT; td.SampleDesc.Count = 1; td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D *rt = nullptr, *st = nullptr; ID3D11RenderTargetView* rtv = nullptr;
    if (FAILED(g_dev->CreateTexture2D(&td, nullptr, &rt)) || FAILED(g_dev->CreateRenderTargetView(rt, nullptr, &rtv))) { Log("probe: cannot make the target"); if (rt) rt->Release(); return false; }
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; g_dev->CreateTexture2D(&td, nullptr, &st);
    FrameCB fc = {}; fc.depthp[0] = ms ? 2.0f : 1.0f; fc.screen[0] = (float)w; fc.screen[1] = (float)h; fc.tint[0] = fx; fc.tint[1] = fy;
    g_ctx->UpdateSubresource(g_cbFrame, 0, nullptr, &fc, 0, 0);
    D3D11_VIEWPORT vp = {0, 0, 8, 1, 0, 1};
    g_ctx->OMSetRenderTargets(1, &rtv, nullptr); g_ctx->RSSetViewports(1, &vp); g_ctx->RSSetState(g_rs); g_ctx->OMSetBlendState(g_blend, nullptr, 0xffffffff);
    g_ctx->IASetInputLayout(nullptr); g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); g_ctx->VSSetShader(g_vsFull, nullptr, 0); g_ctx->PSSetShader(g_psProbe, nullptr, 0);
    g_ctx->PSSetConstantBuffers(0, 1, &g_cbFrame); g_ctx->PSSetShaderResources(ms ? 1 : 2, 1, &sceneSrv);
    g_ctx->GSSetShader(nullptr, nullptr, 0); g_ctx->HSSetShader(nullptr, nullptr, 0); g_ctx->DSSetShader(nullptr, nullptr, 0);
    g_ctx->Draw(3, 0);
    ID3D11ShaderResourceView* z3[3] = {nullptr, nullptr, nullptr}; g_ctx->PSSetShaderResources(0, 3, z3);
    g_ctx->CopyResource(st, rt); D3D11_MAPPED_SUBRESOURCE m;
    bool ok = false;
    if (SUCCEEDED(g_ctx->Map(st, 0, D3D11_MAP_READ, 0, &m))) { memcpy(out, m.pData, 8 * sizeof(float)); g_ctx->Unmap(st, 0); ok = true; }
    rtv->Release(); rt->Release(); if (st) st->Release();
    return ok;
}
static void RunProbe(UINT w, UINT h, ID3D11ShaderResourceView* sceneSrv, bool ms) {
    float v[8]; if (!ProbeDepth(w, h, sceneSrv, ms, v)) return; char msg[300];
    snprintf(msg, sizeof msg, "probe: centre samples %.7g %.7g %.7g %.7g | low %.7g high %.7g left %.7g right %.7g (scene depth, %s)", v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], ms ? "multisampled" : "single sample"); Log(msg);
}
// The scene depth on an 11 x 7 grid over the middle 60 % of the screen (x 0.2..0.8, y 0.2..0.8), one line: the bot finds where on the screen a
// panel really is (the cells at the panel's distance) when its middle is not where its entity's origin says (Quarry Boat Spawn).
static void RunProbeGrid(UINT w, UINT h, ID3D11ShaderResourceView* sceneSrv, bool ms) {
    char b[24];      // one log line per row (the log takes lines up to 512 bytes): "probegrid: row J of 7: v0 .. v10"
    for (int j = 0; j < 7; j++) {
        std::string line = "probegrid: row " + std::to_string(j) + " of 7:";
        for (int i = 0; i < 11; i++) {
            float v[8]; if (!ProbeDepth(w, h, sceneSrv, ms, v, g_gridBox[0] + (g_gridBox[2] - g_gridBox[0]) * i / 10, g_gridBox[1] + (g_gridBox[3] - g_gridBox[1]) * j / 6)) { Log("probegrid: failed"); return; }
            snprintf(b, sizeof b, " %.6g", v[0]); line += b;
        }
        Log(line.c_str());
    }
}
// The distance (along the view direction) of the game's geometry under the middle of the screen, for location pings and free drawing: asked for, answered by the next frame.
static volatile LONG g_centerReq, g_centerReady; static float g_centerDist = -1;
static volatile float g_centerFx = 0.5f, g_centerFy = 0.5f;
void ModelGpuRequestDepthAt(float fx, float fy) { g_centerFx = fx; g_centerFy = fy; InterlockedExchange(&g_centerReady, 0); InterlockedExchange(&g_centerReq, 1); }
void ModelGpuRequestCenterDepth() { ModelGpuRequestDepthAt(0.5f, 0.5f); }
bool ModelGpuTakeCenterDepth(float* dist) { if (!InterlockedExchange(&g_centerReady, 0)) return false; *dist = g_centerDist; return true; }
static void AnswerCenterDepth(UINT w, UINT h) {
    ID3D11ShaderResourceView* sceneSrv = nullptr; bool ms = false; float v[8]; g_centerDist = -1;
    if (!GetCfg().noModelDepth && DepthScene(&sceneSrv, &ms) && ProbeDepth(w, h, sceneSrv, ms, v, g_centerFx, g_centerFy)) {
        float z = 0; int n = 0; for (int i = 0; i < (ms ? 4 : 1); ++i) if (v[i] > 1e-7f) { z += v[i]; ++n; }
        if (n) g_centerDist = GetCfg().depthNear / (z / n);      // the game stores depth as near / distance
    }
    InterlockedExchange(&g_centerReady, 1);
}


// The scene depth as a 192 x 108 picture with the camera it was drawn from, for the bot's walking (tools/win/wnav.py builds a map of where
// it can walk from these): witness_depth.bin next to the DLL, replaced whole. Header: "WDD1", width, height (uint32), then floats: camera
// position x y z, yaw, pitch, vertical field of view (degrees), aspect, the game's near plane; then width * height floats, top row first,
// each the game's depth (near / distance along the view, 0 for the sky).
void ModelGpuRequestDepthDump() { InterlockedExchange(&g_dumpReq, 1); }
static void RunDepthDump(UINT w, UINT h, ID3D11ShaderResourceView* sceneSrv, bool ms, const wm::Cam& cam) {
    const UINT W = 192, H = 108;
    D3D11_TEXTURE2D_DESC td = {}; td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R32_FLOAT; td.SampleDesc.Count = 1; td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D *rt = nullptr, *st = nullptr; ID3D11RenderTargetView* rv = nullptr;
    if (!g_psDump || FAILED(g_dev->CreateTexture2D(&td, nullptr, &rt)) || FAILED(g_dev->CreateRenderTargetView(rt, nullptr, &rv))) { Log("depthdump: cannot make the target"); if (rt) rt->Release(); return; }
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; g_dev->CreateTexture2D(&td, nullptr, &st);
    FrameCB fc = {}; fc.depthp[0] = ms ? 2.0f : 1.0f; fc.tint[0] = (float)w / W; fc.tint[1] = (float)h / H;
    g_ctx->UpdateSubresource(g_cbFrame, 0, nullptr, &fc, 0, 0);
    D3D11_VIEWPORT vp = {0, 0, (float)W, (float)H, 0, 1};
    g_ctx->OMSetRenderTargets(1, &rv, nullptr); g_ctx->RSSetViewports(1, &vp); g_ctx->RSSetState(g_rs); g_ctx->OMSetBlendState(g_blend, nullptr, 0xffffffff);
    g_ctx->IASetInputLayout(nullptr); g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); g_ctx->VSSetShader(g_vsFull, nullptr, 0); g_ctx->PSSetShader(g_psDump, nullptr, 0);
    g_ctx->PSSetConstantBuffers(0, 1, &g_cbFrame); g_ctx->PSSetShaderResources(ms ? 1 : 2, 1, &sceneSrv);
    g_ctx->GSSetShader(nullptr, nullptr, 0); g_ctx->HSSetShader(nullptr, nullptr, 0); g_ctx->DSSetShader(nullptr, nullptr, 0);
    g_ctx->Draw(3, 0);
    ID3D11ShaderResourceView* z3[3] = {nullptr, nullptr, nullptr}; g_ctx->PSSetShaderResources(0, 3, z3);
    g_ctx->CopyResource(st, rt); D3D11_MAPPED_SUBRESOURCE m;
    if (st && SUCCEEDED(g_ctx->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
        std::vector<float> px(W * H); for (UINT y = 0; y < H; y++) memcpy(&px[y * W], (const char*)m.pData + y * m.RowPitch, W * sizeof(float));
        g_ctx->Unmap(st, 0);
        wchar_t path[MAX_PATH]; GetModuleFileNameW(g_selfModule(), path, MAX_PATH); wchar_t* sl = wcsrchr(path, L'\\'); if (sl) sl[1] = 0;
        wchar_t tmp[MAX_PATH], fin[MAX_PATH]; wcscpy_s(tmp, path); wcscat_s(tmp, L"witness_depth.tmp"); wcscpy_s(fin, path); wcscat_s(fin, L"witness_depth.bin");
        if (FILE* f = _wfopen(tmp, L"wb")) {
            uint32_t hd[3] = {0x31444457u, W, H}; float c[8] = {cam.pos[0], cam.pos[1], cam.pos[2], cam.theta, cam.phi, cam.vfovDeg, cam.aspect, GetCfg().depthNear};
            fwrite(hd, 4, 3, f); fwrite(c, 4, 8, f); fwrite(px.data(), 4, px.size(), f); fclose(f); MoveFileExW(tmp, fin, MOVEFILE_REPLACE_EXISTING);
        }
    } else Log("depthdump: cannot read the target");
    rv->Release(); rt->Release(); if (st) st->Release();
}

bool ModelGpuDraw(ID3D11RenderTargetView* rtv, UINT w, UINT h, const wm::Cam& cam, bool valid, unsigned seq, const float pos[3], float yaw, unsigned playerId, bool first, const ModelExtra* extra) {
    if (!g_ready) return false;
    InterlockedExchange(&g_modelRenderThread, (LONG)GetCurrentThreadId());
    if (first && InterlockedExchange(&g_centerReq, 0)) AnswerCenterDepth(w, h);
    if (first && (g_probeReq || g_gridReq || g_dumpReq)) {      // answered here, before anything returns early: with no other player to draw they were never answered
        ID3D11ShaderResourceView* srv = nullptr; bool ms = false; bool have = !GetCfg().noModelDepth && DepthScene(&srv, &ms);
        if (InterlockedExchange(&g_probeReq, 0)) { if (have) RunProbe(w, h, srv, ms); else Log("probe: no scene depth buffer found"); }
        if (InterlockedExchange(&g_gridReq, 0)) { if (have) RunProbeGrid(w, h, srv, ms); else Log("probegrid: no scene depth buffer found"); }
        if (InterlockedExchange(&g_dumpReq, 0)) { if (have) RunDepthDump(w, h, srv, ms, cam); else Log("depthdump: no scene depth buffer found"); }
    }
    static unsigned s_frames; if (first) { g_stats.frames = ++s_frames; g_stats.submittedPlayers = 0; g_playerDraws.clear(); }
    auto& avatar = g_L->avatars[GetCfg().noGroupAvatarState ? 0 : playerId]; if (!avatar) avatar = std::make_unique<wm::Avatar>(g_L->gait, playerId + 1);
    wm::Avatar* g_avatar = avatar.get();
    g_avatar->Update(GetTickCount64(), valid, seq, pos, yaw);
    g_stats.drawn = false;
    if (!valid) { g_stats.visiblePixels = 0; g_queryPending = false; }
    if (!valid || !EnsureDepth(w, h)) return valid;
    const Cfg& cfg = GetCfg();
    struct Motion { wm::GestureState state; ULONGLONG at=0; };
    static std::map<unsigned,Motion> motions;
    auto& motion=motions[playerId]; auto now=GetTickCount64();
    float dt=motion.at ? float(now-motion.at)/1000.f : 0; motion.at=now;
    int id=extra && !cfg.noEmoteAnim ? extra->emote : 0; float t=extra ? extra->emoteT : 0;
    if (!witness::ValidEmote(id)) id=0;
    g_avatar->SetBusy(id != 0 || (extra && extra->seated));      // no fidget during an emote or on a stool
    wm::ClipPick pk = g_avatar->Pick(g_L->model);
    if(extra && extra->seated) wtv::seatedGesture(motion.state,t,extra->proximitySpeaking,dt);
    else motion.state.Step(id,t,g_avatar->speed(),extra && extra->proximitySpeaking,dt);
    std::vector<float> bones(wm::kMaxJoints * 16, 0.0f);
    static std::map<unsigned, std::pair<wradio::Raise, uint64_t>> radioPose;
    auto& radio = radioPose[playerId]; uint64_t radioNow=GetTickCount64();
    radio.first.Step(extra && extra->radioTx && !cfg.noRadioPose, radio.second ? (radioNow-radio.second)/1000.0f : 0); radio.second=radioNow;
    float radioHand[16];
    // the life layer: what this player's figure does by itself (model/life.h), fed with the frame time, its speed, where the local camera is and how the body accelerates
    auto& trackp = g_L->life[playerId]; if (!trackp) trackp = std::make_unique<LifeTrack>(playerId + 1); LifeTrack& track = *trackp;
    {
        const float* P = g_avatar->pos(); float yawA = g_avatar->yaw() + g_L->yaw; float left[3] = {-sinf(yawA), cosf(yawA), 0}, front[3] = {cosf(yawA), sinf(yawA), 0};
        wm::LifeInput li; li.dt = dt; li.speed = g_avatar->speed(); li.viewPitch = extra ? extra->viewPitch : 0;
        float d[3] = {cam.pos[0] - P[0], cam.pos[1] - P[1], cam.pos[2] - P[2]}; float dist = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);      // the remote's pose is its camera: its eyes
        if (dist > 1e-3f) { li.viewer = true; li.viewerDist = dist; li.viewerDir[0] = (d[0]*left[0] + d[1]*left[1]) / dist; li.viewerDir[1] = d[2] / dist; li.viewerDir[2] = (d[0]*front[0] + d[1]*front[1]) / dist; }
        li.headBusy = id != 0 || (extra && extra->seated) || motion.state.pose[wm::Head].weight > 0.05f || motion.state.pose[wm::Neck].weight > 0.05f;
        li.viewYawOffset = g_avatar->yawOffset(); li.emote = id;
        if (track.have && dt > 1e-3f && dt < 0.5f) {
            float v[3], a[3]; for (int k = 0; k < 3; k++) { v[k] = (P[k] - track.lastPos[k]) / dt; a[k] = (v[k] - track.vel[k]) / dt; }
            float kf = 1 - expf(-dt / 0.08f); for (int k = 0; k < 3; k++) { if (fabsf(a[k]) > 40) a[k] = 0; track.acc[k] += (a[k] - track.acc[k]) * kf; track.vel[k] = v[k]; }      // (a teleport is not a push)
        } else if (!track.have || dt >= 0.5f) { memset(track.vel, 0, 12); memset(track.acc, 0, 12); }
        memcpy(track.lastPos, P, 12); track.have = true;
        li.accel[0] = track.acc[0]*left[0] + track.acc[1]*left[1]; li.accel[2] = track.acc[0]*front[0] + track.acc[1]*front[1];
        float cycle = pk.clip >= 0 && pk.clip < (int)g_L->model.clips.size() ? g_L->model.clips[pk.clip].duration : 1.0f;
        li.accel[1] = li.speed > 0.35f ? (2.5f + li.speed) * sinf(4 * 3.14159265f * pk.time / std::max(cycle, 0.2f)) : 0.0f;      // the gait's bounce: two steps a cycle
        for (float& a : li.accel) a = std::max(-15.0f, std::min(15.0f, a));
        if (first && InterlockedExchange(&g_forceBlink, 0)) for (auto& kv : g_L->life) if (kv.second->state.blinkT < 0) { kv.second->state.blinkT = 0; kv.second->state.blinkSlow = 6; }      // a slow blink (1.2 s) for pictures
        if (!cfg.noModelLife) track.state.Step(li);
        bool closed = track.state.blink > 0.9f; if (closed && !track.wasClosed) track.blinks++; track.wasClosed = closed;
        {   // footsteps: the walk and run cycles plant a foot at phase 0 and 0.5 (the generator's planted-foot trajectory); the sound is placed where the walker is
            const bool gaitClip = pk.clip >= 0 && pk.clip < (int)g_L->model.clips.size() && (g_L->model.clips[pk.clip].name == "walk" || g_L->model.clips[pk.clip].name == "run");
            int foot = 0; int n = track.steps.Step(gaitClip ? pk.time / std::max(cycle, 0.2f) : 0.0f, gaitClip && li.speed > 0.35f, &foot);
            for (int k = 0; k < n; k++) VoiceFootstep(playerId, P, li.speed, g_L->model.clips[pk.clip].name == "run");
        }
        bool fidgeting = g_avatar->fidgeting(); if (fidgeting && !track.wasFidgeting) track.fidgets++; track.wasFidgeting = fidgeting;
    }
    wm::PoseLayers layers; layers.gesture = &motion.state; layers.radioRaise = radio.first.value; layers.radioHand = radioHand; layers.life = cfg.noModelLife ? nullptr : &track.state; layers.rig = &g_L->rig;
    wm::JointMatricesBlendEx(g_L->model, pk.from, pk.fromTime, pk.clip, pk.time, pk.blend, bones.data(), layers);
    FrameCB fc = {}; wm::ViewProj(cam, fc.viewProj);
    {   // the camera for the lighting and the shadow: where it is, its axes and the tangents of its half fields of view
        float th = cam.theta, ph = cam.phi; float fwd[3] = {cosf(ph) * cosf(th), cosf(ph) * sinf(th), sinf(ph)}, right[3] = {sinf(th), -cosf(th), 0}, up[3] = {-sinf(ph) * cosf(th), -sinf(ph) * sinf(th), cosf(ph)};
        float tv = tanf(cam.vfovDeg * 3.14159265f / 360.0f); memcpy(fc.camPos, cam.pos, 12); memcpy(fc.camFwd, fwd, 12); memcpy(fc.camRight, right, 12); memcpy(fc.camUp, up, 12); fc.camRight[3] = tv * cam.aspect; fc.camUp[3] = tv;
        fc.look[0] = cfg.modelSun; fc.look[1] = cfg.modelAmbient; fc.look[2] = cfg.modelShading ? 1.0f : 0.0f; fc.look[3] = cfg.modelShadow;
        fc.shadow[3] = cfg.noModelMaterialMap ? 1.0f : 0.0f;
        float n = cam.nearZ, f = cam.farZ; fc.shadow[1] = f / (f - n); fc.shadow[2] = -f * n / (f - n);
    }
    for (unsigned group = 0; group <= witness::kAppearanceFeatures; ++group)
        witness::AppearanceTint(extra ? extra->appearance : witness::Appearance{}, group, fc.appearance[group]);
    float feet[3] = {g_avatar->pos()[0], g_avatar->pos()[1], g_avatar->pos()[2] - wm::kCameraEye};
    if(extra && extra->seated) feet[2]=wtv::seatModelGround(g_avatar->pos()[2]);
    wm::ModelMatrix(feet, g_avatar->yaw(), g_L->fit, g_L->yaw, fc.model); fc.shadow[0] = feet[2] + 0.01f;

    g_stats.emote = extra ? extra->emote : 0; g_stats.lift = g_stats.lean = g_stats.roll = g_stats.dyaw = 0;
    float L[4] = {0.35f, 0.25f, 0.9f, cfg.modelShading && cfg.noModelNormalMap ? 1.0f : 0.55f}, T[4] = {1, 1, 1, 1}; memcpy(fc.light, L, 16);      // light.w: the old shading's ambient share; in the lit shading 1 means 'no normal map' (diagnostic)
    {      // see through a player who is in the way of a puzzle (or in your face): the alpha moves to its target a step at a time
        GhostState& gs = g_ghost[playerId]; ULONGLONG nowT = GetTickCount64(); float dt = gs.t ? std::min(0.25f, float(nowT - gs.t) / 1000.0f) : 1.0f; gs.t = nowT;
        if (!cfg.noSeeThrough) {
            if (nowT - g_panelPtsT > 1000) { g_panelPtsT = nowT; DevMenuPanelPoints(&g_panelPts); g_panelCount = g_panelPts.size() / 3; }
            float fwd[3] = {cosf(cam.phi) * cosf(cam.theta), cosf(cam.phi) * sinf(cam.theta), sinf(cam.phi)}; wghost::Params gp; gp.minAlpha = cfg.ghostAlpha;
            gs.target = wghost::Alpha(cam.pos, fwd, feet, g_panelPts.data(), g_panelCount, gp);
        } else gs.target = 1.0f;
        gs.alpha = wghost::Approach(gs.alpha, gs.target, dt, 5.0f); T[3] = gs.alpha;
    }
    memcpy(fc.tint, T, 16);
    ID3D11ShaderResourceView* sceneSrv = nullptr; bool ms = false;
    bool haveScene = !cfg.noModelDepth && DepthScene(&sceneSrv, &ms);
    if (InterlockedExchange(&g_probeReq, 0)) { if (haveScene) RunProbe(w, h, sceneSrv, ms); else Log("probe: no scene depth buffer found"); }
    if (InterlockedExchange(&g_gridReq, 0)) { if (haveScene) RunProbeGrid(w, h, sceneSrv, ms); else Log("probegrid: no scene depth buffer found"); }
    fc.screen[0] = (float)w; fc.screen[1] = (float)h;
    fc.depthp[0] = haveScene ? (ms ? 2.0f : 1.0f) : 0.0f; fc.depthp[1] = 4; fc.depthp[2] = cfg.depthBias; fc.depthp[3] = cfg.depthNear;
    g_ctx->UpdateSubresource(g_cbFrame, 0, nullptr, &fc, 0, 0); g_ctx->UpdateSubresource(g_cbBones, 0, nullptr, bones.data(), 0, 0);
    if (first) g_ctx->ClearDepthStencilView(g_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
    D3D11_VIEWPORT vp = {0, 0, (float)w, (float)h, 0, 1};
    g_ctx->OMSetRenderTargets(1, &rtv, g_dsv); g_ctx->RSSetViewports(1, &vp); g_ctx->RSSetState(g_rs);
    g_ctx->OMSetBlendState(haveScene ? g_blendAlpha : g_blend, nullptr, 0xffffffff); g_ctx->OMSetDepthStencilState(g_dss, 0);
    UINT stride = sizeof(wm::Vertex), off = 0;
    g_ctx->IASetInputLayout(g_layout); g_ctx->IASetVertexBuffers(0, 1, &g_L->vb, &stride, &off); g_ctx->IASetIndexBuffer(g_L->ib, DXGI_FORMAT_R32_UINT, 0);
    g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->VSSetShader(g_vs, nullptr, 0); g_ctx->PSSetShader(g_ps, nullptr, 0);
    ID3D11Buffer* cbs[2] = {g_cbFrame, g_cbBones}; g_ctx->VSSetConstantBuffers(0, 2, cbs); g_ctx->PSSetConstantBuffers(0, 1, cbs);
    g_ctx->PSSetShaderResources(0, 1, &g_L->tex); g_ctx->PSSetShaderResources(3, 1, &g_L->texN); g_ctx->PSSetShaderResources(4, 1, &g_L->texM); g_ctx->PSSetSamplers(0, 1, &g_samp);
    ID3D11ShaderResourceView* nul = nullptr;
    if (haveScene) g_ctx->PSSetShaderResources(ms ? 1 : 2, 1, &sceneSrv);
    if ((cfg.depthView || g_depthViewCmd) && haveScene) {     // debugging: show the scene depth instead of the model
        g_ctx->IASetInputLayout(nullptr); g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); g_ctx->VSSetShader(g_vsFull, nullptr, 0); g_ctx->PSSetShader(g_psDepth, nullptr, 0);
        g_ctx->OMSetBlendState(g_blend, nullptr, 0xffffffff); g_ctx->PSSetConstantBuffers(0, 1, cbs); g_ctx->Draw(3, 0);
        ID3D11ShaderResourceView* z[3] = {nullptr, nullptr, nullptr}; g_ctx->PSSetShaderResources(0, 3, z); return true;
    }
    g_ctx->GSSetShader(nullptr, nullptr, 0); g_ctx->HSSetShader(nullptr, nullptr, 0); g_ctx->DSSetShader(nullptr, nullptr, 0);
    if (!g_query) { D3D11_QUERY_DESC qd = {D3D11_QUERY_OCCLUSION, 0}; g_dev->CreateQuery(&qd, &g_query); }
    if (g_queryPending) { UINT64 n; if (g_ctx->GetData(g_query, &n, sizeof n, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK) { g_stats.visiblePixels = (unsigned)n; g_queryPending = false; } }
    bool timed = g_query && !g_queryPending; if (timed) g_ctx->Begin(g_query);
    g_ctx->DrawIndexed((UINT)g_L->model.idx.size(), 0, 0); ++g_stats.submittedPlayers;
    PlayerDraw record = {}; record.ground=feet[2]; record.appearance = extra ? witness::ClampAppearance(extra->appearance) : witness::Appearance{}; record.emote=motion.state.emote; record.talk=motion.state.talk;
    for(const auto& j:motion.state.pose) record.gesture=std::max(record.gesture,j.weight);
    // Actual right wrist position from the submitted skin palette, model space.
    for(size_t j=0;j<g_L->model.joints.size();++j) if(g_L->model.nodes[g_L->model.joints[j]].name=="hand.R") {
        const float* ib=g_L->model.ibm.data()+j*16; float rest[3];
        for(int r=0;r<3;++r) rest[r]=-(ib[r*4]*ib[12]+ib[r*4+1]*ib[13]+ib[r*4+2]*ib[14]);
        const float* skin=bones.data()+j*16; for(int r=0;r<3;++r) record.hand[r]=skin[r]*rest[0]+skin[4+r]*rest[1]+skin[8+r]*rest[2]+skin[12+r];
    }
    record.id = playerId; record.seq = seq; memcpy(record.pos, g_avatar->pos(), 12); record.speed = g_avatar->speed(); record.clip = pk.clip; record.yaw = g_avatar->yaw(); g_playerDraws.push_back(record);
    // Only the nearest requested player's back gets a TV; both draw calls use
    // the same chest skin, model transform and scene depth as the adventurer.
    if (TvTarget() == playerId) {
        unsigned chest = 0; bool foundChest = false;
        for (size_t j = 0; j < g_L->model.joints.size(); ++j)
            if (g_L->model.nodes[g_L->model.joints[j]].name == "chest") { chest = unsigned(j); foundChest = true; break; }
        if (foundChest) {
            uint64_t now = GetTickCount64();
            if (g_tvPlayer != playerId) { g_tvMotion = {}; g_tvTime = now; g_tvPlayer = playerId; }
            bool live = false; ID3D11ShaderResourceView* screen = TvTexture(g_dev, g_ctx, &live);
            g_tvMotion.step(float(now - g_tvTime) / 1000.f, g_avatar->speed(), TvEnabled()); g_tvTime = now;
            auto prop = wtv::mesh(chest, g_tvMotion);
            if (!g_vbTv) { D3D11_BUFFER_DESC d = {}; d.ByteWidth = 1024 * sizeof(wm::Vertex); d.Usage = D3D11_USAGE_DYNAMIC; d.BindFlags = D3D11_BIND_VERTEX_BUFFER; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; g_dev->CreateBuffer(&d, nullptr, &g_vbTv); }
            D3D11_MAPPED_SUBRESOURCE mapped;
            if (screen && g_vbTv && prop.body.size() + prop.screen.size() <= 1024 && SUCCEEDED(g_ctx->Map(g_vbTv, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                memcpy(mapped.pData, prop.body.data(), prop.body.size() * sizeof(wm::Vertex));
                memcpy((wm::Vertex*)mapped.pData + prop.body.size(), prop.screen.data(), prop.screen.size() * sizeof(wm::Vertex)); g_ctx->Unmap(g_vbTv, 0);
                if (!g_tvQuery) { D3D11_QUERY_DESC q = {D3D11_QUERY_OCCLUSION, 0}; g_dev->CreateQuery(&q, &g_tvQuery); }
                if (g_tvQueryPending) { UINT64 n; if (g_ctx->GetData(g_tvQuery, &n, sizeof n, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK) { TvVisible(unsigned(n)); g_tvQueryPending = false; } }
                bool tvQuery = g_tvQuery && !g_tvQueryPending; if (tvQuery) g_ctx->Begin(g_tvQuery);
                g_ctx->IASetVertexBuffers(0, 1, &g_vbTv, &stride, &off);
                float lookWas = fc.look[2]; fc.look[2] = 0; g_ctx->UpdateSubresource(g_cbFrame, 0, nullptr, &fc, 0, 0);      // the prop keeps the flat shading (it has no maps of its own)
                g_ctx->Draw((UINT)prop.body.size(), 0);
                // The screen emits its own light; retain ghost opacity and depth testing.
                fc.light[3] = 1; g_ctx->UpdateSubresource(g_cbFrame, 0, nullptr, &fc, 0, 0);
                g_ctx->PSSetShaderResources(0, 1, &screen); g_ctx->Draw((UINT)prop.screen.size(), (UINT)prop.body.size());
                if (tvQuery) { g_ctx->End(g_tvQuery); g_tvQueryPending = true; }
                fc.light[3] = L[3]; fc.look[2] = lookWas;
                TvDrawn(unsigned(prop.body.size()+prop.screen.size()), live, g_tvMotion.sway, g_tvMotion.power);
            }
        }
    }
    if (timed) { g_ctx->End(g_query); g_queryPending = true; }
    // the sun shadow: the figure flattened along the light onto the ground under its feet, drawn into a mask, then laid on the game's geometry at that height
    if (haveScene && cfg.modelShading && cfg.modelShadow > 0 && g_maskRtv && g_depthSrv) {
        float l[3] = {L[0], L[1], L[2]}; float ll = sqrtf(l[0]*l[0] + l[1]*l[1] + l[2]*l[2]); for (float& v : l) v /= ll;
        float z0 = fc.shadow[0], kx = l[0] / l[2], ky = l[1] / l[2];
        float Sh[16] = {1, 0, 0, 0,  0, 1, 0, 0,  -kx, -ky, 0, 0,  kx * z0, ky * z0, z0, 1};      // column-major: x' = x - kx (z - z0), y' = y - ky (z - z0), z' = z0
        float modelWas[16]; memcpy(modelWas, fc.model, 64); Mul16(Sh, modelWas, fc.model);
        // where the shadow can fall on the screen: the flattened bounding box, projected (the whole screen when a corner is behind the camera)
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f; bool behind = false;
        for (int c = 0; c < 8; c++) {
            float mp[3] = {c & 1 ? g_L->model.bmax[0] : g_L->model.bmin[0], c & 2 ? g_L->model.bmax[1] : g_L->model.bmin[1], c & 4 ? g_L->model.bmax[2] : g_L->model.bmin[2]}, wp[3];
            for (int r = 0; r < 3; r++) wp[r] = fc.model[r] * mp[0] + fc.model[4 + r] * mp[1] + fc.model[8 + r] * mp[2] + fc.model[12 + r];
            float cl[4]; for (int r = 0; r < 4; r++) cl[r] = fc.viewProj[r] * wp[0] + fc.viewProj[4 + r] * wp[1] + fc.viewProj[8 + r] * wp[2] + fc.viewProj[12 + r];
            if (cl[3] <= 0.05f) { behind = true; break; }
            float sx = (cl[0] / cl[3] * 0.5f + 0.5f) * w, sy = (0.5f - cl[1] / cl[3] * 0.5f) * h; minX = std::min(minX, sx); maxX = std::max(maxX, sx); minY = std::min(minY, sy); maxY = std::max(maxY, sy);
        }
        if (behind) { minX = minY = 0; maxX = (float)w; maxY = (float)h; }
        minX = std::max(0.0f, minX - 12); minY = std::max(0.0f, minY - 12); maxX = std::min((float)w, maxX + 12); maxY = std::min((float)h, maxY + 12);
        if (maxX > minX && maxY > minY) {
            g_ctx->UpdateSubresource(g_cbFrame, 0, nullptr, &fc, 0, 0);
            const float zero[4] = {0, 0, 0, 0}; g_ctx->ClearRenderTargetView(g_maskRtv, zero);
            g_ctx->OMSetRenderTargets(1, &g_maskRtv, nullptr); g_ctx->OMSetBlendState(g_blend, nullptr, 0xffffffff); g_ctx->PSSetShader(g_psMask, nullptr, 0);
            g_ctx->IASetInputLayout(g_layout); g_ctx->IASetVertexBuffers(0, 1, &g_L->vb, &stride, &off); g_ctx->IASetIndexBuffer(g_L->ib, DXGI_FORMAT_R32_UINT, 0); g_ctx->VSSetShader(g_vs, nullptr, 0);
            g_ctx->DrawIndexed((UINT)g_L->model.idx.size(), 0, 0);
            memcpy(fc.model, modelWas, 64); g_ctx->UpdateSubresource(g_cbFrame, 0, nullptr, &fc, 0, 0);
            g_ctx->OMSetRenderTargets(1, &rtv, nullptr); g_ctx->OMSetBlendState(g_blendAlpha, nullptr, 0xffffffff); g_ctx->OMSetDepthStencilState(nullptr, 0);
            D3D11_VIEWPORT svp = {minX, minY, maxX - minX, maxY - minY, 0, 1}; g_ctx->RSSetViewports(1, &svp);
            g_ctx->IASetInputLayout(nullptr); g_ctx->VSSetShader(g_vsFull, nullptr, 0); g_ctx->PSSetShader(g_psShadow, nullptr, 0);
            g_ctx->PSSetShaderResources(5, 1, &g_maskSrv); g_ctx->PSSetShaderResources(6, 1, &g_depthSrv); if (haveScene) g_ctx->PSSetShaderResources(ms ? 1 : 2, 1, &sceneSrv);
            if (!g_qShadow) { D3D11_QUERY_DESC qd = {D3D11_QUERY_OCCLUSION, 0}; g_dev->CreateQuery(&qd, &g_qShadow); }
            if (g_qShadowPending) { UINT64 n; if (g_ctx->GetData(g_qShadow, &n, sizeof n, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK) { g_stats.shadowPixels = (unsigned)n; g_qShadowPending = false; } }
            bool sq = g_qShadow && !g_qShadowPending; if (sq) g_ctx->Begin(g_qShadow);
            g_ctx->Draw(3, 0);
            if (sq) { g_ctx->End(g_qShadow); g_qShadowPending = true; }
            g_ctx->RSSetViewports(1, &vp); g_ctx->OMSetRenderTargets(1, &rtv, g_dsv); g_ctx->OMSetDepthStencilState(g_dss, 0);
        } else memcpy(fc.model, modelWas, 64);
    }
    ID3D11ShaderResourceView* z7[7] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}; g_ctx->PSSetShaderResources(0, 7, z7); (void)nul;      // the game's depth buffer (and our own) must not stay bound as inputs
    g_stats.drawn = true; g_stats.clip = pk.clip; g_stats.speed = g_avatar->speed(); g_stats.animTime = pk.time; g_stats.from = pk.from; g_stats.blend = pk.blend;
    g_stats.blinks = track.blinks; g_stats.fidgets = track.fidgets; g_stats.glancing = track.state.glancing; g_stats.fidgeting = g_avatar->fidgeting(); g_stats.headYaw = track.state.headYaw; g_stats.headPitch = track.state.headPitch;
    g_stats.breath = track.state.breath; g_stats.exertion = track.state.exertion; g_stats.blink = track.state.blink; g_stats.scarf = 0; for (const auto& pnd : track.state.scarfA) g_stats.scarf = std::max(g_stats.scarf, fabsf(pnd.a) + fabsf(pnd.b));
    { float e = wm::EyeHeightPosed(g_L->model, bones.data(), g_L->eyeJoint); g_stats.eye = e <= wm::kNoEye * 0.5f ? -1.0f : (e - g_L->model.bmin[1]) * g_L->fit.scale; g_stats.yaw = g_avatar->yaw(); } memcpy(g_stats.pos, g_avatar->pos(), 12); memcpy(g_stats.cam, cam.pos, 12);
    if (extra && extra->radioEquipped) {
        std::vector<LineVert> item;
        for (const auto& v : wradio::Mesh(extra->radioTx && (GetTickCount64()/180)%2==0)) {
            // The prop follows the posed wrist, in the model's original units.
            float local[3]={v.p[0]/g_L->fit.scale, (v.p[1]-.02f)/g_L->fit.scale, (v.p[2]+.04f)/g_L->fit.scale};
            float model[3], world[3];
            for(int k=0;k<3;++k) model[k]=radioHand[k]*local[0]+radioHand[4+k]*local[1]+radioHand[8+k]*local[2]+radioHand[12+k];
            for(int k=0;k<3;++k) world[k]=fc.model[k]*model[0]+fc.model[4+k]*model[1]+fc.model[8+k]*model[2]+fc.model[12+k];
            LineVert vert; memcpy(vert.p,world,12); memcpy(vert.c,v.c,16); item.push_back(vert);
        }
        float wrist[3]; for(int k=0;k<3;++k) wrist[k]=fc.model[k]*radioHand[12]+fc.model[4+k]*radioHand[13]+fc.model[8+k]*radioHand[14]+fc.model[12+k];
        if(ModelGpuDrawLines(rtv,w,h,cam,item.data(),item.size(),1,true)) RadioRecord(playerId,radio.first.value,(unsigned)item.size(),wrist);
    }
    return true;
}

bool ModelGpuDrawAudience(ID3D11RenderTargetView* rtv,UINT w,UINT h,const wm::Cam& cam) {
    if(!g_ready) return false; auto screens=TvAudienceScreens();
    for(auto it=g_audienceQueries.begin();it!=g_audienceQueries.end();) {
        if(std::none_of(screens.begin(),screens.end(),[&](const auto& d){return d.from==it->first;})) {
            if(it->second.query) it->second.query->Release(); it=g_audienceQueries.erase(it);
        } else ++it;
    }
    if(screens.empty()) return true; if(!EnsureDepth(w,h)) return false;
    if(!g_vbAudience){ D3D11_BUFFER_DESC d={}; d.ByteWidth=1024*sizeof(wm::Vertex); d.Usage=D3D11_USAGE_DYNAMIC; d.BindFlags=D3D11_BIND_VERTEX_BUFFER; d.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE; if(FAILED(g_dev->CreateBuffer(&d,nullptr,&g_vbAudience))) return false; }
    FrameCB fc={}; wtv::audiencePalette(fc.appearance); wm::ViewProj(cam,fc.viewProj); for(unsigned i=0;i<4;++i) fc.model[i*5]=1;
    float L[4]={.35f,.25f,.9f,.55f},T[4]={1,1,1,1}; memcpy(fc.light,L,16); memcpy(fc.tint,T,16); fc.screen[0]=float(w); fc.screen[1]=float(h);
    const Cfg& cfg=GetCfg(); ID3D11ShaderResourceView* sceneSrv=nullptr; bool ms=false; bool haveScene=!cfg.noModelDepth && DepthScene(&sceneSrv,&ms);
    fc.depthp[0]=haveScene?(ms?2.f:1.f):0; fc.depthp[1]=4; fc.depthp[2]=cfg.depthBias; fc.depthp[3]=cfg.depthNear;
    std::vector<float> bones(wm::kMaxJoints*16,0); for(unsigned i=0;i<wm::kMaxJoints;++i) for(unsigned k=0;k<4;++k) bones[i*16+k*5]=1;
    g_ctx->UpdateSubresource(g_cbBones,0,nullptr,bones.data(),0,0); g_ctx->UpdateSubresource(g_cbFrame,0,nullptr,&fc,0,0);
    if(g_playerDraws.empty()) g_ctx->ClearDepthStencilView(g_dsv,D3D11_CLEAR_DEPTH,1.f,0);
    D3D11_VIEWPORT vp={0,0,float(w),float(h),0,1}; g_ctx->OMSetRenderTargets(1,&rtv,g_dsv); g_ctx->RSSetViewports(1,&vp); g_ctx->RSSetState(g_rs);
    g_ctx->OMSetBlendState(haveScene?g_blendAlpha:g_blend,nullptr,0xffffffff); g_ctx->OMSetDepthStencilState(g_dss,0);
    UINT stride=sizeof(wm::Vertex),off=0; g_ctx->IASetInputLayout(g_layout); g_ctx->IASetVertexBuffers(0,1,&g_vbAudience,&stride,&off); g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->VSSetShader(g_vs,nullptr,0); g_ctx->PSSetShader(g_ps,nullptr,0); ID3D11Buffer* cbs[2]={g_cbFrame,g_cbBones}; g_ctx->VSSetConstantBuffers(0,2,cbs); g_ctx->PSSetConstantBuffers(0,1,cbs); g_ctx->PSSetSamplers(0,1,&g_samp);
    if(haveScene) g_ctx->PSSetShaderResources(ms?1:2,1,&sceneSrv); g_ctx->GSSetShader(nullptr,nullptr,0); g_ctx->HSSetShader(nullptr,nullptr,0); g_ctx->DSSetShader(nullptr,nullptr,0);
    for(const auto& screen:screens){
        auto prop=wtv::audienceMesh(screen); bool live=false; ID3D11ShaderResourceView* picture=TvAudienceTexture(g_dev,g_ctx,screen.target,&live); if(!picture || prop.body.size()+prop.screen.size()>1024) continue;
        D3D11_MAPPED_SUBRESOURCE mapped; if(FAILED(g_ctx->Map(g_vbAudience,0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) continue;
        memcpy(mapped.pData,prop.body.data(),prop.body.size()*sizeof(wm::Vertex)); memcpy((wm::Vertex*)mapped.pData+prop.body.size(),prop.screen.data(),prop.screen.size()*sizeof(wm::Vertex)); g_ctx->Unmap(g_vbAudience,0);
        auto& query=g_audienceQueries[screen.from]; if(!query.query){ D3D11_QUERY_DESC d={D3D11_QUERY_OCCLUSION,0}; g_dev->CreateQuery(&d,&query.query); }
        if(query.pending){ UINT64 n; if(g_ctx->GetData(query.query,&n,sizeof n,D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK){ TvAudienceVisible(screen.from,unsigned(n)); query.pending=false; } }
        g_ctx->Draw(UINT(prop.body.size()),0); fc.light[3]=1; g_ctx->UpdateSubresource(g_cbFrame,0,nullptr,&fc,0,0); g_ctx->PSSetShaderResources(0,1,&picture);
        bool timed=query.query && !query.pending; if(timed) g_ctx->Begin(query.query); g_ctx->Draw(UINT(prop.screen.size()),UINT(prop.body.size())); if(timed){ g_ctx->End(query.query); query.pending=true; }
        TvAudienceDrawn(screen.from,unsigned(prop.body.size()+prop.screen.size()),live); fc.light[3]=.55f; g_ctx->UpdateSubresource(g_cbFrame,0,nullptr,&fc,0,0);
    }
    ID3D11ShaderResourceView* z[3]={nullptr,nullptr,nullptr}; g_ctx->PSSetShaderResources(0,3,z); return true;
}

bool ModelGpuDrawLines(ID3D11RenderTargetView* rtv, UINT w, UINT h, const wm::Cam& cam, const LineVert* verts, size_t count, unsigned lineCount, bool prop, bool viewModel) {
    static unsigned frames; if (!prop) { g_lineStats.frames = ++frames; g_lineStats.lines = lineCount; g_lineStats.verts = (unsigned)count; }
    if (!g_ready || !g_vsLine || !g_psLine || !g_layoutLine || !g_vbLine) return false;
    if (count == 0) { g_lineStats.visiblePixels = 0; g_qLinePending = false; return true; }
    if (count > kLineVerts) count = kLineVerts - kLineVerts % 3;
    const Cfg& cfg = GetCfg();
    if (prop && !EnsureDepth(w,h)) return false;
    if (viewModel) g_ctx->ClearDepthStencilView(g_dsv,D3D11_CLEAR_DEPTH,1.0f,0);
    D3D11_MAPPED_SUBRESOURCE ms2; if (FAILED(g_ctx->Map(g_vbLine, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms2))) return false;
    memcpy(ms2.pData, verts, count * sizeof(LineVert)); g_ctx->Unmap(g_vbLine, 0);
    FrameCB fc; memset(&fc, 0, sizeof fc); wm::ViewProj(cam, fc.viewProj);
    ID3D11ShaderResourceView* sceneSrv = nullptr; bool ms = false;
    bool haveScene = !viewModel && !cfg.noLineDepth && DepthScene(&sceneSrv, &ms);
    fc.screen[0] = (float)w; fc.screen[1] = (float)h;
    fc.depthp[0] = haveScene ? (ms ? 2.0f : 1.0f) : 0.0f; fc.depthp[1] = 4; fc.depthp[2] = cfg.depthBias; fc.depthp[3] = cfg.depthNear;
    g_ctx->UpdateSubresource(g_cbFrame, 0, nullptr, &fc, 0, 0);
    D3D11_VIEWPORT vp = {0, 0, (float)w, (float)h, 0, 1};
    g_ctx->OMSetRenderTargets(1, &rtv, prop ? g_dsv : nullptr); g_ctx->RSSetViewports(1, &vp); g_ctx->RSSetState(g_rs);
    g_ctx->OMSetBlendState(g_blend, nullptr, 0xffffffff); g_ctx->OMSetDepthStencilState(prop ? g_dss : nullptr, 0);
    UINT stride = sizeof(LineVert), off = 0;
    g_ctx->IASetInputLayout(g_layoutLine); g_ctx->IASetVertexBuffers(0, 1, &g_vbLine, &stride, &off); g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->VSSetShader(g_vsLine, nullptr, 0); g_ctx->PSSetShader(g_psLine, nullptr, 0);
    g_ctx->VSSetConstantBuffers(0, 1, &g_cbFrame); g_ctx->PSSetConstantBuffers(0, 1, &g_cbFrame);
    if (haveScene) g_ctx->PSSetShaderResources(ms ? 1 : 2, 1, &sceneSrv);
    g_ctx->GSSetShader(nullptr, nullptr, 0); g_ctx->HSSetShader(nullptr, nullptr, 0); g_ctx->DSSetShader(nullptr, nullptr, 0);
    if (!g_qLine) { D3D11_QUERY_DESC qd = {D3D11_QUERY_OCCLUSION, 0}; g_dev->CreateQuery(&qd, &g_qLine); }
    if (!prop && g_qLinePending) { UINT64 n; if (g_ctx->GetData(g_qLine, &n, sizeof n, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK) { g_lineStats.visiblePixels = (unsigned)n; g_qLinePending = false; } }
    bool timed = !prop && g_qLine && !g_qLinePending; if (timed) g_ctx->Begin(g_qLine);
    g_ctx->Draw((UINT)count, 0);
    if (timed) { g_ctx->End(g_qLine); g_qLinePending = true; }
    ID3D11ShaderResourceView* z3[3] = {nullptr, nullptr, nullptr}; g_ctx->PSSetShaderResources(0, 3, z3);
    return true;
}
LineDrawStats ModelGpuLineStats() { return g_lineStats; }
void ModelGpuLogLineStats() { char m[160]; snprintf(m, sizeof m, "linestat: lines %u verts %u visible_px %u frames %u", g_lineStats.lines, g_lineStats.verts, g_lineStats.visiblePixels, g_lineStats.frames); Log(m); }

void ModelGpuDumpPlayers() {
    wchar_t path[MAX_PATH]; GetModuleFileNameW(g_selfModule(), path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\'); if (slash) *(slash + 1) = 0;
    wcscat_s(path, L"witness_models.json"); wchar_t tmp[MAX_PATH]; wcscpy_s(tmp, path); wcscat_s(tmp, L".tmp");
    FILE* f = _wfopen(tmp, L"w"); if (!f) return;
    fprintf(f, "{\"frame\":%u,\"players\":[", g_stats.frames); bool comma = false;
    for (const auto& p : g_playerDraws) {
        fprintf(f, "%s{\"id\":%u,\"seq\":%u,\"pos\":[%.5f,%.5f,%.5f],\"speed\":%.5f,\"clip\":%d,\"yaw\":%.4f,\"model\":\"%s\"", comma ? "," : "", p.id, p.seq, p.pos[0], p.pos[1], p.pos[2], p.speed, p.clip, p.yaw, "adventurer");
        fprintf(f,",\"model_ground\":%.5f",p.ground);
        fprintf(f, ",\"emote\":%d,\"gesture\":%.5f,\"talk\":%.5f,\"hand\":[%.5f,%.5f,%.5f]",p.emote,p.gesture,p.talk,p.hand[0],p.hand[1],p.hand[2]);
        fprintf(f, ",\"appearance\":["); for (size_t i = 0; i < witness::kAppearanceFeatures; ++i) fprintf(f, "%s%u", i ? "," : "", unsigned(p.appearance.choice[i])); fprintf(f, "]}"); comma = true;
    }
    fprintf(f, "]}\n"); fclose(f); MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING);
}

void ModelGpuLogStats() {
    char t[100]; snprintf(t, sizeof t, "modelthread: render %lu caller %lu", (unsigned long)InterlockedCompareExchange(&g_modelRenderThread, 0, 0), (unsigned long)GetCurrentThreadId()); Log(t);
    ModelDrawStats s = ModelGpuStats(); char m[480];
    snprintf(m, sizeof m, "modelstat: visible_px %u drawn %d clip %d speed %.2f anim %.3f from %d blend %.2f eye %.3f yaw %.3f frames %u model %s pos %.2f %.2f %.2f cam %.2f %.2f %.2f fx emote %d lift %.3f lean %.3f roll %.3f dyaw %.3f", s.visiblePixels, (int)s.drawn, s.clip, s.speed, s.animTime, s.from, s.blend, s.eye, s.yaw, s.frames, "adventurer", s.pos[0], s.pos[1], s.pos[2], s.cam[0], s.cam[1], s.cam[2], s.emote, s.lift, s.lean, s.roll, s.dyaw); Log(m);
    for (const auto& kv : g_ghost) { snprintf(m, sizeof m, "ghoststat: %u alpha %.3f target %.3f panels %zu", kv.first, kv.second.alpha, kv.second.target, g_panelCount); Log(m); }
    snprintf(m, sizeof m, "lifestat: blinks %u blink %.2f glancing %d head_yaw %.3f head_pitch %.3f breath %.3f exertion %.2f scarf %.3f fidgets %u fidgeting %d shadow_px %u shading %d", s.blinks, s.blink, (int)s.glancing, s.headYaw, s.headPitch, s.breath, s.exertion, s.scarf, s.fidgets, (int)s.fidgeting, s.shadowPixels, GetCfg().modelShading); Log(m);
}
