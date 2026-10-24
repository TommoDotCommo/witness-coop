#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include "model_gpu.h"
#include "config.h"
#include "depth.h"

void Log(const char* msg);
extern HMODULE g_selfModule();

static wm::Model g_model; static wm::Avatar* g_avatar; static wm::Fit g_fit;
static bool g_ready, g_tried; static float g_yaw, g_height;
static ID3D11Device* g_dev; static ID3D11DeviceContext* g_ctx;
static ID3D11VertexShader *g_vs, *g_vsFull; static ID3D11PixelShader *g_psDepth, *g_psProbe; static volatile LONG g_probeReq, g_depthViewCmd;
static ID3D11VertexShader* g_vsUnused; static ID3D11PixelShader* g_ps; static ID3D11InputLayout* g_layout;
static ID3D11Buffer *g_vb, *g_ib, *g_cbFrame, *g_cbBones;
static ID3D11ShaderResourceView* g_tex; static ID3D11SamplerState* g_samp;
static ID3D11RasterizerState* g_rs; static ID3D11DepthStencilState* g_dss; static ID3D11BlendState *g_blend, *g_blendAlpha;
static ID3D11Texture2D* g_depthTex; static ID3D11DepthStencilView* g_dsv; static UINT g_depthW, g_depthH;
static ModelDrawStats g_stats; static ID3D11Query* g_query; static bool g_queryPending;

struct FrameCB { float viewProj[16], model[16]; float light[4]; float tint[4]; float depthp[4]; float screen[4]; };       // 192 bytes

static const char* kHlsl = R"(
cbuffer Frame : register(b0) { float4x4 viewProj; float4x4 model; float4 light; float4 tint; float4 depthp; float4 screen; };   // screen: width, height. depthp: x mode (0 none, 1 plain, 2 multisampled), y samples, z relative bias, w the game's near plane (its depth is near / distance, reversed)
cbuffer Bones : register(b1) { float4x4 bones[128]; };
Texture2D tex : register(t0); SamplerState smp : register(s0);
Texture2DMS<float> sceneMS : register(t1); Texture2D<float> scene1 : register(t2);
struct VIn { float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; uint4 j : BLENDINDICES; float4 w : BLENDWEIGHT; };
struct VOut { float4 pos : SV_POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0; float dist : TEXCOORD1; };
VOut VS(VIn v) {
  float4x4 skin = bones[v.j.x] * v.w.x + bones[v.j.y] * v.w.y + bones[v.j.z] * v.w.z + bones[v.j.w] * v.w.w;
  float4 lp = mul(skin, float4(v.p, 1)); float3 ln = mul((float3x3)skin, v.n);
  VOut o; o.pos = mul(viewProj, mul(model, lp)); o.dist = o.pos.w; o.n = mul((float3x3)model, ln); o.uv = v.uv; return o; }
float4 PS(VOut i) : SV_TARGET {
  float3 n = normalize(i.n); float d = saturate(dot(n, normalize(light.xyz)));
  float3 c = tex.Sample(smp, i.uv).rgb * tint.rgb * (light.w + (1 - light.w) * d);
  float vis = 1;
  if (depthp.x > 0.5) {      // how much of this pixel the game's own geometry leaves free: the fraction of the scene's depth samples behind the model
    int2 px = int2(i.pos.xy); float zm = depthp.w / i.dist * (1 + depthp.z);      // the model's depth as the game would store it: near / distance (dist: the view-space depth, clip w)
    if (depthp.x < 1.5) vis = (scene1.Load(int3(px, 0)) <= zm) ? 1 : 0;
    else { int ns = (int)depthp.y; float s = 0; for (int k = 0; k < ns; k++) s += (sceneMS.Load(px, k) <= zm) ? 1 : 0; vis = s / ns; }
    if (vis <= 0) discard;
  }
  return float4(c, vis); }
