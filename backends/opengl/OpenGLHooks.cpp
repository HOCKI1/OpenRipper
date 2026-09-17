#include "OpenGLHooks.h"
#include "OpenGLCapture.h"
#include "MinHook.h"
#include <fstream>
#include <iostream>
#include <string>

namespace OpenRipper {
namespace OpenGL {

static void LogGL(const std::string& msg) {
    static std::ofstream logFile("C:\\OpenRipperDumps\\opengl_debug.log", std::ios::app);
    if (logFile.is_open()) {
        logFile << "[OpenRipper GL] " << msg << std::endl;
        logFile.flush();
    }
}

// Config
static int g_RipKey1 = VK_F10;
static int g_RipKey2 = 0;
static std::string g_OutputDir = "C:\\OpenRipperDumps";
static uint32_t g_FrameNumber = 0;
static uint64_t g_FrameCounter = 0;
static bool g_TriggerPending = false;

// Original function pointers from opengl32.dll / gdi32.dll exports
typedef BOOL (WINAPI *PFN_wglSwapBuffers)(HDC);
static PFN_wglSwapBuffers o_wglSwapBuffers = nullptr;

typedef BOOL (WINAPI *PFN_SwapBuffers)(HDC);
static PFN_SwapBuffers o_SwapBuffers = nullptr;

typedef PROC (WINAPI *PFN_wglGetProcAddress)(LPCSTR);
static PFN_wglGetProcAddress o_wglGetProcAddress = nullptr;

typedef void (APIENTRY *PFN_glDrawElements)(GLenum mode, GLsizei count, GLenum type, const void *indices);
static PFN_glDrawElements o_export_glDrawElements = nullptr;

typedef void (APIENTRY *PFN_glDrawArrays)(GLenum mode, GLint first, GLsizei count);
static PFN_glDrawArrays o_export_glDrawArrays = nullptr;

// Driver function pointers intercepted via wglGetProcAddress
static PFN_glDrawElements real_glDrawElements = nullptr;
static PFN_glDrawArrays real_glDrawArrays = nullptr;
static PFNGLDRAWRANGEELEMENTSPROC real_glDrawRangeElements = nullptr;
static PFNGLDRAWELEMENTSBASEVERTEXPROC real_glDrawElementsBaseVertex = nullptr;
static PFNGLDRAWRANGEELEMENTSBASEVERTEXPROC real_glDrawRangeElementsBaseVertex = nullptr;
static PFNGLDRAWELEMENTSINSTANCEDPROC real_glDrawElementsInstanced = nullptr;
static PFNGLDRAWELEMENTSINSTANCEDBASEVERTEXPROC real_glDrawElementsInstancedBaseVertex = nullptr;
static PFNGLDRAWARRAYSINSTANCEDPROC real_glDrawArraysInstanced = nullptr;

static PFNGLBINDBUFFERPROC real_glBindBuffer = nullptr;
static PFNGLBINDVERTEXARRAYPROC real_glBindVertexArray = nullptr;
static PFNGLVERTEXATTRIBPOINTERPROC real_glVertexAttribPointer = nullptr;
static PFNGLENABLEVERTEXATTRIBARRAYPROC real_glEnableVertexAttribArray = nullptr;
static PFNGLDISABLEVERTEXATTRIBARRAYPROC real_glDisableVertexAttribArray = nullptr;

// Forward declarations of hook wrappers
static void APIENTRY Hook_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);
static void APIENTRY Hook_glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void *indices);
static void APIENTRY Hook_glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void *indices, GLint basevertex);
static void APIENTRY Hook_glDrawRangeElementsBaseVertex(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void *indices, GLint basevertex);
static void APIENTRY Hook_glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount);
static void APIENTRY Hook_glDrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount, GLint basevertex);
static void APIENTRY Hook_glDrawArrays(GLenum mode, GLint first, GLsizei count);
static void APIENTRY Hook_glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instancecount);

