// Immediate-mode 2D drawing over the game's back buffer (see ui.h). One shader, one atlas: a white texel for solid shapes and the
// printable ASCII glyphs rendered once with GDI. States are set for each flush; the game sets its own again next frame.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "ui.h"
#include "config.h"

void Log(const char* msg);
extern HMODULE g_selfModule();

namespace {
// Each glyph has a cell kCellW x kCellH on screen, and a taller slot in the atlas (kPitch rows): brackets, parentheses, the underscore and commas reach below their cell, and in
// a tight atlas that ink landed in the top rows of the glyph underneath ('[' above 't', ']' above 'v'): little bits of pixels over those letters.
const int kAtlasW = 256, kAtlasH = 128, kCellW = 10, kCellH = 20, kExtra = 3, kPitchWide = 24, kCols = 25, kFirst = 32, kLast = 126;
int g_pitch = kPitchWide; bool g_tight;      // g_tight: bug twin (ui_tight_atlas=1), the old layout
const char* kHlsl = R"(
cbuffer C : register(b0) { float2 scr; float2 pad; };
Texture2D tex : register(t0); SamplerState smp : register(s0);
struct VI { float2 p : POSITION; float2 t : TEXCOORD; float4 c : COLOR; };
struct O { float4 p : SV_POSITION; float2 t : TEXCOORD; float4 c : COLOR; };
O VS(VI i) { O o; o.p = float4(i.p.x / scr.x * 2 - 1, 1 - i.p.y / scr.y * 2, 0, 1); o.t = i.t; o.c = i.c; return o; }
float4 PS(O i) : SV_TARGET { float a = tex.Sample(smp, i.t).r; return float4(i.c.rgb, i.c.a * a); }
float4 PSI(O i) : SV_TARGET { return tex.Sample(smp, i.t) * i.c; }
)";
struct Vtx { float x, y, u, v; uint32_t c; };
struct Seg { size_t first; bool clip; D3D11_RECT r; ID3D11ShaderResourceView* img; };

ID3D11Device* g_dev; ID3D11DeviceContext* g_ctx;
ID3D11VertexShader* g_vs; ID3D11PixelShader *g_ps, *g_psi; ID3D11InputLayout* g_il; ID3D11Buffer *g_vb, *g_cb;
ID3D11ShaderResourceView* g_srv; ID3D11SamplerState* g_smp; ID3D11BlendState* g_blend; ID3D11RasterizerState* g_rs; ID3D11DepthStencilState* g_ds;
const size_t kMaxVerts = 60000;
std::vector<uint8_t> g_atlasAlpha;
std::vector<Vtx> g_v; std::vector<Seg> g_seg; float g_w = 1, g_h = 1; bool g_ok;

uint32_t Pack(UiColor c) {
    auto b = [](float v) { return (uint32_t)(v <= 0 ? 0 : v >= 1 ? 255 : v * 255 + 0.5f); };
    return b(c.r) | b(c.g) << 8 | b(c.b) << 16 | b(c.a) << 24;
}
void Quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t c) {
    if (g_v.size() + 6 > kMaxVerts) return;
    g_v.push_back({x0, y0, u0, v0, c}); g_v.push_back({x1, y0, u1, v0, c}); g_v.push_back({x0, y1, u0, v1, c});
    g_v.push_back({x1, y0, u1, v0, c}); g_v.push_back({x1, y1, u1, v1, c}); g_v.push_back({x0, y1, u0, v1, c});
}
const float kWhiteU = (kAtlasW - 3) / (float)kAtlasW, kWhiteV = (kAtlasH - 3) / (float)kAtlasH;
int Pitch() { return g_tight ? kCellH : kPitchWide; }
}  // namespace

