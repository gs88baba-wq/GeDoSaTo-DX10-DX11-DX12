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

typedef BOOL(WINAPI* PFN_EDW)(LPCWSTR, DWORD, DEVMODEW*);
typedef BOOL(WINAPI* PFN_EDA)(LPCSTR, DWORD, DEVMODEA*);
static PFN_EDW o_EDW = nullptr;
static PFN_EDA o_EDA = nullptr;
static DWORD g_BaseW = 0, g_BaseA = 0;

void LoadIni() {
    g_Res.clear();
    char p[MAX_PATH];
    GetModuleFileNameA(nullptr, p, MAX_PATH);
    std::string dir(p);
    size_t pos = dir.find_last_of("\\/");
    if (pos != std::string::npos) dir = dir.substr(0, pos + 1);
    std::string ini = dir + "GeDoSaTo.ini";

    if (GetFileAttributesA(ini.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::ofstream f(ini.c_str());
        if (f.is_open()) {
            f << "# GeDoSaTo 0.22.2320-DX10-11-12\n";
            f << "renderResolution 3200x1800@165\n";
            f << "renderResolution 3840x2160@165\n";
            f << "renderResolution 4480x2520@165\n";
            f << "renderResolution 5120x2880@165\n";
            f << "renderResolution 5760x3240@165\n";
            f << "renderResolution 7680x4320@165\n";
            f << "presentWidth 1920\npresentHeight 1080\npresentHz 165\n";
            f << "scalingType bicubic\nfpsLimit 0\n";
            f.close();
        }
    }
    std::ifstream file(ini.c_str());
    std::string line;
    while (std::getline(file, line)) {
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line.at(first) == '#') continue;
        std::istringstream iss(line.substr(first));
        std::string k, v;
        if (iss >> k >> v && k == "renderResolution") {
            int w = 0, h = 0, hz = 165;
            if (sscanf_s(v.c_str(), "%dx%d@%d", &w, &h, &hz) >= 2 && w > 0 && h > 0)
                g_Res.push_back({ w, h, hz });
        }
    }
    if (g_Res.empty()) g_Res.push_back({ 3840, 2160, 165 });
}

BOOL WINAPI H_EDW(LPCWSTR d, DWORD m, DEVMODEW* dm) {
    if (m == ENUM_CURRENT_SETTINGS || m == ENUM_REGISTRY_SETTINGS) return o_EDW(d, m, dm);
    if (o_EDW(d, m, dm)) { if (m >= g_BaseW) g_BaseW = m + 1; return TRUE; }
    if (m >= g_BaseW && (m - g_BaseW) < g_Res.size() && dm) {
        o_EDW(d, ENUM_CURRENT_SETTINGS, dm);
        size_t i = m - g_BaseW;
        dm->dmPelsWidth = g_Res.at(i).w; dm->dmPelsHeight = g_Res.at(i).h; dm->dmDisplayFrequency = g_Res.at(i).hz;
        dm->dmFields |= DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
        return TRUE;
    }
    return FALSE;
}

BOOL WINAPI H_EDA(LPCSTR d, DWORD m, DEVMODEA* dm) {
    if (m == ENUM_CURRENT_SETTINGS || m == ENUM_REGISTRY_SETTINGS) return o_EDA(d, m, dm);
    if (o_EDA(d, m, dm)) { if (m >= g_BaseA) g_BaseA = m + 1; return TRUE; }
    if (m >= g_BaseA && (m - g_BaseA) < g_Res.size() && dm) {
        o_EDA(d, ENUM_CURRENT_SETTINGS, dm);
        size_t i = m - g_BaseA;
        dm->dmPelsWidth = g_Res.at(i).w; dm->dmPelsHeight = g_Res.at(i).h; dm->dmDisplayFrequency = g_Res.at(i).hz;
        dm->dmFields |= DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
        return TRUE;
    }
    return FALSE;
}