static void APIENTRY Hook_glBindBuffer(GLenum target, GLuint buffer);
static void APIENTRY Hook_glBindVertexArray(GLuint array);
static void APIENTRY Hook_glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
static void APIENTRY Hook_glEnableVertexAttribArray(GLuint index);
static void APIENTRY Hook_glDisableVertexAttribArray(GLuint index);

static void HandleFrameBoundary() {
    g_FrameCounter++;
    if (g_FrameCounter == 1 || g_FrameCounter % 60 == 0) {
        LogGL("Active Frame #" + std::to_string(g_FrameCounter));
    }

    if (OpenGLCapture::Get().IsCapturing()) {
        OpenGLCapture::Get().EndFrameCapture();
        LogGL("Finished capture of frame " + std::to_string(g_FrameNumber));
    }

    // Check hotkey or trigger_rip.txt
    bool trigger = false;
    std::ifstream trigFile("C:\\OpenRipperDumps\\trigger_rip.txt");
    if (trigFile.is_open()) {
        trigFile.close();
        remove("C:\\OpenRipperDumps\\trigger_rip.txt");
        trigger = true;
        LogGL("Capture triggered via trigger_rip.txt!");
    } else if (g_RipKey1 > 0 && (GetAsyncKeyState(g_RipKey1) & 0x8000)) {
        if (g_RipKey2 == 0 || (GetAsyncKeyState(g_RipKey2) & 0x8000)) {
            trigger = true;
            LogGL("Hotkey pressed! Triggering capture...");
        }
    }

    if (trigger) {
        g_FrameNumber++;
        OpenGLCapture::Get().StartFrameCapture(g_FrameNumber);
    }
}

static thread_local bool t_inSwapBuffers = false;

static BOOL WINAPI Hook_wglSwapBuffers(HDC hdc) {
    if (t_inSwapBuffers) return o_wglSwapBuffers ? o_wglSwapBuffers(hdc) : TRUE;
    t_inSwapBuffers = true;
    HandleFrameBoundary();
    BOOL res = o_wglSwapBuffers ? o_wglSwapBuffers(hdc) : TRUE;
    t_inSwapBuffers = false;
    return res;
}

static BOOL WINAPI Hook_SwapBuffers(HDC hdc) {
    if (t_inSwapBuffers) return o_SwapBuffers ? o_SwapBuffers(hdc) : TRUE;
    t_inSwapBuffers = true;
    HandleFrameBoundary();
    BOOL res = o_SwapBuffers ? o_SwapBuffers(hdc) : TRUE;
    t_inSwapBuffers = false;
    return res;
}

static PROC WINAPI Hook_wglGetProcAddress(LPCSTR name) {
    if (!name) return nullptr;
    PROC realProc = o_wglGetProcAddress ? o_wglGetProcAddress(name) : nullptr;

    std::string sName = name;
    if (sName == "glDrawElements") {
        if (realProc) real_glDrawElements = (PFN_glDrawElements)realProc;
        return (PROC)Hook_glDrawElements;
    } else if (sName == "glDrawRangeElements") {
        if (realProc) real_glDrawRangeElements = (PFNGLDRAWRANGEELEMENTSPROC)realProc;
        return (PROC)Hook_glDrawRangeElements;
    } else if (sName == "glDrawElementsBaseVertex") {
        if (realProc) real_glDrawElementsBaseVertex = (PFNGLDRAWELEMENTSBASEVERTEXPROC)realProc;
        return (PROC)Hook_glDrawElementsBaseVertex;
    } else if (sName == "glDrawRangeElementsBaseVertex") {
        if (realProc) real_glDrawRangeElementsBaseVertex = (PFNGLDRAWRANGEELEMENTSBASEVERTEXPROC)realProc;
        return (PROC)Hook_glDrawRangeElementsBaseVertex;
    } else if (sName == "glDrawElementsInstanced") {
        if (realProc) real_glDrawElementsInstanced = (PFNGLDRAWELEMENTSINSTANCEDPROC)realProc;
        return (PROC)Hook_glDrawElementsInstanced;
    } else if (sName == "glDrawElementsInstancedBaseVertex") {
        if (realProc) real_glDrawElementsInstancedBaseVertex = (PFNGLDRAWELEMENTSINSTANCEDBASEVERTEXPROC)realProc;
        return (PROC)Hook_glDrawElementsInstancedBaseVertex;
    } else if (sName == "glDrawArrays") {
        if (realProc) real_glDrawArrays = (PFN_glDrawArrays)realProc;
        return (PROC)Hook_glDrawArrays;
    } else if (sName == "glDrawArraysInstanced") {
        if (realProc) real_glDrawArraysInstanced = (PFNGLDRAWARRAYSINSTANCEDPROC)realProc;
        return (PROC)Hook_glDrawArraysInstanced;
    } else if (sName == "glBindBuffer") {
        if (realProc) real_glBindBuffer = (PFNGLBINDBUFFERPROC)realProc;
        return (PROC)Hook_glBindBuffer;
    } else if (sName == "glBindVertexArray") {
        if (realProc) real_glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)realProc;
        return (PROC)Hook_glBindVertexArray;
    } else if (sName == "glVertexAttribPointer") {
        if (realProc) real_glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)realProc;
        return (PROC)Hook_glVertexAttribPointer;
    } else if (sName == "glEnableVertexAttribArray") {
        if (realProc) real_glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)realProc;
        return (PROC)Hook_glEnableVertexAttribArray;
    } else if (sName == "glDisableVertexAttribArray") {
        if (realProc) real_glDisableVertexAttribArray = (PFNGLDISABLEVERTEXATTRIBARRAYPROC)realProc;
        return (PROC)Hook_glDisableVertexAttribArray;
    }

    return realProc;
}

