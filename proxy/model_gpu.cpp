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
#include "tv.h"
#include "tv/mesh.h"
static_assert(offsetof(wm::Vertex, appearanceGroup) == 56, "appearance input layout must match Vertex");
#include "config.h"
#include "depth.h"
#include "devmenu.h"
#include "ghost/ghost.h"

void Log(const char* msg);
extern HMODULE g_selfModule();

// One loaded character model with everything that belongs to it: the mesh on the GPU, how it is fitted and timed, and the walk state of each player drawn with it.
struct Loaded {
    wm::Model model; ID3D11Buffer *vb = nullptr, *ib = nullptr; ID3D11ShaderResourceView* tex = nullptr; wm::Fit fit = {}; wm::GaitCfg gait; float yaw = 0, height = 0; char eyeJoint[80] = "";
    std::map<unsigned, std::unique_ptr<wm::Avatar>> avatars;
    ~Loaded() { if (vb) vb->Release(); if (ib) ib->Release(); if (tex) tex->Release(); }
};
static Loaded g_adventurer;
static Loaded* g_L = &g_adventurer;
static volatile LONG g_modelRenderThread;
static bool g_ready, g_tried;
static ID3D11Device* g_dev; static ID3D11DeviceContext* g_ctx;
static ID3D11VertexShader *g_vs, *g_vsFull; static ID3D11PixelShader *g_psDepth, *g_psProbe; static volatile LONG g_probeReq, g_depthViewCmd;
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
static ID3D11Texture2D* g_depthTex; static ID3D11DepthStencilView* g_dsv; static UINT g_depthW, g_depthH;
struct PlayerDraw { int emote=0; float gesture=0, talk=0; float hand[3]={}; witness::Appearance appearance; unsigned id, seq; float pos[3], speed; int clip; float yaw; };
static std::vector<PlayerDraw> g_playerDraws;
static ID3D11VertexShader* g_vsLine; static ID3D11PixelShader* g_psLine; static ID3D11InputLayout* g_layoutLine; static ID3D11Buffer* g_vbLine; static ID3D11Query* g_qLine; static bool g_qLinePending; static LineDrawStats g_lineStats;
static const size_t kLineVerts = 65536;
struct GhostState { float alpha = 1.0f; float target = 1.0f; ULONGLONG t = 0; };
static std::map<unsigned, GhostState> g_ghost; static std::vector<float> g_panelPts; static ULONGLONG g_panelPtsT; static size_t g_panelCount;
static ModelDrawStats g_stats; static ID3D11Query* g_query; static bool g_queryPending;

struct FrameCB { float viewProj[16], model[16]; float light[4]; float tint[4]; float depthp[4]; float screen[4]; float appearance[9][4]; };       // 336 bytes

static const char* kHlsl = R"(
cbuffer Frame : register(b0) { float4x4 viewProj; float4x4 model; float4 light; float4 tint; float4 depthp; float4 screen; float4 appearance[9]; };   // screen: width, height. depthp: x mode (0 none, 1 plain, 2 multisampled), y samples, z relative bias, w the game's near plane (its depth is near / distance, reversed)
cbuffer Bones : register(b1) { float4x4 bones[256]; };
Texture2D tex : register(t0); SamplerState smp : register(s0);
Texture2DMS<float> sceneMS : register(t1); Texture2D<float> scene1 : register(t2);
struct VIn { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; uint4 j : BLENDINDICES; float4 w : BLENDWEIGHT; float4 c : COLOR0; float group : TEXCOORD2; };
struct VOut { float4 pos : SV_POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; float dist : TEXCOORD1; float4 c : COLOR0; float3 appearance : TEXCOORD2; };
VOut VS(VIn v) {
  float4x4 skin = bones[v.j.x] * v.w.x + bones[v.j.y] * v.w.y + bones[v.j.z] * v.w.z + bones[v.j.w] * v.w.w;
  float4 lp = mul(skin, float4(v.p, 1)); float3 ln = mul((float3x3)skin, v.n);
  VOut o; o.pos = mul(viewProj, mul(model, lp)); o.dist = o.pos.w; o.n = mul((float3x3)model, ln); o.uv = v.uv; o.c = v.c; o.appearance = appearance[min(8u, (uint)max(0.0, v.group))].rgb; return o; }
