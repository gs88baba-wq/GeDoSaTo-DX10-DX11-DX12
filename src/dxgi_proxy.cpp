#include <windows.h>
#include <dxgi1_6.h>
#include <d3d11.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

#define GEDOSATO_VERSION "0.22.2320-DX10-11-12"

struct ResMode { int width; int height; int hz; };
static std::vector<ResMode> g_Resolutions;
static int g_PresentWidth = 1920;
static int g_PresentHeight = 1080;
static int g_PresentHz = 165;
static std::string g_ScalingType = "bicubic";
static float g_FPSLimit = 0.0f;
static LARGE_INTEGER g_PerfFreq;
static LARGE_INTEGER g_LastFrameTime;

static HMODULE g_RealDXGI = nullptr;
typedef HRESULT(WINAPI* PFN_CreateDXGIFactory)(REFIID, void**);
typedef HRESULT(WINAPI* PFN_CreateDXGIFactory1)(REFIID, void**);
typedef HRESULT(WINAPI* PFN_CreateDXGIFactory2)(UINT, REFIID, void**);
static PFN_CreateDXGIFactory o_CreateDXGIFactory = nullptr;
static PFN_CreateDXGIFactory1 o_CreateDXGIFactory1 = nullptr;
static PFN_CreateDXGIFactory2 o_CreateDXGIFactory2 = nullptr;

void LoadOriginalGeDoSaToIni() {
    g_Resolutions.clear();
    char modulePath[MAX_PATH];
    GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
    std::string dir(modulePath);
    size_t pos = dir.find_last_of("\\/");
    if (pos != std::string::npos) dir = dir.substr(0, pos + 1);
    std::string iniPath = dir + "GeDoSaTo.ini";

    if (GetFileAttributesA(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::ofstream f(iniPath.c_str());
        if (f.is_open()) {
            f << "# GeDoSaTo " << GEDOSATO_VERSION << "\n";
            f << "renderResolution 3200x1800@165\n";
            f << "renderResolution 3840x2160@165\n";
            f << "renderResolution 4480x2520@165\n";
            f << "renderResolution 5120x2880@165\n";
            f << "renderResolution 5760x3240@165\n";
            f << "renderResolution 7680x4320@165\n";
            f << "presentWidth 1920\npresentHeight 1080\npresentHz 165\n";
            f << "scalingType bicubic\nfpsLimit 0\n";
            f << "enablePostprocessing false\nenableTextureDumping false\nenableTextureOverride false\n";
            f.close();
        }
    }

    std::ifstream file(iniPath.c_str());
    std::string line;
    while (std::getline(file, line)) {
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line.at(first) == '#') continue;
        std::istringstream iss(line.substr(first));
        std::string key, val;
        if (!(iss >> key >> val)) continue;
        if (key == "renderResolution") {
            int w = 0, h = 0, hz = 165;
            if (sscanf_s(val.c_str(), "%dx%d@%d", &w, &h, &hz) >= 2 && w > 0 && h > 0)
                g_Resolutions.push_back({ w, h, hz });
        } else if (key == "presentWidth") g_PresentWidth = atoi(val.c_str());
        else if (key == "presentHeight") g_PresentHeight = atoi(val.c_str());
        else if (key == "presentHz") g_PresentHz = atoi(val.c_str());
        else if (key == "scalingType") g_ScalingType = val;
        else if (key == "fpsLimit") g_FPSLimit = (float)atof(val.c_str());
    }
    if (g_Resolutions.empty()) g_Resolutions.push_back({ 3840, 2160, 165 });
}

bool LoadRealDXGI() {
    if (g_RealDXGI) return true;
    char sysDir[MAX_PATH];
    GetSystemDirectoryA(sysDir, MAX_PATH);
    std::string dllPath = std::string(sysDir) + "\\dxgi.dll";
    g_RealDXGI = LoadLibraryA(dllPath.c_str());
    if (!g_RealDXGI) return false;
    o_CreateDXGIFactory = (PFN_CreateDXGIFactory)GetProcAddress(g_RealDXGI, "CreateDXGIFactory");
    o_CreateDXGIFactory1 = (PFN_CreateDXGIFactory1)GetProcAddress(g_RealDXGI, "CreateDXGIFactory1");
    o_CreateDXGIFactory2 = (PFN_CreateDXGIFactory2)GetProcAddress(g_RealDXGI, "CreateDXGIFactory2");
    return true;
}

