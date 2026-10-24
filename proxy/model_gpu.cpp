#include <map>
#include <memory>
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

static wm::Model g_model; static std::map<unsigned, std::unique_ptr<wm::Avatar>> g_avatars; static wm::GaitCfg g_gait; static wm::Fit g_fit;
static bool g_ready, g_tried; static float g_yaw, g_height;
static ID3D11Device* g_dev; static ID3D11DeviceContext* g_ctx;
static ID3D11VertexShader *g_vs, *g_vsFull; static ID3D11PixelShader *g_psDepth, *g_psProbe, *g_psUi; static volatile LONG g_probeReq, g_depthViewCmd;
static ID3D11VertexShader* g_vsUnused; static ID3D11PixelShader* g_ps; static ID3D11InputLayout* g_layout;
static ID3D11Buffer *g_vb, *g_ib, *g_cbFrame, *g_cbBones;
static ID3D11ShaderResourceView* g_tex; static ID3D11SamplerState* g_samp;
static ID3D11RasterizerState* g_rs; static ID3D11DepthStencilState* g_dss; static ID3D11BlendState *g_blend, *g_blendAlpha;
static ID3D11Texture2D* g_depthTex; static ID3D11DepthStencilView* g_dsv; static UINT g_depthW, g_depthH;
struct PlayerDraw { unsigned id, seq; float pos[3], speed; int clip; };
static std::vector<PlayerDraw> g_playerDraws;
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
// the selector panel: a CPU-drawn texture (GDI text) copied 1:1 to the screen; depthp.xy is the panel's top-left corner
Texture2D<float4> uiTex : register(t3);
float4 PSUi(FS i) : SV_TARGET { return uiTex.Load(int3(int2(i.pos.xy - depthp.xy), 0)); }
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


