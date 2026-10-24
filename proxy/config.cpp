#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include "config.h"

extern HMODULE g_selfModule();

static std::wstring DllDir() {
    wchar_t p[MAX_PATH]; GetModuleFileNameW(g_selfModule(), p, MAX_PATH);
    wchar_t* s = wcsrchr(p, L'\\'); if (s) *(s + 1) = 0; return p;
}

const Cfg& GetCfg() {
    static Cfg c = [] {
        Cfg r; FILE* f = _wfopen((DllDir() + L"witness_coop.cfg").c_str(), L"r"); if (!f) return r;
        char line[400];
        while (fgets(line, sizeof line, f)) {
            int v; float fv; char s[300];
            if (sscanf(line, "cap_fps=%d", &v) == 1) r.capFps = v;
            else if (sscanf(line, "share_zip=%d", &v) == 1) r.shareZip = v != 0;
            else if (sscanf(line, "debug_depth=%d", &v) == 1) r.debugDepth = v != 0;
            else if (sscanf(line, "debug_sleep=%d", &v) == 1) r.debugSleep = v != 0;
            else if (sscanf(line, "debug_msgs=%d", &v) == 1) r.debugMsgs = v != 0;
            else if (sscanf(line, "force_active=%d", &v) == 1) r.forceActive = v != 0;
            else if (sscanf(line, "skip_entity_zero=%d", &v) == 1) r.skipEntityZero = v != 0;
            else if (sscanf(line, "dev_menu=%d", &v) == 1) r.devMenu = v != 0;
            else if (sscanf(line, "partner_view=%d", &v) == 1) r.partnerView = v != 0;
            else if (sscanf(line, "no_partner_video=%d", &v) == 1) r.noPartnerVideo = v != 0;
            else if (sscanf(line, "no_restore_copy=%d", &v) == 1) r.noRestoreCopy = v != 0;
            else if (sscanf(line, "no_power_link=%d", &v) == 1) r.noPowerLink = v != 0;
            else if (sscanf(line, "no_target_dispatch=%d", &v) == 1) r.noTargetDispatch = v != 0;
            else if (sscanf(line, "depth_bias=%f", &fv) == 1) r.depthBias = fv;
            else if (sscanf(line, "depth_near=%f", &fv) == 1) r.depthNear = fv;
            else if (sscanf(line, "depth_view=%d", &v) == 1) r.depthView = v != 0;
            else if (sscanf(line, "model_height=%f", &fv) == 1) r.modelHeight = fv;
            else if (sscanf(line, "model_yaw=%f", &fv) == 1) r.modelYaw = fv;
            else if (sscanf(line, "model_speed=%f", &fv) == 1) r.modelStride = fv;
            else if (sscanf(line, "no_model_depth=%d", &v) == 1) r.noModelDepth = v != 0;
            else if (sscanf(line, "no_model_select=%d", &v) == 1) r.noModelSelect = v != 0;
            else if (sscanf(line, "no_model_gait=%d", &v) == 1) r.noModelGait = v != 0;
            else if (sscanf(line, "model=%299[^\r\n]", s) == 1) {
                if (!strcmp(s, "none")) r.modelPath.clear();
                else { wchar_t w[300]; MultiByteToWideChar(CP_UTF8, 0, s, -1, w, 300); r.modelPath = w; }
            }
            else if (sscanf(line, "save_dir=%299[^\r\n]", s) == 1) {
                if (!strcmp(s, "instance")) r.saveDir = DllDir() + L"appdata";
                else { wchar_t w[300]; MultiByteToWideChar(CP_UTF8, 0, s, -1, w, 300); r.saveDir = w; }
            }
        }
        fclose(f); return r;
    }();
    return c;
}
