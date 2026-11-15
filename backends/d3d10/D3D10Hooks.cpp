#include "D3D10Hooks.h"
#include "D3D10Capture.h"
#include "MinHook.h"
#include <stdio.h>
#include <string>

namespace OpenRipper {
namespace D3D10 {

void Log(const char* fmt, ...) {
    FILE* f = nullptr;
    fopen_s(&f, "C:\\OpenRipperDumps\\d3d10_debug.log", "a");
    if (f) {
        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fclose(f);
    }
}

// DXGI SwapChain signatures
typedef HRESULT (STDMETHODCALLTYPE *Fn_Present)(IDXGISwapChain*, UINT, UINT);
typedef HRESULT (STDMETHODCALLTYPE *Fn_ResizeBuffers)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

// ID3D10Device signatures
typedef HRESULT (STDMETHODCALLTYPE *Fn_CreateInputLayout)(ID3D10Device*, const D3D10_INPUT_ELEMENT_DESC*, UINT, const void*, SIZE_T, ID3D10InputLayout**);
typedef void (STDMETHODCALLTYPE *Fn_DrawIndexed)(ID3D10Device*, UINT, UINT, INT);
typedef void (STDMETHODCALLTYPE *Fn_Draw)(ID3D10Device*, UINT, UINT);
typedef void (STDMETHODCALLTYPE *Fn_IASetInputLayout)(ID3D10Device*, ID3D10InputLayout*);
typedef void (STDMETHODCALLTYPE *Fn_IASetVertexBuffers)(ID3D10Device*, UINT, UINT, ID3D10Buffer* const*, const UINT*, const UINT*);
typedef void (STDMETHODCALLTYPE *Fn_IASetIndexBuffer)(ID3D10Device*, ID3D10Buffer*, DXGI_FORMAT, UINT);
typedef void (STDMETHODCALLTYPE *Fn_DrawIndexedInstanced)(ID3D10Device*, UINT, UINT, UINT, INT, UINT);
typedef void (STDMETHODCALLTYPE *Fn_DrawInstanced)(ID3D10Device*, UINT, UINT, UINT, UINT);
typedef void (STDMETHODCALLTYPE *Fn_IASetPrimitiveTopology)(ID3D10Device*, D3D10_PRIMITIVE_TOPOLOGY);

// Original function pointers
static Fn_Present                Orig_Present = nullptr;
static Fn_ResizeBuffers          Orig_ResizeBuffers = nullptr;
static Fn_CreateInputLayout      Orig_CreateInputLayout = nullptr;
static Fn_DrawIndexed            Orig_DrawIndexed = nullptr;
static Fn_Draw                   Orig_Draw = nullptr;
static Fn_IASetInputLayout       Orig_IASetInputLayout = nullptr;
static Fn_IASetVertexBuffers     Orig_IASetVertexBuffers = nullptr;
static Fn_IASetIndexBuffer       Orig_IASetIndexBuffer = nullptr;
static Fn_DrawIndexedInstanced   Orig_DrawIndexedInstanced = nullptr;
static Fn_DrawInstanced          Orig_DrawInstanced = nullptr;
static Fn_IASetPrimitiveTopology Orig_IASetPrimitiveTopology = nullptr;

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

