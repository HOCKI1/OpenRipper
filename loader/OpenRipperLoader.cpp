#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

#define IDC_EDIT_GAME_PATH      101
#define IDC_BTN_BROWSE_GAME     102
#define IDC_COMBO_API           103
#define IDC_COMBO_KEY1          104
#define IDC_COMBO_KEY2          105
#define IDC_EDIT_OUT_DIR        106
#define IDC_BTN_BROWSE_OUT      107
#define IDC_BTN_LAUNCH          108
#define IDC_STATIC_STATUS       109

struct HotkeyEntry {
    const char* name;
    int vkCode;
};

static const HotkeyEntry g_KeyList1[] = {
    { "0 (Top Row)", '0' },
    { "1 (Top Row)", '1' },
    { "2 (Top Row)", '2' },
    { "3 (Top Row)", '3' },
    { "4 (Top Row)", '4' },
    { "5 (Top Row)", '5' },
    { "6 (Top Row)", '6' },
    { "7 (Top Row)", '7' },
    { "8 (Top Row)", '8' },
    { "9 (Top Row)", '9' },
    { "F1", VK_F1 },
    { "F2", VK_F2 },
    { "F3", VK_F3 },
    { "F4", VK_F4 },
    { "F5", VK_F5 },
    { "F6", VK_F6 },
    { "F7", VK_F7 },
    { "F8", VK_F8 },
    { "F9", VK_F9 },
    { "F10", VK_F10 },
    { "F11", VK_F11 },
    { "F12", VK_F12 },
};

static const HotkeyEntry g_KeyList2[] = {
    { "Insert", VK_INSERT },
    { "Delete", VK_DELETE },
    { "Home", VK_HOME },
    { "End", VK_END },
    { "Page Up", VK_PRIOR },
    { "Page Down", VK_NEXT },
    { "F8", VK_F8 },
    { "F9", VK_F9 },
    { "Numpad 0", VK_NUMPAD0 },
    { "Numpad 5", VK_NUMPAD5 },
    { "None", 0 }
};

static HWND g_hEditGame = NULL;
static HWND g_hComboApi = NULL;
static HWND g_hComboKey1 = NULL;
static HWND g_hComboKey2 = NULL;
static HWND g_hEditOut = NULL;
static HWND g_hStatus = NULL;

static std::string GetAppDir() {
    char szPath[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, szPath, MAX_PATH);
    std::filesystem::path p(szPath);
    return p.parent_path().string();
}

static std::string GetConfigPath() {
    return GetAppDir() + "\\openripper.ini";
}

static std::string FindBackendFile(const std::string& filename) {
    std::string appDir = GetAppDir();
    // 1. Check bin/backends/ (standard layout)
    std::string p1 = appDir + "\\backends\\" + filename;
    if (std::filesystem::exists(p1)) return p1;

    // 2. Check root/backends/
    std::string p2 = appDir + "\\..\\backends\\" + filename;
    if (std::filesystem::exists(p2)) return p2;

    // 3. Check build/
    std::string p3 = appDir + "\\build\\" + filename;
    if (std::filesystem::exists(p3)) return p3;

    // 4. Check same directory as loader
    std::string p4 = appDir + "\\" + filename;
    if (std::filesystem::exists(p4)) return p4;

    return "";
}

static std::string FindHelperExecutable(const std::string& exeName) {
    std::string appDir = GetAppDir();
    // 1. Same directory (bin/ or root/)
    std::string p1 = appDir + "\\" + exeName;
    if (std::filesystem::exists(p1)) return p1;

    // 2. bin/ directory
    std::string p2 = appDir + "\\bin\\" + exeName;
    if (std::filesystem::exists(p2)) return p2;

    // 3. build/ directory
    std::string p3 = appDir + "\\build\\" + exeName;
    if (std::filesystem::exists(p3)) return p3;

    return "";
}

