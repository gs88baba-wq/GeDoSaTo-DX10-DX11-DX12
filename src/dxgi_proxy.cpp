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
typedef HRESULT(STDMETHODCALLTYPE* PFN_Present)(IDXGISwapChain*, UINT, UINT);
static PFN_GetDisplayModeList o_GetDisplayModeList = nullptr;
static PFN_Present o_Present = nullptr;

void EnforceFPSLimit() {
    if (g_FPSLimit <= 0.0f) return;
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

HRESULT STDMETHODCALLTYPE Hook_Present(IDXGISwapChain* This, UINT SyncInterval, UINT Flags) {
    if (GetAsyncKeyState(VK_NUMPAD9) & 1) LoadOriginalGeDoSaToIni();
    EnforceFPSLimit();
    return o_Present(This, SyncInterval, Flags);
}

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
