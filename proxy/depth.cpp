#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <cstdio>
#include <cstring>
#include "depth.h"
#include "config.h"

void Log(const char* msg);

typedef void(STDMETHODCALLTYPE* OMSetRTFn)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*);
typedef void(STDMETHODCALLTYPE* ClearDSFn)(ID3D11DeviceContext*, ID3D11DepthStencilView*, UINT, FLOAT, UINT8);
static OMSetRTFn g_origOMSet; static ClearDSFn g_origClear;
static UINT g_w, g_h;

struct Ev { char kind; void* dsv; void* tex; UINT w, h; int fmt; float clearVal; };
static ID3D11Texture2D* g_cand[6]; static int g_ncand;
static Ev g_ev[400]; static volatile LONG g_nev; static volatile LONG g_record;

static void Describe(ID3D11DepthStencilView* v, Ev& e) {
    e.dsv = v; e.tex = nullptr; e.w = e.h = 0; e.fmt = 0;
    if (!v) return;
    ID3D11Resource* r = nullptr; v->GetResource(&r);
    if (r) { ID3D11Texture2D* t = nullptr; if (SUCCEEDED(r->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t))) { D3D11_TEXTURE2D_DESC d; t->GetDesc(&d); e.tex = t; e.w = d.Width; e.h = d.Height; e.fmt = d.Format; t->Release(); } r->Release(); }
}
static void Note(ID3D11Texture2D* t) {
    for (int i = 0; i < g_ncand; i++) if (g_cand[i] == t) return;
    if (g_ncand < 6) { t->AddRef(); g_cand[g_ncand++] = t; }
}
// The scene depth buffer: a full-size typeless depth texture that can also be read by a shader (the game renders with 4x MSAA).
static CRITICAL_SECTION g_cs; static bool g_csInit;
static ID3D11Texture2D* g_scene; static ID3D11ShaderResourceView* g_sceneSrv; static bool g_sceneMs; static UINT g_sceneSamples;
static void ConsiderScene(ID3D11Texture2D* t, const D3D11_TEXTURE2D_DESC& d) {
    if (t == g_scene) return;
    bool typeless = d.Format == DXGI_FORMAT_R24G8_TYPELESS || d.Format == DXGI_FORMAT_R32_TYPELESS || d.Format == DXGI_FORMAT_R32G8X24_TYPELESS;
    if (d.Width != g_w || d.Height != g_h || !typeless || !(d.BindFlags & D3D11_BIND_SHADER_RESOURCE) || !(d.BindFlags & D3D11_BIND_DEPTH_STENCIL)) return;
    EnterCriticalSection(&g_cs);
    t->AddRef(); if (g_scene) g_scene->Release(); if (g_sceneSrv) { g_sceneSrv->Release(); g_sceneSrv = nullptr; }
    g_scene = t; g_sceneSamples = d.SampleDesc.Count; g_sceneMs = d.SampleDesc.Count > 1;
    LeaveCriticalSection(&g_cs);
}
static void STDMETHODCALLTYPE HookOMSet(ID3D11DeviceContext* c, UINT n, ID3D11RenderTargetView* const* rtv, ID3D11DepthStencilView* dsv) {
    if (dsv) {
        ID3D11Resource* r = nullptr; dsv->GetResource(&r);
        if (r) { ID3D11Texture2D* t = nullptr; if (SUCCEEDED(r->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t))) { D3D11_TEXTURE2D_DESC d; t->GetDesc(&d); ConsiderScene(t, d); if (GetCfg().debugDepth && d.Width == g_w && d.Height == g_h) Note(t); t->Release(); } r->Release(); }
    }
    if (g_record && g_nev < 400) { Ev& e = g_ev[g_nev++]; e.kind = 'S'; Describe(dsv, e); e.clearVal = (float)n; }
    g_origOMSet(c, n, rtv, dsv);
}
static void STDMETHODCALLTYPE HookClear(ID3D11DeviceContext* c, ID3D11DepthStencilView* dsv, UINT flags, FLOAT d, UINT8 s) {
    if (g_record && g_nev < 400) { Ev& e = g_ev[g_nev++]; e.kind = 'C'; Describe(dsv, e); e.clearVal = d; }
    g_origClear(c, dsv, flags, d, s);
}

static ID3D11Device* g_dev; static ID3D11DeviceContext* g_ctx;
bool DepthScene(ID3D11ShaderResourceView** srv, bool* multisampled) {
    if (!g_csInit) return false;
    EnterCriticalSection(&g_cs);
    if (g_scene && !g_sceneSrv) {
        D3D11_TEXTURE2D_DESC d; g_scene->GetDesc(&d); D3D11_SHADER_RESOURCE_VIEW_DESC v = {};
        v.Format = d.Format == DXGI_FORMAT_R24G8_TYPELESS ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : d.Format == DXGI_FORMAT_R32_TYPELESS ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        if (g_sceneMs) v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS; else { v.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; v.Texture2D.MipLevels = 1; }
        ID3D11Device* dev = nullptr; g_scene->GetDevice(&dev);
        if (dev) { if (FAILED(dev->CreateShaderResourceView(g_scene, &v, &g_sceneSrv))) Log("depth: cannot make a shader view of the scene depth"); dev->Release(); }
        else Log("depth: no device");
        char m[160]; snprintf(m, sizeof m, "depth: scene depth buffer %p %ux%u fmt %d samples %u", (void*)g_scene, d.Width, d.Height, d.Format, d.SampleDesc.Count); Log(m);
    }
    *srv = g_sceneSrv; *multisampled = g_sceneMs; bool ok = g_sceneSrv != nullptr;
    LeaveCriticalSection(&g_cs);
    return ok;
}

