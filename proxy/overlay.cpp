#include "radio/radio_game.h"
#include "radio/radio.h"
// D3D11 overlay for The Witness (build 11370412). Hooks IDXGISwapChain::Present by patching the shared
// swap-chain vtable, then draws the remote player as a puppet at its world position using the camera read from game memory.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstdint>
#include <cctype>
#include <cmath>
#include <cstring>
#include <algorithm>

void Log(const char* msg);   // from ansel_proxy.cpp
extern HWND g_gameWnd;       // hooks.cpp
extern HMODULE g_selfModule();

// Offsets from the module base, build 11370412 (verified by static scan and live reads).
static const uintptr_t kGlobals = 0x62D0A0, kCamPos = 0x6304B0 + 0x10, kCamAng = 0x6303EC;
static const uintptr_t kPauseDimA = 0x630070, kPauseDimB = 0x62FF3C;      // two floats: 0.0 in every state but the pause menu, where they are 1.0 (a state table: walking, dev menu, at a panel, in and out of a puzzle, after F7, pause menu)
static const uintptr_t kPausedOld = 0x62DAC4;      // the first candidate, WRONG: it flips with F6 / F7 and with entering a puzzle; only kept for the mutant pause_wrong_flag=1
#include "gamecam.h"
#include "coop.h"
#include "game.h"
#include "state.h"
#include "config.h"
#include "input.h"
#include "ui.h"
#include "guide_ui.h"
#include "../common/guide.h"
#include "race_game.h"
#include "frame/panelframe.h"
#include "frame/ribbon.h"
#include "devmenu.h"
#include "video.h"
#include "tv.h"
#include "model_gpu.h"
#include "depth.h"
#include "playercolor.h"
#include "social.h"
#include "povui.h"
#include <atomic>
#include <cctype>

typedef HRESULT(STDMETHODCALLTYPE* PresentFn)(IDXGISwapChain*, UINT, UINT);
static PresentFn g_origPresent;
static volatile LONG g_frames;
static bool g_ready, g_failed, g_uiReady;
ID3D11Device* g_videoDev; ID3D11DeviceContext* g_videoCtx;
static ID3D11Device* g_dev; static ID3D11DeviceContext* g_ctx;
static ID3D11VertexShader* g_vs; static ID3D11PixelShader* g_ps; static ID3D11Buffer* g_cb;
static ID3D11BlendState* g_blend; static ID3D11RasterizerState* g_rs; static ID3D11DepthStencilState* g_ds;

static const char* kHlsl = R"(
cbuffer C : register(b0) { float4 rects[32]; float4 cols[32]; };
struct O { float4 p : SV_POSITION; float4 c : COLOR; };
O VS(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
  float2 q = float2((vid & 1) ? 1 : -1, (vid & 2) ? 1 : -1);
  float4 r = rects[iid]; O o; o.p = float4(r.xy + q * r.zw, 0, 1); o.c = cols[iid]; return o; }
float4 PS(O i) : SV_TARGET { return i.c; }
)";

static char g_shotName[64];     // set by RequestShot from another thread, taken at the next Present
void RequestShot(const char* name) { strncpy(g_shotName, name, sizeof g_shotName - 1); }

// Back buffer to <dll folder>\shots\<name>.bmp (32-bit, top-down). Works while the window is covered or unfocused.
static void TakeShot(IDXGISwapChain* sc, const char* name, ID3D11Texture2D* source = nullptr) {
    ID3D11Texture2D *bb = nullptr, *st = nullptr;
    if (source) { bb = source; bb->AddRef(); }
    else if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb))) return;
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

// The back buffer as RGBA, top row first (free drawing's depth probe, the map picture).
bool OverlayCaptureRgba(IDXGISwapChain* sc, std::vector<uint8_t>* px, unsigned* w, unsigned* h) {
    ID3D11Texture2D *bb = nullptr, *st = nullptr; bool ok = false;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb))) return false;
    D3D11_TEXTURE2D_DESC td; bb->GetDesc(&td); td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
    if (SUCCEEDED(g_dev->CreateTexture2D(&td, nullptr, &st))) {
        g_ctx->CopyResource(st, bb); D3D11_MAPPED_SUBRESOURCE ms;
        if (SUCCEEDED(g_ctx->Map(st, 0, D3D11_MAP_READ, 0, &ms))) { px->resize((size_t)td.Width * td.Height * 4); for (UINT y = 0; y < td.Height; y++) memcpy(&(*px)[(size_t)y * td.Width * 4], (const unsigned char*)ms.pData + (size_t)y * ms.RowPitch, td.Width * 4); *w = td.Width; *h = td.Height; ok = true; g_ctx->Unmap(st, 0); }
        st->Release();
    }
    bb->Release(); return ok;
}
static bool Readable(const void* p, size_t n) { return !IsBadReadPtr(p, n); }
// The game's pause menu is open: nothing of ours may be drawn over it (puppets, shared lines, name tags).
bool GamePaused() {
    if (GetCfg().noPauseHide) return false; uintptr_t base = (uintptr_t)GetModuleHandleA(nullptr);
    if (GetCfg().pauseWrongFlag) { const DWORD* p = (const DWORD*)(base + kPausedOld); return Readable(p, 4) && *p == 1; }
    const float *a = (const float*)(base + kPauseDimA), *b = (const float*)(base + kPauseDimB);
    return Readable(a, 4) && Readable(b, 4) && *a > 0.01f && *b > 0.01f;
}

