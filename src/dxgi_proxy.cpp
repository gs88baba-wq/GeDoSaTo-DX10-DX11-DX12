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
static char g_ScalingType = "lanczos";
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
    char sharpBuf;
    GetPrivateProfileStringA("PostProcessing", "SharpenStrength", "0.65", sharpBuf, sizeof(sharpBuf), iniPath);
    g_SharpenStrength = (float)atof(sharpBuf);
    g_FPSLimit = GetPrivateProfileIntA("Framerate", "FPSLimit", 0, iniPath);
    g_EnableTextureOverride = GetPrivateProfileIntA("Textures", "EnableTextureOverride", 1, iniPath) != 0;
    g_DumpTextures = GetPrivateProfileIntA("Textures", "DumpTextures", 0, iniPath) != 0;

    CreateDirectoryA("gedosato_textures", nullptr);
    CreateDirectoryA("gedosato_textures\\override", nullptr);
    CreateDirectoryA("gedosato_textures\\dump", nullptr);
}

static const char* g_DownsampleHLSL = R"(
Texture2D texInput : register(t0);
SamplerState samLinear : register(s0);
cbuffer Params : register(b0) {
    float2 srcSize;
    float2 dstSize;
    int scalingMode;
    float sharpenStrength;
    float2 pad;
};
float LanczosWeight(float x) {
    if (x == 0.0) return 1.0;
    if (abs(x) >= 3.0) return 0.0;
    float pi_x = 3.14159265 * x;
    return (sin(pi_x) / pi_x) * (sin(pi_x / 3.0) / (pi_x / 3.0));
}
float4 CubicWeight(float v) {
    float4 n = float4(1.0, 2.0, 3.0, 4.0) - v;
    float4 s = n * n * n;
    float x = s.x;
    float y = s.y - 4.0 * s.x;
    float z = s.z - 4.0 * s.y + 6.0 * s.x;
    float w = 6.0 - x - y - z;
    return float4(x, y, z, w) * (1.0 / 6.0);
}
float4 PSMain(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    float2 texel = 1.0 / srcSize;
    float4 color = texInput.Sample(samLinear, uv);
    if (scalingMode == 1) {
        float2 coord = uv * srcSize - 0.5;
        float2 f = frac(coord);
        coord -= f;
        float4 xc = CubicWeight(f.x);
        float4 yc = CubicWeight(f.y);
        float4 c = float4(coord.x - 0.5, coord.x + 1.5, coord.y - 0.5, coord.y + 1.5);
        float4 s = float4(xc.x + xc.y, xc.z + xc.w, yc.x + yc.y, yc.z + yc.w);
        float4 offset = c + float4(xc.y, xc.w, yc.y, yc.w) / s;
        float4 s0 = texInput.Sample(samLinear, float2(offset.x, offset.z) * texel);
        float4 s1 = texInput.Sample(samLinear, float2(offset.y, offset.z) * texel);
        float4 s2 = texInput.Sample(samLinear, float2(offset.x, offset.w) * texel);
        float4 s3 = texInput.Sample(samLinear, float2(offset.y, offset.w) * texel);
        float sx = s.x / (s.x + s.y);
        float sy = s.z / (s.z + s.w);
        color = lerp(lerp(s3, s2, sx), lerp(s1, s0, sx), sy);
    } else if (scalingMode == 2) {
        float2 center = uv * srcSize - 0.5;
        float2 base = floor(center);
        float4 sum = 0.0;
        float wSum = 0.0;
        [unroll] for (int y = -2; y <= 3; y++) {
            float wy = LanczosWeight((base.y + y) - center.y);
            [unroll] for (int x = -2; x <= 3; x++) {
                float wx = LanczosWeight((base.x + x) - center.x);
                float w = wx * wy;
                sum += texInput.Sample(samLinear, (base + float2(x, y) + 0.5) * texel) * w;
                wSum += w;
            }
        }
        color = sum / max(wSum, 0.0001);
    }
    if (sharpenStrength > 0.0) {
        float4 blur = (texInput.Sample(samLinear, uv + float2(texel.x, 0)) +
                       texInput.Sample(samLinear, uv - float2(texel.x, 0)) +
                       texInput.Sample(samLinear, uv + float2(0, texel.y)) +
                       texInput.Sample(samLinear, uv - float2(0, texel.y))) * 0.25;
        color += (color - blur) * sharpenStrength;
    }
    return saturate(color);
}
)";

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
