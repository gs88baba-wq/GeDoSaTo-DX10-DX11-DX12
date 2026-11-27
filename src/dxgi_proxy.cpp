#include <windows.h>
#include <dxgi1_6.h>
#include <d3d11.h>
#include <d3d12.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

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
static char g_ScalingType[32] = "lanczos";
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
    char iniPath[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, iniPath);
    strcat_s(iniPath, "\\GeDoSaTo.ini");

    if (GetFileAttributesA(iniPath) == INVALID_FILE_ATTRIBUTES) {
        WritePrivateProfileStringA("General", "Version", GEDOSATO_VERSION, iniPath);
        WritePrivateProfileStringA("Resolution", "EnableDownsampling", "1", iniPath);
        WritePrivateProfileStringA("Resolution", "RenderWidth", "3840", iniPath);
        WritePrivateProfileStringA("Resolution", "RenderHeight", "2160", iniPath);
        WritePrivateProfileStringA("Resolution", "PresentWidth", "1920", iniPath);
        WritePrivateProfileStringA("Resolution", "PresentHeight", "1080", iniPath);
        WritePrivateProfileStringA("Resolution", "PresentHz", "60", iniPath);
        WritePrivateProfileStringA("Scaling", "ScalingType", "lanczos", iniPath);
        WritePrivateProfileStringA("PostProcessing", "EnablePostprocessing", "1", iniPath);
        WritePrivateProfileStringA("PostProcessing", "SharpenStrength", "0.65", iniPath);
        WritePrivateProfileStringA("Framerate", "FPSLimit", "0", iniPath);
        WritePrivateProfileStringA("Textures", "EnableTextureOverride", "1", iniPath);
        WritePrivateProfileStringA("Textures", "DumpTextures", "0", iniPath);
    }

    g_EnableDownsampling = GetPrivateProfileIntA("Resolution", "EnableDownsampling", 1, iniPath) != 0;
    g_RenderWidth = GetPrivateProfileIntA("Resolution", "RenderWidth", 3840, iniPath);
    g_RenderHeight = GetPrivateProfileIntA("Resolution", "RenderHeight", 2160, iniPath);
    g_PresentWidth = GetPrivateProfileIntA("Resolution", "PresentWidth", 1920, iniPath);
    g_PresentHeight = GetPrivateProfileIntA("Resolution", "PresentHeight", 1080, iniPath);
    g_PresentHz = GetPrivateProfileIntA("Resolution", "PresentHz", 60, iniPath);
    GetPrivateProfileStringA("Scaling", "ScalingType", "lanczos", g_ScalingType, sizeof(g_ScalingType), iniPath);
    g_EnablePostprocessing = GetPrivateProfileIntA("PostProcessing", "EnablePostprocessing", 1, iniPath) != 0;
    char sharpBuf[32];
    GetPrivateProfileStringA("PostProcessing", "SharpenStrength", "0.65", sharpBuf, sizeof(sharpBuf), iniPath);
    g_SharpenStrength = (float)atof(sharpBuf);
    g_FPSLimit = GetPrivateProfileIntA("Framerate", "FPSLimit", 0, iniPath);
    g_EnableTextureOverride = GetPrivateProfileIntA("Textures", "EnableTextureOverride", 1, iniPath) != 0;
    g_DumpTextures = GetPrivateProfileIntA("Textures", "DumpTextures", 0, iniPath) != 0;

    CreateDirectoryA("gedosato_textures", nullptr);
    CreateDirectoryA("gedosato_textures\\override", nullptr);
    CreateDirectoryA("gedosato_textures\\dump", nullptr);
}

bool LoadRealDXGI() {
    if (g_RealDXGI) return true;
    char sysDir[MAX_PATH];
    GetSystemDirectoryA(sysDir, MAX_PATH);
    strcat_s(sysDir, "\\dxgi.dll");
    g_RealDXGI = LoadLibraryA(sysDir);
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
    if (vtable[index] == hookFunc) return;
    DWORD oldProtect;
    VirtualProtect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
    if (!*origFunc) *origFunc = vtable[index];
    vtable[index] = hookFunc;
    VirtualProtect(&vtable[index], sizeof(void*), oldProtect, &oldProtect);
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
            pDesc[idx] = pDesc[0];
            pDesc[idx].Width = g_RenderWidth;
            pDesc[idx].Height = g_RenderHeight;
            pDesc[idx].RefreshRate.Numerator = g_PresentHz;
            pDesc[idx].RefreshRate.Denominator = 1;
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
        modifiedDesc.Width = g_RenderWidth;
        modifiedDesc.Height = g_RenderHeight;
        pDesc = &modifiedDesc;
    }
    HRESULT hr = o_CreateSwapChainForHwnd(This, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain) HookSwapChain(*ppSwapChain);
    return hr;
}

void InstallDXGIHooks(void* ppFactory) {
    if (!ppFactory) return;
    IDXGIFactory2* factory2 = nullptr;