static bool Is64BitExecutable(const std::string& exePath) {
    HANDLE hFile = CreateFileA(exePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return true;

    IMAGE_DOS_HEADER dosHeader = {};
    DWORD read = 0;
    ReadFile(hFile, &dosHeader, sizeof(dosHeader), &read, NULL);
    if (dosHeader.e_magic != IMAGE_DOS_SIGNATURE) {
        CloseHandle(hFile);
        return true;
    }

    SetFilePointer(hFile, dosHeader.e_lfanew, NULL, FILE_BEGIN);
    DWORD ntSignature = 0;
    ReadFile(hFile, &ntSignature, sizeof(ntSignature), &read, NULL);
    if (ntSignature != IMAGE_NT_SIGNATURE) {
        CloseHandle(hFile);
        return true;
    }

    IMAGE_FILE_HEADER fileHeader = {};
    ReadFile(hFile, &fileHeader, sizeof(fileHeader), &read, NULL);
    CloseHandle(hFile);

    return (fileHeader.Machine == IMAGE_FILE_MACHINE_AMD64);
}

static void LoadConfig() {
    std::string ini = GetConfigPath();
    if (!std::filesystem::exists(ini)) {
        ini = "C:\\OpenRipperDumps\\openripper.ini";
    }

    char gamePath[MAX_PATH] = {0};
    char outDir[MAX_PATH] = "C:\\OpenRipperDumps";
    int apiIndex = 0;
    int key1Code = '0';
    int key2Code = VK_INSERT;

    GetPrivateProfileStringA("Config", "GamePath", "", gamePath, MAX_PATH, ini.c_str());
    GetPrivateProfileStringA("Config", "OutputDir", "C:\\OpenRipperDumps", outDir, MAX_PATH, ini.c_str());
    apiIndex = GetPrivateProfileIntA("Config", "ApiIndex", 0, ini.c_str());
    key1Code = GetPrivateProfileIntA("Config", "Key1", '0', ini.c_str());
    key2Code = GetPrivateProfileIntA("Config", "Key2", VK_INSERT, ini.c_str());

    SetWindowTextA(g_hEditGame, gamePath);
    SetWindowTextA(g_hEditOut, outDir);
    SendMessageA(g_hComboApi, CB_SETCURSEL, (WPARAM)apiIndex, 0);

    int sel1 = 0;
    for (int i = 0; i < (int)(sizeof(g_KeyList1) / sizeof(g_KeyList1[0])); ++i) {
        if (g_KeyList1[i].vkCode == key1Code) {
            sel1 = i;
            break;
        }
    }
    SendMessageA(g_hComboKey1, CB_SETCURSEL, (WPARAM)sel1, 0);

    int sel2 = 0;
    for (int i = 0; i < (int)(sizeof(g_KeyList2) / sizeof(g_KeyList2[0])); ++i) {
        if (g_KeyList2[i].vkCode == key2Code) {
            sel2 = i;
            break;
        }
    }
    SendMessageA(g_hComboKey2, CB_SETCURSEL, (WPARAM)sel2, 0);
}

static void SaveConfig() {
    char gamePath[MAX_PATH] = {0};
    char outDir[MAX_PATH] = {0};
    GetWindowTextA(g_hEditGame, gamePath, MAX_PATH);
    GetWindowTextA(g_hEditOut, outDir, MAX_PATH);

    int apiIndex = (int)SendMessageA(g_hComboApi, CB_GETCURSEL, 0, 0);
    int key1Idx = (int)SendMessageA(g_hComboKey1, CB_GETCURSEL, 0, 0);
    int key2Idx = (int)SendMessageA(g_hComboKey2, CB_GETCURSEL, 0, 0);

    int key1Code = (key1Idx >= 0 && key1Idx < (int)(sizeof(g_KeyList1)/sizeof(g_KeyList1[0])))
                   ? g_KeyList1[key1Idx].vkCode : '0';
    int key2Code = (key2Idx >= 0 && key2Idx < (int)(sizeof(g_KeyList2)/sizeof(g_KeyList2[0])))
                   ? g_KeyList2[key2Idx].vkCode : VK_INSERT;

    const char* apiName = "Vulkan";
    if (apiIndex == 1) apiName = "DirectX 9";
    else if (apiIndex == 2) apiName = "DirectX 11";
    else if (apiIndex == 3) apiName = "DXVK";
    else if (apiIndex == 4) apiName = "OpenGL";

    std::vector<std::string> iniTargets = {
        GetConfigPath(),
        std::string("C:\\OpenRipperDumps\\openripper.ini"),
        GetAppDir() + "\\build\\openripper.ini"
    };

    try {
        std::filesystem::create_directories(outDir);
    } catch (...) {}

    for (const auto& ini : iniTargets) {
        WritePrivateProfileStringA("Config", "GamePath", gamePath, ini.c_str());
        WritePrivateProfileStringA("Config", "OutputDir", outDir, ini.c_str());
        WritePrivateProfileStringA("Config", "Api", apiName, ini.c_str());
        WritePrivateProfileStringA("Config", "ApiIndex", std::to_string(apiIndex).c_str(), ini.c_str());
        WritePrivateProfileStringA("Config", "Key1", std::to_string(key1Code).c_str(), ini.c_str());
        WritePrivateProfileStringA("Config", "Key2", std::to_string(key2Code).c_str(), ini.c_str());
    }
}

static void BrowseGameFile(HWND hWnd) {
    char szFile[MAX_PATH] = {0};
    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Executable Files (*.exe)\0*.exe\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        SetWindowTextA(g_hEditGame, szFile);
    }
}

