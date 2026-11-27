#include <windows.h>
#include <dxgi1_4.h>
#include <d3d11.h>
#include <d3d12.h>

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        OutputDebugStringA("[GeDoSaTo-DX12] DXGI Proxy DLL basariyla yuklendi!\n");
    }
    return TRUE;
}
