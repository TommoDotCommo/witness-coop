// D3D11 overlay for The Witness (build 11370412). Hooks IDXGISwapChain::Present by patching the shared
// swap-chain vtable, then draws the remote player as a puppet at its world position using the camera read from game memory.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <algorithm>

void Log(const char* msg);   // from ansel_proxy.cpp
extern HWND g_gameWnd;       // hooks.cpp
extern HMODULE g_selfModule();

// Offsets from the module base, build 11370412 (verified by static scan and live reads).
static const uintptr_t kGlobals = 0x62D0A0, kCamPos = 0x6304B0 + 0x10, kCamAng = 0x6303EC;
static const float kVFovDeg = 60.0f;                 // calibrated: horizontal ~89 deg at 16:9
#include "coop.h"
#include "game.h"
#include "state.h"
#include "config.h"
#include "input.h"
#include "ui.h"
#include "devmenu.h"
#include "video.h"
#include "model_gpu.h"
#include "depth.h"

typedef HRESULT(STDMETHODCALLTYPE* PresentFn)(IDXGISwapChain*, UINT, UINT);
static PresentFn g_origPresent;
static volatile LONG g_frames;
static bool g_ready, g_failed, g_uiReady;
ID3D11Device* g_videoDev; ID3D11DeviceContext* g_videoCtx;
static ID3D11Device* g_dev; static ID3D11DeviceContext* g_ctx;
static ID3D11VertexShader* g_vs; static ID3D11PixelShader* g_ps; static ID3D11Buffer* g_cb;
static ID3D11BlendState* g_blend; static ID3D11RasterizerState* g_rs; static ID3D11DepthStencilState* g_ds;

static const char* kHlsl = R"(
cbuffer C : register(b0) { float4 rects[16]; float4 cols[16]; };
struct O { float4 p : SV_POSITION; float4 c : COLOR; };
O VS(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
  float2 q = float2((vid & 1) ? 1 : -1, (vid & 2) ? 1 : -1);
  float4 r = rects[iid]; O o; o.p = float4(r.xy + q * r.zw, 0, 1); o.c = cols[iid]; return o; }
float4 PS(O i) : SV_TARGET { return i.c; }
)";

static char g_shotName[64];     // set by RequestShot from another thread, taken at the next Present
void RequestShot(const char* name) { strncpy(g_shotName, name, sizeof g_shotName - 1); }

// Back buffer to <dll folder>\shots\<name>.bmp (32-bit, top-down). Works while the window is covered or unfocused.
static void TakeShot(IDXGISwapChain* sc, const char* name) {
    ID3D11Texture2D *bb = nullptr, *st = nullptr;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb))) return;
    D3D11_TEXTURE2D_DESC td; bb->GetDesc(&td); td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
    if (SUCCEEDED(g_dev->CreateTexture2D(&td, nullptr, &st))) {
        g_ctx->CopyResource(st, bb);
        D3D11_MAPPED_SUBRESOURCE ms;
        if (SUCCEEDED(g_ctx->Map(st, 0, D3D11_MAP_READ, 0, &ms))) {
            wchar_t path[MAX_PATH]; GetModuleFileNameW(g_selfModule(), path, MAX_PATH); wchar_t* sl = wcsrchr(path, L'\\'); if (sl) *(sl + 1) = 0;
            wcscat_s(path, L"shots"); CreateDirectoryW(path, nullptr); wcscat_s(path, L"\\");
            wchar_t w[80]; MultiByteToWideChar(CP_UTF8, 0, name, -1, w, 80); wcscat_s(path, w); wcscat_s(path, L".bmp");
            HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f != INVALID_HANDLE_VALUE) {
                BITMAPFILEHEADER fh = {0x4D42, (DWORD)(54 + td.Width * td.Height * 4), 0, 0, 54}; BITMAPINFOHEADER ih = {40, (LONG)td.Width, -(LONG)td.Height, 1, 32, BI_RGB, td.Width * td.Height * 4, 0, 0, 0, 0};
                DWORD n; WriteFile(f, &fh, 14, &n, nullptr); WriteFile(f, &ih, 40, &n, nullptr);
                unsigned char* row = new unsigned char[td.Width * 4];
                for (UINT y = 0; y < td.Height; y++) {      // RGBA -> BGRA
                    const unsigned char* s = (const unsigned char*)ms.pData + (size_t)y * ms.RowPitch;
                    for (UINT x = 0; x < td.Width; x++) { row[x*4] = s[x*4+2]; row[x*4+1] = s[x*4+1]; row[x*4+2] = s[x*4]; row[x*4+3] = 255; }
                    WriteFile(f, row, td.Width * 4, &n, nullptr);
                }
                delete[] row; CloseHandle(f);
                char m[120]; snprintf(m, sizeof m, "shot: wrote %s.bmp (%ux%u)", name, td.Width, td.Height); Log(m);
            }
            g_ctx->Unmap(st, 0);
        }
        st->Release();
    }
    bb->Release();
}

