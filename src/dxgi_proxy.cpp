#include <windows.h>
#include <dxgi1_6.h>
#include <d3d11.h>
#include <d3d12.h>
#include <cstdio>
#include <string>

#define GEDOSATO_VERSION "0.22.2320-DX10-11-12"

typedef HRESULT(WINAPI* PFN_CreateDXGIFactory)(REFIID, void**);
typedef HRESULT(WINAPI* PFN_CreateDXGIFactory1)(REFIID, void**);
typedef HRESULT(WINAPI* PFN_CreateDXGIFactory2)(UINT, REFIID, void**);

static HMODULE g_RealDXGI = nullptr;
static PFN_CreateDXGIFactory o_CreateDXGIFactory = nullptr;
static PFN_CreateDXGIFactory1 o_CreateDXGIFactory1 = nullptr;
static PFN_CreateDXGIFactory2 o_CreateDXGIFactory2 = nullptr;

static int g_RenderWidth = 3840;
static int g_RenderHeight = 2160;
static int g_PresentHz = 165;
static bool g_EnableDownsampling = true;

void LoadConfig() {
    char modulePath[MAX_PATH];
    GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
    std::string dir(modulePath);
    size_t pos = dir.find_last_of("\\/");
    if (pos != std::string::npos) dir = dir.substr(0, pos + 1);
    std::string iniPath = dir + "GeDoSaTo.ini";

    if (GetFileAttributesA(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        WritePrivateProfileStringA("General", "Version", GEDOSATO_VERSION, iniPath.c_str());
        WritePrivateProfileStringA("Resolution", "EnableDownsampling", "1", iniPath.c_str());
        WritePrivateProfileStringA("Resolution", "RenderWidth", "3840", iniPath.c_str());
        WritePrivateProfileStringA("Resolution", "RenderHeight", "2160", iniPath.c_str());
        WritePrivateProfileStringA("Resolution", "PresentHz", "165", iniPath.c_str());
        WritePrivateProfileStringA("Scaling", "ScalingType", "bicubic", iniPath.c_str());
        WritePrivateProfileStringA("PostProcessing", "EnablePostprocessing", "0", iniPath.c_str());
        WritePrivateProfileStringA("PostProcessing", "SharpenStrength", "0.0", iniPath.c_str());
        WritePrivateProfileStringA("Framerate", "FPSLimit", "0", iniPath.c_str());
        WritePrivateProfileStringA("Textures", "EnableTextureOverride", "0", iniPath.c_str());
        WritePrivateProfileStringA("Textures", "DumpTextures", "0", iniPath.c_str());
    }

    g_EnableDownsampling = GetPrivateProfileIntA("Resolution", "EnableDownsampling", 1, iniPath.c_str()) != 0;
    g_RenderWidth = GetPrivateProfileIntA("Resolution", "RenderWidth", 3840, iniPath.c_str());
    g_RenderHeight = GetPrivateProfileIntA("Resolution", "RenderHeight", 2160, iniPath.c_str());
    g_PresentHz = GetPrivateProfileIntA("Resolution", "PresentHz", 165, iniPath.c_str());
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
    if (SUCCEEDED(hr) && g_EnableDownsampling && pNumModes) {
        if (pDesc == nullptr) {
            *pNumModes += 1;
        } else if (*pNumModes > 0) {
            UINT idx = *pNumModes - 1;
            *(pDesc + idx) = *pDesc;
            (pDesc + idx)->Width = g_RenderWidth;
            (pDesc + idx)->Height = g_RenderHeight;
            (pDesc + idx)->RefreshRate.Numerator = g_PresentHz;
            (pDesc + idx)->RefreshRate.Denominator = 1;
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
        LoadConfig();
        LoadRealDXGI();
    }
    return TRUE;
}