    Log("[OpenRipper D3D10] Config loaded: Key1=%d, Key2=%d, OutDir=%s\n", s_Key1, s_Key2, s_OutputDir.c_str());
    D3D10Capture::Get().Init(s_OutputDir);
}

// Hook Implementations
static HRESULT STDMETHODCALLTYPE Hook_Present(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    s_PresentCount++;
    if (s_PresentCount == 1 || s_PresentCount % 60 == 0) {
        Log("[OpenRipper D3D10] Hook_Present active! Frame #%u\n", s_PresentCount);
    }

    bool k1 = ((GetAsyncKeyState(s_Key1) & 0x8000) != 0) || ((GetKeyState(s_Key1) & 0x8000) != 0);
    bool k2 = ((GetAsyncKeyState(s_Key2) & 0x8000) != 0) || ((GetKeyState(s_Key2) & 0x8000) != 0);
    bool pressed = (k1 || k2);

    if (GetFileAttributesA("C:\\OpenRipperDumps\\trigger_rip.txt") != INVALID_FILE_ATTRIBUTES) {
        DeleteFileA("C:\\OpenRipperDumps\\trigger_rip.txt");
        pressed = true;
        Log("[OpenRipper D3D10] Capture triggered via trigger_rip.txt!\n");
    }

    if (pressed && !s_PrevKeyPressed) {
        Log("[OpenRipper D3D10] Hotkey pressed at Present #%u! Triggering capture...\n", s_PresentCount);
        s_TriggerCapture = true;
    }
    s_PrevKeyPressed = pressed;

    if (D3D10Capture::Get().IsCapturing()) {
        D3D10Capture::Get().EndFrameCapture();
    } else if (s_TriggerCapture) {
        s_TriggerCapture = false;
        D3D10Capture::Get().StartFrameCapture(s_CaptureFrameIndex++);
    }

    return Orig_Present(pSwapChain, SyncInterval, Flags);
}

static HRESULT STDMETHODCALLTYPE Hook_ResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    if (D3D10Capture::Get().IsCapturing()) {
        D3D10Capture::Get().EndFrameCapture();
    }
    return Orig_ResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

static HRESULT STDMETHODCALLTYPE Hook_CreateInputLayout(ID3D10Device* pDevice,
                                                       const D3D10_INPUT_ELEMENT_DESC* pInputElementDescs,
                                                       UINT NumElements,
                                                       const void* pShaderBytecodeWithInputSignature,
                                                       SIZE_T BytecodeLength,
                                                       ID3D10InputLayout** ppInputLayout) {
    HRESULT hr = Orig_CreateInputLayout(pDevice, pInputElementDescs, NumElements, pShaderBytecodeWithInputSignature, BytecodeLength, ppInputLayout);
    if (SUCCEEDED(hr) && ppInputLayout && *ppInputLayout) {
        Log("[OpenRipper D3D10] Hook_CreateInputLayout called! numElements=%u, layout=%p\n", NumElements, *ppInputLayout);
        D3D10Capture::Get().RegisterInputLayout(*ppInputLayout, pInputElementDescs, NumElements);
    }
    return hr;
}

static void STDMETHODCALLTYPE Hook_IASetInputLayout(ID3D10Device* pDevice, ID3D10InputLayout* pInputLayout) {
    if (D3D10Capture::Get().IsCapturing()) {
        Log("[OpenRipper D3D10] Hook_IASetInputLayout called: layout=%p\n", pInputLayout);
    }
    D3D10Capture::Get().OnSetInputLayout(pInputLayout);
    Orig_IASetInputLayout(pDevice, pInputLayout);
}

static void STDMETHODCALLTYPE Hook_IASetVertexBuffers(ID3D10Device* pDevice, UINT StartSlot, UINT NumBuffers,
                                                     ID3D10Buffer* const* ppVertexBuffers, const UINT* pStrides, const UINT* pOffsets) {
    if (D3D10Capture::Get().IsCapturing()) {
        Log("[OpenRipper D3D10] Hook_IASetVertexBuffers called: StartSlot=%u, NumBuffers=%u, pBuf[0]=%p, stride[0]=%u\n",
            StartSlot, NumBuffers, (ppVertexBuffers ? ppVertexBuffers[0] : nullptr), (pStrides ? pStrides[0] : 0));
    }
    D3D10Capture::Get().OnSetVertexBuffers(StartSlot, NumBuffers, ppVertexBuffers, pStrides, pOffsets);
    Orig_IASetVertexBuffers(pDevice, StartSlot, NumBuffers, ppVertexBuffers, pStrides, pOffsets);
}

static void STDMETHODCALLTYPE Hook_IASetIndexBuffer(ID3D10Device* pDevice, ID3D10Buffer* pIndexBuffer, DXGI_FORMAT Format, UINT Offset) {
    if (D3D10Capture::Get().IsCapturing()) {
        Log("[OpenRipper D3D10] Hook_IASetIndexBuffer called: pIB=%p, format=%u, offset=%u\n", pIndexBuffer, Format, Offset);
    }
    D3D10Capture::Get().OnSetIndexBuffer(pIndexBuffer, Format, Offset);
    Orig_IASetIndexBuffer(pDevice, pIndexBuffer, Format, Offset);
}

static void STDMETHODCALLTYPE Hook_IASetPrimitiveTopology(ID3D10Device* pDevice, D3D10_PRIMITIVE_TOPOLOGY Topology) {
    if (D3D10Capture::Get().IsCapturing()) {
        Log("[OpenRipper D3D10] Hook_IASetPrimitiveTopology called: %u\n", Topology);
    }
    D3D10Capture::Get().OnSetPrimitiveTopology(Topology);
    Orig_IASetPrimitiveTopology(pDevice, Topology);
}

static void STDMETHODCALLTYPE Hook_Draw(ID3D10Device* pDevice, UINT VertexCount, UINT StartVertexLocation) {
    if (D3D10Capture::Get().IsCapturing()) {
        Log("[OpenRipper D3D10] Hook_Draw called while capturing! VertexCount=%u\n", VertexCount);
    }
    D3D10Capture::Get().OnDraw(pDevice, VertexCount, StartVertexLocation);
    Orig_Draw(pDevice, VertexCount, StartVertexLocation);
}

static void STDMETHODCALLTYPE Hook_DrawIndexed(ID3D10Device* pDevice, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation) {
    if (D3D10Capture::Get().IsCapturing()) {
        Log("[OpenRipper D3D10] Hook_DrawIndexed called while capturing! IndexCount=%u\n", IndexCount);
    }
    D3D10Capture::Get().OnDrawIndexed(pDevice, IndexCount, StartIndexLocation, BaseVertexLocation);
    Orig_DrawIndexed(pDevice, IndexCount, StartIndexLocation, BaseVertexLocation);
}

static void STDMETHODCALLTYPE Hook_DrawInstanced(ID3D10Device* pDevice, UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation, UINT StartInstanceLocation) {
    D3D10Capture::Get().OnDrawInstanced(pDevice, VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
    Orig_DrawInstanced(pDevice, VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
}

static void STDMETHODCALLTYPE Hook_DrawIndexedInstanced(ID3D10Device* pDevice, UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation) {
    D3D10Capture::Get().OnDrawIndexedInstanced(pDevice, IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
    Orig_DrawIndexedInstanced(pDevice, IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
}

static LRESULT CALLBACK DummyWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

bool InitializeHooks() {
    if (s_HooksInstalled) return true;

    Log("[OpenRipper D3D10] InitializeHooks() started\n");
    LoadConfig();

    if (MH_Initialize() != MH_OK) {
        Log("[OpenRipper D3D10] MH_Initialize failed\n");
        return false;
    }

    // Create dummy window
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = DummyWndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "OpenRipperD3D10Dummy";
    RegisterClassA(&wc);

    HWND hWnd = CreateWindowA("OpenRipperD3D10Dummy", "Dummy", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);
    if (!hWnd) {
        Log("[OpenRipper D3D10] CreateWindow failed: %lu\n", GetLastError());
        return false;
    }

    DXGI_SWAP_CHAIN_DESC scd = {0};
    scd.BufferCount = 1;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.Width = 100;
    scd.BufferDesc.Height = 100;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hWnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ID3D10Device* pDevice = nullptr;
    IDXGISwapChain* pSwapChain = nullptr;

    HRESULT hr = D3D10CreateDeviceAndSwapChain(
        NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0,
        D3D10_SDK_VERSION, &scd, &pSwapChain, &pDevice
    );

    if (FAILED(hr)) {
        hr = D3D10CreateDeviceAndSwapChain(
            NULL, D3D10_DRIVER_TYPE_WARP, NULL, 0,
            D3D10_SDK_VERSION, &scd, &pSwapChain, &pDevice
        );
    }

    if (FAILED(hr) || !pSwapChain || !pDevice) {
        Log("[OpenRipper D3D10] D3D10CreateDeviceAndSwapChain failed: 0x%08X\n", (unsigned int)hr);
        DestroyWindow(hWnd);
        UnregisterClassA("OpenRipperD3D10Dummy", wc.hInstance);
        return false;
    }

    void** scVtbl = *(void***)pSwapChain;
    void** devVtbl = *(void***)pDevice;

    Log("[OpenRipper D3D10] Dummy objects created: SwapChain=%p, Device=%p\n", scVtbl, devVtbl);

    // Hook IDXGISwapChain: [8] Present, [13] ResizeBuffers
    MH_CreateHook(scVtbl[8],  (void*)Hook_Present,       (void**)&Orig_Present);
    MH_CreateHook(scVtbl[13], (void*)Hook_ResizeBuffers, (void**)&Orig_ResizeBuffers);

    // Hook ID3D10Device methods
    // [8]  DrawIndexed
    // [9]  Draw
    // [11] IASetInputLayout
    // [12] IASetVertexBuffers
    // [13] IASetIndexBuffer
    // [14] DrawIndexedInstanced
    // [15] DrawInstanced
    // [18] IASetPrimitiveTopology
    // [78] CreateInputLayout
    MH_CreateHook(devVtbl[8],  (void*)Hook_DrawIndexed,            (void**)&Orig_DrawIndexed);
    MH_CreateHook(devVtbl[9],  (void*)Hook_Draw,                   (void**)&Orig_Draw);
    MH_CreateHook(devVtbl[11], (void*)Hook_IASetInputLayout,       (void**)&Orig_IASetInputLayout);
    MH_CreateHook(devVtbl[12], (void*)Hook_IASetVertexBuffers,     (void**)&Orig_IASetVertexBuffers);
    MH_CreateHook(devVtbl[13], (void*)Hook_IASetIndexBuffer,       (void**)&Orig_IASetIndexBuffer);
    MH_CreateHook(devVtbl[14], (void*)Hook_DrawIndexedInstanced,   (void**)&Orig_DrawIndexedInstanced);
    MH_CreateHook(devVtbl[15], (void*)Hook_DrawInstanced,          (void**)&Orig_DrawInstanced);
    MH_CreateHook(devVtbl[18], (void*)Hook_IASetPrimitiveTopology, (void**)&Orig_IASetPrimitiveTopology);
    MH_CreateHook(devVtbl[78], (void*)Hook_CreateInputLayout,      (void**)&Orig_CreateInputLayout);

    MH_STATUS status = MH_EnableHook(MH_ALL_HOOKS);
    Log("[OpenRipper D3D10] MH_EnableHook status = %d\n", status);

    pSwapChain->Release();
    pDevice->Release();
    DestroyWindow(hWnd);
    UnregisterClassA("OpenRipperD3D10Dummy", wc.hInstance);

    s_HooksInstalled = true;
    Log("[OpenRipper D3D10] All hooks installed successfully!\n");
    return true;
}

void ShutdownHooks() {
    if (s_HooksInstalled) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        s_HooksInstalled = false;
        Log("[OpenRipper D3D10] Shutdown completed\n");
    }
}

} // namespace D3D10
} // namespace OpenRipper

static DWORD WINAPI InitThreadProc(LPVOID) {
    OpenRipper::D3D10::InitializeHooks();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        CreateThread(NULL, 0, InitThreadProc, NULL, 0, NULL);
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        OpenRipper::D3D10::ShutdownHooks();
    }
    return TRUE;
}
