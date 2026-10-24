// Partner view transport and codec (see video.h). Capture happens on the render thread without stalling the GPU (a copy now, the read
// a few frames later); JPEG encode and decode use the Windows imaging component on their own threads.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>
#include <objbase.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include "video.h"
#include "config.h"
#include "coop.h"

void Log(const char* msg);
extern HMODULE g_selfModule();

extern ID3D11DeviceContext* g_videoCtx; extern ID3D11Device* g_videoDev;
namespace {
const int kW = 480, kH = 270;                       // size of the picture that is sent
const uint32_t kMagicVid = 0x34565457;              // "WTV4"
const size_t kChunk = 1100;
CRITICAL_SECTION g_cs; bool g_csInit;
void Init() { if (!g_csInit) { InitializeCriticalSection(&g_cs); g_csInit = true; } }

// ---- capture (render thread)
ID3D11Texture2D* g_stage; UINT g_sw, g_sh; DXGI_FORMAT g_sfmt; enum { Idle, Copied } g_state; LONG g_copyFrame; DWORD g_lastCap; LONG g_frames;
std::vector<uint8_t> g_outRgba;                      // the newest downsampled picture, waiting for the encoder
bool g_outFresh; HANDLE g_outEvent; HANDLE g_thread;
volatile LONG g_sent, g_recv, g_decoded; volatile int g_mode;

// ---- receive
uint32_t g_curFrame; std::vector<std::vector<uint8_t>> g_chunks; std::vector<bool> g_have; unsigned g_haveCount, g_expect; uint32_t g_lastDone;
std::vector<uint8_t> g_remote; bool g_remoteNew; DWORD g_remoteAt; ID3D11Texture2D* g_remoteTex; ID3D11ShaderResourceView* g_remoteSrv;
IWICImagingFactory* g_wicRecv;

uint32_t Sum(const uint8_t* p, size_t n) { uint32_t h = 2166136261u; for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; } return h; }

bool Encode(IWICImagingFactory* wic, const uint8_t* rgba, std::vector<uint8_t>& out) {
    bool ok = false; IWICBitmap* bmp = nullptr; IStream* st = nullptr; IWICBitmapEncoder* enc = nullptr; IWICBitmapFrameEncode* fr = nullptr; IPropertyBag2* pb = nullptr;
    if (SUCCEEDED(wic->CreateBitmapFromMemory(kW, kH, GUID_WICPixelFormat32bppRGBA, kW * 4, kW * kH * 4, (BYTE*)rgba, &bmp)) && SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &st)) &&
        SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &enc)) && SUCCEEDED(enc->Initialize(st, WICBitmapEncoderNoCache)) && SUCCEEDED(enc->CreateNewFrame(&fr, &pb))) {
        PROPBAG2 opt = {}; opt.pstrName = (LPOLESTR)L"ImageQuality"; VARIANT v; VariantInit(&v); v.vt = VT_R4; v.fltVal = 0.65f; pb->Write(1, &opt, &v);
        WICPixelFormatGUID pf = GUID_WICPixelFormat24bppBGR;
        if (SUCCEEDED(fr->Initialize(pb)) && SUCCEEDED(fr->SetSize(kW, kH)) && SUCCEEDED(fr->SetPixelFormat(&pf)) && SUCCEEDED(fr->WriteSource(bmp, nullptr)) && SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit())) {
            HGLOBAL hg = nullptr; if (SUCCEEDED(GetHGlobalFromStream(st, &hg)) && hg) { SIZE_T n = GlobalSize(hg); void* p = GlobalLock(hg); if (p) { out.assign((uint8_t*)p, (uint8_t*)p + n); GlobalUnlock(hg); ok = !out.empty(); } }
        }
    }
    if (pb) pb->Release(); if (fr) fr->Release(); if (enc) enc->Release(); if (st) st->Release(); if (bmp) bmp->Release();
    return ok;
}

