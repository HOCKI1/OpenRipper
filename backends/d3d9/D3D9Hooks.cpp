#include "D3D9Hooks.h"
#include "D3D9Capture.h"
#include "MinHook.h"
#include <stdio.h>
#include <string>

namespace OpenRipper {
namespace D3D9 {

static void Log(const char* fmt, ...) {
    FILE* f = nullptr;
    fopen_s(&f, "C:\\OpenRipperDumps\\d3d9_debug.log", "a");
    if (f) {
        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fclose(f);
    }
}

typedef HRESULT (STDMETHODCALLTYPE *Fn_Reset)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
typedef HRESULT (STDMETHODCALLTYPE *Fn_Present)(IDirect3DDevice9*, CONST RECT*, CONST RECT*, HWND, CONST RGNDATA*);
typedef HRESULT (STDMETHODCALLTYPE *Fn_EndScene)(IDirect3DDevice9*);
typedef HRESULT (STDMETHODCALLTYPE *Fn_DrawPrimitive)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT);
typedef HRESULT (STDMETHODCALLTYPE *Fn_DrawIndexedPrimitive)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
typedef HRESULT (STDMETHODCALLTYPE *Fn_DrawPrimitiveUP)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, CONST void*, UINT);
typedef HRESULT (STDMETHODCALLTYPE *Fn_DrawIndexedPrimitiveUP)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT, UINT, CONST void*, D3DFORMAT, CONST void*, UINT);
typedef HRESULT (STDMETHODCALLTYPE *Fn_SetVertexDeclaration)(IDirect3DDevice9*, IDirect3DVertexDeclaration9*);
typedef HRESULT (STDMETHODCALLTYPE *Fn_SetFVF)(IDirect3DDevice9*, DWORD);
typedef HRESULT (STDMETHODCALLTYPE *Fn_SetStreamSource)(IDirect3DDevice9*, UINT, IDirect3DVertexBuffer9*, UINT, UINT);
typedef HRESULT (STDMETHODCALLTYPE *Fn_SetIndices)(IDirect3DDevice9*, IDirect3DIndexBuffer9*);

static Fn_Reset                  Orig_Reset = nullptr;
static Fn_Present                Orig_Present = nullptr;
static Fn_EndScene               Orig_EndScene = nullptr;
static Fn_DrawPrimitive          Orig_DrawPrimitive = nullptr;
static Fn_DrawIndexedPrimitive   Orig_DrawIndexedPrimitive = nullptr;
static Fn_DrawPrimitiveUP        Orig_DrawPrimitiveUP = nullptr;
static Fn_DrawIndexedPrimitiveUP Orig_DrawIndexedPrimitiveUP = nullptr;
static Fn_SetVertexDeclaration   Orig_SetVertexDeclaration = nullptr;
static Fn_SetFVF                 Orig_SetFVF = nullptr;
static Fn_SetStreamSource        Orig_SetStreamSource = nullptr;
static Fn_SetIndices             Orig_SetIndices = nullptr;

static int s_Key1 = 48; // '0'
static int s_Key2 = 45; // VK_INSERT
static std::string s_OutputDir = "C:\\OpenRipperDumps";
static bool s_PrevKeyPressed = false;
static bool s_TriggerCapture = false;
static uint32_t s_CaptureFrameIndex = 1;
static uint32_t s_PresentCount = 0;
static bool s_HooksInstalled = false;