// Fills ndc center for a world position. Returns false if behind the camera.
static bool Project(const float* cam, float th, float ph, const float* w, float aspect, float* nx, float* ny) {
    float d[3] = {w[0]-cam[0], w[1]-cam[1], w[2]-cam[2]};
    float fwd[3] = {cosf(ph)*cosf(th), cosf(ph)*sinf(th), sinf(ph)};
    float right[3] = {sinf(th), -cosf(th), 0};
    float up[3] = {-sinf(ph)*cosf(th), -sinf(ph)*sinf(th), cosf(ph)};
    float f = d[0]*fwd[0] + d[1]*fwd[1] + d[2]*fwd[2];
    if (f < 0.1f) return false;
    float tv = tanf(GameProjection().vfovDeg * 3.14159265f / 360.0f), th_ = tv * aspect;
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
    D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = 16 * 64; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
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
    g_uiReady = UiInit(g_dev, g_ctx); // F1 help remains available when other UI features are disabled
    return g_vs && g_ps && g_cb && g_blend && g_rs && g_ds;
}

static volatile LONG g_lineCaptureRequested = 0;

// The other players' lines: each is a list of points in bounded local units from its panel's position; drawn as 1.2 cm ribbons in the panel surface, centered on the native trace without endpoint extensions.
static unsigned BuildLineVerts(const float cam[3], std::vector<LineVert>& out) {
    FILE* capture = nullptr;
    if (InterlockedExchange(&g_lineCaptureRequested, 0)) {
        wchar_t path[MAX_PATH]; GetModuleFileNameW(g_selfModule(), path, MAX_PATH);
        wchar_t* slash = wcsrchr(path, L'\\'); if (slash) *(slash + 1) = 0;
        wcscat_s(path, L"witness_line_geometry.jsonl"); capture = _wfopen(path, L"w");
    }
    unsigned lines = 0;
    for (const auto& r : CoopGetRemoteLines()) {
        uintptr_t e = r.panel < 0x40000 ? GameEntity(r.panel) : 0; if (!e || !Readable((void*)(e + 0x24), 12)) continue;
        const float* pp = (const float*)(e + 0x24); float col[3]; PlayerColor(r.from, col); bool any = false;
        if (!Readable((void*)(e + 0x34), 16)) continue; const float* qp = (const float*)(e + 0x34); wframe::Q pq = {qp[0], qp[1], qp[2], qp[3]};      // this game's panel, where it is now and how it is turned (a door may have swung it): the line is in the panel's frame
        for (size_t i = 0; i + 1 < r.pts.size(); ++i) {
            float a[3], b[3], la[3], lb[3];
            for (int k = 0; k < 3; ++k) { la[k] = r.pts[i].d[k] * witness::LineUnit(r.fine); lb[k] = r.pts[i + 1].d[k] * witness::LineUnit(r.fine); }
            wframe::ToWorld(pp, pq, la, a); wframe::ToWorld(pp, pq, lb, b);
            float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (len < 1e-5f || len > 3.0f) continue;
            float q[4][3];
            if (!wframe::Ribbon(a, b, pq, 0.006f, q)) continue;
            // Capture the exact geometry this render submits, with its source units
            // and current panel frame; QA compares it with native process records.
            if (capture) {
                fprintf(capture, "{\"panel\":%u,\"from\":%u,\"seq\":%u,\"segment\":%zu,\"fine\":%s,\"pose\":[%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g],\"ends\":[", r.panel, r.from, r.seq, i, r.fine ? "true" : "false", pp[0],pp[1],pp[2],qp[0],qp[1],qp[2],qp[3]);
                for (int j=0;j<2;++j) { const float* p=j?b:a; fprintf(capture,"%s[%.9g,%.9g,%.9g]",j?",":"",p[0],p[1],p[2]); }
                fprintf(capture, "],\"quad\":[");
                for (int j=0;j<4;++j) fprintf(capture,"%s[%.9g,%.9g,%.9g]",j?",":"",q[j][0],q[j][1],q[j][2]);
                fprintf(capture,"]}\n");
            }
            static const int idx[6] = {0, 1, 2, 1, 3, 2};
            for (int k : idx) { LineVert lv; memcpy(lv.p, q[k], 12); lv.c[0] = col[0]; lv.c[1] = col[1]; lv.c[2] = col[2]; lv.c[3] = 1; out.push_back(lv); }
            any = true;
        }
        if (any) ++lines;
    }
    if (capture) fclose(capture);
    return lines;
}

