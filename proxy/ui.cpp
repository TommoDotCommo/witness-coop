// Immediate-mode 2D drawing over the game's back buffer (see ui.h). One shader, one atlas: a white texel for solid shapes and the
// printable ASCII glyphs rendered once with GDI. States are set for each flush; the game sets its own again next frame.
// The glyphs are the game's own menu face (Karmina Bold) when the player's data-pc.zip has it (common/gamefont.h: read in memory at start,
// a private GDI font, never written anywhere), else Segoe UI.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <deque>
#include <map>
#include "depth.h"
#include "gamecam.h"
#include "ui.h"
#include "ui/text.h"
#include "config.h"
#include "../common/gamefont.h"

void Log(const char* msg);
extern HMODULE g_selfModule();

namespace {
// Proportional glyphs have individual advances and padded, clipped slots. The tight config remains as a regression mutant.
const int kAtlasW = 1024, kAtlasH = 384, kLineHeight = 24, kPitchWide = 56, kFirst = 32, kLast = 126;
struct Glyph { float advance, x, y, w, h; } g_glyph[kLast - kFirst + 1];
bool g_tight;
const char* kHlsl = R"(
cbuffer C : register(b0) { float2 scr; float2 pad; float4 anchor; float4 depth; };
Texture2D<float> scene1 : register(t1); Texture2DMS<float> sceneMS : register(t2);
Texture2D tex : register(t0); SamplerState smp : register(s0);
struct VI { float2 p : POSITION; float2 t : TEXCOORD; float4 c : COLOR; };
struct O { float4 p : SV_POSITION; float2 t : TEXCOORD; float4 c : COLOR; };
O VS(VI i) { O o; o.p = float4(i.p.x / scr.x * 2 - 1, 1 - i.p.y / scr.y * 2, 0, 1); o.t = i.t; o.c = i.c; return o; }
void PlayerVisibility() {
  if (anchor.w < 0.5) return;
  // Fail closed while the scene buffer is unavailable. Reversed depth: near / view distance.
  if (depth.x < 0.5 || anchor.z <= 0 || any(anchor.xy < 0) || any(anchor.xy >= scr)) discard;
  int2 p = int2(anchor.xy); float z = depth.z / max(0.01, anchor.z - 0.10) * 1.005;
  float visible = 0;
  if (depth.x < 1.5) visible = scene1.Load(int3(p, 0)) <= z;
  else for (int n = 0; n < (int)depth.y; ++n) visible += sceneMS.Load(p, n) <= z;
  if (visible < 0.5) discard;
}
float4 PS(O i) : SV_TARGET { PlayerVisibility(); float a = tex.Sample(smp, i.t).r; clip(i.c.a * a - 0.001); return float4(i.c.rgb, i.c.a * a); }
float4 PSI(O i) : SV_TARGET { PlayerVisibility(); return tex.Sample(smp, i.t) * i.c; }
)";
struct Vtx { float x, y, u, v; uint32_t c; };
struct Seg { size_t first; bool clip; D3D11_RECT r; ID3D11ShaderResourceView* img; float anchor[4] = {}; unsigned queryId = 0; float opacity = 1; };

ID3D11Device* g_dev; ID3D11DeviceContext* g_ctx;
ID3D11VertexShader* g_vs; ID3D11PixelShader *g_ps, *g_psi; ID3D11InputLayout* g_il; ID3D11Buffer *g_vb, *g_cb;
ID3D11ShaderResourceView* g_srv; ID3D11SamplerState* g_smp; ID3D11BlendState* g_blend; ID3D11RasterizerState* g_rs; ID3D11DepthStencilState* g_ds;
struct PixelQuery { ID3D11Query* q; unsigned id; };
std::deque<PixelQuery> g_pending;
std::vector<ID3D11Query*> g_freeQueries;
std::map<unsigned, unsigned long long> g_playerPixels;
const size_t kMaxVerts = 60000;
unsigned g_textCalls = 0, g_textOverflow = 0, g_smallTargets = 0, g_clippedActions = 0;
float g_smallestText = 0;
std::vector<uint8_t> g_atlasAlpha;
std::vector<Vtx> g_v; std::vector<Seg> g_seg; float g_w = 1, g_h = 1; bool g_ok;

