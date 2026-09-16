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
    SendMessage(g_hComboApi, CB_SETCURSEL, apiIndex, 0);

    int sel1 = 0;
    for (size_t i = 0; i < sizeof(g_KeyList1)/sizeof(g_KeyList1[0]); ++i) {
        if (g_KeyList1[i].vkCode == key1Code) {
            sel1 = (int)i;
            break;
        }
    }
    SendMessage(g_hComboKey1, CB_SETCURSEL, sel1, 0);

    int sel2 = 0;
    for (size_t i = 0; i < sizeof(g_KeyList2)/sizeof(g_KeyList2[0]); ++i) {
        if (g_KeyList2[i].vkCode == key2Code) {
            sel2 = (int)i;
            break;
        }
    }
    SendMessage(g_hComboKey2, CB_SETCURSEL, sel2, 0);
}

static void SaveConfig() {
    char gamePath[MAX_PATH] = {0};
    char outDir[MAX_PATH] = {0};

    GetWindowTextA(g_hEditGame, gamePath, MAX_PATH);
    GetWindowTextA(g_hEditOut, outDir, MAX_PATH);
    int apiIndex = (int)SendMessage(g_hComboApi, CB_GETCURSEL, 0, 0);
    int key1Idx = (int)SendMessage(g_hComboKey1, CB_GETCURSEL, 0, 0);
    int key2Idx = (int)SendMessage(g_hComboKey2, CB_GETCURSEL, 0, 0);

    int key1Code = (key1Idx >= 0) ? g_KeyList1[key1Idx].vkCode : '0';
    int key2Code = (key2Idx >= 0) ? g_KeyList2[key2Idx].vkCode : VK_INSERT;

    const char* apiName = "Vulkan";
    if (apiIndex == 1) apiName = "DXVK";
    else if (apiIndex == 2) apiName = "OpenGL";

    std::vector<std::string> iniTargets = {
        GetConfigPath(),
        std::string(outDir) + "\\openripper.ini",
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

    // Save config into openripper.ini
    SaveConfig();

    int key1Idx = (int)SendMessage(g_hComboKey1, CB_GETCURSEL, 0, 0);
    int key2Idx = (int)SendMessage(g_hComboKey2, CB_GETCURSEL, 0, 0);
    int apiIndex = (int)SendMessage(g_hComboApi, CB_GETCURSEL, 0, 0);

    // Set Vulkan Layer path
    std::string appDir = GetAppDir();
    std::string layerDir = appDir + "\\build";
    if (!std::filesystem::exists(layerDir + "\\VkLayer_openripper.json")) {
        layerDir = appDir;
    }

    SetEnvironmentVariableA("VK_LAYER_PATH", layerDir.c_str());
    SetEnvironmentVariableA("VK_INSTANCE_LAYERS", "VK_LAYER_OPENRIPPER_capture");
    SetEnvironmentVariableA("ENABLE_OPENRIPPER", "1");

    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);

    std::filesystem::path exePath(gamePath);
    std::string workDir = exePath.parent_path().string();

    std::string statusMsg = "Launching: " + exePath.filename().string() + "...";
    SetWindowTextA(g_hStatus, statusMsg.c_str());

    BOOL success = CreateProcessA(
        gamePath,
        NULL,
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        workDir.c_str(),
        &si,
        &pi
    );

    if (success) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        std::ostringstream ss;
        ss << "Game launched! OpenRipper is active.\n\n"
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