struct FS { float4 pos : SV_POSITION; };
// depth probe: renders into an 8x1 float target; pixel i holds the scene depth at the i-th probe position (centre, 4 samples of it, then four other points)
float PSProbe(FS i) : SV_TARGET {
  int k = (int)i.pos.x; float w = screen.x, h = screen.y; int2 p = int2(w * 0.5, h * 0.5); int smp = 0;
  if (k < 4) smp = k; else if (k == 4) p = int2(w * 0.5, h * 0.8); else if (k == 5) p = int2(w * 0.5, h * 0.2); else if (k == 6) p = int2(w * 0.2, h * 0.5); else p = int2(w * 0.8, h * 0.5);
  return depthp.x < 1.5 ? scene1.Load(int3(p, 0)) : sceneMS.Load(p, smp); }
// debugging view of the scene depth: red = 1 - z (x10), green = z (x10), blue = untouched (z at 1)
FS VSFull(uint id : SV_VertexID) { FS o; float2 q = float2((id << 1) & 2, id & 2); o.pos = float4(q * float2(2, -2) + float2(-1, 1), 0, 1); return o; }
float4 PSDepth(FS i) : SV_TARGET {
  int2 px = int2(i.pos.xy); float z = depthp.x < 1.5 ? scene1.Load(int3(px, 0)) : sceneMS.Load(px, 0);
  return float4(saturate((1 - z) * 10), saturate(z * 10), z >= 0.99999 ? 1 : 0, 1); }
)";

static bool Fail(const char* m) { Log(m); return false; }

static std::wstring ResolvePath(const std::wstring& p) {
    if (p.size() > 1 && (p[1] == L':' || p[0] == L'\\')) return p;
    wchar_t d[MAX_PATH]; GetModuleFileNameW(g_selfModule(), d, MAX_PATH); wchar_t* sl = wcsrchr(d, L'\\'); if (sl) *(sl + 1) = 0;
    bool bare = p.find_first_of(L"\\/.") == std::wstring::npos;      // a bundled name such as cesium-man
    return bare ? std::wstring(d) + L"witness_models\\" + p + L".glb" : std::wstring(d) + p;
}

bool ModelGpuReady() { return g_ready; }
void ModelGpuDepthView(bool on) { InterlockedExchange(&g_depthViewCmd, on); }
void ModelGpuRequestProbe() { InterlockedExchange(&g_probeReq, 1); }
ModelDrawStats ModelGpuStats() { return g_stats; }