static bool Readable(const void* p, size_t n) { return !IsBadReadPtr(p, n); }

// Fills ndc center for a world position. Returns false if behind the camera.
static bool Project(const float* cam, float th, float ph, const float* w, float aspect, float* nx, float* ny) {
    float d[3] = {w[0]-cam[0], w[1]-cam[1], w[2]-cam[2]};
    float fwd[3] = {cosf(ph)*cosf(th), cosf(ph)*sinf(th), sinf(ph)};
    float right[3] = {sinf(th), -cosf(th), 0};
    float up[3] = {-sinf(ph)*cosf(th), -sinf(ph)*sinf(th), cosf(ph)};
    float f = d[0]*fwd[0] + d[1]*fwd[1] + d[2]*fwd[2];
    if (f < 0.1f) return false;
    float tv = tanf(kVFovDeg * 3.14159265f / 360.0f), th_ = tv * aspect;
    *nx = ((d[0]*right[0] + d[1]*right[1] + d[2]*right[2]) / f) / th_;
    *ny = ((d[0]*up[0] + d[1]*up[1] + d[2]*up[2]) / f) / tv;
    return true;
}

static bool InitResources(IDXGISwapChain* sc) {
    if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&g_dev))) { Log("overlay: GetDevice failed"); return false; }
    g_dev->GetImmediateContext(&g_ctx);
    typedef HRESULT(WINAPI* CompileFn)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
    HMODULE c = LoadLibraryA("d3dcompiler_47.dll");
    CompileFn compile = c ? (CompileFn)GetProcAddress(c, "D3DCompile") : nullptr;
    if (!compile) { Log("overlay: no d3dcompiler_47"); return false; }
    ID3DBlob *vsb = nullptr, *psb = nullptr, *err = nullptr;
    if (FAILED(compile(kHlsl, strlen(kHlsl), "ov", nullptr, nullptr, "VS", "vs_5_0", 0, 0, &vsb, &err))) { Log("overlay: VS compile failed"); return false; }
    if (FAILED(compile(kHlsl, strlen(kHlsl), "ov", nullptr, nullptr, "PS", "ps_5_0", 0, 0, &psb, &err))) { Log("overlay: PS compile failed"); return false; }
    g_dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &g_vs);
    g_dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g_ps);
    D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = 16 * 32; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    g_dev->CreateBuffer(&bd, nullptr, &g_cb);
    D3D11_BLEND_DESC bl = {}; bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; g_dev->CreateBlendState(&bl, &g_blend);
    D3D11_RASTERIZER_DESC rd = {}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; g_dev->CreateRasterizerState(&rd, &g_rs);
    D3D11_DEPTH_STENCIL_DESC dd = {}; g_dev->CreateDepthStencilState(&dd, &g_ds);   // depth and stencil off
    DXGI_SWAP_CHAIN_DESC sd; sc->GetDesc(&sd);
    DepthInstall(g_ctx, g_dev, sd.BufferDesc.Width, sd.BufferDesc.Height);
    ModelGpuInit(g_dev, g_ctx);
    char m[200]; snprintf(m, sizeof m, "overlay: resources ready, backbuffer %ux%u fmt %d", sd.BufferDesc.Width, sd.BufferDesc.Height, (int)sd.BufferDesc.Format);
    Log(m);
    g_videoDev = g_dev; g_videoCtx = g_ctx;
    if (DevMenuEnabled() || VideoEnabled()) g_uiReady = UiInit(g_dev, g_ctx);
    return g_vs && g_ps && g_cb && g_blend && g_rs && g_ds;
}