typedef HRESULT(STDMETHODCALLTYPE* PFN_GetDisplayModeList)(IDXGIOutput*, DXGI_FORMAT, UINT, UINT*, DXGI_MODE_DESC*);
static PFN_GetDisplayModeList o_GetDisplayModeList = nullptr;

HRESULT STDMETHODCALLTYPE Hook_GetDisplayModeList(IDXGIOutput* This, DXGI_FORMAT EnumFormat, UINT Flags, UINT* pNumModes, DXGI_MODE_DESC* pDesc) {
    HRESULT hr = o_GetDisplayModeList(This, EnumFormat, Flags, pNumModes, pDesc);
    if (SUCCEEDED(hr) && pNumModes && !g_Resolutions.empty()) {
        UINT extraCount = (UINT)g_Resolutions.size();
        if (pDesc == nullptr) {
            *pNumModes += extraCount;
        } else if (*pNumModes > extraCount) {
            UINT baseCount = *pNumModes - extraCount;
            for (UINT i = 0; i < extraCount; ++i) {
                UINT idx = baseCount + i;
                *(pDesc + idx) = *pDesc;
                (pDesc + idx)->Width = g_Resolutions.at(i).width;
                (pDesc + idx)->Height = g_Resolutions.at(i).height;
                (pDesc + idx)->RefreshRate.Numerator = g_Resolutions.at(i).hz;
                (pDesc + idx)->RefreshRate.Denominator = 1;
            }
        }
    }
    return hr;
}

void PatchOutputSafe(void* ppFactory) {
    if (!ppFactory) return;
    IDXGIFactory* factory = nullptr;
    if (SUCCEEDED(((IUnknown*)ppFactory)->QueryInterface(__uuidof(IDXGIFactory), (void**)&factory))) {
        IDXGIAdapter* adapter = nullptr;
        if (SUCCEEDED(factory->EnumAdapters(0, &adapter))) {
            IDXGIOutput* output = nullptr;
            if (SUCCEEDED(adapter->EnumOutputs(0, &output))) {
                void** vtable = *(void***)output;
                if (vtable && *(vtable + 7) != (void*)Hook_GetDisplayModeList) {
                    DWORD oldProtect;
                    if (VirtualProtect(vtable + 7, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
                        if (!o_GetDisplayModeList) o_GetDisplayModeList = (PFN_GetDisplayModeList)(*(vtable + 7));
                        *(vtable + 7) = (void*)Hook_GetDisplayModeList;
                        VirtualProtect(vtable + 7, sizeof(void*), oldProtect, &oldProtect);
                    }
                }
                output->Release();
            }
            adapter->Release();
        }
        factory->Release();
    }
}

extern "C" HRESULT WINAPI CreateDXGIFactory(REFIID riid, void** ppFactory) {
    if (!LoadRealDXGI() || !o_CreateDXGIFactory) return E_FAIL;
    HRESULT hr = o_CreateDXGIFactory(riid, ppFactory);
    if (SUCCEEDED(hr) && ppFactory) PatchOutputSafe(*ppFactory);
    return hr;
}

extern "C" HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void** ppFactory) {
    if (!LoadRealDXGI() || !o_CreateDXGIFactory1) return E_FAIL;
    HRESULT hr = o_CreateDXGIFactory1(riid, ppFactory);
    if (SUCCEEDED(hr) && ppFactory) PatchOutputSafe(*ppFactory);
    return hr;
}

extern "C" HRESULT WINAPI CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory) {
    if (!LoadRealDXGI() || !o_CreateDXGIFactory2) return E_FAIL;
    HRESULT hr = o_CreateDXGIFactory2(Flags, riid, ppFactory);
    if (SUCCEEDED(hr) && ppFactory) PatchOutputSafe(*ppFactory);
    return hr;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        QueryPerformanceFrequency(&g_PerfFreq);
        QueryPerformanceCounter(&g_LastFrameTime);
        LoadOriginalGeDoSaToIni();
        LoadRealDXGI();
    }
    return TRUE;
}