bool ModelGpuInit(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    if (g_tried) return g_ready;
    g_tried = true; g_dev = dev; g_ctx = ctx;
    const Cfg& cfg = GetCfg();
    if (cfg.modelPath.empty()) { Log("model: off (model=none), flat puppet"); return false; }
    std::wstring path = ResolvePath(cfg.modelPath);
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return Fail("model: file not found, flat puppet");
    std::vector<uint8_t> bytes(GetFileSize(f, nullptr)); DWORD got = 0; ReadFile(f, bytes.data(), (DWORD)bytes.size(), &got, nullptr); CloseHandle(f);
    std::string err;
    if (got != bytes.size() || !wm::LoadGlb(bytes.data(), bytes.size(), &g_model, &err)) { char m[200]; snprintf(m, sizeof m, "model: cannot load: %s, flat puppet", err.c_str()); return Fail(m); }
    float height = 1.8f, speed = 1.6f, yaw = cfg.modelYaw;       // the model's own <file>.glb.txt sets its size, walking speed and facing; witness_coop.cfg overrides it
    if (FILE* sc = _wfopen((path + L".txt").c_str(), L"r")) {
        char l[200]; float v; while (fgets(l, sizeof l, sc)) { if (sscanf(l, "height=%f", &v) == 1) height = v; else if (sscanf(l, "speed=%f", &v) == 1) speed = v; else if (sscanf(l, "yaw=%f", &v) == 1) yaw += v; }
        fclose(sc);
    }
    if (cfg.modelHeight > 0) height = cfg.modelHeight; if (cfg.modelStride > 0) speed = cfg.modelStride;
    g_yaw = yaw * 3.14159265f / 180.0f; g_height = height;
    g_fit = wm::FitToHeight(g_model, height);
    wm::GaitCfg gc; gc.refSpeed = speed; if (cfg.noModelGait) gc.idleBelow = 1e9f; g_avatar = new wm::Avatar(gc);

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
    dev->CreateVertexShader(fvb->GetBufferPointer(), fvb->GetBufferSize(), nullptr, &g_vsFull); dev->CreatePixelShader(fpb->GetBufferPointer(), fpb->GetBufferSize(), nullptr, &g_psDepth);
    dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g_ps);
    static_assert(sizeof(wm::Vertex) == 52, "vertex layout");
    D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}, {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0}, {"BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    if (FAILED(dev->CreateInputLayout(il, 5, vsb->GetBufferPointer(), vsb->GetBufferSize(), &g_layout))) return Fail("model: input layout failed");
    D3D11_BUFFER_DESC bd = {}; D3D11_SUBRESOURCE_DATA sd = {};
    bd.ByteWidth = (UINT)(g_model.verts.size() * sizeof(wm::Vertex)); bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; sd.pSysMem = g_model.verts.data();
    if (FAILED(dev->CreateBuffer(&bd, &sd, &g_vb))) return Fail("model: vertex buffer failed");
    bd.ByteWidth = (UINT)(g_model.idx.size() * 4); bd.BindFlags = D3D11_BIND_INDEX_BUFFER; sd.pSysMem = g_model.idx.data();
    if (FAILED(dev->CreateBuffer(&bd, &sd, &g_ib))) return Fail("model: index buffer failed");
    bd.ByteWidth = sizeof(FrameCB); bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; if (FAILED(dev->CreateBuffer(&bd, nullptr, &g_cbFrame))) return Fail("model: cb failed");
    bd.ByteWidth = wm::kMaxJoints * 64; if (FAILED(dev->CreateBuffer(&bd, nullptr, &g_cbBones))) return Fail("model: bone cb failed");
    // texture (a 1x1 white one when the model has none), with mip maps
    uint32_t white = 0xFFFFFFFF; int tw = g_model.texW ? g_model.texW : 1, th = g_model.texW ? g_model.texH : 1; const void* px = g_model.texW ? (const void*)g_model.rgba.data() : &white;
    D3D11_TEXTURE2D_DESC td = {}; td.Width = tw; td.Height = th; td.MipLevels = 0; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET; td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    ID3D11Texture2D* tx = nullptr;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &tx))) return Fail("model: texture failed");
    ctx->UpdateSubresource(tx, 0, nullptr, px, tw * 4, 0);
    if (FAILED(dev->CreateShaderResourceView(tx, nullptr, &g_tex))) return Fail("model: texture view failed");
    ctx->GenerateMips(g_tex); tx->Release();
    D3D11_SAMPLER_DESC ss = {}; ss.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; ss.AddressU = ss.AddressV = ss.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; ss.MaxLOD = D3D11_FLOAT32_MAX; ss.MaxAnisotropy = 1;
    dev->CreateSamplerState(&ss, &g_samp);
    D3D11_RASTERIZER_DESC rd = {}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE; dev->CreateRasterizerState(&rd, &g_rs);
    D3D11_DEPTH_STENCIL_DESC dd = {}; dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dd.DepthFunc = D3D11_COMPARISON_LESS; dev->CreateDepthStencilState(&dd, &g_dss);
    D3D11_BLEND_DESC bl = {}; bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; dev->CreateBlendState(&bl, &g_blend);
    bl.RenderTarget[0].BlendEnable = TRUE; bl.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA; bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO; bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE; bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD; dev->CreateBlendState(&bl, &g_blendAlpha);
    g_ready = g_vs && g_ps && g_layout && g_vb && g_ib && g_cbFrame && g_cbBones && g_tex && g_samp && g_rs && g_dss && g_blend && g_blendAlpha && g_vsFull && g_psDepth && g_psProbe;
    char m[240]; snprintf(m, sizeof m, "model: loaded %zu vertices, %zu triangles, %zu joints, %zu clips, texture %dx%d, bind height %.2f scaled to %.2f", g_model.verts.size(), g_model.idx.size() / 3, g_model.joints.size(), g_model.clips.size(), g_model.texW, g_model.texH, g_model.bmax[1] - g_model.bmin[1], g_height);
    Log(m);
    return g_ready;
}