bool Decode(IWICImagingFactory* wic, const uint8_t* jpg, size_t n, std::vector<uint8_t>& rgba) {
    bool ok = false; IWICStream* st = nullptr; IWICBitmapDecoder* dec = nullptr; IWICBitmapFrameDecode* fr = nullptr; IWICFormatConverter* cv = nullptr;
    if (SUCCEEDED(wic->CreateStream(&st)) && SUCCEEDED(st->InitializeFromMemory((BYTE*)jpg, (DWORD)n)) && SUCCEEDED(wic->CreateDecoderFromStream(st, nullptr, WICDecodeMetadataCacheOnDemand, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &fr)) && SUCCEEDED(wic->CreateFormatConverter(&cv)) && SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
        UINT w = 0, h = 0; cv->GetSize(&w, &h);
        if ((int)w == kW && (int)h == kH) { rgba.resize((size_t)kW * kH * 4); ok = SUCCEEDED(cv->CopyPixels(nullptr, kW * 4, (UINT)rgba.size(), rgba.data())); }
    }
    if (cv) cv->Release(); if (fr) fr->Release(); if (dec) dec->Release(); if (st) st->Release();
    return ok;
}

DWORD WINAPI EncodeThread(LPVOID) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED); IWICImagingFactory* wic = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) { Log("video: no WIC factory, partner view cannot send"); return 0; }
    uint32_t frameId = 0; std::vector<uint8_t> px, jpg;
    for (;;) {
        WaitForSingleObject(g_outEvent, INFINITE);
        EnterCriticalSection(&g_cs); bool have = g_outFresh; if (have) { px.swap(g_outRgba); g_outFresh = false; } LeaveCriticalSection(&g_cs);
        if (!have || px.size() != (size_t)kW * kH * 4 || !Encode(wic, px.data(), jpg)) continue;
        size_t count = (jpg.size() + kChunk - 1) / kChunk; ++frameId;
        for (size_t i = 0; i < count; i++) {
            size_t len = std::min(kChunk, jpg.size() - i * kChunk); std::vector<uint8_t> pkt(20 + len + 4);
            uint32_t magic = kMagicVid, from = 0; memcpy(&pkt[0], &magic, 4); memcpy(&pkt[4], &from, 4); memcpy(&pkt[8], &frameId, 4);
            uint16_t idx = (uint16_t)i, cnt = (uint16_t)count, l = (uint16_t)len; memcpy(&pkt[12], &idx, 2); memcpy(&pkt[14], &cnt, 2); memcpy(&pkt[16], &l, 2); pkt[18] = pkt[19] = 0;
            memcpy(&pkt[20], &jpg[i * kChunk], len); uint32_t sum = Sum(pkt.data(), 20 + len); memcpy(&pkt[20 + len], &sum, 4);
            CoopSendRaw(pkt.data(), pkt.size());
        }
        InterlockedIncrement(&g_sent);
    }
}
}  // namespace

bool VideoEnabled() { return GetCfg().partnerView || GetCfg().devMenu; }
int VideoViewMode() { return g_mode; }
void VideoSetMode(int m) { g_mode = m < 0 ? 0 : m > 2 ? 2 : m; }
void VideoToggleView() { g_mode = (g_mode + 1) % 3; }
void VideoStats(unsigned* s, unsigned* r, unsigned* d) { *s = (unsigned)g_sent; *r = (unsigned)g_recv; *d = (unsigned)g_decoded; }