static void LoadConfig() {
    char iniPath[MAX_PATH] = "C:\\OpenRipperDumps\\openripper.ini";
    if (GetFileAttributesA(iniPath) == INVALID_FILE_ATTRIBUTES) {
        HMODULE hMod = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&LoadConfig, &hMod);
        if (hMod) {
            char dllPath[MAX_PATH] = {};
            GetModuleFileNameA(hMod, dllPath, MAX_PATH);
            char* lastSlash = strrchr(dllPath, '\\');
            if (lastSlash) {
                *lastSlash = '\0';
                char candidate1[MAX_PATH] = {};
                sprintf_s(candidate1, "%s\\openripper.ini", dllPath);

                char candidate2[MAX_PATH] = {};
                char* secondSlash = strrchr(dllPath, '\\');
                if (secondSlash) {
                    *secondSlash = '\0';
                    sprintf_s(candidate2, "%s\\openripper.ini", dllPath);
                }

                if (GetFileAttributesA(candidate1) != INVALID_FILE_ATTRIBUTES) {
                    strcpy_s(iniPath, candidate1);
                } else if (candidate2[0] && GetFileAttributesA(candidate2) != INVALID_FILE_ATTRIBUTES) {
                    strcpy_s(iniPath, candidate2);
                }
            }
        }
    }

    if (GetFileAttributesA(iniPath) != INVALID_FILE_ATTRIBUTES) {
        s_Key1 = GetPrivateProfileIntA("Config", "Key1", 48, iniPath);
        s_Key2 = GetPrivateProfileIntA("Config", "Key2", 45, iniPath);
        char outDir[MAX_PATH] = {};
        GetPrivateProfileStringA("Config", "OutputDir", "C:\\OpenRipperDumps", outDir, MAX_PATH, iniPath);
        s_OutputDir = outDir;
    }

    Log("[OpenRipper D3D9] Config loaded: Key1=%d, Key2=%d, OutDir=%s\n", s_Key1, s_Key2, s_OutputDir.c_str());
    D3D9Capture::Get().Init(s_OutputDir);
}

// Hooked methods
static HRESULT STDMETHODCALLTYPE Hook_Reset(IDirect3DDevice9* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters) {
    if (D3D9Capture::Get().IsCapturing()) {
        D3D9Capture::Get().EndFrameCapture();
    }
    return Orig_Reset(pDevice, pPresentationParameters);
}