static void DrawMarkers(IDXGISwapChain* sc) {
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb))) return;
    D3D11_TEXTURE2D_DESC td; bb->GetDesc(&td);
    ID3D11RenderTargetView* rtv = nullptr;
    if (FAILED(g_dev->CreateRenderTargetView(bb, nullptr, &rtv))) { bb->Release(); return; }
    uintptr_t base = (uintptr_t)GetModuleHandleA(nullptr);
    const float* cam = (const float*)(base + kCamPos); const float* ang = (const float*)(base + kCamAng);
    float rects[16][4] = {}, cols[16][4] = {}; int n = 0; float aspect = (float)td.Width / (float)td.Height;
    uintptr_t glob = Readable((void*)(base + kGlobals), 8) ? *(uintptr_t*)(base + kGlobals) : 0;
    uintptr_t arr = glob && Readable((void*)(glob + 0x18), 8) ? *(uintptr_t*)(glob + 0x18) : 0;
    RemotePose rp;
    bool have = CoopGetRemote(&rp) && rp.ageMs < 2000;
    if (ModelGpuReady()) {      // the character model replaces the flat puppet
        wm::Cam wc = {{cam[0], cam[1], cam[2]}, ang[0], ang[1], kVFovDeg, aspect, 0.1f, 2000.0f};
        ModelGpuDraw(rtv, td.Width, td.Height, wc, have, rp.seq, rp.pos, rp.ang[0]);
        rtv->Release(); bb->Release(); return;
    }
    if (have) {
        float feet[3] = {rp.pos[0], rp.pos[1], rp.pos[2] - 1.71f}, head[3] = {rp.pos[0], rp.pos[1], rp.pos[2] + 0.12f};
        float nfx, nfy, nhx, nhy;
        if (Project(cam, ang[0], ang[1], feet, aspect, &nfx, &nfy) && Project(cam, ang[0], ang[1], head, aspect, &nhx, &nhy) && n < 15) {
            float hh = fabsf(nhy - nfy) * 0.5f; if (hh < 2.0f / td.Height) hh = 2.0f / td.Height;
            rects[n][0] = (nfx + nhx) * 0.5f; rects[n][1] = (nfy + nhy) * 0.5f; rects[n][2] = hh * ((float)td.Height / td.Width) * 0.45f; rects[n][3] = hh;
            cols[n][0] = 0.1f; cols[n][1] = 0.9f; cols[n][2] = 0.2f; cols[n][3] = 1; n++;          // body: green
            float nose[3] = {rp.pos[0] + 0.7f*cosf(rp.ang[1])*cosf(rp.ang[0]), rp.pos[1] + 0.7f*cosf(rp.ang[1])*sinf(rp.ang[0]), rp.pos[2] + 0.7f*sinf(rp.ang[1])};
            float nnx, nny;
            if (Project(cam, ang[0], ang[1], nose, aspect, &nnx, &nny)) {
                rects[n][0] = nnx; rects[n][1] = nny; rects[n][2] = 6.0f / td.Width * 2; rects[n][3] = 6.0f / td.Height * 2;
                cols[n][0] = 1; cols[n][1] = 0.9f; cols[n][2] = 0; cols[n][3] = 1; n++;               // facing: yellow
            }
            if (g_frames % 300 == 1) { char m[200]; snprintf(m, sizeof m, "overlay: remote id %u age %lums at (%.2f,%.2f,%.2f) body ndc (%.3f,%.3f)", rp.id, rp.ageMs, rp.pos[0], rp.pos[1], rp.pos[2], rects[n-2][0], rects[n-2][1]); Log(m); }
        }
    }
    if (n) {
        float cbdata[32][4] = {}; memcpy(cbdata, rects, sizeof rects); memcpy(cbdata + 16, cols, sizeof cols);
        g_ctx->UpdateSubresource(g_cb, 0, nullptr, cbdata, 0, 0);
        D3D11_VIEWPORT vp = {0, 0, (float)td.Width, (float)td.Height, 0, 1};
        g_ctx->OMSetRenderTargets(1, &rtv, nullptr); g_ctx->RSSetViewports(1, &vp);
        g_ctx->RSSetState(g_rs); g_ctx->OMSetBlendState(g_blend, nullptr, 0xffffffff); g_ctx->OMSetDepthStencilState(g_ds, 0);
        g_ctx->IASetInputLayout(nullptr); g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        g_ctx->VSSetShader(g_vs, nullptr, 0); g_ctx->PSSetShader(g_ps, nullptr, 0);
        g_ctx->VSSetConstantBuffers(0, 1, &g_cb); g_ctx->PSSetConstantBuffers(0, 1, &g_cb);
        g_ctx->GSSetShader(nullptr, nullptr, 0); g_ctx->HSSetShader(nullptr, nullptr, 0); g_ctx->DSSetShader(nullptr, nullptr, 0);
        g_ctx->DrawInstanced(4, n, 0, 0);
    }
    rtv->Release(); bb->Release();
}