static void BrowseOutputDir(HWND hWnd) {
    BROWSEINFOA bi = {0};
    bi.hwndOwner = hWnd;
    bi.lpszTitle = "Select Output Directory for 3D Geometry Dumps:";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl) {
        char szDir[MAX_PATH];
        if (SHGetPathFromIDListA(pidl, szDir)) {
            SetWindowTextA(g_hEditOut, szDir);
        }
        CoTaskMemFree(pidl);
    }
}

static void LaunchGame(HWND hWnd) {
    char gamePath[MAX_PATH] = {0};
    char outDir[MAX_PATH] = {0};
    GetWindowTextA(g_hEditGame, gamePath, MAX_PATH);
    GetWindowTextA(g_hEditOut, outDir, MAX_PATH);

    if (strlen(gamePath) == 0) {
        MessageBoxA(hWnd, "Please select game executable (.exe) first!", "OpenRipper Loader", MB_ICONWARNING);
        return;
    }

    if (!std::filesystem::exists(gamePath)) {
        MessageBoxA(hWnd, "Game executable not found!", "OpenRipper Loader", MB_ICONERROR);
        return;
    }

    SaveConfig();

    int key1Idx = (int)SendMessage(g_hComboKey1, CB_GETCURSEL, 0, 0);
    int key2Idx = (int)SendMessage(g_hComboKey2, CB_GETCURSEL, 0, 0);
    int apiIndex = (int)SendMessage(g_hComboApi, CB_GETCURSEL, 0, 0);

    std::string appDir = GetAppDir();
    std::filesystem::path exePath(gamePath);
    std::string workDir = exePath.parent_path().string();

    bool is64Bit = Is64BitExecutable(gamePath);

    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);

    std::string statusMsg = "Launching: " + exePath.filename().string() + (is64Bit ? " (x64)..." : " (x86)...");
    SetWindowTextA(g_hStatus, statusMsg.c_str());

    if (apiIndex == 0) {
        // Vulkan (Native / RPCS3)
        std::string jsonPath = FindBackendFile("VkLayer_openripper.json");
        std::string layerDir = appDir;
        if (!jsonPath.empty()) {
            std::filesystem::path jp(jsonPath);
            layerDir = jp.parent_path().string();
        }

        SetEnvironmentVariableA("VK_LAYER_PATH", layerDir.c_str());
        SetEnvironmentVariableA("VK_INSTANCE_LAYERS", "VK_LAYER_OPENRIPPER_capture");
        SetEnvironmentVariableA("ENABLE_OPENRIPPER", "1");

        BOOL success = CreateProcessA(
            gamePath, NULL, NULL, NULL, FALSE, 0, NULL,
            workDir.c_str(), &si, &pi
        );

        if (success) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            std::ostringstream ss;
            ss << "Vulkan Game launched! OpenRipper is active.\n\n"
               << "Primary hotkey: [ " << g_KeyList1[key1Idx].name << " ]\n"
               << "Secondary hotkey: [ " << g_KeyList2[key2Idx].name << " ]\n"
               << "Output folder: " << outDir;
            SetWindowTextA(g_hStatus, "Game is running. Press hotkey in-game to dump geometry.");
            MessageBoxA(hWnd, ss.str().c_str(), "OpenRipper Active", MB_ICONINFORMATION);
        } else {
            DWORD err = GetLastError();
            std::string errStr = "Failed to launch game. Error code: " + std::to_string(err);
            SetWindowTextA(g_hStatus, errStr.c_str());
            MessageBoxA(hWnd, errStr.c_str(), "Launch Error", MB_ICONERROR);
        }
    } else if (apiIndex == 1) {
        std::string dllName = is64Bit ? "OpenRipperDx9.dll" : "OpenRipperDx9_x86.dll";
        std::string dllPath = FindBackendFile(dllName);

        if (dllPath.empty() || !std::filesystem::exists(dllPath)) {
            std::string msg = dllName + " not found in backends or application directory!";
            MessageBoxA(hWnd, msg.c_str(), "OpenRipper Loader", MB_ICONERROR);
            return;
        }

        BOOL success = CreateProcessA(
            gamePath, NULL, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL,
            workDir.c_str(), &si, &pi
        );

        if (success) {
            bool injected = false;

            if (is64Bit) {
                // 64-bit direct injection
                SIZE_T len = dllPath.length() + 1;
                LPVOID pRemoteMem = VirtualAllocEx(pi.hProcess, NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                if (pRemoteMem) {
                    if (WriteProcessMemory(pi.hProcess, pRemoteMem, dllPath.c_str(), len, NULL)) {
                        HMODULE hK32 = GetModuleHandleA("kernel32.dll");
                        LPTHREAD_START_ROUTINE pfnLoadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(hK32, "LoadLibraryA");
                        HANDLE hThread = CreateRemoteThread(pi.hProcess, NULL, 0, pfnLoadLib, pRemoteMem, 0, NULL);
                        if (hThread) {
                            WaitForSingleObject(hThread, 3000);
                            CloseHandle(hThread);
                            injected = true;
                        }
                    }
                    VirtualFreeEx(pi.hProcess, pRemoteMem, 0, MEM_RELEASE);
                }
            } else {
                // 32-bit helper injection via OpenRipperInjector32.exe
                std::string injectorPath = FindHelperExecutable("OpenRipperInjector32.exe");

                if (injectorPath.empty() || !std::filesystem::exists(injectorPath)) {
                    MessageBoxA(hWnd, "OpenRipperInjector32.exe helper not found!", "OpenRipper Loader", MB_ICONERROR);
                    ResumeThread(pi.hThread);
                    CloseHandle(pi.hThread);
                    CloseHandle(pi.hProcess);
                    return;
                }

                std::string cmd = "\"" + injectorPath + "\" " + std::to_string(pi.dwProcessId) + " \"" + dllPath + "\"";
                STARTUPINFOA injSi = {0};
                PROCESS_INFORMATION injPi = {0};
                injSi.cb = sizeof(injSi);

                if (CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &injSi, &injPi)) {
                    WaitForSingleObject(injPi.hProcess, 5000);
                    DWORD exitCode = 0;
                    GetExitCodeProcess(injPi.hProcess, &exitCode);
                    CloseHandle(injPi.hProcess);
                    CloseHandle(injPi.hThread);
                    if (exitCode == 0) {
                        injected = true;
                    }
                }
            }

            ResumeThread(pi.hThread);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);

            if (injected) {
                std::ostringstream ss;
                ss << "DirectX 9 Game launched and injected successfully!\n\n"
                   << "Architecture: " << (is64Bit ? "64-bit (x64)" : "32-bit (x86)") << "\n"
                   << "Injected DLL: " << dllName << "\n"
                   << "Primary hotkey: [ " << g_KeyList1[key1Idx].name << " ]\n"
                   << "Secondary hotkey: [ " << g_KeyList2[key2Idx].name << " ]\n"
                   << "Output folder: " << outDir;
                SetWindowTextA(g_hStatus, "Game is running with D3D9 hook. Press hotkey in-game.");
                MessageBoxA(hWnd, ss.str().c_str(), "OpenRipper D3D9 Active", MB_ICONINFORMATION);
            } else {
                std::string warnStr = "Game started, but DLL injection failed (possible anti-cheat or permissions issue).";
                SetWindowTextA(g_hStatus, warnStr.c_str());
                MessageBoxA(hWnd, warnStr.c_str(), "Injection Warning", MB_ICONWARNING);
            }
        } else {
            DWORD err = GetLastError();
            std::string errStr = "Failed to launch game. Error code: " + std::to_string(err);
            SetWindowTextA(g_hStatus, errStr.c_str());
            MessageBoxA(hWnd, errStr.c_str(), "Launch Error", MB_ICONERROR);
        }
    } else if (apiIndex == 2) {
        // DirectX 11 (Injection)
        if (!is64Bit) {
            MessageBoxA(hWnd, "DirectX 11 support is currently 64-bit (x64) only!", "OpenRipper Loader", MB_ICONWARNING);
            return;
        }

        std::string dllPath = FindBackendFile("OpenRipperDx11.dll");

        if (dllPath.empty() || !std::filesystem::exists(dllPath)) {
            MessageBoxA(hWnd, "OpenRipperDx11.dll not found in backends or application directory!", "OpenRipper Loader", MB_ICONERROR);
            return;
        }

        BOOL success = CreateProcessA(
            gamePath, NULL, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL,
            workDir.c_str(), &si, &pi
        );

        if (success) {
            bool injected = false;
            SIZE_T len = dllPath.length() + 1;
            LPVOID pRemoteMem = VirtualAllocEx(pi.hProcess, NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (pRemoteMem) {
                if (WriteProcessMemory(pi.hProcess, pRemoteMem, dllPath.c_str(), len, NULL)) {
                    HMODULE hK32 = GetModuleHandleA("kernel32.dll");
                    LPTHREAD_START_ROUTINE pfnLoadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(hK32, "LoadLibraryA");
                    HANDLE hThread = CreateRemoteThread(pi.hProcess, NULL, 0, pfnLoadLib, pRemoteMem, 0, NULL);
                    if (hThread) {
                        WaitForSingleObject(hThread, 3000);
                        CloseHandle(hThread);
                        injected = true;
                    }
                }
                VirtualFreeEx(pi.hProcess, pRemoteMem, 0, MEM_RELEASE);
            }

            ResumeThread(pi.hThread);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);

            if (injected) {
                std::ostringstream ss;
                ss << "DirectX 11 Game launched and injected successfully!\n\n"
                   << "Architecture: 64-bit (x64)\n"
                   << "Injected DLL: OpenRipperDx11.dll\n"
                   << "Primary hotkey: [ " << g_KeyList1[key1Idx].name << " ]\n"
                   << "Secondary hotkey: [ " << g_KeyList2[key2Idx].name << " ]\n"
                   << "Output folder: " << outDir;
                SetWindowTextA(g_hStatus, "Game is running with D3D11 hook. Press hotkey in-game.");
                MessageBoxA(hWnd, ss.str().c_str(), "OpenRipper D3D11 Active", MB_ICONINFORMATION);
            } else {
                std::string warnStr = "Game started, but DLL injection failed (possible anti-cheat or permissions issue).";
                SetWindowTextA(g_hStatus, warnStr.c_str());
                MessageBoxA(hWnd, warnStr.c_str(), "Injection Warning", MB_ICONWARNING);
            }
        } else {
            DWORD err = GetLastError();
            std::string errStr = "Failed to launch game. Error code: " + std::to_string(err);
            SetWindowTextA(g_hStatus, errStr.c_str());
            MessageBoxA(hWnd, errStr.c_str(), "Launch Error", MB_ICONERROR);
        }
    } else {
        // Fallback for DXVK / other
        BOOL success = CreateProcessA(
            gamePath, NULL, NULL, NULL, FALSE, 0, NULL,
            workDir.c_str(), &si, &pi
        );
        if (success) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            SetWindowTextA(g_hStatus, "Game is running.");
        }
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

            CreateWindowA("STATIC", "Target Executable (.exe):", WS_VISIBLE | WS_CHILD,
                          20, 15, 450, 18, hWnd, NULL, NULL, NULL);
            g_hEditGame = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
                                          20, 35, 360, 24, hWnd, (HMENU)IDC_EDIT_GAME_PATH, NULL, NULL);
            CreateWindowA("BUTTON", "Browse...", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                          390, 34, 80, 26, hWnd, (HMENU)IDC_BTN_BROWSE_GAME, NULL, NULL);

            CreateWindowA("STATIC", "Graphics API:", WS_VISIBLE | WS_CHILD,
                          20, 75, 140, 18, hWnd, NULL, NULL, NULL);
            g_hComboApi = CreateWindowA("COMBOBOX", "", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
                                        20, 95, 200, 200, hWnd, (HMENU)IDC_COMBO_API, NULL, NULL);
            SendMessageA(g_hComboApi, CB_ADDSTRING, 0, (LPARAM)"Vulkan (Native / RPCS3)");
            SendMessageA(g_hComboApi, CB_ADDSTRING, 0, (LPARAM)"DirectX 9 (D3D9 Injection)");
            SendMessageA(g_hComboApi, CB_ADDSTRING, 0, (LPARAM)"DirectX 11 (D3D11 Injection)");
            SendMessageA(g_hComboApi, CB_ADDSTRING, 0, (LPARAM)"DXVK (DirectX -> Vulkan)");
            SendMessageA(g_hComboApi, CB_ADDSTRING, 0, (LPARAM)"OpenGL (Future)");
            SendMessageA(g_hComboApi, CB_SETCURSEL, 0, 0);

            CreateWindowA("STATIC", "Primary Hotkey:", WS_VISIBLE | WS_CHILD,
                          240, 75, 100, 18, hWnd, NULL, NULL, NULL);
            g_hComboKey1 = CreateWindowA("COMBOBOX", "", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
                                         240, 95, 105, 200, hWnd, (HMENU)IDC_COMBO_KEY1, NULL, NULL);
            for (const auto& k : g_KeyList1) {
                SendMessageA(g_hComboKey1, CB_ADDSTRING, 0, (LPARAM)k.name);
            }

            CreateWindowA("STATIC", "Secondary Hotkey:", WS_VISIBLE | WS_CHILD,
                          365, 75, 110, 18, hWnd, NULL, NULL, NULL);
            g_hComboKey2 = CreateWindowA("COMBOBOX", "", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
                                         365, 95, 105, 200, hWnd, (HMENU)IDC_COMBO_KEY2, NULL, NULL);
            for (const auto& k : g_KeyList2) {
                SendMessageA(g_hComboKey2, CB_ADDSTRING, 0, (LPARAM)k.name);
            }

            CreateWindowA("STATIC", "Output Directory:", WS_VISIBLE | WS_CHILD,
                          20, 135, 450, 18, hWnd, NULL, NULL, NULL);
            g_hEditOut = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "C:\\OpenRipperDumps", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
                                         20, 155, 360, 24, hWnd, (HMENU)IDC_EDIT_OUT_DIR, NULL, NULL);
            CreateWindowA("BUTTON", "Browse...", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                          390, 154, 80, 26, hWnd, (HMENU)IDC_BTN_BROWSE_OUT, NULL, NULL);

            HWND hBtnLaunch = CreateWindowA("BUTTON", "Launch Game with OpenRipper", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                                            20, 200, 450, 42, hWnd, (HMENU)IDC_BTN_LAUNCH, NULL, NULL);

            g_hStatus = CreateWindowA("STATIC", "Ready. Select executable and click Launch.", WS_VISIBLE | WS_CHILD | SS_SIMPLE,
                                      20, 255, 450, 20, hWnd, (HMENU)IDC_STATIC_STATUS, NULL, NULL);

            SendMessage(g_hEditGame, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessage(g_hComboApi, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessage(g_hComboKey1, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessage(g_hComboKey2, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessage(g_hEditOut, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessage(hBtnLaunch, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessage(g_hStatus, WM_SETFONT, (WPARAM)hFont, TRUE);

            LoadConfig();
            break;
        }
        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            switch (wmId) {
                case IDC_BTN_BROWSE_GAME:
                    BrowseGameFile(hWnd);
                    break;
                case IDC_BTN_BROWSE_OUT:
                    BrowseOutputDir(hWnd);
                    break;
                case IDC_BTN_LAUNCH:
                    LaunchGame(hWnd);
                    break;
            }
            break;
        }
        case WM_DESTROY:
            SaveConfig();
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProcA(hWnd, message, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    WNDCLASSEXA wcex = {0};
    wcex.cbSize = sizeof(WNDCLASSEXA);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcex.lpszClassName = "OpenRipperLoaderClass";

    RegisterClassExA(&wcex);

    HWND hWnd = CreateWindowA(
        "OpenRipperLoaderClass",
        "OpenRipper 3D Geometry Dumper",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 505, 325,
        NULL, NULL, hInstance, NULL
    );

    if (!hWnd) return FALSE;

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