static bool EnsureDepth(UINT w, UINT h) {
    if (g_dsv && g_depthW == w && g_depthH == h) return true;
    if (g_dsv) { g_dsv->Release(); g_dsv = nullptr; } if (g_depthTex) { g_depthTex->Release(); g_depthTex = nullptr; }
    D3D11_TEXTURE2D_DESC td = {}; td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_D32_FLOAT; td.SampleDesc.Count = 1; td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(g_dev->CreateTexture2D(&td, nullptr, &g_depthTex)) || FAILED(g_dev->CreateDepthStencilView(g_depthTex, nullptr, &g_dsv))) return false;
    g_depthW = w; g_depthH = h; return true;
}

static void RunProbe(UINT w, UINT h, ID3D11ShaderResourceView* sceneSrv, bool ms) {
    D3D11_TEXTURE2D_DESC td = {}; td.Width = 8; td.Height = 1; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R32_FLOAT; td.SampleDesc.Count = 1; td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D *rt = nullptr, *st = nullptr; ID3D11RenderTargetView* rtv = nullptr;
    if (FAILED(g_dev->CreateTexture2D(&td, nullptr, &rt)) || FAILED(g_dev->CreateRenderTargetView(rt, nullptr, &rtv))) { Log("probe: cannot make the target"); if (rt) rt->Release(); return; }
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; g_dev->CreateTexture2D(&td, nullptr, &st);
    FrameCB fc = {}; fc.depthp[0] = ms ? 2.0f : 1.0f; fc.screen[0] = (float)w; fc.screen[1] = (float)h;
    g_ctx->UpdateSubresource(g_cbFrame, 0, nullptr, &fc, 0, 0);
    D3D11_VIEWPORT vp = {0, 0, 8, 1, 0, 1};
    g_ctx->OMSetRenderTargets(1, &rtv, nullptr); g_ctx->RSSetViewports(1, &vp); g_ctx->RSSetState(g_rs); g_ctx->OMSetBlendState(g_blend, nullptr, 0xffffffff);
    g_ctx->IASetInputLayout(nullptr); g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); g_ctx->VSSetShader(g_vsFull, nullptr, 0); g_ctx->PSSetShader(g_psProbe, nullptr, 0);
    g_ctx->PSSetConstantBuffers(0, 1, &g_cbFrame); g_ctx->PSSetShaderResources(ms ? 1 : 2, 1, &sceneSrv);
    g_ctx->GSSetShader(nullptr, nullptr, 0); g_ctx->HSSetShader(nullptr, nullptr, 0); g_ctx->DSSetShader(nullptr, nullptr, 0);
    g_ctx->Draw(3, 0);
    ID3D11ShaderResourceView* z3[3] = {nullptr, nullptr, nullptr}; g_ctx->PSSetShaderResources(0, 3, z3);
    g_ctx->CopyResource(st, rt); D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(g_ctx->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
        const float* v = (const float*)m.pData; char msg[300];
        snprintf(msg, sizeof msg, "probe: centre samples %.7g %.7g %.7g %.7g | low %.7g high %.7g left %.7g right %.7g (scene depth, %s)", v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], ms ? "multisampled" : "single sample");
        Log(msg); g_ctx->Unmap(st, 0);
    }
    rtv->Release(); rt->Release(); if (st) st->Release();
}