// Hook wrappers
static void APIENTRY Hook_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
    if (OpenGLCapture::Get().IsCapturing()) {
        LogGL("Hook_glDrawElements active while capturing! mode=" + std::to_string(mode) + " count=" + std::to_string(count));
        OpenGLCapture::Get().OnDrawElements(mode, count, type, indices, 0);
    }
    if (real_glDrawElements) {
        real_glDrawElements(mode, count, type, indices);
    } else if (o_export_glDrawElements) {
        o_export_glDrawElements(mode, count, type, indices);
    }
}

static void APIENTRY Hook_glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void *indices) {
    if (OpenGLCapture::Get().IsCapturing()) {
        OpenGLCapture::Get().OnDrawElements(mode, count, type, indices, 0);
    }
    if (real_glDrawRangeElements) {
        real_glDrawRangeElements(mode, start, end, count, type, indices);
    } else if (real_glDrawElements) {
        real_glDrawElements(mode, count, type, indices);
    } else if (o_export_glDrawElements) {
        o_export_glDrawElements(mode, count, type, indices);
    }
}

static void APIENTRY Hook_glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void *indices, GLint basevertex) {
    if (OpenGLCapture::Get().IsCapturing()) {
        OpenGLCapture::Get().OnDrawElements(mode, count, type, indices, basevertex);
    }
    if (real_glDrawElementsBaseVertex) {
        real_glDrawElementsBaseVertex(mode, count, type, indices, basevertex);
    } else if (real_glDrawElements) {
        real_glDrawElements(mode, count, type, indices);
    } else if (o_export_glDrawElements) {
        o_export_glDrawElements(mode, count, type, indices);
    }
}

static void APIENTRY Hook_glDrawRangeElementsBaseVertex(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void *indices, GLint basevertex) {
    if (OpenGLCapture::Get().IsCapturing()) {
        OpenGLCapture::Get().OnDrawElements(mode, count, type, indices, basevertex);
    }
    if (real_glDrawRangeElementsBaseVertex) {
        real_glDrawRangeElementsBaseVertex(mode, start, end, count, type, indices, basevertex);
    } else if (real_glDrawElementsBaseVertex) {
        real_glDrawElementsBaseVertex(mode, count, type, indices, basevertex);
    } else if (real_glDrawElements) {
        real_glDrawElements(mode, count, type, indices);
    } else if (o_export_glDrawElements) {
        o_export_glDrawElements(mode, count, type, indices);
    }
}