// The dev menu takes F4/F8, the keys and the mouse while it is open, by sitting in front of the game's window procedure; raw mouse and
// keyboard input is emptied in the GetRawInputData hook (input.cpp). Key-ups and button-ups still reach the game.
static WNDPROC g_origWndProc; static bool g_wndUnicode;
static LRESULT CALLBACK DevWndProc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_KEYDOWN: case WM_SYSKEYDOWN: if (DevMenuKey((unsigned)wp, true)) return 0; break;
    case WM_KEYUP: case WM_SYSKEYUP: DevMenuKey((unsigned)wp, false); break;
    case WM_CHAR: if (DevMenuOpen()) return 0; break;
    case WM_LBUTTONDOWN: if (DevMenuMouseButton(0, true)) return 0; break;
    case WM_RBUTTONDOWN: if (DevMenuMouseButton(1, true)) return 0; break;
    case WM_MBUTTONDOWN: if (DevMenuMouseButton(2, true)) return 0; break;
    case WM_LBUTTONUP: DevMenuMouseButton(0, false); break;
    case WM_RBUTTONUP: DevMenuMouseButton(1, false); break;
    case WM_MOUSEWHEEL: if (DevMenuWheel(GET_WHEEL_DELTA_WPARAM(wp))) return 0; break;
    case WM_MOUSEMOVE: if (DevMenuOpen()) return 0; break;
    }
    return g_wndUnicode ? CallWindowProcW(g_origWndProc, w, m, wp, lp) : CallWindowProcA(g_origWndProc, w, m, wp, lp);
}
static void InstallDevWndProc(HWND w) {
    if (g_origWndProc || !w) return;
    g_wndUnicode = IsWindowUnicode(w) != 0;
    g_origWndProc = (WNDPROC)(g_wndUnicode ? SetWindowLongPtrW(w, GWLP_WNDPROC, (LONG_PTR)DevWndProc) : SetWindowLongPtrA(w, GWLP_WNDPROC, (LONG_PTR)DevWndProc));
    Log(g_origWndProc ? "devmenu: window procedure hooked" : "devmenu: window procedure hook FAILED");
}
static void DrawDevMenu(IDXGISwapChain* sc) {
    if (!g_uiReady) return;
    DevMenuTick();
    if (!DevMenuOpen() && !DevMenuToastWanted() && !VideoViewMode()) return;
    ID3D11Texture2D* bb = nullptr; if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb))) return;
    D3D11_TEXTURE2D_DESC td; bb->GetDesc(&td); ID3D11RenderTargetView* rtv = nullptr;
    if (SUCCEEDED(g_dev->CreateRenderTargetView(bb, nullptr, &rtv))) { UiBegin((float)td.Width, (float)td.Height); if (DevMenuOpen()) DevMenuDraw((float)td.Width, (float)td.Height); else DevMenuToast((float)td.Width, (float)td.Height);
        if (VideoViewMode()) {      // the partner's screen, in a corner (F9: small, large, off)
            int vw = 0, vh = 0; unsigned long age = 0; ID3D11ShaderResourceView* srv = VideoRemoteSrv(&vw, &vh, &age);
            float k = (VideoViewMode() == 2 ? 2.0f : 1.0f) * std::max(1.f, td.Height / 1080.f), iw = (vw ? vw : 480) * k, ih = (vh ? vh : 270) * k, x = td.Width - iw - 14, y = 14;
            UiRect(x - 3, y - 3, iw + 6, ih + 6 + UiLineHeight(0.85f), Rgba(0, 0, 0, 0.8f));
            if (srv) UiImage(x, y, iw, ih, srv); else UiText(x + 10, y + ih / 2 - 10, "waiting for the other player's picture", Rgba(1, 0.8f, 0.5f), 0.9f);
            char b[80]; snprintf(b, sizeof b, "other player's view%s", srv ? (age > 2000 ? "  (stale)" : "") : ""); UiText(x + 2, y + ih + 2, b, Rgba(1, 0.6f, 0.2f), 0.85f);
        }
        UiEnd(rtv); rtv->Release(); }
    bb->Release();
}

static HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
    LONG f = InterlockedIncrement(&g_frames);
    GameRunQueued();
    StateApplyRemote();
    if (f == 1) Log("overlay: Present hook called (first frame)");
    if (!g_ready && !g_failed) { g_ready = InitResources(sc); g_failed = !g_ready; }
    DepthDiagFrame((unsigned)f);
    if (g_ready) DrawMarkers(sc);
    if (g_ready && g_uiReady) { VideoFrame(sc, g_dev, g_ctx); DrawDevMenu(sc); }      // the picture for the partner is taken before the menu and the inset are drawn
    if (GetCfg().forceActive) {   // build 11370412: the game's focus state is byte +0x21 of the object at exe+0x469A5B0; its loop does
        // Sleep(100) (about 10 fps) when this and the byte at exe+0x62D394 are both 0, and it ignores WM_INPUT while it is 0
        uintptr_t base = (uintptr_t)GetModuleHandleA(nullptr), p = IsBadReadPtr((void*)(base + 0x469A5B0), 8) ? 0 : *(uintptr_t*)(base + 0x469A5B0);
        if (p && !IsBadWritePtr((void*)(p + 0x21), 1)) *(volatile unsigned char*)(p + 0x21) = 1;
        // the input manager ([[exe+0x469A060]]) drops every key and mouse-button message while its byte +0xE5C is 0 (cleared when
        // the window loses activation); mouse motion arrives as raw input and does not check it
        uintptr_t g = IsBadReadPtr((void*)(base + 0x469A060), 8) ? 0 : *(uintptr_t*)(base + 0x469A060);
        uintptr_t im = (g && !IsBadReadPtr((void*)g, 8)) ? *(uintptr_t*)g : 0;
        if (im && !IsBadWritePtr((void*)(im + 0xE5C), 1)) *(volatile unsigned char*)(im + 0xE5C) = 1;
    }
    { DXGI_SWAP_CHAIN_DESC d; if (SUCCEEDED(sc->GetDesc(&d))) { g_gameWnd = d.OutputWindow; if (DevMenuEnabled()) InstallDevWndProc(d.OutputWindow); InputPump(d.OutputWindow); } }
    if (g_shotName[0] && g_ready) { char n[64]; strcpy(n, g_shotName); g_shotName[0] = 0; TakeShot(sc, n); }
    HRESULT hr = g_origPresent(sc, sync, flags);
    if (int cap = GetCfg().capFps; cap > 0) {   // frame cap: sleep out the rest of the frame (Sleep(1) until close, then spin)
        static LARGE_INTEGER freq, next; if (!freq.QuadPart) { QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&next); }
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        next.QuadPart += freq.QuadPart / cap;
        if (now.QuadPart > next.QuadPart + freq.QuadPart / 10) next = now;      // far behind (a load): do not try to catch up
        for (;;) { QueryPerformanceCounter(&now); LONGLONG left = next.QuadPart - now.QuadPart; if (left <= 0) break; if (left > freq.QuadPart / 500) Sleep(1); }
    }
    return hr;
}

void OverlayInstall() {
    WNDCLASSA wc = {}; wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(nullptr); wc.lpszClassName = "WitnessOvDummy";
    RegisterClassA(&wc);
    HWND w = CreateWindowA("WitnessOvDummy", "", WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    DXGI_SWAP_CHAIN_DESC sd = {}; sd.BufferCount = 1; sd.BufferDesc.Width = 64; sd.BufferDesc.Height = 64; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.OutputWindow = w; sd.SampleDesc.Count = 1; sd.Windowed = TRUE;
    IDXGISwapChain* sc = nullptr; ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr; D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev, &fl, &ctx);
    if (FAILED(hr)) { char m[100]; snprintf(m, sizeof m, "overlay: dummy swap chain failed hr=%08lx", (unsigned long)hr); Log(m); return; }
    void** vtbl = *(void***)sc;
    g_origPresent = (PresentFn)vtbl[8];
    DWORD old;
    VirtualProtect(&vtbl[8], sizeof(void*), PAGE_READWRITE, &old);
    vtbl[8] = (void*)HookPresent;
    VirtualProtect(&vtbl[8], sizeof(void*), old, &old);
    Log("overlay: Present vtable patched");
    // keep the dummy objects alive: releasing them could free the shared vtable's owner
    (void)dev; (void)ctx;
}