void VideoFrame(IDXGISwapChain* sc, ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    if (!VideoEnabled() || GetCfg().noPartnerVideo) return;
    Init(); if (!g_thread) { g_outEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr); g_thread = CreateThread(nullptr, 0, EncodeThread, nullptr, 0, nullptr); }
    LONG frame = InterlockedIncrement(&g_frames);
    ID3D11Texture2D* bb = nullptr; if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb))) return;
    D3D11_TEXTURE2D_DESC td; bb->GetDesc(&td);
    if (td.Format != DXGI_FORMAT_R8G8B8A8_UNORM && td.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB && td.Format != DXGI_FORMAT_B8G8R8A8_UNORM) { bb->Release(); return; }
    if (!g_stage || g_sw != td.Width || g_sh != td.Height || g_sfmt != td.Format) {
        if (g_stage) { g_stage->Release(); g_stage = nullptr; } g_state = Idle;
        D3D11_TEXTURE2D_DESC sd = td; sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; sd.MiscFlags = 0; sd.SampleDesc.Count = 1; sd.SampleDesc.Quality = 0;
        if (FAILED(dev->CreateTexture2D(&sd, nullptr, &g_stage))) { bb->Release(); return; } g_sw = td.Width; g_sh = td.Height; g_sfmt = td.Format;
    }
    if (g_state == Idle && GetTickCount() - g_lastCap >= 200) { ctx->CopyResource(g_stage, bb); g_state = Copied; g_copyFrame = frame; g_lastCap = GetTickCount(); }
    else if (g_state == Copied && frame - g_copyFrame >= 2) {
        D3D11_MAPPED_SUBRESOURCE ms;
        HRESULT hr = ctx->Map(g_stage, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &ms);
        if (SUCCEEDED(hr)) {
            std::vector<uint8_t> px((size_t)kW * kH * 4); bool bgra = td.Format == DXGI_FORMAT_B8G8R8A8_UNORM;
            for (int y = 0; y < kH; y++) for (int x = 0; x < kW; x++) {
                unsigned sx0 = (unsigned)((uint64_t)x * g_sw / kW), sx1 = (unsigned)((uint64_t)(x + 1) * g_sw / kW), sy0 = (unsigned)((uint64_t)y * g_sh / kH), sy1 = (unsigned)((uint64_t)(y + 1) * g_sh / kH);
                unsigned xs[2] = {sx0 + (sx1 - sx0) / 4, sx0 + 3 * (sx1 - sx0) / 4}, ys[2] = {sy0 + (sy1 - sy0) / 4, sy0 + 3 * (sy1 - sy0) / 4}; unsigned acc[3] = {0, 0, 0};
                for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) { const uint8_t* s = (const uint8_t*)ms.pData + (size_t)ys[j] * ms.RowPitch + (size_t)xs[i] * 4; acc[0] += s[0]; acc[1] += s[1]; acc[2] += s[2]; }
                uint8_t* d = &px[((size_t)y * kW + x) * 4]; d[0] = (uint8_t)(acc[bgra ? 2 : 0] / 4); d[1] = (uint8_t)(acc[1] / 4); d[2] = (uint8_t)(acc[bgra ? 0 : 2] / 4); d[3] = 255;
            }
            ctx->Unmap(g_stage, 0); g_state = Idle;
            EnterCriticalSection(&g_cs); g_outRgba.swap(px); g_outFresh = true; LeaveCriticalSection(&g_cs); SetEvent(g_outEvent);
        } else if (hr != DXGI_ERROR_WAS_STILL_DRAWING) g_state = Idle;
    }
    bb->Release();
}

void VideoOnPacket(const uint8_t* d, size_t n) {
    Init(); if (n < 24 || n > 20 + kChunk + 4) return;
    uint32_t magic; memcpy(&magic, d, 4); if (magic != kMagicVid) return;
    uint32_t sum; memcpy(&sum, d + n - 4, 4); if (Sum(d, n - 4) != sum) return;
    uint32_t frame; uint16_t idx, cnt, len; memcpy(&frame, d + 8, 4); memcpy(&idx, d + 12, 2); memcpy(&cnt, d + 14, 2); memcpy(&len, d + 16, 2);
    if (len + 24u != n || idx >= cnt || cnt == 0 || cnt > 64 || frame <= g_lastDone) return;
    if (frame > g_curFrame) { g_curFrame = frame; g_chunks.assign(cnt, {}); g_have.assign(cnt, false); g_haveCount = 0; g_expect = cnt; }
    if (frame < g_curFrame || cnt != g_expect || g_have[idx]) return;
    g_chunks[idx].assign(d + 20, d + 20 + len); g_have[idx] = true; g_haveCount++;
    if (g_haveCount < g_expect) return;
    std::vector<uint8_t> jpg; for (auto& c : g_chunks) jpg.insert(jpg.end(), c.begin(), c.end());
    g_lastDone = frame; InterlockedIncrement(&g_recv);
    if (!g_wicRecv) { CoInitializeEx(nullptr, COINIT_MULTITHREADED); if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wicRecv)))) { Log("video: no WIC factory, partner view cannot decode"); return; } }
    std::vector<uint8_t> px; if (!Decode(g_wicRecv, jpg.data(), jpg.size(), px)) return;
    EnterCriticalSection(&g_cs); g_remote.swap(px); g_remoteNew = true; g_remoteAt = GetTickCount(); LeaveCriticalSection(&g_cs); InterlockedIncrement(&g_decoded);
}