bool UiInit(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    g_dev = dev; g_ctx = ctx;
    // glyph atlas: printable ASCII in a fixed-pitch font, white on black, plus a solid 4x4 block in the corner
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO bi = {}; bi.bmiHeader = {sizeof(BITMAPINFOHEADER), kAtlasW, -kAtlasH, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
    void* bits = nullptr; HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dc || !bmp || !bits) { Log("ui: atlas bitmap failed"); return false; }
    SelectObject(dc, bmp);
    HFONT font = CreateFontA(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    SelectObject(dc, font); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
    memset(bits, 0, kAtlasW * kAtlasH * 4);
    g_tight = GetCfg().uiTightAtlas;
    for (int ch = kFirst; ch <= kLast; ch++) {
        int i = ch - kFirst, cx = (i % kCols) * kCellW, cy = (i / kCols) * Pitch(); char s[2] = {(char)ch, 0};
        HRGN clip = g_tight ? nullptr : CreateRectRgn(cx, cy, cx + kCellW, cy + Pitch()); if (clip) SelectClipRgn(dc, clip);      // a glyph may not draw into its neighbours' slots
        TextOutA(dc, cx, cy + 1, s, 1);
        if (clip) { SelectClipRgn(dc, nullptr); DeleteObject(clip); }
    }
    { char m[120]; snprintf(m, sizeof m, "ui: atlas %dx%d cell %dx%d pitch %d cols %d first %d", kAtlasW, kAtlasH, kCellW, kCellH, Pitch(), kCols, kFirst); Log(m); }
    GdiFlush();
    std::vector<uint8_t> alpha(kAtlasW * kAtlasH);
    for (int i = 0; i < kAtlasW * kAtlasH; i++) alpha[i] = ((uint8_t*)bits)[i * 4 + 1];
    for (int y = kAtlasH - 4; y < kAtlasH; y++) for (int x = kAtlasW - 4; x < kAtlasW; x++) alpha[y * kAtlasW + x] = 255;
    g_atlasAlpha = alpha;
    DeleteObject(font); DeleteObject(bmp); DeleteDC(dc);
    D3D11_TEXTURE2D_DESC td = {}; td.Width = kAtlasW; td.Height = kAtlasH; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd = {alpha.data(), (UINT)kAtlasW, 0}; ID3D11Texture2D* tex = nullptr;
    if (FAILED(dev->CreateTexture2D(&td, &sd, &tex))) { Log("ui: atlas texture failed"); return false; }
    dev->CreateShaderResourceView(tex, nullptr, &g_srv); tex->Release();
    typedef HRESULT(WINAPI* CompileFn)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
    HMODULE c = LoadLibraryA("d3dcompiler_47.dll"); CompileFn compile = c ? (CompileFn)GetProcAddress(c, "D3DCompile") : nullptr;
    if (!compile) { Log("ui: no d3dcompiler_47"); return false; }
    ID3DBlob *vsb = nullptr, *psb = nullptr, *err = nullptr;
    if (FAILED(compile(kHlsl, strlen(kHlsl), "ui", nullptr, nullptr, "VS", "vs_5_0", 0, 0, &vsb, &err))) { Log("ui: VS compile failed"); return false; }
    if (FAILED(compile(kHlsl, strlen(kHlsl), "ui", nullptr, nullptr, "PS", "ps_5_0", 0, 0, &psb, &err))) { Log("ui: PS compile failed"); return false; }
    dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &g_vs);
    dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g_ps);
    ID3DBlob* pib = nullptr; if (FAILED(compile(kHlsl, strlen(kHlsl), "ui", nullptr, nullptr, "PSI", "ps_5_0", 0, 0, &pib, &err))) { Log("ui: PSI compile failed"); return false; }
    dev->CreatePixelShader(pib->GetBufferPointer(), pib->GetBufferSize(), nullptr, &g_psi);
    D3D11_INPUT_ELEMENT_DESC el[] = {{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                                     {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
                                     {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    dev->CreateInputLayout(el, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &g_il);
    D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = (UINT)(kMaxVerts * sizeof(Vtx)); bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    dev->CreateBuffer(&bd, nullptr, &g_vb);
    bd = {}; bd.ByteWidth = 16; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; dev->CreateBuffer(&bd, nullptr, &g_cb);
    D3D11_SAMPLER_DESC sm = {}; sm.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; dev->CreateSamplerState(&sm, &g_smp);
    D3D11_BLEND_DESC bl = {}; auto& rt = bl.RenderTarget[0]; rt.BlendEnable = TRUE; rt.SrcBlend = D3D11_BLEND_SRC_ALPHA; rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D11_BLEND_ONE; rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOpAlpha = D3D11_BLEND_OP_ADD; rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; dev->CreateBlendState(&bl, &g_blend);
    D3D11_RASTERIZER_DESC rd = {}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.ScissorEnable = TRUE; dev->CreateRasterizerState(&rd, &g_rs);
    D3D11_DEPTH_STENCIL_DESC dd = {}; dev->CreateDepthStencilState(&dd, &g_ds);
    g_ok = g_vs && g_ps && g_psi && g_il && g_vb && g_cb && g_srv && g_smp && g_blend && g_rs && g_ds;
    Log(g_ok ? "ui: ready" : "ui: init failed");
    return g_ok;
}

void UiBegin(float w, float h) { g_w = w; g_h = h; g_v.clear(); g_seg.clear(); g_seg.push_back({0, false, {}, nullptr}); }
void UiClip(float x, float y, float w, float h) {
    Seg s = {g_v.size(), w > 0, {(LONG)x, (LONG)y, (LONG)(x + w), (LONG)(y + h)}, nullptr};
    if (g_seg.back().first == s.first) g_seg.back() = s; else g_seg.push_back(s);
}
ID3D11ShaderResourceView* UiMakeTexture(const uint8_t* bgra, int w, int h) {
    if (!g_dev || w <= 0 || h <= 0) return nullptr; D3D11_TEXTURE2D_DESC td = {}; td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd = {bgra, (UINT)w * 4, 0}; ID3D11Texture2D* tex = nullptr; ID3D11ShaderResourceView* srv = nullptr;
    if (SUCCEEDED(g_dev->CreateTexture2D(&td, &sd, &tex))) { g_dev->CreateShaderResourceView(tex, nullptr, &srv); tex->Release(); } return srv;
}
void UiImage(float x, float y, float w, float h, ID3D11ShaderResourceView* srv, UiColor tint) {
    if (!srv || g_v.size() + 6 > kMaxVerts) return;
    Seg prev = g_seg.back(); Seg a = prev; a.first = g_v.size(); a.img = srv;
    if (prev.first == a.first) g_seg.back() = a; else g_seg.push_back(a);
    Quad(x, y, x + w, y + h, 0, 0, 1, 1, Pack(tint));
    Seg b = prev; b.first = g_v.size(); b.img = nullptr; g_seg.push_back(b);
}
void UiRect(float x, float y, float w, float h, UiColor c) { Quad(x, y, x + w, y + h, kWhiteU, kWhiteV, kWhiteU, kWhiteV, Pack(c)); }
void UiFrame(float x, float y, float w, float h, UiColor c, float t) { UiRect(x, y, w, t, c); UiRect(x, y + h - t, w, t, c); UiRect(x, y + t, t, h - 2 * t, c); UiRect(x + w - t, y + t, t, h - 2 * t, c); }
void UiLine(float x0, float y0, float x1, float y1, UiColor c, float t) {
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy); if (l < 0.01f) return;
    float nx = -dy / l * t * 0.5f, ny = dx / l * t * 0.5f; uint32_t col = Pack(c);
    if (g_v.size() + 6 > kMaxVerts) return;
    g_v.push_back({x0 + nx, y0 + ny, kWhiteU, kWhiteV, col}); g_v.push_back({x1 + nx, y1 + ny, kWhiteU, kWhiteV, col}); g_v.push_back({x0 - nx, y0 - ny, kWhiteU, kWhiteV, col});
    g_v.push_back({x1 + nx, y1 + ny, kWhiteU, kWhiteV, col}); g_v.push_back({x1 - nx, y1 - ny, kWhiteU, kWhiteV, col}); g_v.push_back({x0 - nx, y0 - ny, kWhiteU, kWhiteV, col});
}
void UiTri(float cx, float cy, float s, float a, UiColor c) {
    uint32_t col = Pack(c); if (g_v.size() + 3 > kMaxVerts) return;
    auto pt = [&](float ang, float r) { return Vtx{cx + cosf(ang) * r, cy + sinf(ang) * r, kWhiteU, kWhiteV, col}; };
    g_v.push_back(pt(a, s)); g_v.push_back(pt(a + 2.5f, s * 0.8f)); g_v.push_back(pt(a - 2.5f, s * 0.8f));
}
float UiLineHeight(float sc) { return kCellH * sc; }
float UiTextWidth(const char* s, float sc) { float w = 0, m = 0; for (; *s; s++) { if (*s == '\n') { m = w > m ? w : m; w = 0; } else w += kCellW * sc; } return w > m ? w : m; }
void UiText(float x, float y, const char* s, UiColor c, float sc) {
    uint32_t col = Pack(c); float px = x;
    for (; *s; s++) {
        if (*s == '\n') { px = x; y += kCellH * sc; continue; }
        int ch = (unsigned char)*s; if (ch < kFirst || ch > kLast) ch = '?';
        if (ch != ' ') {
            int i = ch - kFirst, cx = (i % kCols) * kCellW, cy = (i / kCols) * Pitch();
            if (g_tight) { Quad(px, y, px + kCellW * sc, y + kCellH * sc, cx / (float)kAtlasW, cy / (float)kAtlasH, (cx + kCellW) / (float)kAtlasW, (cy + kCellH) / (float)kAtlasH, col); }
            else {      // the quad also covers the few rows below the cell where a descender reaches; the texture is read half a texel inside the slot so a neighbour never bleeds in
                float h = (float)(kCellH + kExtra); const float e = 0.5f;
                Quad(px, y, px + kCellW * sc, y + h * sc, (cx + e) / (float)kAtlasW, (cy + e) / (float)kAtlasH, (cx + kCellW - e) / (float)kAtlasW, (cy + h - e) / (float)kAtlasH, col);
            }
        }
        px += kCellW * sc;
    }
}

void UiEnd(ID3D11RenderTargetView* rtv) {
    if (!g_ok || g_v.empty()) return;
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(g_ctx->Map(g_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
    memcpy(ms.pData, g_v.data(), g_v.size() * sizeof(Vtx)); g_ctx->Unmap(g_vb, 0);
    float cb[4] = {g_w, g_h, 0, 0}; g_ctx->UpdateSubresource(g_cb, 0, nullptr, cb, 0, 0);
    D3D11_VIEWPORT vp = {0, 0, g_w, g_h, 0, 1};
    g_ctx->OMSetRenderTargets(1, &rtv, nullptr); g_ctx->RSSetViewports(1, &vp); g_ctx->RSSetState(g_rs);
    g_ctx->OMSetBlendState(g_blend, nullptr, 0xffffffff); g_ctx->OMSetDepthStencilState(g_ds, 0);
    g_ctx->IASetInputLayout(g_il); g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = sizeof(Vtx), off = 0; g_ctx->IASetVertexBuffers(0, 1, &g_vb, &stride, &off);
    g_ctx->VSSetShader(g_vs, nullptr, 0); g_ctx->PSSetShader(g_ps, nullptr, 0); g_ctx->VSSetConstantBuffers(0, 1, &g_cb);
    g_ctx->PSSetShaderResources(0, 1, &g_srv); g_ctx->PSSetSamplers(0, 1, &g_smp);
    g_ctx->GSSetShader(nullptr, nullptr, 0); g_ctx->HSSetShader(nullptr, nullptr, 0); g_ctx->DSSetShader(nullptr, nullptr, 0);
    for (size_t i = 0; i < g_seg.size(); i++) {
        size_t a = g_seg[i].first, b = i + 1 < g_seg.size() ? g_seg[i + 1].first : g_v.size(); if (b <= a) continue;
        D3D11_RECT r = g_seg[i].clip ? g_seg[i].r : D3D11_RECT{0, 0, (LONG)g_w, (LONG)g_h}; g_ctx->RSSetScissorRects(1, &r);
        if (g_seg[i].img) { g_ctx->PSSetShader(g_psi, nullptr, 0); g_ctx->PSSetShaderResources(0, 1, &g_seg[i].img); } else { g_ctx->PSSetShader(g_ps, nullptr, 0); g_ctx->PSSetShaderResources(0, 1, &g_srv); }
        g_ctx->Draw((UINT)(b - a), (UINT)a);
    }
}

void UiDumpAtlas(const char* name) {      // test command: the glyph atlas as shots\<name>.bmp (grey = alpha)
    if (g_atlasAlpha.empty()) { Log("ui: no atlas to dump"); return; }
    wchar_t path[MAX_PATH]; GetModuleFileNameW(g_selfModule(), path, MAX_PATH); wchar_t* sl = wcsrchr(path, L'\\'); if (sl) *(sl + 1) = 0; wcscat_s(path, L"shots"); CreateDirectoryW(path, nullptr); wcscat_s(path, L"\\");
    wchar_t w[80]; MultiByteToWideChar(CP_UTF8, 0, name, -1, w, 80); wcscat_s(path, w); wcscat_s(path, L".bmp");
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr); if (f == INVALID_HANDLE_VALUE) return;
    BITMAPFILEHEADER fh = {0x4D42, (DWORD)(54 + kAtlasW * kAtlasH * 4), 0, 0, 54}; BITMAPINFOHEADER ih = {40, kAtlasW, -kAtlasH, 1, 32, BI_RGB, (DWORD)(kAtlasW * kAtlasH * 4), 0, 0, 0, 0}; DWORD n;
    WriteFile(f, &fh, 14, &n, nullptr); WriteFile(f, &ih, 40, &n, nullptr);
    std::vector<uint8_t> px(kAtlasW * kAtlasH * 4); for (int i = 0; i < kAtlasW * kAtlasH; i++) { px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = g_atlasAlpha[i]; px[i * 4 + 3] = 255; }
    WriteFile(f, px.data(), (DWORD)px.size(), &n, nullptr); CloseHandle(f); Log("ui: atlas written");
}