static HRESULT STDMETHODCALLTYPE Hook_Present(IDirect3DDevice9* pDevice, CONST RECT* pSourceRect, CONST RECT* pDestRect,
                                              HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion) {
    s_PresentCount++;
    if (s_PresentCount == 1 || s_PresentCount % 30 == 0) {
        Log("[OpenRipper D3D9] Hook_Present active! Frame #%u\n", s_PresentCount);
    }

    bool k1 = ((GetAsyncKeyState(s_Key1) & 0x8000) != 0) || ((GetKeyState(s_Key1) & 0x8000) != 0);
    bool k2 = ((GetAsyncKeyState(s_Key2) & 0x8000) != 0) || ((GetKeyState(s_Key2) & 0x8000) != 0);
    bool pressed = (k1 || k2);

    if (GetFileAttributesA("C:\\OpenRipperDumps\\trigger_rip.txt") != INVALID_FILE_ATTRIBUTES) {
        DeleteFileA("C:\\OpenRipperDumps\\trigger_rip.txt");
        pressed = true;
        Log("[OpenRipper D3D9] Capture triggered via trigger_rip.txt!\n");
    }

    if (pressed && !s_PrevKeyPressed) {
        Log("[OpenRipper D3D9] Hotkey pressed at Present #%u! Triggering capture...\n", s_PresentCount);
        s_TriggerCapture = true;
    }
    s_PrevKeyPressed = pressed;

    if (D3D9Capture::Get().IsCapturing()) {
        D3D9Capture::Get().EndFrameCapture();
    } else if (s_TriggerCapture) {
        s_TriggerCapture = false;
        D3D9Capture::Get().StartFrameCapture(s_CaptureFrameIndex++);
    }

    return Orig_Present(pDevice, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}

static HRESULT STDMETHODCALLTYPE Hook_EndScene(IDirect3DDevice9* pDevice) {
    return Orig_EndScene(pDevice);
}

static HRESULT STDMETHODCALLTYPE Hook_SetStreamSource(IDirect3DDevice9* pDevice, UINT StreamNumber,
                                                     IDirect3DVertexBuffer9* pStreamData, UINT OffsetInBytes, UINT Stride) {
    D3D9Capture::Get().OnSetStreamSource(StreamNumber, pStreamData, OffsetInBytes, Stride);
    return Orig_SetStreamSource(pDevice, StreamNumber, pStreamData, OffsetInBytes, Stride);
}

static HRESULT STDMETHODCALLTYPE Hook_SetIndices(IDirect3DDevice9* pDevice, IDirect3DIndexBuffer9* pIndexData) {
    D3D9Capture::Get().OnSetIndices(pIndexData);
    return Orig_SetIndices(pDevice, pIndexData);
}

static HRESULT STDMETHODCALLTYPE Hook_SetFVF(IDirect3DDevice9* pDevice, DWORD FVF) {
    D3D9Capture::Get().OnSetFVF(FVF);
    return Orig_SetFVF(pDevice, FVF);
}

static HRESULT STDMETHODCALLTYPE Hook_SetVertexDeclaration(IDirect3DDevice9* pDevice, IDirect3DVertexDeclaration9* pDecl) {
    D3D9Capture::Get().OnSetVertexDeclaration(pDecl);
    return Orig_SetVertexDeclaration(pDevice, pDecl);
}

static HRESULT STDMETHODCALLTYPE Hook_DrawPrimitive(IDirect3DDevice9* pDevice, D3DPRIMITIVETYPE PrimitiveType,
                                                   UINT StartVertex, UINT PrimitiveCount) {
    D3D9Capture::Get().OnDrawPrimitive(pDevice, PrimitiveType, StartVertex, PrimitiveCount);
    return Orig_DrawPrimitive(pDevice, PrimitiveType, StartVertex, PrimitiveCount);
}

static HRESULT STDMETHODCALLTYPE Hook_DrawIndexedPrimitive(IDirect3DDevice9* pDevice, D3DPRIMITIVETYPE PrimitiveType,
                                                          INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices,
                                                          UINT StartIndex, UINT PrimitiveCount) {
    D3D9Capture::Get().OnDrawIndexedPrimitive(pDevice, PrimitiveType, BaseVertexIndex, MinVertexIndex,
                                              NumVertices, StartIndex, PrimitiveCount);
    return Orig_DrawIndexedPrimitive(pDevice, PrimitiveType, BaseVertexIndex, MinVertexIndex,
                                     NumVertices, StartIndex, PrimitiveCount);
}

static HRESULT STDMETHODCALLTYPE Hook_DrawPrimitiveUP(IDirect3DDevice9* pDevice, D3DPRIMITIVETYPE PrimitiveType,
                                                     UINT PrimitiveCount, CONST void* pVertexStreamZeroData,
                                                     UINT VertexStreamZeroStride) {
    D3D9Capture::Get().OnDrawPrimitiveUP(pDevice, PrimitiveType, PrimitiveCount,
                                        pVertexStreamZeroData, VertexStreamZeroStride);
    return Orig_DrawPrimitiveUP(pDevice, PrimitiveType, PrimitiveCount,
                                pVertexStreamZeroData, VertexStreamZeroStride);
}

static HRESULT STDMETHODCALLTYPE Hook_DrawIndexedPrimitiveUP(IDirect3DDevice9* pDevice, D3DPRIMITIVETYPE PrimitiveType,
                                                            UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount,
                                                            CONST void* pIndexData, D3DFORMAT IndexDataFormat,
                                                            CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride) {
    D3D9Capture::Get().OnDrawIndexedPrimitiveUP(pDevice, PrimitiveType, MinVertexIndex, NumVertices,
                                               PrimitiveCount, pIndexData, IndexDataFormat,
                                               pVertexStreamZeroData, VertexStreamZeroStride);
    return Orig_DrawIndexedPrimitiveUP(pDevice, PrimitiveType, MinVertexIndex, NumVertices,
                                       PrimitiveCount, pIndexData, IndexDataFormat,
                                       pVertexStreamZeroData, VertexStreamZeroStride);
}

static LRESULT CALLBACK DummyWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

typedef HRESULT (WINAPI *LPDIRECT3DCREATE9EX)(UINT, IDirect3D9Ex**);

static bool TryCreateDummyDevice(void**& outVTable, IUnknown*& outDevice, IUnknown*& outD3D) {
    HMODULE hD3D9 = LoadLibraryA("d3d9.dll");
    if (!hD3D9) {
        Log("[OpenRipper D3D9] Failed to LoadLibrary(d3d9.dll)\n");
        return false;
    }

    WNDCLASSA wc = {};
    wc.lpfnWndProc = DummyWndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "OpenRipperD3D9Dummy";
    RegisterClassA(&wc);

    HWND hWnd = CreateWindowA("OpenRipperD3D9Dummy", "Dummy", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);
    if (!hWnd) {
        Log("[OpenRipper D3D9] CreateWindow failed: %lu\n", GetLastError());
        return false;
    }

    // Try Direct3DCreate9Ex first
    LPDIRECT3DCREATE9EX pfnCreate9Ex = (LPDIRECT3DCREATE9EX)GetProcAddress(hD3D9, "Direct3DCreate9Ex");
    if (pfnCreate9Ex) {
        IDirect3D9Ex* pD3DEx = nullptr;
        if (SUCCEEDED(pfnCreate9Ex(D3D_SDK_VERSION, &pD3DEx)) && pD3DEx) {
            D3DDISPLAYMODEEX mode = { sizeof(D3DDISPLAYMODEEX) };
            pD3DEx->GetAdapterDisplayModeEx(D3DADAPTER_DEFAULT, &mode, NULL);

            D3DPRESENT_PARAMETERS d3dpp = {};
            d3dpp.Windowed = TRUE;
            d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
            d3dpp.BackBufferFormat = mode.Format;
            d3dpp.hDeviceWindow = hWnd;

            IDirect3DDevice9Ex* pDeviceEx = nullptr;
            HRESULT hr = pD3DEx->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
                                               D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, NULL, &pDeviceEx);
            if (FAILED(hr)) {
                hr = pD3DEx->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
                                            D3DCREATE_HARDWARE_VERTEXPROCESSING, &d3dpp, NULL, &pDeviceEx);
            }
            if (SUCCEEDED(hr) && pDeviceEx) {
                outVTable = *(void***)pDeviceEx;
                outDevice = pDeviceEx;
                outD3D = pD3DEx;
                DestroyWindow(hWnd);
                UnregisterClassA("OpenRipperD3D9Dummy", wc.hInstance);
                Log("[OpenRipper D3D9] Created dummy Direct3DDevice9Ex! vtable=%p\n", outVTable);
                return true;
            }
            Log("[OpenRipper D3D9] CreateDeviceEx failed: 0x%08X\n", (unsigned int)hr);
            pD3DEx->Release();
        }
    }

    // Fallback: standard Direct3DCreate9
    typedef IDirect3D9* (WINAPI *LPDIRECT3DCREATE9)(UINT);
    LPDIRECT3DCREATE9 pfnCreate9 = (LPDIRECT3DCREATE9)GetProcAddress(hD3D9, "Direct3DCreate9");
    if (pfnCreate9) {
        IDirect3D9* pD3D = pfnCreate9(D3D_SDK_VERSION);
        if (pD3D) {
            D3DDISPLAYMODE mode = {};
            pD3D->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode);

            D3DPRESENT_PARAMETERS d3dpp = {};
            d3dpp.Windowed = TRUE;
            d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
            d3dpp.BackBufferFormat = mode.Format;
            d3dpp.hDeviceWindow = hWnd;

            IDirect3DDevice9* pDevice = nullptr;
            HRESULT hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
                                           D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDevice);
            if (FAILED(hr)) {
                hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
                                        D3DCREATE_HARDWARE_VERTEXPROCESSING, &d3dpp, &pDevice);
            }
            if (SUCCEEDED(hr) && pDevice) {
                outVTable = *(void***)pDevice;
                outDevice = pDevice;
                outD3D = pD3D;
                DestroyWindow(hWnd);
                UnregisterClassA("OpenRipperD3D9Dummy", wc.hInstance);
                Log("[OpenRipper D3D9] Created dummy Direct3DDevice9! vtable=%p\n", outVTable);
                return true;
            }
            Log("[OpenRipper D3D9] CreateDevice standard failed: 0x%08X\n", (unsigned int)hr);
            pD3D->Release();
        }
    }

    DestroyWindow(hWnd);
    UnregisterClassA("OpenRipperD3D9Dummy", wc.hInstance);
    return false;
}