// ---- the models on offer: every .glb in witness_models\ next to the DLL (and the configured one, if it is somewhere else)
struct Entry { std::wstring path; std::string name; };
static std::vector<Entry> g_list; static int g_cur; static volatile LONG g_selReq; static volatile LONG g_selNamed; static char g_selName[64];
static std::string NameOf(const std::wstring& p) { size_t s = p.find_last_of(L"\\/"), e = p.rfind(L'.'); std::wstring n = p.substr(s == std::wstring::npos ? 0 : s + 1, e == std::wstring::npos || (s != std::wstring::npos && e < s) ? std::wstring::npos : e - (s == std::wstring::npos ? 0 : s + 1)); return std::string(n.begin(), n.end()); }
static void BuildList() {
    const Cfg& cfg = GetCfg(); wchar_t d[MAX_PATH]; GetModuleFileNameW(g_selfModule(), d, MAX_PATH); wchar_t* sl = wcsrchr(d, L'\\'); if (sl) *(sl + 1) = 0;
    std::wstring dir = std::wstring(d) + L"witness_models\\"; WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"*.glb").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) { do { g_list.push_back({dir + fd.cFileName, NameOf(fd.cFileName)}); } while (FindNextFileW(h, &fd)); FindClose(h); }
    std::wstring want = ResolvePath(cfg.modelPath); int at = -1;
    for (size_t i = 0; i < g_list.size(); i++) if (!_wcsicmp(g_list[i].path.c_str(), want.c_str())) at = (int)i;
    if (at < 0 && GetFileAttributesW(want.c_str()) != INVALID_FILE_ATTRIBUTES) { g_list.insert(g_list.begin(), {want, NameOf(want)}); at = 0; }
    g_cur = at < 0 ? 0 : at;
}
static void ReleaseModel() { if (g_vb) { g_vb->Release(); g_vb = nullptr; } if (g_ib) { g_ib->Release(); g_ib = nullptr; } if (g_tex) { g_tex->Release(); g_tex = nullptr; } g_avatars.clear(); g_model = wm::Model(); }
// Loads the model at g_list[g_cur] and puts it on the GPU. On failure the previous one stays (nothing is released before the new one is ready).
static bool LoadFile(const std::wstring& path, wm::Model* out, std::string* err);
static bool LoadCurrent() {
    const Cfg& cfg = GetCfg(); ID3D11Device* dev = g_dev; ID3D11DeviceContext* ctx = g_ctx;
    const std::wstring path = g_list[g_cur].path;
    wm::Model nm; std::string err;
    if (!LoadFile(path, &nm, &err)) { char m[240]; snprintf(m, sizeof m, "model: cannot load %s: %s", g_list[g_cur].name.c_str(), err.c_str()); return Fail(m); }
    ReleaseModel(); g_model = std::move(nm);
    float height = 1.8f, speed = 1.6f, yaw = cfg.modelYaw;       // the model's own <file>.glb.txt sets its size, walking speed and facing; witness_coop.cfg overrides it
    if (FILE* sc = _wfopen((path + L".txt").c_str(), L"r")) {
        char l[200]; float v; while (fgets(l, sizeof l, sc)) { if (sscanf(l, "height=%f", &v) == 1) height = v; else if (sscanf(l, "speed=%f", &v) == 1) speed = v; else if (sscanf(l, "yaw=%f", &v) == 1) yaw += v; }
        fclose(sc);
    }
    if (cfg.modelHeight > 0) height = cfg.modelHeight; if (cfg.modelStride > 0) speed = cfg.modelStride;
    g_yaw = yaw * 3.14159265f / 180.0f; g_height = height;
    g_fit = wm::FitToHeight(g_model, height);
    wm::GaitCfg gc; gc.refSpeed = speed; if (cfg.noModelGait) gc.idleBelow = 1e9f; g_gait = gc;
    D3D11_BUFFER_DESC bd = {}; D3D11_SUBRESOURCE_DATA sd = {};
    bd.ByteWidth = (UINT)(g_model.verts.size() * sizeof(wm::Vertex)); bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; sd.pSysMem = g_model.verts.data();
    if (FAILED(dev->CreateBuffer(&bd, &sd, &g_vb))) return Fail("model: vertex buffer failed");
    bd.ByteWidth = (UINT)(g_model.idx.size() * 4); bd.BindFlags = D3D11_BIND_INDEX_BUFFER; sd.pSysMem = g_model.idx.data();
    if (FAILED(dev->CreateBuffer(&bd, &sd, &g_ib))) return Fail("model: index buffer failed");
    // texture (a 1x1 white one when the model has none), with mip maps
    uint32_t white = 0xFFFFFFFF; int tw = g_model.texW ? g_model.texW : 1, th = g_model.texW ? g_model.texH : 1; const void* px = g_model.texW ? (const void*)g_model.rgba.data() : &white;
    D3D11_TEXTURE2D_DESC td = {}; td.Width = tw; td.Height = th; td.MipLevels = 0; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET; td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    ID3D11Texture2D* tx = nullptr;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &tx))) return Fail("model: texture failed");
    ctx->UpdateSubresource(tx, 0, nullptr, px, tw * 4, 0);
    if (FAILED(dev->CreateShaderResourceView(tx, nullptr, &g_tex))) return Fail("model: texture view failed");
    ctx->GenerateMips(g_tex); tx->Release();
    if (!g_vb || !g_ib || !g_tex) return Fail("model: GPU upload failed");
    char m[240]; snprintf(m, sizeof m, "model: loaded %zu vertices, %zu triangles, %zu joints, %zu clips, texture %dx%d, bind height %.2f scaled to %.2f", g_model.verts.size(), g_model.idx.size() / 3, g_model.joints.size(), g_model.clips.size(), g_model.texW, g_model.texH, g_model.bmax[1] - g_model.bmin[1], g_height);
    Log(m);
    return true;
}
static bool LoadFile(const std::wstring& path, wm::Model* out, std::string* err) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { *err = "file not found"; return false; }
    std::vector<uint8_t> bytes(GetFileSize(f, nullptr)); DWORD got = 0; ReadFile(f, bytes.data(), (DWORD)bytes.size(), &got, nullptr); CloseHandle(f);
    return got == bytes.size() && wm::LoadGlb(bytes.data(), bytes.size(), out, err);
}


