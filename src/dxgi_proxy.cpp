#include <windows.h>
#include <dxgi1_6.h>
#include <d3d11.h>
#include <d3d12.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

#define GEDOSATO_VERSION "0.22.2320-DX10-11-12"

typedef HRESULT(WINAPI* PFN_CreateDXGIFactory)(REFIID, void**);
typedef HRESULT(WINAPI* PFN_CreateDXGIFactory1)(REFIID, void**);
typedef HRESULT(WINAPI* PFN_CreateDXGIFactory2)(UINT, REFIID, void**);
typedef HRESULT(WINAPI* PFN_DXGIDeclareAdapterRemovalSupport)();
typedef HRESULT(WINAPI* PFN_DXGIGetDebugInterface1)(UINT, REFIID, void**);

static HMODULE g_RealDXGI = nullptr;
static PFN_CreateDXGIFactory o_CreateDXGIFactory = nullptr;
static PFN_CreateDXGIFactory1 o_CreateDXGIFactory1 = nullptr;
static PFN_CreateDXGIFactory2 o_CreateDXGIFactory2 = nullptr;
static PFN_DXGIDeclareAdapterRemovalSupport o_DXGIDeclareAdapterRemovalSupport = nullptr;
static PFN_DXGIGetDebugInterface1 o_DXGIGetDebugInterface1 = nullptr;

static int g_RenderWidth = 3840;
static int g_RenderHeight = 2160;
static int g_PresentWidth = 1920;
static int g_PresentHeight = 1080;
static int g_PresentHz = 60;
static std::string g_ScalingType = "lanczos";
static bool g_EnableDownsampling = true;
static bool g_EnablePostprocessing = true;
static float g_SharpenStrength = 0.65f;
static int g_FPSLimit = 0;
static bool g_EnableTextureOverride = true;
static bool g_DumpTextures = false;
static bool g_HideHUD = false;
static LARGE_INTEGER g_PerfFreq;
static LARGE_INTEGER g_LastFrameTime;