float4 PS(VOut i) : SV_TARGET {
  float3 n = normalize(i.n); float d = saturate(dot(n, normalize(light.xyz)));
  float3 base = lerp(float3(1, 1, 1), tex.Sample(smp, i.uv).rgb, i.c.a) * i.c.rgb;      // c.a: 1 where the vertex samples the model's texture, 0 for a flat-colour material
  // Convert the baked sRGB atlas for palette operations. Preserve the exact
  // legacy path for the identity palette.
  if (any(i.appearance != float3(1,1,1))) {
    float3 lin = lerp(base / 12.92, pow((base + 0.055) / 1.055, 2.4), step(0.04045, base));
    lin *= i.appearance;
    base = lerp(lin * 12.92, 1.055 * pow(max(lin, 0.0), 1.0 / 2.4) - 0.055, step(0.0031308, lin));
  }
  float3 c = base * tint.rgb * (light.w + (1 - light.w) * d);
  float vis = 1;
  if (depthp.x > 0.5) {      // how much of this pixel the game's own geometry leaves free: the fraction of the scene's depth samples behind the model
    int2 px = int2(i.pos.xy); float zm = depthp.w / i.dist * (1 + depthp.z);      // the model's depth as the game would store it: near / distance (dist: the view-space depth, clip w)
    if (depthp.x < 1.5) vis = (scene1.Load(int3(px, 0)) <= zm) ? 1 : 0;
    else { int ns = (int)depthp.y; float s = 0; for (int k = 0; k < ns; k++) s += (sceneMS.Load(px, k) <= zm) ? 1 : 0; vis = s / ns; }
    if (vis <= 0) discard;
  }
  return float4(c, vis * tint.w); }      // tint.w: how solid this player is (see-through players, ghost/ghost.h)
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
// debugging view of the scene depth: red = 1 - z (x10), green = z (x10), blue = untouched (z at 1)
FS VSFull(uint id : SV_VertexID) { FS o; float2 q = float2((id << 1) & 2, id & 2); o.pos = float4(q * float2(2, -2) + float2(-1, 1), 0, 1); return o; }
float4 PSDepth(FS i) : SV_TARGET {
  int2 px = int2(i.pos.xy); float z = depthp.x < 1.5 ? scene1.Load(int3(px, 0)) : sceneMS.Load(px, 0);
  return float4(saturate((1 - z) * 10), saturate(z * 10), z >= 0.99999 ? 1 : 0, 1); }
)";

static bool Fail(const char* m) { Log(m); return false; }