void OverlayLineDump() {      // test command linedump: every line another player has on a panel, as this game holds it, and where that panel is now
    InterlockedExchange(&g_lineCaptureRequested, 1); // the next rendered frame also writes a geometry bridge dump
    char m[400]; unsigned n = 0;
    for (const auto& r : CoopGetRemoteLines()) {
        uintptr_t e = r.panel < 0x40000 ? GameEntity(r.panel) : 0; const float* pp = e && Readable((void*)(e + 0x24), 12) ? (const float*)(e + 0x24) : nullptr;
        snprintf(m, sizeof m, "linedump: from %u panel %#x final %d points %zu panel_at %.3f %.3f %.3f age_ms %llu", r.from, r.panel, r.final ? 1 : 0, r.pts.size(), pp ? pp[0] : 0, pp ? pp[1] : 0, pp ? pp[2] : 0, (unsigned long long)r.ageMs); Log(m);
        for (size_t i = 0; i < r.pts.size() && i < 8; ++i) { snprintf(m, sizeof m, "linedump:   point %zu node %u d_mm %d %d %d", i, (unsigned)r.pts[i].node, (int)witness::LineLegacy(r.pts[i].d[0], r.fine), (int)witness::LineLegacy(r.pts[i].d[1], r.fine), (int)witness::LineLegacy(r.pts[i].d[2], r.fine)); Log(m); }
        ++n;
    }
    snprintf(m, sizeof m, "linedump: %u lines", n); Log(m);
}
static void DrawMarkers(IDXGISwapChain* sc) {
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb))) return;
    D3D11_TEXTURE2D_DESC td; bb->GetDesc(&td);
    ID3D11RenderTargetView* rtv = nullptr;
    if (FAILED(g_dev->CreateRenderTargetView(bb, nullptr, &rtv))) { bb->Release(); return; }
    uintptr_t base = (uintptr_t)GetModuleHandleA(nullptr);
    const float* cam = (const float*)(base + kCamPos); const float* ang = (const float*)(base + kCamAng);
    float rects[32][4] = {}, cols[32][4] = {}; int n = 0; float aspect = (float)td.Width / (float)td.Height; const float vfov = GameProjection().vfovDeg;
    uintptr_t glob = Readable((void*)(base + kGlobals), 8) ? *(uintptr_t*)(base + kGlobals) : 0;
    uintptr_t arr = glob && Readable((void*)(glob + 0x18), 8) ? *(uintptr_t*)(glob + 0x18) : 0;
    auto remotes = CoopGetRemotes();
    if (GamePaused()) {
        if (ModelGpuReady()) { wm::Cam wc = {{cam[0], cam[1], cam[2]}, ang[0], ang[1], vfov, aspect, 0.1f, 2000.0f}; float p[3] = {}; ModelGpuDraw(rtv, td.Width, td.Height, wc, false, 0, p, 0); }      // draws nothing and says so in the stats
        rtv->Release(); bb->Release(); return;
    }
    if (ModelGpuReady()) {      // the character model replaces the flat puppet
        wm::Cam wc = {{cam[0], cam[1], cam[2]}, ang[0], ang[1], vfov, aspect, 0.1f, 2000.0f};
        bool first = true;
        for (const auto& rp : remotes) if (rp.ageMs < 2000) {
            ModelExtra ex; ex.seated=rp.emote==14 && TvSeatedPose(rp.pos); ex.appearance = rp.appearance; ex.proximitySpeaking = rp.proximitySpeaking; ex.radioEquipped = rp.radioEquipped; ex.radioTx = rp.radioTx; ex.viewPitch = rp.ang[1]; { int e = 0; float t = 0; if (SocialEmoteOf(rp.id, &e, &t)) { ex.emote = e; ex.emoteT = t; } }
            ModelGpuDraw(rtv, td.Width, td.Height, wc, true, rp.seq, rp.pos, rp.ang[0], rp.id, first, &ex); first = false;
        }
        if (first) { float p[3] = {}; ModelGpuDraw(rtv, td.Width, td.Height, wc, false, 0, p, 0); }
        ModelGpuDrawAudience(rtv,td.Width,td.Height,wc);
        std::vector<LineVert> lv; unsigned nl = 0; if (!GetCfg().noLineDraw) nl = BuildLineVerts(wc.pos, lv);
        nl += SocialInkVerts(wc.pos, lv, 60000);
        ModelGpuDrawLines(rtv, td.Width, td.Height, wc, lv.data(), lv.size(), nl);
        if (GetCfg().radioEnabled && !GetCfg().noRadioView) {
            static wradio::Raise raise; static uint64_t last; uint64_t now=GetTickCount64();
            raise.Step(VoiceRadioTx(), last ? (now-last)/1000.0f : 0); last=now;
            std::vector<LineVert> radio; float tv=std::tan(wc.vfovDeg*.00872664626f);
            for (const auto& v : wradio::Mesh(VoiceRadioTx() && (now/180)%2==0)) {
                float p[3]; wradio::FirstPerson(aspect,tv,raise.value,v.p,p); LineVert q;
                wradio::CameraToWorld(cam,wc.theta,wc.phi,p,q.p); memcpy(q.c,v.c,16); radio.push_back(q);
            }
            if (ModelGpuDrawLines(rtv,td.Width,td.Height,wc,radio.data(),radio.size(),1,true,true)) RadioRecord(0,raise.value,(unsigned)radio.size());
        }
        rtv->Release(); bb->Release(); return;
    }
    for (const auto& rp : remotes) if (rp.ageMs < 2000) {
        float feet[3] = {rp.pos[0], rp.pos[1], rp.pos[2] - 1.71f}, head[3] = {rp.pos[0], rp.pos[1], rp.pos[2] + 0.12f};
        float nfx, nfy, nhx, nhy;
        if (Project(cam, ang[0], ang[1], feet, aspect, &nfx, &nfy) && Project(cam, ang[0], ang[1], head, aspect, &nhx, &nhy) && n < 31) {
            float hh = fabsf(nhy - nfy) * 0.5f; if (hh < 2.0f / td.Height) hh = 2.0f / td.Height;
            int bodyIndex = n;
            rects[n][0] = (nfx + nhx) * 0.5f; rects[n][1] = (nfy + nhy) * 0.5f; rects[n][2] = hh * ((float)td.Height / td.Width) * 0.45f; rects[n][3] = hh;
            cols[n][0] = 0.1f; cols[n][1] = 0.9f; cols[n][2] = 0.2f; cols[n][3] = 1; n++;          // body: green
            float nose[3] = {rp.pos[0] + 0.7f*cosf(rp.ang[1])*cosf(rp.ang[0]), rp.pos[1] + 0.7f*cosf(rp.ang[1])*sinf(rp.ang[0]), rp.pos[2] + 0.7f*sinf(rp.ang[1])};
            float nnx, nny;
            if (Project(cam, ang[0], ang[1], nose, aspect, &nnx, &nny)) {
                rects[n][0] = nnx; rects[n][1] = nny; rects[n][2] = 6.0f / td.Width * 2; rects[n][3] = 6.0f / td.Height * 2;
                cols[n][0] = 1; cols[n][1] = 0.9f; cols[n][2] = 0; cols[n][3] = 1; n++;               // facing: yellow
            }
            if (g_frames % 300 == 1) { char m[200]; snprintf(m, sizeof m, "overlay: remote id %u age %lums at (%.2f,%.2f,%.2f) body ndc (%.3f,%.3f)", rp.id, rp.ageMs, rp.pos[0], rp.pos[1], rp.pos[2], rects[bodyIndex][0], rects[bodyIndex][1]); Log(m); }
        }
    }
    if (n) {
        float cbdata[64][4] = {}; memcpy(cbdata, rects, sizeof rects); memcpy(cbdata + 32, cols, sizeof cols);
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
    case WM_KILLFOCUS: GuideReset(); break;
    case WM_ACTIVATEAPP: if (!wp) GuideReset(); break;
    case WM_KEYDOWN: case WM_SYSKEYDOWN: if (GuideKey((unsigned)wp, true)) return 0; if (DevMenuKey((unsigned)wp, true)) return 0; break;
    case WM_KEYUP: case WM_SYSKEYUP: if (GuideKey((unsigned)wp, false)) return 0; DevMenuKey((unsigned)wp, false); break;
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
// Name tags: every other player's name and ping over their head, in their colour, visible through walls (so a friend can always be found).
static volatile LONG g_povBox, g_povPic;      // frames the other player's screen was drawn, and of those the ones with a real picture in it (the test reads them)
void OverlayPovDrawn(unsigned* box, unsigned* pic) { *box = (unsigned)g_povBox; *pic = (unsigned)g_povPic; }
static std::atomic<unsigned> g_aimed{0},g_tvAimed{0};          // the player the crosshair is on (0: none), set while drawing, read by the key handler
static std::atomic<bool> g_rosterOpen{false}; static std::atomic<int> g_rosterSel{0};
unsigned OverlayTvAim(){ return g_tvAimed.load(); }
bool PovRosterOpen() { return g_rosterOpen.load(); }
void OverlayPovState(unsigned* aimed, int* rosterOpen) { *aimed = g_aimed.load(); *rosterOpen = g_rosterOpen.load() ? 1 : 0; }
static std::vector<unsigned> PlayerIds() { std::vector<unsigned> ids; for (const auto& rp : CoopGetRemotes()) if (rp.valid && rp.ageMs < 5000) ids.push_back(rp.id); std::sort(ids.begin(), ids.end()); return ids; }
bool PovKey(unsigned vk, bool down) {       // V: watch the player you look at (again: stop); ] next, [ previous; F5: the player list
    if (GetCfg().noPartnerView && !DevMenuEnabled()) return false;
    if (g_rosterOpen.load()) {
        if (!down) return false;       // key-ups still reach the game
        std::vector<unsigned> ids = PlayerIds(); int n = (int)ids.size(), sel = g_rosterSel.load();
        if (vk == VK_UP && n) g_rosterSel = (sel + n - 1) % n;
        else if (vk == VK_DOWN && n) g_rosterSel = (sel + 1) % n;
        else if (vk == VK_RETURN) { if (n) VideoWatch(ids[std::min(sel, n - 1)]); g_rosterOpen = false; }
        else if (n && (vk == VK_LEFT || vk == VK_RIGHT || vk == 'M')) {
            unsigned id=ids[std::min(sel,n-1)]; float v=VoiceGetPlayerVolume(id);
            VoicePlayerVolume(id,vk=='M' ? (v>0 ? 0 : 1) : v+(vk==VK_RIGHT ? .1f : -.1f));
        }
        else if (vk == VK_BACK || vk == VK_DELETE) VideoClear();
        else if (vk == VK_ESCAPE || vk == guide::Roster) g_rosterOpen = false;
        return true;
    }
    if (!down) return false;
    if (vk == guide::Roster) { g_rosterOpen = true; g_rosterSel = 0; return true; }
    if (vk == guide::Watch) {      // the player the crosshair is on; failing that, the only other player, or the list when there are several (so the key never does nothing while somebody is there)
        unsigned a = g_aimed.load(); if (a) { VideoToggleWatch(a); return true; } if (VideoWatching()) { VideoClear(); return true; }
        if (GetCfg().povNeedsAim) return false;
        std::vector<unsigned> ids = PlayerIds(); if (ids.size() == 1) { VideoWatch(ids[0]); return true; } if (ids.size() > 1) { g_rosterOpen = true; g_rosterSel = 0; return true; } return false; }
    if (vk == guide::NextView) { VideoNext(PlayerIds()); return true; }
    if (vk == guide::PrevView) { VideoPrev(PlayerIds()); return true; }
    return false;
}
struct TagInfo { unsigned id; char name[25]; int ping; bool onScreen; float dist; };
static CRITICAL_SECTION g_tagLock; static bool g_tagLockInit; static std::vector<TagInfo> g_tags;
static bool TagsWanted() { return !GetCfg().noNameTags; }
// One warm prompt chip (the shared look of the guide and dev menu): dark surface, thin border, accent bar, cream text.
static void PromptChip(float x, float y, const char* text, float k, UiColor accent) {
    float tw = UiTextWidth(text, k), th = UiLineHeight(k), pad = 9 * k;
    UiRect(x - pad, y - 5 * k, tw + 2 * pad + 3 * k, th + 10 * k, uitheme::Surface); UiFrame(x - pad, y - 5 * k, tw + 2 * pad + 3 * k, th + 10 * k, uitheme::Border);
    UiRect(x - pad, y - 5 * k, 3 * k, th + 10 * k, accent); UiText(x + 3 * k, y, text, uitheme::Text, k);
}
static void DrawNameTags(float W, float H) {
    if (!g_tagLockInit) { InitializeCriticalSection(&g_tagLock); g_tagLockInit = true; }
    uintptr_t base = (uintptr_t)GetModuleHandleA(nullptr);
    const float* cam = (const float*)(base + kCamPos); const float* ang = (const float*)(base + kCamAng);
    std::vector<TagInfo> now;
    if (TagsWanted() && !GamePaused() && !IsBadReadPtr(cam, 12) && !IsBadReadPtr(ang, 8)) for (const auto& rp : CoopGetRemotes()) if (rp.ageMs < 2000) {
        TagInfo t = {}; t.id = rp.id; strncpy(t.name, rp.name[0] ? rp.name : "?", sizeof t.name - 1); t.ping = rp.pingMs;
        float head[3] = {rp.pos[0], rp.pos[1], rp.pos[2] + 0.38f}, nx, ny, d[3] = {rp.pos[0] - cam[0], rp.pos[1] - cam[1], rp.pos[2] - cam[2]};
        t.dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (Project(cam, ang[0], ang[1], head, W / H, &nx, &ny) && fabsf(nx) < 1.05f && fabsf(ny) < 1.05f) {
            t.onScreen = true; float col[3]; PlayerColor(rp.id, col);
            float k = std::max(1.0f, H / 1080.0f) * std::min(1.0f, std::max(0.7f, 10.0f / std::max(t.dist, 1.0f)));
            char b[64]; if (t.ping >= 0) snprintf(b, sizeof b, "%s  %d ms", t.name, t.ping); else snprintf(b, sizeof b, "%s  - ms", t.name);
            float tw = UiTextWidth(b, k), th = UiLineHeight(k), x = (nx * 0.5f + 0.5f) * W - tw / 2, y = (0.5f - ny * 0.5f) * H - th;
            UiRect(x - 5 * k, y - 3 * k, tw + 10 * k, th + 6 * k, Rgba(0, 0, 0, 0.55f)); UiRect(x - 5 * k, y + th + 3 * k, tw + 10 * k, 3 * k, Rgba(col[0], col[1], col[2], 1));
            UiText(x, y, b, Rgba(col[0], col[1], col[2], 1), k);
        }
        now.push_back(t);
    }
    // the player the crosshair is on: close to the screen centre and not too far; the nearest such player gets the "press V" prompt
    unsigned aimed = 0; float best = 1e9f; std::string aimedName; int aimedPing = -1;
    if (TagsWanted() && !GamePaused() && !IsBadReadPtr(cam, 12) && !IsBadReadPtr(ang, 8)) for (const auto& rp : CoopGetRemotes()) if (rp.ageMs < 2000) {
        float body[3] = {rp.pos[0], rp.pos[1], rp.pos[2] - 0.55f}, nx, ny, d[3] = {rp.pos[0] - cam[0], rp.pos[1] - cam[1], rp.pos[2] - cam[2]};
        float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (dist < 60.0f && Project(cam, ang[0], ang[1], body, W / H, &nx, &ny) && fabsf(nx) < 0.07f * 9.0f / std::max(dist, 9.0f) + 0.035f && fabsf(ny) < 0.35f * 9.0f / std::max(dist, 9.0f) + 0.10f && dist < best) { best = dist; aimed = rp.id; aimedName = rp.name; aimedPing = rp.pingMs; }
    }
    g_aimed = g_rosterOpen.load() ? 0 : aimed; g_tvAimed = g_rosterOpen.load() || best>12 ? 0 : aimed;
    VideoUpdatePlayers(PlayerIds());
    if (aimed && !g_rosterOpen.load()) {
        float col[3]; PlayerColor(aimed, col); bool watching = VideoWatching() == aimed; float k = std::max(1.0f, H / 1080.0f);
        char b[96]; snprintf(b, sizeof b, watching ? "[V] stop watching %s" : "[V] watch %s's view", aimedName.empty() ? "player" : aimedName.c_str()); (void)aimedPing;
        float tw = UiTextWidth(b, k), th = UiLineHeight(k), x = W / 2 - tw / 2, y = H / 2 + 70 * k;
        (void)th; PromptChip(x, y, b, k, Rgba(col[0], col[1], col[2], 1));
    }
    if((TvSeated() || TvSeatAvailable()) && !GamePaused()) {
        char b[128]; snprintf(b,sizeof b,TvSeated()?"[%s] stand up; movement exits":"[%s] sit and watch",guide::KeyName(GetCfg().keyTvSeat).c_str());
        float k=std::max(1.f,H/1080.f),tw=UiTextWidth(b,k);
        PromptChip(W/2-tw/2,H/2+130*k,b,k,uitheme::Accent);
    }
    unsigned deployed=TvDeploymentTarget(),tvTarget=deployed?deployed:g_tvAimed.load();
    if(tvTarget && !g_rosterOpen.load()){
        std::string name="player"; for(const auto& r:CoopGetRemotes()) if(r.id==tvTarget && r.name[0]) name=r.name;
        char b[160]; snprintf(b,sizeof b,deployed?"[%s] retract %s's sky TV":"[%s] deploy %s's sky TV",guide::KeyName(GetCfg().keyTvDeploy).c_str(),name.c_str());
        float k=std::max(1.f,H/1080.f),tw=UiTextWidth(b,k),th=UiLineHeight(k),x=W/2-tw/2,y=H/2+100*k;
        (void)th; (void)tw; PromptChip(x,y,b,k,uitheme::Accent);
    }
    if (!aimed && !g_rosterOpen.load() && !VideoWatching() && !GamePaused()) {      // a quiet reminder that the other players' screens can be watched
        std::vector<unsigned> ids = PlayerIds(); if (!ids.empty()) {
            float k = std::max(1.0f, H / 1080.0f); char b[96]; std::string nm; if (ids.size() == 1) for (const auto& r : CoopGetRemotes()) if (r.id == ids[0] && r.name[0]) nm = r.name;
            if (!nm.empty()) snprintf(b, sizeof b, "V  watch %s's view", nm.c_str()); else snprintf(b, sizeof b, "V  watch a player's view   F5  list");
            float tw = UiTextWidth(b, 0.8f * k), x = W - tw - 24 * k, y = H - 50 * k; (void)tw; PromptChip(x, y, b, 0.8f * k, uitheme::Muted);
        }
    }
    if (g_rosterOpen.load()) {          // the player list: Up/Down, Enter watches, Backspace clears, F5 / Esc closes
        std::vector<unsigned> ids = PlayerIds(); auto layout=wradio::Roster(W,H,(unsigned)ids.size(),UiLineHeight(1));
        float k=layout.scale,lh=layout.rowStep,x=layout.x,y=layout.y,w=layout.width;
        int rows = (int)ids.size(); int sel = std::min(g_rosterSel.load(), std::max(0, rows - 1)); g_rosterSel = sel;
        UiRect(x, y, w, layout.height, uitheme::Surface);
        UiFrame(x, y, w, layout.height, uitheme::Border);
        UiRect(x, y, 4 * k, layout.height, uitheme::Accent);
        UiText(x + 14 * k, y + 8 * k, "Players   Up/Down  Enter: watch  Backspace: clear  F5: close", uitheme::Accent, 0.8f * k);
        UiText(x+14*k,y+lh*(rows+2)+6*k,wradio::kRosterHint,uitheme::Muted,.8f*k);
        std::vector<RemotePose> rem = CoopGetRemotes();
        for (int i = 0; i < rows; ++i) {
            const RemotePose* p = nullptr; for (const auto& r : rem) if (r.id == ids[i]) p = &r;
            float col[3]; PlayerColor(ids[i], col); float ry = y + 10 * k + lh * (i + 1.4f);
            if (i == sel) {
                UiRect(x + 6 * k, ry - 3 * k, w - 12 * k, lh, uitheme::Selected);
                UiRect(x + 6 * k, ry - 3 * k, 3 * k, lh, uitheme::Accent);
            }
            UiRect(x + 14 * k, ry + 2 * k, 10 * k, 10 * k, Rgba(col[0], col[1], col[2], 1));
            char b[96]; snprintf(b, sizeof b, "%-18.18s %s%d ms %s", p && p->name[0] ? p->name : "?", p && p->pingMs >= 0 ? "" : "- ", p && p->pingMs >= 0 ? p->pingMs : 0, VideoWatching() == ids[i] ? "  [watching]" : "");
            if (p && p->pingMs < 0) snprintf(b, sizeof b, "%-18.18s -- ms %s", p->name[0] ? p->name : "?", VideoWatching() == ids[i] ? "  [watching]" : "");
            { size_t len=strlen(b); snprintf(b+len,sizeof b-len,"  %d%%",int(VoiceGetPlayerVolume(ids[i])*100+.5f)); }
            UiText(x + 32 * k, ry, b, i == sel ? uitheme::Text : uitheme::Muted, k);
        }
        if (!rows) UiText(x + 14 * k, y + 10 * k + lh * 1.4f, "nobody else is here yet", uitheme::Muted, k);
    }
    EnterCriticalSection(&g_tagLock); g_tags = std::move(now); LeaveCriticalSection(&g_tagLock);
}
void OverlayLogTags() {       // witness_cmd.txt: tagstat -> one log line per remote player: name, ping, whether its tag is on screen
    if (!g_tagLockInit) return;
    EnterCriticalSection(&g_tagLock); std::vector<TagInfo> t = g_tags; LeaveCriticalSection(&g_tagLock);
    char m[200]; snprintf(m, sizeof m, "tagstat: players %zu", t.size()); Log(m);
    for (const auto& x : t) { snprintf(m, sizeof m, "tagstat: id %u name %s ping %d onscreen %d dist %.1f", x.id, x.name, x.ping, (int)x.onScreen, x.dist); Log(m); }
}

void SaveMetaTick();      // savemeta_game.cpp
#include "mapgen.h"
void MapGenCountOverlayFrame();
unsigned OverlayFrames() { return (unsigned)g_frames; }
// A test capture uses the exact live scene renderer at a requested size. This
// avoids desktop/work-area limits on native windows when checking ultrawide/4K.
static SRWLOCK g_uiCaptureLock = SRWLOCK_INIT;
static unsigned g_uiCaptureW, g_uiCaptureH;
static char g_uiCaptureName[16];
void OverlayRequestUiCapture(unsigned width, unsigned height, const char* name) {
    if (width < 320 || width > 7680 || height < 200 || height > 4320) return;
    for (const char* p = name; *p; ++p) if (!isalnum((unsigned char)*p) && *p != '_') return;
    AcquireSRWLockExclusive(&g_uiCaptureLock);
    g_uiCaptureW = width; g_uiCaptureH = height;
    snprintf(g_uiCaptureName, sizeof g_uiCaptureName, "%s", name);
    ReleaseSRWLockExclusive(&g_uiCaptureLock);
}
static void DrawUiScene(float width, float height, ID3D11RenderTargetView* rtv) {
    UiBegin(width, height); DrawNameTags(width, height); { uintptr_t b0 = (uintptr_t)GetModuleHandleA(nullptr); const float* c0 = (const float*)(b0 + kCamPos); const float* a0 = (const float*)(b0 + kCamAng); if (!IsBadReadPtr(c0, 12) && !IsBadReadPtr(a0, 8)) SocialFrame(width, height, c0, a0); } RaceDraw(width, height); if (DevMenuOpen()) DevMenuDraw(width, height); else DevMenuToast(width, height);
        if (TvConsent() || TvEnabled()) PromptChip(14, 14, TvConsent() ? "Sharing my TV view   F11 stop   F10 back TV" : "Back TV on   F10 off   F11 share my view", 0.9f * std::max(1.f, height / 1080.f), uitheme::Accent);
        if (VideoViewMode()) {      // the partner's screen, in a corner (F9: small, large, off)
            int vw = 0, vh = 0; unsigned long age = 0; ID3D11ShaderResourceView* srv = VideoRemoteSrv(&vw, &vh, &age); InterlockedIncrement(&g_povBox); if (srv) InterlockedIncrement(&g_povPic);
            float k = (VideoViewMode() == 2 ? 2.0f : 1.0f) * std::max(1.f, height / 1080.f), iw = (vw ? vw : 480) * k, ih = (vh ? vh : 270) * k, x = width - iw - 14, y = 14;
            UiRect(x - 3, y - 3, iw + 6, ih + 6 + UiLineHeight(0.85f), Rgba(0, 0, 0, 0.8f));
            if (srv) UiImage(x, y, iw, ih, srv); else UiText(x + 10, y + ih / 2 - 10, "waiting for the picture", Rgba(1, 0.8f, 0.5f), 0.9f);
            { const char* nm = "player"; unsigned wid = VideoWatching(); float pc[3] = {1, 0.6f, 0.2f}; PlayerColor(wid, pc); std::vector<RemotePose> rem = CoopGetRemotes(); for (const auto& r : rem) if (r.id == wid && r.name[0]) nm = r.name;
              char b[96]; snprintf(b, sizeof b, "%s's view%s    [ ] cycle   V clear   F5 list", nm, srv ? (age > 2000 ? "  (stale)" : "") : "  (waiting)"); UiText(x + 2, y + ih + 2, b, Rgba(pc[0], pc[1], pc[2]), 0.85f); }
        }
        GuideDraw(width, height);
        UiEnd(rtv);
}
static void CaptureUiScene(ID3D11Texture2D* background) {
    unsigned width, height; char name[16];
    AcquireSRWLockExclusive(&g_uiCaptureLock);
    width = g_uiCaptureW; height = g_uiCaptureH; strcpy(name, g_uiCaptureName); g_uiCaptureW = 0;
    ReleaseSRWLockExclusive(&g_uiCaptureLock);
    if (!width) return;
    D3D11_TEXTURE2D_DESC desc{}; background->GetDesc(&desc);
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D *copy = nullptr, *target = nullptr;
    ID3D11ShaderResourceView* picture = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    if (SUCCEEDED(g_dev->CreateTexture2D(&desc, nullptr, &copy))) {
        g_ctx->CopyResource(copy, background);
        g_dev->CreateShaderResourceView(copy, nullptr, &picture);
    }
    desc.Width = width; desc.Height = height; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (SUCCEEDED(g_dev->CreateTexture2D(&desc, nullptr, &target)) &&
        SUCCEEDED(g_dev->CreateRenderTargetView(target, nullptr, &rtv))) {
        float clear[4] = {0, 0, 0, 1}; g_ctx->ClearRenderTargetView(rtv, clear);
        UiBegin(float(width), float(height)); UiImage(0, 0, float(width), float(height), picture); UiEnd(rtv);
        DrawUiScene(float(width), float(height), rtv);
        UiLogStats(); TakeShot(nullptr, name, target);
    } else Log("uicapture: render target failed");
    if (rtv) rtv->Release(); if (target) target->Release();
    if (picture) picture->Release(); if (copy) copy->Release();
}
static void DrawDevMenu(IDXGISwapChain* sc) {
    if (!g_uiReady) return;
    DevMenuTick(); SaveMetaTick();
    if (!GuideOpen() && !DevMenuOpen() && !DevMenuToastWanted() && !VideoViewMode() && !TagsWanted() && !RaceActive() && !g_rosterOpen.load() && !TvConsent() && !TvEnabled() && !TvDeploymentTarget()) return;
    ID3D11Texture2D* bb = nullptr; if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb))) return;
    CaptureUiScene(bb);
    D3D11_TEXTURE2D_DESC td; bb->GetDesc(&td); ID3D11RenderTargetView* rtv = nullptr;
    if (SUCCEEDED(g_dev->CreateRenderTargetView(bb, nullptr, &rtv))) {
        DrawUiScene(float(td.Width), float(td.Height), rtv); rtv->Release();
    }
    bb->Release();
}

