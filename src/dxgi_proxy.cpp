#include <windows.h>
#include <dxgi1_6.h>
#include <d3d11.h>
#include <d3d12.h>
#include <cstdio>

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
static bool g_EnableDownsampling = true;

void LoadConfig() {
    char iniPath[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, iniPath);
    strcat_s(iniPath, "\\GeDoSaTo.ini");

    g_RenderWidth = GetPrivateProfileIntA("Resolution", "RenderWidth", 3840, iniPath);
    g_RenderHeight = GetPrivateProfileIntA("Resolution", "RenderHeight", 2160, iniPath);
    g_EnableDownsampling = GetPrivateProfileIntA("Settings", "EnableDownsampling", 1, iniPath) != 0;

    if (GetFileAttributesA(iniPath) == INVALID_FILE_ATTRIBUTES) {
        WritePrivateProfileStringA("Resolution", "RenderWidth", "3840", iniPath);
        WritePrivateProfileStringA("Resolution", "RenderHeight", "2160", iniPath);
        WritePrivateProfileStringA("Settings", "EnableDownsampling", "1", iniPath);
    }
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

static PFN_GetDisplayModeList o_GetDisplayModeList = nullptr;
static PFN_CreateSwapChain o_CreateSwapChain = nullptr;
static PFN_CreateSwapChainForHwnd o_CreateSwapChainForHwnd = nullptr;

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
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE Hook_CreateSwapChain(IDXGIFactory* This, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain) {
    if (g_EnableDownsampling && pDesc) {
        pDesc->BufferDesc.Width = g_RenderWidth;
        pDesc->BufferDesc.Height = g_RenderHeight;
    }
    return o_CreateSwapChain(This, pDevice, pDesc, ppSwapChain);
}

HRESULT STDMETHODCALLTYPE Hook_CreateSwapChainForHwnd(IDXGIFactory2* This, IUnknown* pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain) {
    DXGI_SWAP_CHAIN_DESC1 modifiedDesc;
    if (g_EnableDownsampling && pDesc) {
        modifiedDesc = *pDesc;
        modifiedDesc.Width = g_RenderWidth;
        modifiedDesc.Height = g_RenderHeight;
        pDesc = &modifiedDesc;
    }
    return o_CreateSwapChainForHwnd(This, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
}

void HookVTable(void* pInterface, int index, void* hookFunc, void** origFunc) {
    void** vtable = *(void***)pInterface;
    if (vtable[index] == hookFunc) return;
    DWORD oldProtect;
    VirtualProtect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
    *origFunc = vtable[index];
    vtable[index] = hookFunc;
    VirtualProtect(&vtable[index], sizeof(void*), oldProtect, &oldProtect);
}

void InstallDXGIHooks(void* ppFactory) {
    if (!ppFactory) return;
    IDXGIFactory2* factory2 = nullptr;
    if (SUCCEEDED(((IUnknown*)ppFactory)->QueryInterface(__uuidof(IDXGIFactory2), (void**)&factory2))) {
        HookVTable(factory2, 10, (void*)Hook_CreateSwapChain, (void**)&o_CreateSwapChain);
        HookVTable(factory2, 15, (void*)Hook_CreateSwapChainForHwnd, (void**)&o_CreateSwapChainForHwnd);
        IDXGIAdapter* adapter = nullptr;
        if (SUCCEEDED(factory2->EnumAdapters(0, &adapter))) {
            IDXGIOutput* output = nullptr;
            if (SUCCEEDED(adapter->EnumOutputs(0, &output))) {
                HookVTable(output, 7, (void*)Hook_GetDisplayModeList, (void**)&o_GetDisplayModeList);
                output->Release();
            }
            adapter->Release();
        }
        factory2->Release();
    }
}

extern "C" HRESULT WINAPI CreateDXGIFactory(REFIID riid, void** ppFactory) {
    if (!LoadRealDXGI() || !o_CreateDXGIFactory) return E_FAIL;
    HRESULT hr = o_CreateDXGIFactory(riid, ppFactory);
    if (SUCCEEDED(hr) && ppFactory) InstallDXGIHooks(*ppFactory);
    return hr;
}

extern "C" HRESULT WINAPI CreateDXGIFactory1(REFIID riid, void** ppFactory) {
    if (!LoadRealDXGI() || !o_CreateDXGIFactory1) return E_FAIL;
    HRESULT hr = o_CreateDXGIFactory1(riid, ppFactory);
    if (SUCCEEDED(hr) && ppFactory) InstallDXGIHooks(*ppFactory);
    return hr;
}

extern "C" HRESULT WINAPI CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory) {
    if (!LoadRealDXGI() || !o_CreateDXGIFactory2) return E_FAIL;
    HRESULT hr = o_CreateDXGIFactory2(Flags, riid, ppFactory);
    if (SUCCEEDED(hr) && ppFactory) InstallDXGIHooks(*ppFactory);
    return hr;
}

extern "C" HRESULT WINAPI DXGIDeclareAdapterRemovalSupport() {
    if (!LoadRealDXGI() || !o_DXGIDeclareAdapterRemovalSupport) return E_FAIL;
    return o_DXGIDeclareAdapterRemovalSupport();
}

extern "C" HRESULT WINAPI DXGIGetDebugInterface1(UINT Flags, REFIID riid, void** pDebug) {
    if (!LoadRealDXGI() || !o_DXGIGetDebugInterface1) return E_FAIL;
    return o_DXGIGetDebugInterface1(Flags, riid, pDebug);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        LoadConfig();
        LoadRealDXGI();
        char logMsg[256];
        sprintf_s(logMsg, "[GeDoSaTo-DX12] Aktif! Hedef Cozunurluk: %dx%d\n", g_RenderWidth, g_RenderHeight);
        OutputDebugStringA(logMsg);
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_RealDXGI) FreeLibrary(g_RealDXGI);
    }
    return TRUE;
}