bool ModelGpuDraw(ID3D11RenderTargetView* rtv, UINT w, UINT h, const wm::Cam& cam, bool valid, unsigned seq, const float pos[3], float yaw) {
    if (!g_ready) return false;
    static unsigned s_frames; g_stats.frames = ++s_frames;
    g_avatar->Update(GetTickCount64(), valid, seq, pos, yaw);
    g_stats.drawn = false;
    if (!valid) { g_stats.visiblePixels = 0; g_queryPending = false; }
    if (!valid || !EnsureDepth(w, h)) return valid;
    const Cfg& cfg = GetCfg();
    wm::ClipPick pk = g_avatar->Pick(g_model);
    std::vector<float> bones(wm::kMaxJoints * 16, 0.0f);
    wm::JointMatrices(g_model, pk.clip, pk.time, bones.data());
    FrameCB fc; wm::ViewProj(cam, fc.viewProj);
    float feet[3] = {g_avatar->pos()[0], g_avatar->pos()[1], g_avatar->pos()[2] - 1.71f};
    wm::ModelMatrix(feet, g_avatar->yaw(), g_fit, g_yaw, fc.model);
    float L[4] = {0.35f, 0.25f, 0.9f, 0.55f}, T[4] = {1, 1, 1, 1}; memcpy(fc.light, L, 16); memcpy(fc.tint, T, 16);
    ID3D11ShaderResourceView* sceneSrv = nullptr; bool ms = false;
    bool haveScene = !cfg.noModelDepth && DepthScene(&sceneSrv, &ms);
    if (InterlockedExchange(&g_probeReq, 0)) { if (haveScene) RunProbe(w, h, sceneSrv, ms); else Log("probe: no scene depth buffer found"); }
    fc.screen[0] = (float)w; fc.screen[1] = (float)h;
    fc.depthp[0] = haveScene ? (ms ? 2.0f : 1.0f) : 0.0f; fc.depthp[1] = 4; fc.depthp[2] = cfg.depthBias; fc.depthp[3] = cfg.depthNear;
    g_ctx->UpdateSubresource(g_cbFrame, 0, nullptr, &fc, 0, 0); g_ctx->UpdateSubresource(g_cbBones, 0, nullptr, bones.data(), 0, 0);
    g_ctx->ClearDepthStencilView(g_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
    D3D11_VIEWPORT vp = {0, 0, (float)w, (float)h, 0, 1};
    g_ctx->OMSetRenderTargets(1, &rtv, g_dsv); g_ctx->RSSetViewports(1, &vp); g_ctx->RSSetState(g_rs);
    g_ctx->OMSetBlendState(haveScene ? g_blendAlpha : g_blend, nullptr, 0xffffffff); g_ctx->OMSetDepthStencilState(g_dss, 0);
    UINT stride = sizeof(wm::Vertex), off = 0;
    g_ctx->IASetInputLayout(g_layout); g_ctx->IASetVertexBuffers(0, 1, &g_vb, &stride, &off); g_ctx->IASetIndexBuffer(g_ib, DXGI_FORMAT_R32_UINT, 0);
    g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->VSSetShader(g_vs, nullptr, 0); g_ctx->PSSetShader(g_ps, nullptr, 0);
    ID3D11Buffer* cbs[2] = {g_cbFrame, g_cbBones}; g_ctx->VSSetConstantBuffers(0, 2, cbs); g_ctx->PSSetConstantBuffers(0, 1, cbs);
    g_ctx->PSSetShaderResources(0, 1, &g_tex); g_ctx->PSSetSamplers(0, 1, &g_samp);
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
    g_ctx->DrawIndexed((UINT)g_model.idx.size(), 0, 0);
    if (timed) { g_ctx->End(g_query); g_queryPending = true; }
    ID3D11ShaderResourceView* z3[3] = {nullptr, nullptr, nullptr}; g_ctx->PSSetShaderResources(0, 3, z3); (void)nul;      // the game's depth buffer must not stay bound as an input
    g_stats.drawn = true; g_stats.clip = pk.clip; g_stats.speed = g_avatar->speed(); g_stats.animTime = pk.time; memcpy(g_stats.pos, g_avatar->pos(), 12); memcpy(g_stats.cam, cam.pos, 12);
    return true;
}

void ModelGpuLogStats() {
    ModelDrawStats s = ModelGpuStats(); char m[200];
    snprintf(m, sizeof m, "modelstat: visible_px %u drawn %d clip %d speed %.2f anim %.3f frames %u pos %.2f %.2f %.2f cam %.2f %.2f %.2f", s.visiblePixels, (int)s.drawn, s.clip, s.speed, s.animTime, s.frames, s.pos[0], s.pos[1], s.pos[2], s.cam[0], s.cam[1], s.cam[2]); Log(m);
}