void LoadConfig() {
    std::string iniPath(MAX_PATH, '\0');
    DWORD len = GetCurrentDirectoryA(MAX_PATH, &iniPath.at(0));
    iniPath.resize(len);
    iniPath += "\\GeDoSaTo.ini";

    if (GetFileAttributesA(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        WritePrivateProfileStringA("General", "Version", GEDOSATO_VERSION, iniPath.c_str());
        WritePrivateProfileStringA("Resolution", "EnableDownsampling", "1", iniPath.c_str());
        WritePrivateProfileStringA("Resolution", "RenderWidth", "3840", iniPath.c_str());
        WritePrivateProfileStringA("Resolution", "RenderHeight", "2160", iniPath.c_str());
        WritePrivateProfileStringA("Resolution", "PresentWidth", "1920", iniPath.c_str());
        WritePrivateProfileStringA("Resolution", "PresentHeight", "1080", iniPath.c_str());
        WritePrivateProfileStringA("Resolution", "PresentHz", "60", iniPath.c_str());
        WritePrivateProfileStringA("Scaling", "ScalingType", "lanczos", iniPath.c_str());
        WritePrivateProfileStringA("PostProcessing", "EnablePostprocessing", "1", iniPath.c_str());
        WritePrivateProfileStringA("PostProcessing", "SharpenStrength", "0.65", iniPath.c_str());
        WritePrivateProfileStringA("Framerate", "FPSLimit", "0", iniPath.c_str());
        WritePrivateProfileStringA("Textures", "EnableTextureOverride", "1", iniPath.c_str());
        WritePrivateProfileStringA("Textures", "DumpTextures", "0", iniPath.c_str());
    }

    g_EnableDownsampling = GetPrivateProfileIntA("Resolution", "EnableDownsampling", 1, iniPath.c_str()) != 0;
    g_RenderWidth = GetPrivateProfileIntA("Resolution", "RenderWidth", 3840, iniPath.c_str());
    g_RenderHeight = GetPrivateProfileIntA("Resolution", "RenderHeight", 2160, iniPath.c_str());
    g_PresentWidth = GetPrivateProfileIntA("Resolution", "PresentWidth", 1920, iniPath.c_str());
    g_PresentHeight = GetPrivateProfileIntA("Resolution", "PresentHeight", 1080, iniPath.c_str());
    g_PresentHz = GetPrivateProfileIntA("Resolution", "PresentHz", 60, iniPath.c_str());

    std::string scaleBuf(64, '\0');
    DWORD sLen = GetPrivateProfileStringA("Scaling", "ScalingType", "lanczos", &scaleBuf.at(0), 64, iniPath.c_str());
    scaleBuf.resize(sLen);
    g_ScalingType = scaleBuf;

    g_EnablePostprocessing = GetPrivateProfileIntA("PostProcessing", "EnablePostprocessing", 1, iniPath.c_str()) != 0;
    std::string sharpBuf(32, '\0');
    GetPrivateProfileStringA("PostProcessing", "SharpenStrength", "0.65", &sharpBuf.at(0), 32, iniPath.c_str());
    g_SharpenStrength = (float)atof(sharpBuf.c_str());

    g_FPSLimit = GetPrivateProfileIntA("Framerate", "FPSLimit", 0, iniPath.c_str());
    g_EnableTextureOverride = GetPrivateProfileIntA("Textures", "EnableTextureOverride", 1, iniPath.c_str()) != 0;
    g_DumpTextures = GetPrivateProfileIntA("Textures", "DumpTextures", 0, iniPath.c_str()) != 0;

    CreateDirectoryA("gedosato_textures", nullptr);
    CreateDirectoryA("gedosato_textures\\override", nullptr);
    CreateDirectoryA("gedosato_textures\\dump", nullptr);
}

bool LoadRealDXGI() {
    if (g_RealDXGI) return true;
    std::string sysDir(MAX_PATH, '\0');
    DWORD len = GetSystemDirectoryA(&sysDir.at(0), MAX_PATH);
    sysDir.resize(len);
    sysDir += "\\dxgi.dll";
    g_RealDXGI = LoadLibraryA(sysDir.c_str());
    if (!g_RealDXGI) return false;

    o_CreateDXGIFactory = (PFN_CreateDXGIFactory)GetProcAddress(g_RealDXGI, "CreateDXGIFactory");
    o_CreateDXGIFactory1 = (PFN_CreateDXGIFactory1)GetProcAddress(g_RealDXGI, "CreateDXGIFactory1");
    o_CreateDXGIFactory2 = (PFN_CreateDXGIFactory2)GetProcAddress(g_RealDXGI, "CreateDXGIFactory2");
    o_DXGIDeclareAdapterRemovalSupport = (PFN_DXGIDeclareAdapterRemovalSupport)GetProcAddress(g_RealDXGI, "DXGIDeclareAdapterRemovalSupport");
    o_DXGIGetDebugInterface1 = (PFN_DXGIGetDebugInterface1)GetProcAddress(g_RealDXGI, "DXGIGetDebugInterface1");
    return true;
}

typedef HRESULT(STDMETHODCALLTYPE* PFN_GetDisplayModeList)(IDXGIOutput*, DXGI_FORMAT, UINT, UINT*, DXGI_MODE_DESC*);
typedef HRESULT(STDMETHODCALLTYPE* PFN_CreateSwapChain)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
typedef HRESULT(STDMETHODCALLTYPE* PFN_CreateSwapChainForHwnd)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
typedef HRESULT(STDMETHODCALLTYPE* PFN_Present)(IDXGISwapChain*, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* PFN_ResizeBuffers)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

static PFN_GetDisplayModeList o_GetDisplayModeList = nullptr;
static PFN_CreateSwapChain o_CreateSwapChain = nullptr;
static PFN_CreateSwapChainForHwnd o_CreateSwapChainForHwnd = nullptr;
static PFN_Present o_Present = nullptr;
static PFN_ResizeBuffers o_ResizeBuffers = nullptr;

void EnforceFPSLimit() {
    if (g_FPSLimit <= 0) return;
    double targetSec = 1.0 / (double)g_FPSLimit;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double elapsed = (double)(now.QuadPart - g_LastFrameTime.QuadPart) / (double)g_PerfFreq.QuadPart;
    while (elapsed < targetSec) {
        if (targetSec - elapsed > 0.002) Sleep(1);
        QueryPerformanceCounter(&now);
        elapsed = (double)(now.QuadPart - g_LastFrameTime.QuadPart) / (double)g_PerfFreq.QuadPart;
    }
    g_LastFrameTime = now;
}

void HandleHotkeys() {
    if (GetAsyncKeyState(VK_NUMPAD0) & 1) g_EnableDownsampling = !g_EnableDownsampling;
    if (GetAsyncKeyState(VK_NUMPAD1) & 1) g_EnablePostprocessing = !g_EnablePostprocessing;
    if (GetAsyncKeyState(VK_NUMPAD2) & 1) g_HideHUD = !g_HideHUD;
    if (GetAsyncKeyState(VK_NUMPAD9) & 1) LoadConfig();
}

void HookVTable(void* pInterface, int index, void* hookFunc, void** origFunc) {
    void** vtable = *(void***)pInterface;
    if (*(vtable + index) == hookFunc) return;
    DWORD oldProtect;
    VirtualProtect(vtable + index, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
    if (!*origFunc) *origFunc = *(vtable + index);
    *(vtable + index) = hookFunc;
    VirtualProtect(vtable + index, sizeof(void*), oldProtect, &oldProtect);
}

HRESULT STDMETHODCALLTYPE Hook_Present(IDXGISwapChain* This, UINT SyncInterval, UINT Flags) {
    HandleHotkeys();
    EnforceFPSLimit();
    return o_Present(This, SyncInterval, Flags);
}

HRESULT STDMETHODCALLTYPE Hook_ResizeBuffers(IDXGISwapChain* This, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    if (g_EnableDownsampling && Width > 0 && Height > 0) {
        Width = g_RenderWidth;
        Height = g_RenderHeight;
    }
    return o_ResizeBuffers(This, BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

void HookSwapChain(IDXGISwapChain* pSwapChain) {
    if (!pSwapChain) return;
    HookVTable(pSwapChain, 8, (void*)Hook_Present, (void**)&o_Present);
    HookVTable(pSwapChain, 13, (void*)Hook_ResizeBuffers, (void**)&o_ResizeBuffers);
}

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

HRESULT STDMETHODCALLTYPE Hook_CreateSwapChain(IDXGIFactory* This, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain) {
    if (g_EnableDownsampling && pDesc) {
        pDesc->BufferDesc.Width = g_RenderWidth;
        pDesc->BufferDesc.Height = g_RenderHeight;
    }
    HRESULT hr = o_CreateSwapChain(This, pDevice, pDesc, ppSwapChain);
    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain) HookSwapChain(*ppSwapChain);
    return hr;
}

HRESULT STDMETHODCALLTYPE Hook_CreateSwapChainForHwnd(IDXGIFactory2* This, IUnknown* pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain) {
    DXGI_SWAP_CHAIN_DESC1 modifiedDesc;
    if (g_EnableDownsampling && pDesc) {
        modifiedDesc = *pDesc;