uint32_t Pack(UiColor c) {
    auto b = [](float v) { return (uint32_t)(v <= 0 ? 0 : v >= 1 ? 255 : v * 255 + 0.5f); };
    return b(c.r) | b(c.g) << 8 | b(c.b) << 16 | b(c.a * (g_seg.empty() ? 1.f : g_seg.back().opacity)) << 24;
}
void Quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t c) {
    if (g_v.size() + 6 > kMaxVerts) return;
    g_v.push_back({x0, y0, u0, v0, c}); g_v.push_back({x1, y0, u1, v0, c}); g_v.push_back({x0, y1, u0, v1, c});
    g_v.push_back({x1, y0, u1, v0, c}); g_v.push_back({x1, y1, u1, v1, c}); g_v.push_back({x0, y1, u0, v1, c});
}
const float kWhiteU = (kAtlasW - 3) / (float)kAtlasW, kWhiteV = (kAtlasH - 3) / (float)kAtlasH;
int Pitch() { return g_tight ? 40 : kPitchWide; }

// The face of the atlas: the game's font read before the game locks its archive (UiPrefetchGameFont, from the CreateFile hook), or Segoe UI.
SRWLOCK g_fontMu = SRWLOCK_INIT; struct FontLock { FontLock() { AcquireSRWLockExclusive(&g_fontMu); } ~FontLock() { ReleaseSRWLockExclusive(&g_fontMu); } }; gamefont::Result g_gameFont; bool g_fontTried; std::string g_fontMember, g_fontState = "not loaded";
HANDLE g_fontHandle; std::string g_fontFace = "Segoe UI";
void LoadGameFont(const wchar_t* zipPath, void* (*openFile)(const wchar_t*)) {      // caller holds g_fontMu
    if (g_fontTried) return;
    const gamefont::Face* face = gamefont::FindFace(GetCfg().uiFont);
    if (!face) { g_fontTried = true; g_fontState = "ui_font=" + GetCfg().uiFont + " (Segoe UI chosen)"; return; }
    g_fontMember = face->member; g_gameFont = gamefont::FromZipFile(zipPath, face->member, openFile);
    g_fontTried = true;
    g_fontState = g_gameFont.ok() ? "read " + std::to_string(g_gameFont.bytes.size()) + " bytes, family " + g_gameFont.family : g_gameFont.error;
}
// Selects the face for the atlas into dc; returns the font object. Falls back to Segoe UI with a log line when the game's font is missing or GDI refuses it.
HFONT AtlasFont(HDC dc) {
    FontLock lk;
    if (!g_fontTried) {      // the hook did not see the game open its archive: try it beside the exe (works while nothing holds it exclusively)
        wchar_t p[MAX_PATH]; GetModuleFileNameW(nullptr, p, MAX_PATH); wchar_t* sl = wcsrchr(p, L'\\'); if (sl) *(sl + 1) = 0; wcscat_s(p, L"data-pc.zip");
        LoadGameFont(p, nullptr); g_fontTried = true;
    }
    const gamefont::Face* face = gamefont::FindFace(GetCfg().uiFont);
    if (g_gameFont.ok() && face) {
        DWORD n = 0; g_fontHandle = AddFontMemResourceEx(g_gameFont.bytes.data(), (DWORD)g_gameFont.bytes.size(), nullptr, &n);      // private to this process by definition
        if (g_fontHandle && n) {
            HFONT f = CreateFontA(-40, 0, 0, 0, face->weight, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, g_gameFont.family.c_str());
            char got[64] = {}; if (f) { SelectObject(dc, f); GetTextFaceA(dc, sizeof got, got); }
            if (f && g_gameFont.family == got) {
                g_fontFace = got; char m[220]; snprintf(m, sizeof m, "ui: font %s from the game (%s, %zu bytes)", got, g_fontMember.c_str(), g_gameFont.bytes.size()); Log(m); return f;
            }
            g_fontState = std::string("GDI chose '") + got + "' instead of " + g_gameFont.family; if (f) DeleteObject(f);
        } else g_fontState = "AddFontMemResourceEx failed";
    }
    HFONT f = CreateFontA(-40, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    g_fontFace = "Segoe UI"; char m[260]; snprintf(m, sizeof m, "ui: font Segoe UI (the game's font is not used: %s)", g_fontState.c_str()); Log(m);
    SelectObject(dc, f); return f;
}
}  // namespace

void UiPrefetchGameFont(const wchar_t* zipPath, void* (*openFile)(const wchar_t*)) { FontLock lk; LoadGameFont(zipPath, openFile); }
void UiFontStat() {
    FontLock lk; char m[300];
    snprintf(m, sizeof m, "fontstat: face %s game %d member %s state %s", g_fontFace.c_str(), g_fontFace != "Segoe UI" ? 1 : 0, g_fontMember.empty() ? "-" : g_fontMember.c_str(), g_fontState.c_str()); Log(m);
}

