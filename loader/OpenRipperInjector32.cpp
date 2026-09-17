#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <fstream>

// Usage: OpenRipperInjector32.exe <PID> <DLL_PATH>
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argvW || argc < 3) {
        if (argvW) LocalFree(argvW);
        return 1;
    }

    DWORD pid = _wtoi(argvW[1]);
    std::wstring dllPathW = argvW[2];
    LocalFree(argvW);

    if (pid == 0 || dllPathW.empty()) {
        return 2;
    }

    // Convert wide path to ANSI
    int len = WideCharToMultiByte(CP_ACP, 0, dllPathW.c_str(), -1, NULL, 0, NULL, NULL);
    if (len <= 0) return 3;
    std::string dllPathA(len, '\0');
    WideCharToMultiByte(CP_ACP, 0, dllPathW.c_str(), -1, &dllPathA[0], len, NULL, NULL);

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        return 4;
    }

    void* pRemoteBuf = VirtualAllocEx(hProcess, NULL, dllPathA.size() + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pRemoteBuf) {
        CloseHandle(hProcess);
        return 5;
    }

    if (!WriteProcessMemory(hProcess, pRemoteBuf, dllPathA.c_str(), dllPathA.size() + 1, NULL)) {
        VirtualFreeEx(hProcess, pRemoteBuf, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 6;
    }

    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!hKernel32) {
        VirtualFreeEx(hProcess, pRemoteBuf, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 7;
    }

    FARPROC pLoadLibraryA = GetProcAddress(hKernel32, "LoadLibraryA");
    if (!pLoadLibraryA) {
        VirtualFreeEx(hProcess, pRemoteBuf, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 8;
    }

    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pLoadLibraryA, pRemoteBuf, 0, NULL);
    if (!hThread) {
        VirtualFreeEx(hProcess, pRemoteBuf, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 9;
    }

    WaitForSingleObject(hThread, 5000);
    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, pRemoteBuf, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    // exitCode is the HMODULE returned by LoadLibraryA! If 0, LoadLibrary failed!
    if (exitCode == 0) {
        return 10;
    }

    return 0;
}