bool ModelGpuReady() { return g_ready; }
void ModelGpuDepthView(bool on) { InterlockedExchange(&g_depthViewCmd, on); }
void ModelGpuRequestProbe() { InterlockedExchange(&g_probeReq, 1); }
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
    float height = 1.8f, speed = 1.6f, yaw = cfg.modelYaw, runSpeed = 0, eye = wm::kCameraEye; char eyeJoint[80] = "";
    // the model's own <file>.glb.txt sets its size, walking speed and facing; witness_coop.cfg overrides it. eye_joint=<part of a joint name> sizes the model so that
    // those joints (its eyes) are at the camera's height above the feet (eye=<metres> asks for another height); run_speed=<m/s> is what the run clip was made for.
    if (FILE* sc = _wfopen((path + L".txt").c_str(), L"r")) {
        char l[200]; float v; while (fgets(l, sizeof l, sc)) { if (sscanf(l, "height=%f", &v) == 1) height = v; else if (sscanf(l, "speed=%f", &v) == 1) speed = v; else if (sscanf(l, "yaw=%f", &v) == 1) yaw += v;
            else if (sscanf(l, "run_speed=%f", &v) == 1) runSpeed = v; else if (sscanf(l, "eye=%f", &v) == 1) eye = v; else if (sscanf(l, "eye_joint=%79s", eyeJoint) == 1) {} }
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
    if (std::none_of(g_L->model.verts.begin(), g_L->model.verts.end(), [](const wm::Vertex& v) { return v.appearanceGroup > 0; }))
        Log("model: appearance regions absent; regenerate the bundled adventurer to enable palettes");
    D3D11_BUFFER_DESC bd = {}; D3D11_SUBRESOURCE_DATA sd = {};
    bd.ByteWidth = (UINT)(g_L->model.verts.size() * sizeof(wm::Vertex)); bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; sd.pSysMem = g_L->model.verts.data();
    if (FAILED(dev->CreateBuffer(&bd, &sd, &g_L->vb))) return Fail("model: vertex buffer failed");
    bd.ByteWidth = (UINT)(g_L->model.idx.size() * 4); bd.BindFlags = D3D11_BIND_INDEX_BUFFER; sd.pSysMem = g_L->model.idx.data();
    if (FAILED(dev->CreateBuffer(&bd, &sd, &g_L->ib))) return Fail("model: index buffer failed");
    // texture (a 1x1 white one when the model has none), with mip maps
    uint32_t white = 0xFFFFFFFF; int tw = g_L->model.texW ? g_L->model.texW : 1, th = g_L->model.texW ? g_L->model.texH : 1; const void* px = g_L->model.texW ? (const void*)g_L->model.rgba.data() : &white;
    D3D11_TEXTURE2D_DESC td = {}; td.Width = tw; td.Height = th; td.MipLevels = 0; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET; td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    ID3D11Texture2D* tx = nullptr;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &tx))) return Fail("model: texture failed");
    ctx->UpdateSubresource(tx, 0, nullptr, px, tw * 4, 0);
    if (FAILED(dev->CreateShaderResourceView(tx, nullptr, &g_L->tex))) return Fail("model: texture view failed");
    ctx->GenerateMips(g_L->tex); tx->Release();
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
    D3D11_SAMPLER_DESC ss = {}; ss.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; ss.AddressU = ss.AddressV = ss.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; ss.MaxLOD = D3D11_FLOAT32_MAX; ss.MaxAnisotropy = 1;
    dev->CreateSamplerState(&ss, &g_samp);
    D3D11_RASTERIZER_DESC rd = {}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE; dev->CreateRasterizerState(&rd, &g_rs);
    D3D11_DEPTH_STENCIL_DESC dd = {}; dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dd.DepthFunc = D3D11_COMPARISON_LESS; dev->CreateDepthStencilState(&dd, &g_dss);
    D3D11_BLEND_DESC bl = {}; bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; dev->CreateBlendState(&bl, &g_blend);
    bl.RenderTarget[0].BlendEnable = TRUE; bl.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA; bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO; bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE; bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD; dev->CreateBlendState(&bl, &g_blendAlpha);
    g_ready = g_vs && g_ps && g_layout && g_cbFrame && g_cbBones && g_samp && g_rs && g_dss && g_blend && g_blendAlpha && g_vsFull && g_psDepth && g_psProbe;
    if (g_ready && !LoadAdventurer()) g_ready = false;
    return g_ready;
}

static bool EnsureDepth(UINT w, UINT h) {
    if (g_dsv && g_depthW == w && g_depthH == h) return true;
    if (g_dsv) { g_dsv->Release(); g_dsv = nullptr; } if (g_depthTex) { g_depthTex->Release(); g_depthTex = nullptr; }
    D3D11_TEXTURE2D_DESC td = {}; td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_D32_FLOAT; td.SampleDesc.Count = 1; td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(g_dev->CreateTexture2D(&td, nullptr, &g_depthTex)) || FAILED(g_dev->CreateDepthStencilView(g_depthTex, nullptr, &g_dsv))) return false;
    g_depthW = w; g_depthH = h; return true;
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