bool UiInit(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    g_dev = dev; g_ctx = ctx;
    // A 2x atlas keeps scaled text crisp. The face is the game's own (AtlasFont), or Segoe UI, a Windows system font that needs no bundled asset.
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO bi = {}; bi.bmiHeader = {sizeof(BITMAPINFOHEADER), kAtlasW, -kAtlasH, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
    void* bits = nullptr; HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dc || !bmp || !bits) { Log("ui: atlas bitmap failed"); return false; }
    SelectObject(dc, bmp);
    HFONT font = AtlasFont(dc);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
    TEXTMETRICA tm = {}; GetTextMetricsA(dc, &tm); const int top = 2 + std::max(0, 43 - (int)tm.tmAscent);      // every face's baseline where Segoe UI's is (ascent 43 at this size), so the layout keeps its line positions
    memset(bits, 0, kAtlasW * kAtlasH * 4);
    g_tight = GetCfg().uiTightAtlas;
    int penX = 1, penY = 1; float minAdv = 1000, maxAdv = 0;
    for (int ch = kFirst; ch <= kLast; ch++) {
        char s[2] = {(char)ch, 0}; SIZE size = {}; GetTextExtentPoint32A(dc, s, 1, &size);
        int cw = std::max(1, (int)size.cx) + 8;
        if (penX + cw + 1 > kAtlasW - 4) { penX = 1; penY += Pitch(); }
        if (penY + Pitch() >= kAtlasH - 4) { Log("ui: glyph atlas overflow"); DeleteObject(font); DeleteObject(bmp); DeleteDC(dc); return false; }
        Glyph& g = g_glyph[ch - kFirst]; g = Glyph{std::max(1, (int)size.cx) / 2.f, (float)penX, (float)penY, (float)cw, (float)(Pitch() - 4)};
        minAdv = std::min(minAdv, g.advance); maxAdv = std::max(maxAdv, g.advance);
        if (ch != ' ') {
            HRGN clip = g_tight ? nullptr : CreateRectRgn(penX, penY, penX + cw, penY + Pitch()); if (clip) SelectClipRgn(dc, clip);
            TextOutA(dc, penX + 4, penY + top, s, 1);
            if (clip) { SelectClipRgn(dc, nullptr); DeleteObject(clip); }
        }
        char m[120]; snprintf(m, sizeof m, "uiglyph: %d %d %d %d %d %.3f", ch, penX, penY, cw, Pitch() - 4, g.advance); Log(m);
        penX += cw + 1;
    }
    { char m[160]; snprintf(m, sizeof m, "ui: atlas %dx%d line %d pitch %d glyphs %d advances %.1f..%.1f", kAtlasW, kAtlasH, kLineHeight * 2, Pitch(), kLast - kFirst + 1, minAdv, maxAdv); Log(m); }
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
    bd = {}; bd.ByteWidth = 48; bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; dev->CreateBuffer(&bd, nullptr, &g_cb);
    D3D11_SAMPLER_DESC sm = {}; sm.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; dev->CreateSamplerState(&sm, &g_smp);
    D3D11_BLEND_DESC bl = {}; auto& rt = bl.RenderTarget[0]; rt.BlendEnable = TRUE; rt.SrcBlend = D3D11_BLEND_SRC_ALPHA; rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D11_BLEND_ONE; rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; rt.BlendOpAlpha = D3D11_BLEND_OP_ADD; rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; dev->CreateBlendState(&bl, &g_blend);
    D3D11_RASTERIZER_DESC rd = {}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.ScissorEnable = TRUE; dev->CreateRasterizerState(&rd, &g_rs);
    D3D11_DEPTH_STENCIL_DESC dd = {}; dev->CreateDepthStencilState(&dd, &g_ds);
    g_ok = g_vs && g_ps && g_psi && g_il && g_vb && g_cb && g_srv && g_smp && g_blend && g_rs && g_ds;
    Log(g_ok ? "ui: ready" : "ui: init failed");
    return g_ok;
}