// While a new audience screen measures the ground (tv.cpp drops the player onto each point), the last normal frame is shown instead, for
// the player and for the TV stream taken from it, and a few frames more after the return so the jump back is not seen either.
static ID3D11Texture2D* g_held; static int g_heldTail;
static void HoldPictureWhileProbing(IDXGISwapChain* sc) {
    if (TvProbing()) g_heldTail = 4; else if (g_heldTail > 0) --g_heldTail;
    if (!g_heldTail) { if (g_held) { g_held->Release(); g_held = nullptr; } return; }
    ID3D11Texture2D* bb = nullptr; if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb))) return;
    D3D11_TEXTURE2D_DESC d; bb->GetDesc(&d);
    if (g_held) { D3D11_TEXTURE2D_DESC h; g_held->GetDesc(&h); if (h.Width != d.Width || h.Height != d.Height || h.Format != d.Format || h.SampleDesc.Count != d.SampleDesc.Count) { g_held->Release(); g_held = nullptr; g_heldTail = 0; bb->Release(); return; } }
    if (GetCfg().tvProbeShow) {}
    else if (!g_held) { d.BindFlags = 0; d.CPUAccessFlags = 0; d.Usage = D3D11_USAGE_DEFAULT; d.MiscFlags = 0; if (SUCCEEDED(g_dev->CreateTexture2D(&d, nullptr, &g_held))) g_ctx->CopyResource(g_held, bb); }
    else g_ctx->CopyResource(bb, g_held);
    static DWORD said; if (TvProbing() && GetTickCount() - said > 2000) { DevMenuNotice("Setting up the screen: measuring the ground under it..."); said = GetTickCount(); }
    bb->Release();
}