void PatchIAT() {
    HMODULE h = GetModuleHandleA(nullptr);
    if (!h) return;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((BYTE*)h + ((PIMAGE_DOS_HEADER)h)->e_lfanew);
    DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva) return;
    for (PIMAGE_IMPORT_DESCRIPTOR imp = (PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)h + rva); imp->Name; imp++) {
        if (_stricmp((const char*)((BYTE*)h + imp->Name), "user32.dll") == 0) {
            for (PIMAGE_THUNK_DATA t = (PIMAGE_THUNK_DATA)((BYTE*)h + imp->FirstThunk); t->u1.Function; t++) {
                void** fp = (void**)&t->u1.Function;
                DWORD old;
                if (*fp == (void*)o_EDW || *fp == (void*)o_EDA) {
                    VirtualProtect(fp, sizeof(void*), PAGE_READWRITE, &old);
                    *fp = (*fp == (void*)o_EDW) ? (void*)H_EDW : (void*)H_EDA;
                    VirtualProtect(fp, sizeof(void*), old, &old);
                }
            }
        }
    }
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

typedef HRESULT(STDMETHODCALLTYPE* PFN_GDML)(IDXGIOutput*, DXGI_FORMAT, UINT, UINT*, DXGI_MODE_DESC*);
static PFN_GDML o_GDML = nullptr;

HRESULT STDMETHODCALLTYPE H_GDML(IDXGIOutput* T, DXGI_FORMAT F, UINT Fl, UINT* N, DXGI_MODE_DESC* D) {
    HRESULT hr = o_GDML(T, F, Fl, N, D);
    if (SUCCEEDED(hr) && N && !g_Res.empty()) {
        UINT ex = (UINT)g_Res.size();
        if (!D) *N += ex;
        else if (*N > ex) {
            UINT b = *N - ex;
            for (UINT i = 0; i < ex; ++i) {
                *(D + b + i) = *D;
                (D + b + i)->Width = g_Res.at(i).w;
                (D + b + i)->Height = g_Res.at(i).h;
                (D + b + i)->RefreshRate.Numerator = g_Res.at(i).hz;
                (D + b + i)->RefreshRate.Denominator = 1;
            }
        }
    }
    return hr;
}

void HookOut(void* ppF) {
    IDXGIFactory* f = nullptr;
    if (ppF && SUCCEEDED(((IUnknown*)ppF)->QueryInterface(__uuidof(IDXGIFactory), (void**)&f))) {
        IDXGIAdapter* a = nullptr;
        if (SUCCEEDED(f->EnumAdapters(0, &a))) {
            IDXGIOutput* o = nullptr;
            if (SUCCEEDED(a->EnumOutputs(0, &o))) {
                void** vt = *(void***)o;
                if (vt && *(vt + 7) != (void*)H_GDML) {
                    DWORD old;
                    if (VirtualProtect(vt + 7, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
                        if (!o_GDML) o_GDML = (PFN_GDML)(*(vt + 7));
                        *(vt + 7) = (void*)H_GDML;
                        VirtualProtect(vt + 7, sizeof(void*), old, &old);
                    }
                }
                o->Release();
            }
            a->Release();
        }
        f->Release();
    }
}

extern "C" HRESULT WINAPI CreateDXGIFactory(REFIID r, void** p) {
    if (!InitDXGI() || !o_F0) return E_FAIL;
    HRESULT hr = o_F0(r, p); if (SUCCEEDED(hr) && p) HookOut(*p); return hr;
}
extern "C" HRESULT WINAPI CreateDXGIFactory1(REFIID r, void** p) {
    if (!InitDXGI() || !o_F1) return E_FAIL;
    HRESULT hr = o_F1(r, p); if (SUCCEEDED(hr) && p) HookOut(*p); return hr;
}
extern "C" HRESULT WINAPI CreateDXGIFactory2(UINT fl, REFIID r, void** p) {
    if (!InitDXGI() || !o_F2) return E_FAIL;
    HRESULT hr = o_F2(fl, r, p); if (SUCCEEDED(hr) && p) HookOut(*p); return hr;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD r, LPVOID lp) {
    if (r == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        HMODULE u = GetModuleHandleA("user32.dll");
        if (u) { o_EDW = (PFN_EDW)GetProcAddress(u, "EnumDisplaySettingsW"); o_EDA = (PFN_EDA)GetProcAddress(u, "EnumDisplaySettingsA"); }
        LoadIni(); PatchIAT(); InitDXGI();
    }
    return TRUE;
}