ID3D11ShaderResourceView* VideoRemoteSrv(int* w, int* h, unsigned long* age) {
    Init();
    if (!g_videoDev || !g_videoCtx) return nullptr;
    if (!g_remoteTex) {
        D3D11_TEXTURE2D_DESC td = {}; td.Width = kW; td.Height = kH; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(g_videoDev->CreateTexture2D(&td, nullptr, &g_remoteTex)) || FAILED(g_videoDev->CreateShaderResourceView(g_remoteTex, nullptr, &g_remoteSrv))) return nullptr;
    }
    EnterCriticalSection(&g_cs); bool fresh = g_remoteNew && g_remote.size() == (size_t)kW * kH * 4; if (fresh) { g_videoCtx->UpdateSubresource(g_remoteTex, 0, nullptr, g_remote.data(), kW * 4, 0); g_remoteNew = false; } bool any = g_remoteAt != 0; DWORD at = g_remoteAt; LeaveCriticalSection(&g_cs);
    if (!any) return nullptr; *w = kW; *h = kH; *age = GetTickCount() - at; return g_remoteSrv;
}

void VideoSavePartnerShot(const char* name) {
    Init(); std::vector<uint8_t> px; EnterCriticalSection(&g_cs); px = g_remote; LeaveCriticalSection(&g_cs);
    if (px.size() != (size_t)kW * kH * 4) { Log("video: no partner picture to save"); return; }
    wchar_t path[MAX_PATH]; GetModuleFileNameW(g_selfModule(), path, MAX_PATH); wchar_t* sl = wcsrchr(path, L'\\'); if (sl) *(sl + 1) = 0; wcscat_s(path, L"shots"); CreateDirectoryW(path, nullptr); wcscat_s(path, L"\\");
    wchar_t w[80]; MultiByteToWideChar(CP_UTF8, 0, name, -1, w, 80); wcscat_s(path, w); wcscat_s(path, L".bmp");
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr); if (f == INVALID_HANDLE_VALUE) return;
    BITMAPFILEHEADER fh = {0x4D42, (DWORD)(54 + kW * kH * 4), 0, 0, 54}; BITMAPINFOHEADER ih = {40, kW, -kH, 1, 32, BI_RGB, (DWORD)(kW * kH * 4), 0, 0, 0, 0}; DWORD n;
    WriteFile(f, &fh, 14, &n, nullptr); WriteFile(f, &ih, 40, &n, nullptr);
    std::vector<uint8_t> row(kW * 4); for (int y = 0; y < kH; y++) { for (int x = 0; x < kW; x++) { const uint8_t* s = &px[((size_t)y * kW + x) * 4]; row[x * 4] = s[2]; row[x * 4 + 1] = s[1]; row[x * 4 + 2] = s[0]; row[x * 4 + 3] = 255; } WriteFile(f, row.data(), kW * 4, &n, nullptr); }
    CloseHandle(f); char m[100]; snprintf(m, sizeof m, "video: wrote partner picture %s.bmp", name); Log(m);
}
