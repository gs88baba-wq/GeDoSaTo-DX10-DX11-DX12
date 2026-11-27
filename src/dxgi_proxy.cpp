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

// Bellege dokunmadan IDXGIOutput sarmalayici (Safe COM Proxy)
class WrappedOutput : public IDXGIOutput1 {
    IDXGIOutput* m_orig;
    ULONG m_ref;
public:
    WrappedOutput(IDXGIOutput* orig) : m_orig(orig), m_ref(1) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIOutput) || riid == __uuidof(IDXGIOutput1)) {
            AddRef(); *ppv = this; return S_OK;
        }
        return m_orig->QueryInterface(riid, ppv);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = InterlockedDecrement(&m_ref);
        if (r == 0) { m_orig->Release(); delete this; }
        return r;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID g, UINT s, const void* d) override { return m_orig->SetPrivateData(g, s, d); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID g, const IUnknown* u) override { return m_orig->SetPrivateDataInterface(g, u); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID g, UINT* s, void* d) override { return m_orig->GetPrivateData(g, s, d); }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID r, void** p) override { return m_orig->GetParent(r, p); }
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_OUTPUT_DESC* d) override { return m_orig->GetDesc(d); }
    HRESULT STDMETHODCALLTYPE GetDisplayModeList(DXGI_FORMAT fmt, UINT fl, UINT* n, DXGI_MODE_DESC* d) override {
        HRESULT hr = m_orig->GetDisplayModeList(fmt, fl, n, d);
        if (SUCCEEDED(hr) && n) {
            UINT ex = (UINT)g_Res.size();
            if (!d) *n += ex;
            else if (*n > ex) {
                UINT b = *n - ex;
                for (UINT i = 0; i < ex; ++i) {
                    *(d + b + i) = *d;
                    (d + b + i)->Width = g_Res.at(i).w; (d + b + i)->Height = g_Res.at(i).h;
                    (d + b + i)->RefreshRate.Numerator = g_Res.at(i).hz; (d + b + i)->RefreshRate.Denominator = 1;
                }
            }
        }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE FindClosestMatchingMode(const DXGI_MODE_DESC* m, DXGI_MODE_DESC* c, IUnknown* dev) override { return m_orig->FindClosestMatchingMode(m, c, dev); }
    HRESULT STDMETHODCALLTYPE WaitForVBlank() override { return m_orig->WaitForVBlank(); }
    HRESULT STDMETHODCALLTYPE TakeOwnership(IUnknown* d, BOOL e) override { return m_orig->TakeOwnership(d, e); }
    void STDMETHODCALLTYPE ReleaseOwnership() override { m_orig->ReleaseOwnership(); }
    HRESULT STDMETHODCALLTYPE GetGammaControlCapabilities(DXGI_GAMMA_CONTROL_CAPABILITIES* g) override { return m_orig->GetGammaControlCapabilities(g); }
    HRESULT STDMETHODCALLTYPE SetGammaControl(const DXGI_GAMMA_CONTROL* g) override { return m_orig->SetGammaControl(g); }
    HRESULT STDMETHODCALLTYPE GetGammaControl(DXGI_GAMMA_CONTROL* g) override { return m_orig->GetGammaControl(g); }
    HRESULT STDMETHODCALLTYPE SetDisplaySurface(IDXGISurface* s) override { return m_orig->SetDisplaySurface(s); }
    HRESULT STDMETHODCALLTYPE GetDisplaySurfaceData(IDXGISurface* s) override { return m_orig->GetDisplaySurfaceData(s); }
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* s) override { return m_orig->GetFrameStatistics(s); }
    HRESULT STDMETHODCALLTYPE GetDisplayModeList1(DXGI_FORMAT fmt, UINT fl, UINT* n, DXGI_MODE_DESC1* d) override {
        IDXGIOutput1* o1 = nullptr;
        if (FAILED(m_orig->QueryInterface(__uuidof(IDXGIOutput1), (void**)&o1))) return E_NOINTERFACE;
        HRESULT hr = o1->GetDisplayModeList1(fmt, fl, n, d);
        o1->Release();
        if (SUCCEEDED(hr) && n) {
            UINT ex = (UINT)g_Res.size();
            if (!d) *n += ex;
            else if (*n > ex) {
                UINT b = *n - ex;
                for (UINT i = 0; i < ex; ++i) {
                    *(d + b + i) = *d;
                    (d + b + i)->Width = g_Res.at(i).w; (d + b + i)->Height = g_Res.at(i).h;
                    (d + b + i)->RefreshRate.Numerator = g_Res.at(i).hz; (d + b + i)->RefreshRate.Denominator = 1;
                }
            }
        }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE FindClosestMatchingMode1(const DXGI_MODE_DESC1* m, DXGI_MODE_DESC1* c, IUnknown* dev) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetDisplaySurfaceData1(IDXGIResource* r) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DuplicateOutput(IUnknown* d, IDXGIOutputDuplication** dup) override { return E_NOTIMPL; }
};

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