// ---- the selector: F6 / F7 (or the `model` command) change the model; a panel lists them for a few seconds
void ModelGpuSelect(int delta) { InterlockedExchange(&g_selReq, delta); }
void ModelGpuSelectName(const char* name) { strncpy(g_selName, name, sizeof g_selName - 1); InterlockedExchange(&g_selNamed, 1); }
static ID3D11Texture2D* g_uiTex; static ID3D11ShaderResourceView* g_uiSrv; static UINT g_uiW, g_uiH; static ULONGLONG g_toastUntil; static int g_toastScale = 1;
static void BuildPanel(UINT screenH) {
    if (g_uiSrv) { g_uiSrv->Release(); g_uiSrv = nullptr; } if (g_uiTex) { g_uiTex->Release(); g_uiTex = nullptr; }
    int fh = screenH >= 1000 ? 22 : 16, rowH = fh + 10, pad = 14, n = (int)g_list.size();
    HDC dc = CreateCompatibleDC(nullptr); HFONT font = CreateFontA(-fh, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, NONANTIALIASED_QUALITY, 0, "Segoe UI"), bold = CreateFontA(-fh, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, NONANTIALIASED_QUALITY, 0, "Segoe UI");
    SelectObject(dc, font); int width = 0; SIZE sz; const char* title = "Player model    F6 / F7 to change";
    GetTextExtentPoint32A(dc, title, (int)strlen(title), &sz); width = sz.cx;
    for (auto& e : g_list) { char l[100]; snprintf(l, sizeof l, "   %s", e.name.c_str()); GetTextExtentPoint32A(dc, l, (int)strlen(l), &sz); if (sz.cx > width) width = sz.cx; }
    int W = width + pad * 2, H = rowH * (n + 1) + pad * 2 - 4;
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr; HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0); SelectObject(dc, bmp);
    const COLORREF bg = RGB(22, 24, 32); HBRUSH bb = CreateSolidBrush(bg), hb = CreateSolidBrush(RGB(58, 64, 96)); RECT all = {0, 0, W, H}; FillRect(dc, &all, bb);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(150, 160, 190)); SelectObject(dc, font); TextOutA(dc, pad, pad - 2, title, (int)strlen(title));
    for (int i = 0; i < n; i++) {
        int y = pad + rowH * (i + 1) - 2; bool cur = i == g_cur;
        if (cur) { RECT r = {pad / 2, y - 2, W - pad / 2, y + rowH - 4}; FillRect(dc, &r, hb); }
        char l[100]; snprintf(l, sizeof l, "%s %s", cur ? ">" : "  ", g_list[i].name.c_str());
        SelectObject(dc, cur ? bold : font); SetTextColor(dc, cur ? RGB(255, 214, 96) : RGB(225, 228, 238)); TextOutA(dc, pad, y, l, (int)strlen(l));
    }
    GdiFlush(); uint32_t* px = (uint32_t*)bits; uint32_t bgp = (22u << 16) | (24u << 8) | 32u;
    for (int i = 0; i < W * H; i++) { uint32_t c = px[i] & 0xFFFFFF; px[i] = c | ((c == bgp ? 215u : 255u) << 24); }
    D3D11_TEXTURE2D_DESC td = {}; td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1; td.BindFlags = D3D11_BIND_SHADER_RESOURCE; td.Usage = D3D11_USAGE_IMMUTABLE;
    D3D11_SUBRESOURCE_DATA sd = {bits, (UINT)W * 4, 0};
    if (SUCCEEDED(g_dev->CreateTexture2D(&td, &sd, &g_uiTex))) g_dev->CreateShaderResourceView(g_uiTex, nullptr, &g_uiSrv);
    g_uiW = W; g_uiH = H;
    DeleteObject(bmp); DeleteObject(bb); DeleteObject(hb); DeleteObject(font); DeleteObject(bold); DeleteDC(dc);
}
static void ShowPanel() { g_toastUntil = GetTickCount64() + 4000; if (g_uiSrv) { g_uiSrv->Release(); g_uiSrv = nullptr; } }      // rebuilt at the next draw
static void DrawPanel(ID3D11RenderTargetView* rtv, UINT w, UINT h) {
    if (GetTickCount64() > g_toastUntil || g_list.size() < 1) return;
    if (!g_uiSrv) BuildPanel(h);
    if (!g_uiSrv) return;
    int x = (int)(w / 64), y = (int)(h / 40);
    FrameCB fc = {}; fc.depthp[0] = (float)x; fc.depthp[1] = (float)y; g_ctx->UpdateSubresource(g_cbFrame, 0, nullptr, &fc, 0, 0);
    D3D11_VIEWPORT vp = {(float)x, (float)y, (float)g_uiW, (float)g_uiH, 0, 1};
    g_ctx->OMSetRenderTargets(1, &rtv, nullptr); g_ctx->RSSetViewports(1, &vp); g_ctx->RSSetState(g_rs); g_ctx->OMSetBlendState(g_blendAlpha, nullptr, 0xffffffff); g_ctx->OMSetDepthStencilState(g_dss, 0);
    g_ctx->IASetInputLayout(nullptr); g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); g_ctx->VSSetShader(g_vsFull, nullptr, 0); g_ctx->PSSetShader(g_psUi, nullptr, 0);
    g_ctx->PSSetConstantBuffers(0, 1, &g_cbFrame); g_ctx->PSSetShaderResources(3, 1, &g_uiSrv);
    g_ctx->GSSetShader(nullptr, nullptr, 0); g_ctx->HSSetShader(nullptr, nullptr, 0); g_ctx->DSSetShader(nullptr, nullptr, 0);
    g_ctx->Draw(3, 0);
    ID3D11ShaderResourceView* z = nullptr; g_ctx->PSSetShaderResources(3, 1, &z);
}
struct PanelAtExit { ID3D11RenderTargetView* rtv; UINT w, h; ~PanelAtExit() { if (g_ready) DrawPanel(rtv, w, h); } };
// Applies a pending selection (render thread). Returns true if the model changed.
static void ApplySelection() {
    LONG d = InterlockedExchange(&g_selReq, 0); int n = (int)g_list.size();
    if (InterlockedExchange(&g_selNamed, 0)) { for (int i = 0; i < n; i++) if (!_stricmp(g_list[i].name.c_str(), g_selName)) { d = 0; int old = g_cur; g_cur = i; if (i != old && !LoadCurrent()) g_cur = old; ShowPanel(); return; } Log("model: no such model"); return; }
    if (!d || n < 2) { if (d) ShowPanel(); return; }
    int old = g_cur; g_cur = ((g_cur + (int)d) % n + n) % n;
    if (!LoadCurrent()) { g_cur = old; }
    ShowPanel();
}