bool ModelGpuDraw(ID3D11RenderTargetView* rtv, UINT w, UINT h, const wm::Cam& cam, bool valid, unsigned seq, const float pos[3], float yaw, unsigned playerId, bool first, const ModelExtra* extra) {
    if (!g_ready) return false;
    InterlockedExchange(&g_modelRenderThread, (LONG)GetCurrentThreadId());
    if (first && InterlockedExchange(&g_centerReq, 0)) AnswerCenterDepth(w, h);
    static unsigned s_frames; if (first) { g_stats.frames = ++s_frames; g_stats.submittedPlayers = 0; g_playerDraws.clear(); }
    auto& avatar = g_L->avatars[GetCfg().noGroupAvatarState ? 0 : playerId]; if (!avatar) avatar = std::make_unique<wm::Avatar>(g_L->gait);
    wm::Avatar* g_avatar = avatar.get();
    g_avatar->Update(GetTickCount64(), valid, seq, pos, yaw);
    g_stats.drawn = false;
    if (!valid) { g_stats.visiblePixels = 0; g_queryPending = false; }
    if (!valid || !EnsureDepth(w, h)) return valid;
    const Cfg& cfg = GetCfg();
    wm::ClipPick pk = g_avatar->Pick(g_L->model);
    struct Motion { wm::GestureState state; ULONGLONG at=0; };
    static std::map<unsigned,Motion> motions;
    auto& motion=motions[playerId]; auto now=GetTickCount64();
    float dt=motion.at ? float(now-motion.at)/1000.f : 0; motion.at=now;
    int id=extra && !cfg.noEmoteAnim ? extra->emote : 0; float t=extra ? extra->emoteT : 0;
    if (!witness::ValidEmote(id)) id=0;
    motion.state.Step(id,t,g_avatar->speed(),extra && extra->proximitySpeaking,dt);
    std::vector<float> bones(wm::kMaxJoints * 16, 0.0f);
    static std::map<unsigned, std::pair<wradio::Raise, uint64_t>> radioPose;
    auto& radio = radioPose[playerId]; uint64_t radioNow=GetTickCount64();
    radio.first.Step(extra && extra->radioTx && !cfg.noRadioPose, radio.second ? (radioNow-radio.second)/1000.0f : 0); radio.second=radioNow;
    float radioHand[16];
    wm::JointMatricesBlend(g_L->model, pk.from, pk.fromTime, pk.clip, pk.time, pk.blend, bones.data(), &motion.state, radio.first.value, radioHand);
    FrameCB fc = {}; wm::ViewProj(cam, fc.viewProj);
    for (unsigned group = 0; group <= witness::kAppearanceFeatures; ++group)
        witness::AppearanceTint(extra ? extra->appearance : witness::Appearance{}, group, fc.appearance[group]);
    float feet[3] = {g_avatar->pos()[0], g_avatar->pos()[1], g_avatar->pos()[2] - wm::kCameraEye};
    wm::ModelMatrix(feet, g_avatar->yaw(), g_L->fit, g_L->yaw, fc.model);

    g_stats.emote = extra ? extra->emote : 0; g_stats.lift = g_stats.lean = g_stats.roll = g_stats.dyaw = 0;
    float L[4] = {0.35f, 0.25f, 0.9f, 0.55f}, T[4] = {1, 1, 1, 1}; memcpy(fc.light, L, 16);
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
    g_ctx->PSSetShaderResources(0, 1, &g_L->tex); g_ctx->PSSetSamplers(0, 1, &g_samp);
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
    PlayerDraw record = {}; record.appearance = extra ? witness::ClampAppearance(extra->appearance) : witness::Appearance{}; record.emote=motion.state.emote; record.talk=motion.state.talk;
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
                g_ctx->Draw((UINT)prop.body.size(), 0);
                // The screen emits its own light; retain ghost opacity and depth testing.
                fc.light[3] = 1; g_ctx->UpdateSubresource(g_cbFrame, 0, nullptr, &fc, 0, 0);
                g_ctx->PSSetShaderResources(0, 1, &screen); g_ctx->Draw((UINT)prop.screen.size(), (UINT)prop.body.size());
                if (tvQuery) { g_ctx->End(g_tvQuery); g_tvQueryPending = true; }
                TvDrawn(unsigned(prop.body.size()+prop.screen.size()), live, g_tvMotion.sway, g_tvMotion.power);
            }
        }
    }
    if (timed) { g_ctx->End(g_query); g_queryPending = true; }
    ID3D11ShaderResourceView* z3[3] = {nullptr, nullptr, nullptr}; g_ctx->PSSetShaderResources(0, 3, z3); (void)nul;      // the game's depth buffer must not stay bound as an input
    g_stats.drawn = true; g_stats.clip = pk.clip; g_stats.speed = g_avatar->speed(); g_stats.animTime = pk.time; g_stats.from = pk.from; g_stats.blend = pk.blend;
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
    if(!g_ready) return false; auto screens=TvAudienceScreens(); if(screens.empty()) return true; if(!EnsureDepth(w,h)) return false;
    if(!g_vbAudience){ D3D11_BUFFER_DESC d={}; d.ByteWidth=1024*sizeof(wm::Vertex); d.Usage=D3D11_USAGE_DYNAMIC; d.BindFlags=D3D11_BIND_VERTEX_BUFFER; d.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE; if(FAILED(g_dev->CreateBuffer(&d,nullptr,&g_vbAudience))) return false; }
    FrameCB fc={}; wm::ViewProj(cam,fc.viewProj); for(unsigned i=0;i<4;++i) fc.model[i*5]=1;
    // World props use no avatar palette; zero would multiply every sampled pixel black.
    for(auto& color:fc.appearance) color[0]=color[1]=color[2]=1;
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
}