static void APIENTRY Hook_glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount) {
    if (OpenGLCapture::Get().IsCapturing()) {
        OpenGLCapture::Get().OnDrawElements(mode, count, type, indices, 0);
    }
    if (real_glDrawElementsInstanced) {
        real_glDrawElementsInstanced(mode, count, type, indices, instancecount);
    } else if (real_glDrawElements) {
        real_glDrawElements(mode, count, type, indices);
    } else if (o_export_glDrawElements) {
        o_export_glDrawElements(mode, count, type, indices);
    }
}

static void APIENTRY Hook_glDrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount, GLint basevertex) {
    if (OpenGLCapture::Get().IsCapturing()) {
        OpenGLCapture::Get().OnDrawElements(mode, count, type, indices, basevertex);
    }
    if (real_glDrawElementsInstancedBaseVertex) {
        real_glDrawElementsInstancedBaseVertex(mode, count, type, indices, instancecount, basevertex);
    } else if (real_glDrawElementsBaseVertex) {
        real_glDrawElementsBaseVertex(mode, count, type, indices, basevertex);
    } else if (real_glDrawElements) {
        real_glDrawElements(mode, count, type, indices);
    } else if (o_export_glDrawElements) {
        o_export_glDrawElements(mode, count, type, indices);
    }
}

static void APIENTRY Hook_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    if (OpenGLCapture::Get().IsCapturing()) {
        OpenGLCapture::Get().OnDrawArrays(mode, first, count);
    }
    if (real_glDrawArrays) {
        real_glDrawArrays(mode, first, count);
    } else if (o_export_glDrawArrays) {
        o_export_glDrawArrays(mode, first, count);
    }
}

static void APIENTRY Hook_glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instancecount) {
    if (OpenGLCapture::Get().IsCapturing()) {
        OpenGLCapture::Get().OnDrawArrays(mode, first, count);
    }
    if (real_glDrawArraysInstanced) {
        real_glDrawArraysInstanced(mode, first, count, instancecount);
    } else if (real_glDrawArrays) {
        real_glDrawArrays(mode, first, count);
    } else if (o_export_glDrawArrays) {
        o_export_glDrawArrays(mode, first, count);
    }
}

static void APIENTRY Hook_glBindBuffer(GLenum target, GLuint buffer) {
    OpenGLCapture::Get().OnBindBuffer(target, buffer);
    if (real_glBindBuffer) real_glBindBuffer(target, buffer);
}

static void APIENTRY Hook_glBindVertexArray(GLuint array) {
    OpenGLCapture::Get().OnBindVertexArray(array);
    if (real_glBindVertexArray) real_glBindVertexArray(array);
}

static void APIENTRY Hook_glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer) {
    OpenGLCapture::Get().OnVertexAttribPointer(index, size, type, normalized, stride, pointer);
    if (real_glVertexAttribPointer) real_glVertexAttribPointer(index, size, type, normalized, stride, pointer);
}

static void APIENTRY Hook_glEnableVertexAttribArray(GLuint index) {
    OpenGLCapture::Get().OnEnableVertexAttribArray(index);
    if (real_glEnableVertexAttribArray) real_glEnableVertexAttribArray(index);
}

static void APIENTRY Hook_glDisableVertexAttribArray(GLuint index) {
    OpenGLCapture::Get().OnDisableVertexAttribArray(index);
    if (real_glDisableVertexAttribArray) real_glDisableVertexAttribArray(index);
}