static void Dump() {
    for (int i = 0; i < g_ncand; i++) {
        ID3D11Texture2D* t = g_cand[i]; D3D11_TEXTURE2D_DESC d; t->GetDesc(&d); d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
        ID3D11Texture2D* st = nullptr; char m[300];
        D3D11_TEXTURE2D_DESC o; t->GetDesc(&o);
        snprintf(m, sizeof m, "depth: cand %d desc %ux%u mips %u array %u fmt %d samples %u usage %d bind %#x misc %#x", i, o.Width, o.Height, o.MipLevels, o.ArraySize, o.Format, o.SampleDesc.Count, o.Usage, o.BindFlags, o.MiscFlags); Log(m);
        HRESULT hr = g_dev->CreateTexture2D(&d, nullptr, &st);
        if (FAILED(hr)) { snprintf(m, sizeof m, "depth: cand %d: staging failed hr=%08lx", i, (unsigned long)hr); Log(m); continue; }
        g_ctx->CopyResource(st, t); D3D11_MAPPED_SUBRESOURCE ms;
        if (SUCCEEDED(g_ctx->Map(st, 0, D3D11_MAP_READ, 0, &ms))) {
            auto val = [&](UINT x, UINT y) -> double { const unsigned char* p = (const unsigned char*)ms.pData + (size_t)y * ms.RowPitch + (size_t)x * 4;
                if (d.Format == DXGI_FORMAT_D32_FLOAT || d.Format == DXGI_FORMAT_R32_TYPELESS) return *(const float*)p; return (*(const unsigned*)p & 0xFFFFFF) / 16777215.0; };
            double mn = 1e30, mx = -1e30; unsigned zero = 0, one = 0, tot = 0;
            for (UINT y = 0; y < d.Height; y += 4) for (UINT x = 0; x < d.Width; x += 4) { double v = val(x, y); mn = v < mn ? v : mn; mx = v > mx ? v : mx; zero += v == 0; one += v >= 1.0; tot++; }
            snprintf(m, sizeof m, "depth: cand %d tex %p %ux%u fmt %d: min %.6g max %.6g at0 %.1f%% at1 %.1f%% | center %.6g  low-center %.6g  left-mid %.6g  top-center %.6g  bottom-left %.6g", i, (void*)t, d.Width, d.Height, d.Format, mn, mx, 100.0 * zero / tot, 100.0 * one / tot,
                val(d.Width / 2, d.Height / 2), val(d.Width / 2, d.Height * 9 / 10), val(d.Width / 8, d.Height / 2), val(d.Width / 2, d.Height / 10), val(d.Width / 8, d.Height * 9 / 10));
            Log(m); g_ctx->Unmap(st, 0);
        }
        st->Release();
    }
}

void DepthInstall(ID3D11DeviceContext* ctx, ID3D11Device* dev, UINT w, UINT h) {
    static bool done; if (done) return; done = true; InitializeCriticalSection(&g_cs); g_csInit = true; g_w = w; g_h = h; g_dev = dev; g_ctx = ctx;
    void** vt = *(void***)ctx; DWORD old;
    g_origOMSet = (OMSetRTFn)vt[33]; g_origClear = (ClearDSFn)vt[53];
    VirtualProtect(&vt[33], 8, PAGE_READWRITE, &old); vt[33] = (void*)HookOMSet; VirtualProtect(&vt[33], 8, old, &old);
    VirtualProtect(&vt[53], 8, PAGE_READWRITE, &old); vt[53] = (void*)HookClear; VirtualProtect(&vt[53], 8, old, &old);
    Log("depth: context vtable patched (OMSetRenderTargets, ClearDepthStencilView)");
}

void DepthDiagFrame(unsigned frame) {
    if (!GetCfg().debugDepth) return;
    if (frame % 600 == 300) Dump();
    if (frame % 600 == 100) { g_nev = 0; g_record = 1; return; }
    if (frame % 600 == 101 && g_record) {
        g_record = 0; LONG n = g_nev; char m[200]; snprintf(m, sizeof m, "depth: frame %u had %ld OMSetRenderTargets/ClearDepthStencilView calls (backbuffer %ux%u)", frame, (long)n, g_w, g_h); Log(m);
        for (LONG i = 0; i < n && i < 60; i++) { const Ev& e = g_ev[i]; snprintf(m, sizeof m, "depth:  #%ld %c dsv %p tex %p %ux%u fmt %d val %.2f", (long)i, e.kind, e.dsv, e.tex, e.w, e.h, e.fmt, e.clearVal); Log(m); }
    }
}