void UiBegin(float w, float h) {
    while (!g_pending.empty()) {
        auto q = g_pending.front(); UINT64 px = 0;
        HRESULT hr = g_ctx->GetData(q.q, &px, sizeof px, D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (hr == S_FALSE) break;
        if (hr == S_OK) g_playerPixels[q.id] = px;
        g_freeQueries.push_back(q.q); g_pending.pop_front();
    }
 g_w = w; g_h = h; g_textCalls = g_textOverflow = g_smallTargets = g_clippedActions = 0; g_smallestText = h; g_v.clear(); g_seg.clear(); g_seg.push_back({0, false, {}, nullptr}); }
void UiClip(float x, float y, float w, float h) {
    Seg s = g_seg.back(); s.first = g_v.size(); s.clip = w > 0; s.r = {(LONG)x, (LONG)y, (LONG)(x + w), (LONG)(y + h)}; s.img = nullptr;
    if (g_seg.back().first == s.first) g_seg.back() = s; else g_seg.push_back(s);
}
void UiPlayerAnchor(const float* cam, const float* ang, const float* eye, unsigned queryId, float opacity) {
    float dx=eye[0]-cam[0], dy=eye[1]-cam[1], dz=eye[2]-cam[2];
    float ct=cosf(ang[0]), st=sinf(ang[0]), cp=cosf(ang[1]), sp=sinf(ang[1]);
    float f=cp*(ct*dx+st*dy)+sp*dz, r=st*dx-ct*dy, u=-sp*(ct*dx+st*dy)+cp*dz;
    float tv=tanf(GameProjection().vfovDeg*3.14159265f/360.f);
    Seg s=g_seg.back(); s.first=g_v.size(); s.queryId=queryId; s.opacity=opacity;
    s.anchor[0]=(0.5f+r/(2*std::max(f,0.01f)*tv*(g_w/g_h)))*g_w;
    s.anchor[1]=(0.5f-u/(2*std::max(f,0.01f)*tv))*g_h;
    s.anchor[2]=f; s.anchor[3]=GetCfg().tagsThroughWalls ? 0.f : 1.f;
    if (g_seg.back().first==s.first) g_seg.back()=s; else g_seg.push_back(s);
}
void UiPlayerAnchorEnd() {
    Seg s=g_seg.back(); s.first=g_v.size(); memset(s.anchor,0,sizeof s.anchor); s.queryId=0; s.opacity=1;
    if (g_seg.back().first==s.first) g_seg.back()=s; else g_seg.push_back(s);
}
bool UiPlayerPixels(unsigned id, unsigned long long* pixels) {
    auto it=g_playerPixels.find(id); if(it==g_playerPixels.end()) return false; *pixels=it->second; return true;
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
float UiLineHeight(float sc) { return kLineHeight * sc; }
float UiTextWidth(const char* s, float sc) {
    return uitext::Width(s, sc, g_glyph, kFirst);
}
void UiText(float x, float y, const char* s, UiColor c, float sc) {
    ++g_textCalls; g_smallestText = std::min(g_smallestText, 20 * sc);
    uint32_t col = Pack(c); float px = x; const float e = 0.5f;
    for (; *s; s++) {
        if (*s == '\n') { px = x; y += kLineHeight * sc; continue; }
        int ch = (unsigned char)*s; if (ch < kFirst || ch > kLast) ch = '?';
        const Glyph& g = g_glyph[ch - kFirst];
        if (!g_seg.back().clip && ch != ' ' && (px < 0 || y < 0 || px + g.advance * sc > g_w + .5f || y + 24 * sc > g_h + .5f)) ++g_textOverflow;
        if (ch != ' ') Quad(px - 2 * sc, y, px + (g.w / 2 - 2) * sc, y + g.h / 2 * sc,
            (g.x + e) / kAtlasW, (g.y + e) / kAtlasH, (g.x + g.w - e) / kAtlasW, (g.y + g.h - e) / kAtlasH, col);
        px += g.advance * sc;
    }
}

void UiEnd(ID3D11RenderTargetView* rtv) {
    if (!g_ok || g_v.empty()) return;
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(g_ctx->Map(g_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
    memcpy(ms.pData, g_v.data(), g_v.size() * sizeof(Vtx)); g_ctx->Unmap(g_vb, 0);
    ID3D11ShaderResourceView* scene=nullptr; bool msaa=false; unsigned samples=1;
    bool have=DepthScene(&scene,&msaa,&samples);
    float cb[12] = {g_w, g_h, 0, 0, 0,0,0,0, have ? (msaa ? 2.f : 1.f) : 0.f, (float)samples, GetCfg().depthNear, 0};
    D3D11_VIEWPORT vp = {0, 0, g_w, g_h, 0, 1};
    g_ctx->OMSetRenderTargets(1, &rtv, nullptr); g_ctx->RSSetViewports(1, &vp); g_ctx->RSSetState(g_rs);
    g_ctx->OMSetBlendState(g_blend, nullptr, 0xffffffff); g_ctx->OMSetDepthStencilState(g_ds, 0);
    g_ctx->IASetInputLayout(g_il); g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = sizeof(Vtx), off = 0; g_ctx->IASetVertexBuffers(0, 1, &g_vb, &stride, &off);
    g_ctx->VSSetShader(g_vs, nullptr, 0); g_ctx->PSSetShader(g_ps, nullptr, 0); g_ctx->VSSetConstantBuffers(0, 1, &g_cb);
    g_ctx->PSSetConstantBuffers(0, 1, &g_cb);
    ID3D11ShaderResourceView* scenes[2]={msaa ? nullptr : scene, msaa ? scene : nullptr};
    g_ctx->PSSetShaderResources(1, 2, scenes);
    g_ctx->PSSetShaderResources(0, 1, &g_srv); g_ctx->PSSetSamplers(0, 1, &g_smp);
    g_ctx->GSSetShader(nullptr, nullptr, 0); g_ctx->HSSetShader(nullptr, nullptr, 0); g_ctx->DSSetShader(nullptr, nullptr, 0);
    for (size_t i = 0; i < g_seg.size(); i++) {
        size_t a = g_seg[i].first, b = i + 1 < g_seg.size() ? g_seg[i + 1].first : g_v.size(); if (b <= a) continue;
        D3D11_RECT r = g_seg[i].clip ? g_seg[i].r : D3D11_RECT{0, 0, (LONG)g_w, (LONG)g_h}; g_ctx->RSSetScissorRects(1, &r);
        if (g_seg[i].img) { g_ctx->PSSetShader(g_psi, nullptr, 0); g_ctx->PSSetShaderResources(0, 1, &g_seg[i].img); } else { g_ctx->PSSetShader(g_ps, nullptr, 0); g_ctx->PSSetShaderResources(0, 1, &g_srv); }
        memcpy(cb+4,g_seg[i].anchor,4*sizeof(float)); g_ctx->UpdateSubresource(g_cb,0,nullptr,cb,0,0);
        ID3D11Query* query=nullptr;
        if(g_seg[i].queryId && g_pending.size()<128) {
            if(!g_freeQueries.empty()) { query=g_freeQueries.back(); g_freeQueries.pop_back(); }
            else { D3D11_QUERY_DESC qd={D3D11_QUERY_OCCLUSION,0}; g_dev->CreateQuery(&qd,&query); }
        }
        if(query) g_ctx->Begin(query);
        g_ctx->Draw((UINT)(b - a), (UINT)a);
        if(query) { g_ctx->End(query); g_pending.push_back({query,g_seg[i].queryId}); }
    }
    ID3D11ShaderResourceView* clear[2]={nullptr,nullptr}; g_ctx->PSSetShaderResources(1,2,clear);
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

void UiLogStats() {
    char msg[160]; snprintf(msg, sizeof msg, "uistat: size %.0f %.0f texts %u overflow %u minfont %.2f smalltargets %u clipped %u", g_w, g_h, g_textCalls, g_textOverflow, g_smallestText, g_smallTargets, g_clippedActions); Log(msg);
}

void UiCheckReachable(float x, float y, float w, float h) {      // a button or an information line that sticks out of its clip rectangle or the screen cannot be fully used or read
    const Seg& s = g_seg.back(); float l = s.clip ? (float)s.r.left : 0, t = s.clip ? (float)s.r.top : 0, r = s.clip ? (float)s.r.right : g_w, b = s.clip ? (float)s.r.bottom : g_h;
    if (x < l - .5f || y < t - .5f || x + w > r + .5f || y + h > b + .5f) {
        static float lastX = -1, lastY = -1; ++g_clippedActions;
        if (x != lastX || y != lastY) { lastX = x; lastY = y; char m[200]; snprintf(m, sizeof m, "uistat: clipped item at %.0f %.0f size %.0f %.0f inside %.0f %.0f %.0f %.0f", x, y, w, h, l, t, r, b); Log(m); }
    }
}

void UiCheckTarget(float width, float height, float scale) {
    if (width + .1f < 24 * scale || height + .1f < 24 * scale) ++g_smallTargets;
}