bool InitializeHooks() {
    if (s_HooksInstalled) return true;

    Log("[OpenRipper D3D9] InitializeHooks() started\n");
    LoadConfig();

    if (MH_Initialize() != MH_OK) {
        Log("[OpenRipper D3D9] MH_Initialize failed\n");
        return false;
    }

    void** vtable = nullptr;
    IUnknown* pDummyDevice = nullptr;
    IUnknown* pDummyD3D = nullptr;

    if (!TryCreateDummyDevice(vtable, pDummyDevice, pDummyD3D) || !vtable) {
        Log("[OpenRipper D3D9] Failed to create dummy device\n");
        return false;
    }

    Log("[OpenRipper D3D9] Hooking Present=%p, DrawIndexedPrimitive=%p\n", vtable[17], vtable[82]);

    MH_CreateHook(vtable[16],  (void*)Hook_Reset,                  (void**)&Orig_Reset);
    MH_CreateHook(vtable[17],  (void*)Hook_Present,                (void**)&Orig_Present);
    MH_CreateHook(vtable[42],  (void*)Hook_EndScene,               (void**)&Orig_EndScene);
    MH_CreateHook(vtable[81],  (void*)Hook_DrawPrimitive,          (void**)&Orig_DrawPrimitive);
    MH_CreateHook(vtable[82],  (void*)Hook_DrawIndexedPrimitive,   (void**)&Orig_DrawIndexedPrimitive);
    MH_CreateHook(vtable[83],  (void*)Hook_DrawPrimitiveUP,        (void**)&Orig_DrawPrimitiveUP);
    MH_CreateHook(vtable[84],  (void*)Hook_DrawIndexedPrimitiveUP, (void**)&Orig_DrawIndexedPrimitiveUP);
    MH_CreateHook(vtable[87],  (void*)Hook_SetVertexDeclaration,   (void**)&Orig_SetVertexDeclaration);
    MH_CreateHook(vtable[89],  (void*)Hook_SetFVF,                 (void**)&Orig_SetFVF);
    MH_CreateHook(vtable[100], (void*)Hook_SetStreamSource,        (void**)&Orig_SetStreamSource);
    MH_CreateHook(vtable[104], (void*)Hook_SetIndices,             (void**)&Orig_SetIndices);

    uint8_t* pPresentBytes = (uint8_t*)vtable[17];
    Log("[OpenRipper D3D9] Before hook, Present bytes: %02X %02X %02X %02X %02X\n",
        pPresentBytes[0], pPresentBytes[1], pPresentBytes[2], pPresentBytes[3], pPresentBytes[4]);

    MH_STATUS status = MH_EnableHook(MH_ALL_HOOKS);
    Log("[OpenRipper D3D9] MH_EnableHook status = %d\n", status);
    Log("[OpenRipper D3D9] After hook, Present bytes: %02X %02X %02X %02X %02X\n",
        pPresentBytes[0], pPresentBytes[1], pPresentBytes[2], pPresentBytes[3], pPresentBytes[4]);

    pDummyDevice->Release();
    pDummyD3D->Release();

    s_HooksInstalled = true;
    Log("[OpenRipper D3D9] All hooks installed successfully!\n");
    return true;
}

void ShutdownHooks() {
    if (s_HooksInstalled) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        s_HooksInstalled = false;
        Log("[OpenRipper D3D9] Shutdown completed\n");
    }
}

} // namespace D3D9
} // namespace OpenRipper

static DWORD WINAPI InitThreadProc(LPVOID) {
    Sleep(50);
    OpenRipper::D3D9::InitializeHooks();
    return 0;
}

// DllMain entry point for injection
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        CreateThread(NULL, 0, InitThreadProc, NULL, 0, NULL);
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        OpenRipper::D3D9::ShutdownHooks();
    }
    return TRUE;
}