static void LoadConfig(HMODULE hModule) {
    char dllPath[MAX_PATH];
    GetModuleFileNameA(hModule, dllPath, MAX_PATH);
    std::string pathStr = dllPath;
    size_t lastSlash = pathStr.find_last_of("\\/");
    std::string dllDir = (lastSlash != std::string::npos) ? pathStr.substr(0, lastSlash) : ".";

    std::string candidates[] = {
        "C:\\OpenRipperDumps\\openripper.ini",
        dllDir + "\\openripper.ini",
        dllDir + "\\..\\openripper.ini"
    };

    std::string iniFile;
    for (const auto& cand : candidates) {
        if (GetFileAttributesA(cand.c_str()) != INVALID_FILE_ATTRIBUTES) {
            iniFile = cand;
            break;
        }
    }

    if (!iniFile.empty()) {
        g_RipKey1 = GetPrivateProfileIntA("Config", "Key1", VK_F10, iniFile.c_str());
        g_RipKey2 = GetPrivateProfileIntA("Config", "Key2", 0, iniFile.c_str());
        char outBuf[MAX_PATH];
        if (GetPrivateProfileStringA("Config", "OutputDir", "C:\\OpenRipperDumps", outBuf, MAX_PATH, iniFile.c_str()) > 0) {
            g_OutputDir = outBuf;
        }
    }

    OpenGLCapture::Get().Init(g_OutputDir);
    LogGL("Config loaded: Key1=" + std::to_string(g_RipKey1) + ", Key2=" + std::to_string(g_RipKey2) + ", OutDir=" + g_OutputDir);
}

static DWORD WINAPI InitOpenGLHooksThread(LPVOID lpParam) {
    HMODULE hModule = (HMODULE)lpParam;
    LogGL("InitializeHooks() started");
    LoadConfig(hModule);

    // Wait for opengl32.dll to be loaded
    HMODULE hOpenGL = nullptr;
    for (int i = 0; i < 50; ++i) {
        hOpenGL = GetModuleHandleA("opengl32.dll");
        if (hOpenGL) break;
        Sleep(50);
    }

    if (!hOpenGL) {
        hOpenGL = LoadLibraryA("opengl32.dll");
    }

    HMODULE hGDI = GetModuleHandleA("gdi32.dll");

    MH_Initialize();

    if (hOpenGL) {
        void* pWglSwapBuffers = (void*)GetProcAddress(hOpenGL, "wglSwapBuffers");
        if (pWglSwapBuffers) {
            MH_CreateHook(pWglSwapBuffers, (void*)&Hook_wglSwapBuffers, (void**)&o_wglSwapBuffers);
            MH_EnableHook(pWglSwapBuffers);
            LogGL("Hooked wglSwapBuffers");
        }

        void* pWglGetProcAddress = (void*)GetProcAddress(hOpenGL, "wglGetProcAddress");
        if (pWglGetProcAddress) {
            MH_CreateHook(pWglGetProcAddress, (void*)&Hook_wglGetProcAddress, (void**)&o_wglGetProcAddress);
            MH_EnableHook(pWglGetProcAddress);
            LogGL("Hooked wglGetProcAddress");
        }

        void* pGlDrawElements = (void*)GetProcAddress(hOpenGL, "glDrawElements");
        if (pGlDrawElements) {
            MH_CreateHook(pGlDrawElements, (void*)&Hook_glDrawElements, (void**)&o_export_glDrawElements);
            MH_EnableHook(pGlDrawElements);
            LogGL("Hooked export glDrawElements");
        }

        void* pGlDrawArrays = (void*)GetProcAddress(hOpenGL, "glDrawArrays");
        if (pGlDrawArrays) {
            MH_CreateHook(pGlDrawArrays, (void*)&Hook_glDrawArrays, (void**)&o_export_glDrawArrays);
            MH_EnableHook(pGlDrawArrays);
            LogGL("Hooked export glDrawArrays");
        }
    }

    if (hGDI) {
        void* pSwapBuffers = (void*)GetProcAddress(hGDI, "SwapBuffers");
        if (pSwapBuffers) {
            MH_CreateHook(pSwapBuffers, (void*)&Hook_SwapBuffers, (void**)&o_SwapBuffers);
            MH_EnableHook(pSwapBuffers);
            LogGL("Hooked GDI SwapBuffers");
        }
    }

    LogGL("All OpenGL hooks installed successfully!");
    return 0;
}

void InitializeHooks(HMODULE hModule) {
    CreateThread(nullptr, 0, InitOpenGLHooksThread, hModule, 0, nullptr);
}

void ShutdownHooks() {
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    LogGL("Shutdown completed");
}

} // namespace OpenGL
} // namespace OpenRipper

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        OpenRipper::OpenGL::InitializeHooks(hinstDLL);
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        OpenRipper::OpenGL::ShutdownHooks();
    }
    return TRUE;
}
