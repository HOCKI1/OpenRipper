@echo off
setlocal enabledelayedexpansion

echo ========================================================
echo         OpenRipper - Complete Project Build Script
echo ========================================================
echo.

cd /d "%~dp0"

if not exist bin mkdir bin
if not exist bin\backends mkdir bin\backends

echo [*] Building 64-bit components (Loader, Vulkan, D3D9 x64, D3D11 x64)...
set "PATH=C:\msys64\ucrt64\bin;C:\msys64\usr\bin;%PATH%"

cmake -B build -S . -G Ninja
if errorlevel 1 (
    echo [ERROR] CMake configuration failed!
    exit /b 1
)

cmake --build build
if errorlevel 1 (
    echo [ERROR] 64-bit build failed!
    exit /b 1
)

echo.
echo [*] Building 32-bit components (D3D9 x86, Injector32)...
set "PATH=C:\msys64\mingw32\bin;C:\msys64\usr\bin;%PATH%"

g++ -O3 -shared -static -static-libgcc -static-libstdc++ ^
    -Icore -Ibackends/d3d9 -Ithird_party/dxsdk/Include -Ithird_party/minhook/include ^
    core/ObjExporter.cpp backends/d3d9/D3D9Capture.cpp backends/d3d9/D3D9Hooks.cpp ^
    third_party/minhook/src/buffer.c third_party/minhook/src/hook.c ^
    third_party/minhook/src/trampoline.c third_party/minhook/src/hde/hde32.c ^
    -o bin/backends/OpenRipperDx9_x86.dll -ld3d9 -luser32 -lgdi32

if errorlevel 1 (
    echo [ERROR] 32-bit OpenRipperDx9_x86.dll build failed!
    exit /b 1
)

g++ -O3 -static -static-libgcc -static-libstdc++ -mwindows ^
    loader/OpenRipperInjector32.cpp ^
    -o bin/OpenRipperInjector32.exe

if errorlevel 1 (
    echo [ERROR] OpenRipperInjector32.exe build failed!
    exit /b 1
)

echo.
echo [*] Copying configs and launcher files to bin/...
if exist openripper.ini copy /y openripper.ini bin\ >nul
if exist README.txt copy /y README.txt bin\ >nul
if exist run_vulkan_capture.bat copy /y run_vulkan_capture.bat bin\ >nul
if exist backends\vulkan\VkLayer_openripper.json copy /y backends\vulkan\VkLayer_openripper.json bin\backends\ >nul

echo.
echo ========================================================
echo                 BUILD FINISHED SUCCESSFULLY!
echo ========================================================
echo Output directory layout:
echo   bin\
echo     OpenRipperLoader.exe
echo     OpenRipperInjector32.exe
echo     openripper.ini
echo     README.txt
echo     run_vulkan_capture.bat
echo     backends\
echo       OpenRipperVk.dll
echo       VkLayer_openripper.json
echo       OpenRipperDx9.dll
echo       OpenRipperDx9_x86.dll
echo       OpenRipperDx11.dll
echo ========================================================