void TracerTick();      // tracer.cpp: the bot's in-game line tracer
static HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
    LONG f = InterlockedIncrement(&g_frames);
    GameRunQueued();
    StateApplyRemote();
    if (f == 1) Log("overlay: Present hook called (first frame)");
    if (!g_ready && !g_failed) { g_ready = InitResources(sc); g_failed = !g_ready; }
    DepthDiagFrame((unsigned)f);
    bool mapgen = MapGenActive() && !GetCfg().mapgenShowOverlay; if (MapGenActive() && !mapgen) MapGenCountOverlayFrame();      // the map picture is made of the game's frames alone
    if (g_ready) { const float* camera = (const float*)((uintptr_t)GetModuleHandleA(nullptr) + kCamPos);
        if (Readable(camera, 12)) TvTick(camera, !mapgen && !GamePaused() && CoopCore() && CoopCore()->ready());
        HoldPictureWhileProbing(sc);
        if (!mapgen) VideoFrame(sc, g_dev, g_ctx);
    }
    if (g_ready && !mapgen) DrawMarkers(sc);
    if (g_ready && g_uiReady && !mapgen) { DrawDevMenu(sc); }
    RadioDumpFrame();
    if (g_ready) MapGenTick(sc);      // the picture for the partner is taken before the menu and the inset are drawn
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
    else if (GetCfg().runUnfocused) {      // the game's loop sleeps 100 ms (about 10 fps) when both the byte +0x21 (its window is active) and the byte at exe+0x62D394 are 0. Setting +0x21 made every window believe it was active and grab the real mouse (both windows, all the time); the other byte does not touch the cursor.
        uintptr_t base = (uintptr_t)GetModuleHandleA(nullptr);
        if (GetCfg().runUnfocusedOldByte) { uintptr_t p = IsBadReadPtr((void*)(base + 0x469A5B0), 8) ? 0 : *(uintptr_t*)(base + 0x469A5B0); if (p && !IsBadWritePtr((void*)(p + 0x21), 1)) *(volatile unsigned char*)(p + 0x21) = 1; }
        else if (!IsBadWritePtr((void*)(base + 0x62D394), 1)) *(volatile unsigned char*)(base + 0x62D394) = 1;
    }
    { DXGI_SWAP_CHAIN_DESC d; if (SUCCEEDED(sc->GetDesc(&d))) { g_gameWnd = d.OutputWindow; InstallDevWndProc(d.OutputWindow); TracerTick(); InputPump(d.OutputWindow); } }
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
