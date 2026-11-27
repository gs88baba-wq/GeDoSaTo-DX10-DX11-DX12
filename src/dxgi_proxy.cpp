#include <windows.h>
#include <dxgi1_6.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

struct ResMode { int w; int h; int hz; };
static std::vector<ResMode> g_Res;
static HMODULE g_RealDXGI = nullptr;
typedef HRESULT(WINAPI* PFN_F0)(REFIID, void**);
typedef HRESULT(WINAPI* PFN_F1)(REFIID, void**);
typedef HRESULT(WINAPI* PFN_F2)(UINT, REFIID, void**);
static PFN_F0 o_F0 = nullptr;
static PFN_F1 o_F1 = nullptr;
static PFN_F2 o_F2 = nullptr;

void WriteDefaultIni(const std::string& ini) {
    std::ofstream f(ini.c_str());
    if (!f.is_open()) return;
    f << "# GeDoSaTo 0.22.2320-DX10-11-12 Configuration\n\n";
    f << "renderResolution 3200x1800@165\n";
    f << "renderResolution 3840x2160@165\n";
    f << "renderResolution 4480x2520@165\n";
    f << "renderResolution 5120x2880@165\n";
    f << "renderResolution 5760x3240@165\n";
    f << "renderResolution 7680x4320@165\n\n";
    f << "presentWidth 1920\npresentHeight 1080\npresentHz 165\npresentInterval 0\n\n";
    f << "scalingType bicubic\nmaintainAspectRatio true\n\n";
    f << "fpsLimit 0\nforceAlwaysDownsamplingRes false\n";
    f << "overrideWidth 0\noverrideHeight 0\n";
    f << "enablePostprocessing false\nenableTextureDumping false\nenableTextureOverride false\n";
    f.close();
}

void LoadIni() {
    g_Res.clear();
    char p[MAX_PATH]; GetModuleFileNameA(nullptr, p, MAX_PATH);
    std::string dir(p); size_t pos = dir.find_last_of("\\/");
    if (pos != std::string::npos) dir = dir.substr(0, pos + 1);
    std::string ini = dir + "GeDoSaTo.ini";
    if (GetFileAttributesA(ini.c_str()) == INVALID_FILE_ATTRIBUTES) WriteDefaultIni(ini);

    std::ifstream file(ini.c_str()); std::string line;
    while (std::getline(file, line)) {
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line.at(first) == '#') continue;
        std::istringstream iss(line.substr(first)); std::string k, v;
        if (iss >> k >> v && k == "renderResolution") {
            int w = 0, h = 0, hz = 165;
            if (sscanf_s(v.c_str(), "%dx%d@%d", &w, &h, &hz) >= 2 && w > 0 && h > 0)
                g_Res.push_back({ w, h, hz });
        }
    }
    if (g_Res.empty()) g_Res.push_back({ 3840, 2160, 165 });
}

bool InitDXGI() {
    if (g_RealDXGI) return true;
    char s[MAX_PATH]; GetSystemDirectoryA(s, MAX_PATH);
    g_RealDXGI = LoadLibraryA((std::string(s) + "\\dxgi.dll").c_str());
    if (!g_RealDXGI) return false;
    o_F0 = (PFN_F0)GetProcAddress(g_RealDXGI, "CreateDXGIFactory");
    o_F1 = (PFN_F1)GetProcAddress(g_RealDXGI, "CreateDXGIFactory1");
    o_F2 = (PFN_F2)GetProcAddress(g_RealDXGI, "CreateDXGIFactory2");
    return true;
}

extern "C" HRESULT WINAPI CreateDXGIFactory(REFIID r, void** p) {
    if (!InitDXGI() || !o_F0) return E_FAIL;
    return o_F0(r, p);
}
extern "C" HRESULT WINAPI CreateDXGIFactory1(REFIID r, void** p) {
    if (!InitDXGI() || !o_F1) return E_FAIL;
    return o_F1(r, p);
}
extern "C" HRESULT WINAPI CreateDXGIFactory2(UINT fl, REFIID r, void** p) {
    if (!InitDXGI() || !o_F2) return E_FAIL;
    return o_F2(fl, r, p);
}

BOOL APIENTRY DllMain(HMODULE h, DWORD r, LPVOID lp) {
    if (r == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        LoadIni();
    }
    return TRUE;
}