bool ModelGpuInit(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    if (g_tried) return g_ready;
    g_tried = true; g_dev = dev; g_ctx = ctx;
    const Cfg& cfg = GetCfg();
    if (cfg.modelPath.empty()) { Log("model: off (model=none), flat puppet"); return false; }
    BuildList(); if (g_list.empty()) return Fail("model: no model file found, flat puppet");

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
    ID3DBlob* upb = nullptr; if (FAILED(compile(kHlsl, strlen(kHlsl), "model", nullptr, nullptr, "PSUi", "ps_5_0", 0, 0, &upb, &eb))) return Fail(eb ? (const char*)eb->GetBufferPointer() : "model: UI shader failed");
    dev->CreatePixelShader(upb->GetBufferPointer(), upb->GetBufferSize(), nullptr, &g_psUi);
    dev->CreateVertexShader(fvb->GetBufferPointer(), fvb->GetBufferSize(), nullptr, &g_vsFull); dev->CreatePixelShader(fpb->GetBufferPointer(), fpb->GetBufferSize(), nullptr, &g_psDepth);
    dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g_ps);
    static_assert(sizeof(wm::Vertex) == 52, "vertex layout");
    D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}, {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0}, {"BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    if (FAILED(dev->CreateInputLayout(il, 5, vsb->GetBufferPointer(), vsb->GetBufferSize(), &g_layout))) return Fail("model: input layout failed");
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
    g_ready = g_vs && g_ps && g_layout && g_cbFrame && g_cbBones && g_samp && g_rs && g_dss && g_blend && g_blendAlpha && g_vsFull && g_psDepth && g_psProbe && g_psUi;
    if (g_ready && !LoadCurrent()) g_ready = false;
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

bool ModelGpuDraw(ID3D11RenderTargetView* rtv, UINT w, UINT h, const wm::Cam& cam, bool valid, unsigned seq, const float pos[3], float yaw, unsigned playerId, bool first) {
    if (!g_ready) return false;
    if (!GetCfg().noModelSelect) ApplySelection(); else InterlockedExchange(&g_selReq, 0);
    PanelAtExit panel{rtv, w, h};
    static unsigned s_frames; if (first) { g_stats.frames = ++s_frames; g_stats.submittedPlayers = 0; g_playerDraws.clear(); }
    auto& avatar = g_avatars[GetCfg().noGroupAvatarState ? 0 : playerId]; if (!avatar) avatar = std::make_unique<wm::Avatar>(g_gait);
    wm::Avatar* g_avatar = avatar.get();
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
    if (first) g_ctx->ClearDepthStencilView(g_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
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
    g_ctx->DrawIndexed((UINT)g_model.idx.size(), 0, 0); ++g_stats.submittedPlayers;
    PlayerDraw record = {}; record.id = playerId; record.seq = seq; memcpy(record.pos, g_avatar->pos(), 12); record.speed = g_avatar->speed(); record.clip = pk.clip; g_playerDraws.push_back(record);
    if (timed) { g_ctx->End(g_query); g_queryPending = true; }
    ID3D11ShaderResourceView* z3[3] = {nullptr, nullptr, nullptr}; g_ctx->PSSetShaderResources(0, 3, z3); (void)nul;      // the game's depth buffer must not stay bound as an input
    g_stats.drawn = true; g_stats.clip = pk.clip; g_stats.speed = g_avatar->speed(); g_stats.animTime = pk.time; memcpy(g_stats.pos, g_avatar->pos(), 12); memcpy(g_stats.cam, cam.pos, 12);
    return true;
}

void ModelGpuDumpPlayers() {
    wchar_t path[MAX_PATH]; GetModuleFileNameW(g_selfModule(), path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\'); if (slash) *(slash + 1) = 0;
    wcscat_s(path, L"witness_models.json"); wchar_t tmp[MAX_PATH]; wcscpy_s(tmp, path); wcscat_s(tmp, L".tmp");
    FILE* f = _wfopen(tmp, L"w"); if (!f) return;
    fprintf(f, "{\"frame\":%u,\"players\":[", g_stats.frames); bool comma = false;
    for (const auto& p : g_playerDraws) {
        fprintf(f, "%s{\"id\":%u,\"seq\":%u,\"pos\":[%.5f,%.5f,%.5f],\"speed\":%.5f,\"clip\":%d}", comma ? "," : "", p.id, p.seq, p.pos[0], p.pos[1], p.pos[2], p.speed, p.clip); comma = true;
    }
    fprintf(f, "]}\n"); fclose(f); MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING);
}

void ModelGpuLogStats() {
    ModelDrawStats s = ModelGpuStats(); char m[200];
    snprintf(m, sizeof m, "modelstat: visible_px %u drawn %d clip %d speed %.2f anim %.3f frames %u model %s %d/%d pos %.2f %.2f %.2f cam %.2f %.2f %.2f", s.visiblePixels, (int)s.drawn, s.clip, s.speed, s.animTime, s.frames, g_list.empty() ? "-" : g_list[g_cur].name.c_str(), g_cur + 1, (int)g_list.size(), s.pos[0], s.pos[1], s.pos[2], s.cam[0], s.cam[1], s.cam[2]); Log(m);
}
